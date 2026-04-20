#include <stdio.h>
#include <stdbool.h>
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "pin_config.h"
#include "esp32s3_box_lcd_config.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "nvs_flash.h"

static const char *TAG = "final_project";
static lv_disp_t *disp;

// --- Piezo config ---
#define PZ                  42
#define PIEZO_DEBOUNCE_MS   200

// --- Motor config ---
#define MOTOR_PWM_PIN       11
#define MOTOR_DIR_PIN       12
#define MOTOR_RUN_MS        3000    // how long motor runs after a hit

// --- WiFi config ---
#define WIFI_SSID           "DukeVisitor"
#define WIFI_MAX_RETRY      5

// --- Camera device config ---
#define CAMERA_DEVICE_HOSTNAME  "doorbell.local"
#define CAMERA_DEVICE_PORT      80
#define CAMERA_TRIGGER_URI      "/trigger_capture"

static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
static int s_retry_num = 0;

int raw = 0;
int hit_count = 0;

// ============================================================================
// Motor functions
// ============================================================================

static void motor_init(void)
{
    gpio_config_t dir_conf = {
        .pin_bit_mask = (1ULL << MOTOR_DIR_PIN),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&dir_conf);

    ledc_timer_config_t timer_conf = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .timer_num       = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .freq_hz         = 1000,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer_conf);

    ledc_channel_config_t channel_conf = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LEDC_CHANNEL_0,
        .timer_sel  = LEDC_TIMER_0,
        .gpio_num   = MOTOR_PWM_PIN,
        .duty       = 0,
        .hpoint     = 0,
    };
    ledc_channel_config(&channel_conf);
}

static void motor_set(uint32_t speed, bool forward)
{
    gpio_set_level(MOTOR_DIR_PIN, forward ? 1 : 0);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, speed);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

static void motor_stop(void)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

// ============================================================================
// Motor task — runs motor for MOTOR_RUN_MS when notified
// ============================================================================

static TaskHandle_t motor_task_handle = NULL;

static void motor_task(void *pvParameters)
{
    while (1) {
        // Block here until piezo loop sends a notification
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        ESP_LOGI(TAG, "Motor ON");
        motor_set(768, true);
        vTaskDelay(pdMS_TO_TICKS(MOTOR_RUN_MS));
        motor_stop();
        ESP_LOGI(TAG, "Motor OFF");
    }
}

// ============================================================================
// WiFi event handler
// ============================================================================

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < WIFI_MAX_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Retrying WiFi... (%d/%d)", s_retry_num, WIFI_MAX_RETRY);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGE(TAG, "WiFi connection failed after %d retries", WIFI_MAX_RETRY);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// ============================================================================
// WiFi init
// ============================================================================

void wifi_init(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler, NULL, &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = "",
            .threshold.authmode = WIFI_AUTH_OPEN,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE, portMAX_DELAY);
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "WiFi Connected");
    } else {
        ESP_LOGE(TAG, "WiFi connection failed");
    }
}

// ============================================================================
// HTTP event handler for camera trigger request
// ============================================================================

static esp_err_t camera_http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGE(TAG, "HTTP Client Error");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGI(TAG, "HTTP Connected to camera");
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGI(TAG, "Camera response: %.*s", evt->data_len, (char *)evt->data);
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGI(TAG, "Camera trigger completed");
            break;
        case HTTP_EVENT_DISCONNECTED:
            break;
        default:
            break;
    }
    return ESP_OK;
}

// ============================================================================
// Trigger camera capture
// ============================================================================

void trigger_camera_capture(void)
{
    char camera_url[256];
    snprintf(camera_url, sizeof(camera_url), "http://%s:%d%s", 
             CAMERA_DEVICE_HOSTNAME, CAMERA_DEVICE_PORT, CAMERA_TRIGGER_URI);

    ESP_LOGI(TAG, "Triggering camera at: %s", camera_url);

    esp_http_client_config_t config = {
        .url = camera_url,
        .method = HTTP_POST,
        .timeout_ms = 5000,
        .event_handler = camera_http_event_handler,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to init HTTP client for camera");
        return;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");

    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "Camera HTTP Status: %d", status_code);
    } else {
        ESP_LOGE(TAG, "Camera trigger failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
}

// ============================================================================
// LCD / GUI setup — unchanged from your original
// ============================================================================

static lv_disp_t *gui_setup(void)
{
    ESP_LOGI(TAG, "Turn off LCD backlight");
    gpio_config_t bk_gpio_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << EXAMPLE_PIN_NUM_BK_LIGHT
    };
    ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));

    ESP_LOGI(TAG, "Initialize SPI bus");
    spi_bus_config_t bus_config = {
        .sclk_io_num     = EXAMPLE_PIN_NUM_SCLK,
        .mosi_io_num     = EXAMPLE_PIN_NUM_MOSI,
        .miso_io_num     = EXAMPLE_PIN_NUM_MISO,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = EXAMPLE_LCD_H_RES * 80 * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &bus_config, SPI_DMA_CH_AUTO));

    ESP_LOGI(TAG, "Install panel IO");
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num       = EXAMPLE_PIN_NUM_LCD_DC,
        .cs_gpio_num       = EXAMPLE_PIN_NUM_LCD_CS,
        .pclk_hz           = EXAMPLE_LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits      = EXAMPLE_LCD_CMD_BITS,
        .lcd_param_bits    = EXAMPLE_LCD_PARAM_BITS,
        .spi_mode          = 0,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(
        (esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle));

    ESP_LOGI(TAG, "Install ILI9341 panel driver");
    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num       = EXAMPLE_PIN_NUM_LCD_RST,
        .flags.reset_active_high = 1,
        .rgb_ele_order        = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel       = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(
        io_handle, &panel_config, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    ESP_LOGI(TAG, "Turn on LCD backlight");
    gpio_set_level(EXAMPLE_PIN_NUM_BK_LIGHT, EXAMPLE_LCD_BK_LIGHT_ON_LEVEL);

    ESP_LOGI(TAG, "Initialize LVGL");
    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_port_init(&lvgl_cfg);

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle    = io_handle,
        .panel_handle = panel_handle,
        .buffer_size  = EXAMPLE_LCD_H_RES * EXAMPLE_LVGL_DRAW_BUF_LINES,
        .double_buffer = true,
        .hres         = EXAMPLE_LCD_H_RES,
        .vres         = EXAMPLE_LCD_V_RES,
        .monochrome   = false,
        .flags        = {.swap_bytes = true},
        .rotation     = {
            .swap_xy  = false,
            .mirror_x = true,
            .mirror_y = true,
        }
    };
    lv_disp_t *disp = lvgl_port_add_disp(&disp_cfg);
    return disp;
}

// ============================================================================
// app_main
// ============================================================================

void app_main(void)
{
    // Init NVS for WiFi
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Init WiFi
    wifi_init();
    ESP_LOGI(TAG, "WiFi initialized");

    // Init motor hardware
    motor_init();
    ESP_LOGI(TAG, "Motor initialized");

    // Start motor task — it blocks until notified
    xTaskCreate(motor_task, "motor_task", 2048, NULL, 5, &motor_task_handle);

    // Init piezo pin
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PZ),
        .mode         = GPIO_MODE_INPUT,
    };
    gpio_config(&io_conf);

    // Init LCD and LVGL
    disp = gui_setup();
    lv_obj_t *scr = lv_disp_get_scr_act(disp);
    lv_obj_clean(scr);

    lv_obj_t *label_hits = lv_label_create(scr);
    lv_obj_align(label_hits, LV_ALIGN_TOP_MID, 0, 40);

    lv_obj_t *label_status = lv_label_create(scr);
    lv_obj_align(label_status, LV_ALIGN_TOP_MID, 0, 100);
    lv_label_set_text(label_status, "Status: Idle");

    // Camera trigger status label
    lv_obj_t *label_camera = lv_label_create(scr);
    lv_obj_align(label_camera, LV_ALIGN_TOP_MID, 0, 140);
    lv_label_set_text(label_camera, "Camera: Ready");

    char buf[128];
    bool in_hit = false;
    TickType_t last_hit_tick = 0;

    while (1) {
        TickType_t now = xTaskGetTickCount();
        bool hit_detected = false;
        raw = gpio_get_level(PZ);

        if (raw) {
            if (!in_hit ||
                (now - last_hit_tick) > pdMS_TO_TICKS(PIEZO_DEBOUNCE_MS)) {

                hit_count++;
                hit_detected = true;
                in_hit = true;
                last_hit_tick = now;

                ESP_LOGI(TAG, "Hit #%d | Raw: %d", hit_count, raw);

                // --- Notify motor task to run ---
                xTaskNotifyGive(motor_task_handle);

                // --- Trigger camera capture ---
                lvgl_port_lock(0);
                lv_label_set_text(label_camera, "Camera: Sending...");
                lvgl_port_unlock();

                trigger_camera_capture();

                lvgl_port_lock(0);
                lv_label_set_text(label_camera, "Camera: Sent");
                lvgl_port_unlock();
            }
        } else {
            if (in_hit &&
                (now - last_hit_tick) > pdMS_TO_TICKS(PIEZO_DEBOUNCE_MS)) {
                in_hit = false;
            }
        }

        lvgl_port_lock(0);
        snprintf(buf, sizeof(buf), "Hits: %d", hit_count);
        lv_label_set_text(label_hits, buf);
        lv_label_set_text(label_status,
            hit_detected ? "Status: HIT!" : "Status: Idle");
        lvgl_port_unlock();

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}