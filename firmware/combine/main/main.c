#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_camera.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "driver/gpio.h"       // GPIO driver for controlling LCD backlight
#include "driver/spi_master.h" // SPI master driver for LCD communication
#include "esp_err.h"           // Error handling macros and types
#include "esp_lcd_panel_io.h"  // LCD panel IO abstraction
#include "esp_lcd_panel_ops.h" // LCD panel operations (reset, mirror, etc.)
#include "esp_log.h"           // Logging utilities
#include "esp_lvgl_port.h"     // ESP LVGL port component
#include "lvgl.h" // LVGL graphics library
#include "pin_config.h"

#include "esp32s3_box_lcd_config.h"
 
// ----------------------------------------------------------------
// TAG for log messages
// ----------------------------------------------------------------
static const char *TAG = "final_project";
 
// ----------------------------------------------------------------
// WiFi config — DukeVisitor is open (no password)
// ----------------------------------------------------------------
#define WIFI_SSID      "DukeVisitor"
#define WIFI_MAX_RETRY  5
 
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
static int s_retry_num = 0;
 
// ----------------------------------------------------------------
// AI-Thinker ESP32-CAM pin map
// (replaces the WROVER-KIT pins that were in the original)
// ----------------------------------------------------------------
#define CAM_PIN_PWDN    32
#define CAM_PIN_RESET   -1  // software reset
#define CAM_PIN_XCLK     0
#define CAM_PIN_SIOD    26
#define CAM_PIN_SIOC    27
 
#define CAM_PIN_D7      35
#define CAM_PIN_D6      34
#define CAM_PIN_D5      39
#define CAM_PIN_D4      36
#define CAM_PIN_D3      21
#define CAM_PIN_D2      19
#define CAM_PIN_D1      18
#define CAM_PIN_D0       5
#define CAM_PIN_VSYNC   25
#define CAM_PIN_HREF    23
#define CAM_PIN_PCLK    22

static lv_disp_t *disp;
#define PZ 42
#define PIEZO_DEBOUNCE_MS   200
int raw = 0;
int hit_count = 0;
 
// ----------------------------------------------------------------
// Camera config
// Dropped to QVGA for reliability on first run — easy to increase later
// ----------------------------------------------------------------
static camera_config_t camera_config = {
    .pin_pwdn     = CAM_PIN_PWDN,
    .pin_reset    = CAM_PIN_RESET,
    .pin_xclk     = CAM_PIN_XCLK,
    .pin_sccb_sda = CAM_PIN_SIOD,
    .pin_sccb_scl = CAM_PIN_SIOC,
 
    .pin_d7    = CAM_PIN_D7,
    .pin_d6    = CAM_PIN_D6,
    .pin_d5    = CAM_PIN_D5,
    .pin_d4    = CAM_PIN_D4,
    .pin_d3    = CAM_PIN_D3,
    .pin_d2    = CAM_PIN_D2,
    .pin_d1    = CAM_PIN_D1,
    .pin_d0    = CAM_PIN_D0,
    .pin_vsync = CAM_PIN_VSYNC,
    .pin_href  = CAM_PIN_HREF,
    .pin_pclk  = CAM_PIN_PCLK,
 
    .xclk_freq_hz = 20000000,
    .ledc_timer   = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,
 
    .pixel_format = PIXFORMAT_JPEG,
    .frame_size   = FRAMESIZE_QVGA,   // start small; bump to UXGA once working
    .jpeg_quality = 12,                // 0-63, lower = better quality
    .fb_count     = 1,
    .grab_mode    = CAMERA_GRAB_WHEN_EMPTY
};
 
// ----------------------------------------------------------------
// Camera init
// Removed Arduino pinMode/digitalWrite — used IDF gpio instead
// ----------------------------------------------------------------
esp_err_t camera_init(void)
{
    // Power-cycle the camera via PWDN pin (AI-Thinker requires this)
    gpio_set_direction(CAM_PIN_PWDN, GPIO_MODE_OUTPUT);
    gpio_set_level(CAM_PIN_PWDN, 1);  // power down
    vTaskDelay(10 / portTICK_PERIOD_MS);
    gpio_set_level(CAM_PIN_PWDN, 0);  // power up
    vTaskDelay(10 / portTICK_PERIOD_MS);
 
    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed: 0x%x", err);
        return err;
    }
    ESP_LOGI(TAG, "Camera init OK");
    return ESP_OK;
}
 
// ----------------------------------------------------------------
// HTTP handler — serves a JPEG snapshot at GET /capture
// ----------------------------------------------------------------
typedef struct {
    httpd_req_t *req;
    size_t len;
} jpg_chunking_t;
 
static size_t jpg_encode_stream(void *arg, size_t index, const void *data, size_t len)
{
    jpg_chunking_t *j = (jpg_chunking_t *)arg;
    if (!index) j->len = 0;
    if (httpd_resp_send_chunk(j->req, (const char *)data, len) != ESP_OK) return 0;
    j->len += len;
    return len;
}
 
esp_err_t jpg_httpd_handler(httpd_req_t *req)
{
    camera_fb_t *fb = NULL;
    esp_err_t res = ESP_OK;
    size_t fb_len = 0;
    int64_t fr_start = esp_timer_get_time();
 
    fb = esp_camera_fb_get();
    if (!fb) {
        ESP_LOGE(TAG, "Camera capture failed");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
 
    res = httpd_resp_set_type(req, "image/jpeg");
    if (res == ESP_OK)
        res = httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
 
    if (res == ESP_OK) {
        if (fb->format == PIXFORMAT_JPEG) {
            fb_len = fb->len;
            res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
        } else {
            jpg_chunking_t jchunk = {req, 0};
            res = frame2jpg_cb(fb, 80, jpg_encode_stream, &jchunk) ? ESP_OK : ESP_FAIL;
            httpd_resp_send_chunk(req, NULL, 0);
            fb_len = jchunk.len;
        }
    }
 
    esp_camera_fb_return(fb);
    int64_t fr_end = esp_timer_get_time();
    ESP_LOGI(TAG, "JPG: %uKB %ums",
             (uint32_t)(fb_len / 1024),
             (uint32_t)((fr_end - fr_start) / 1000));
    return res;
}
 
// ----------------------------------------------------------------
// WiFi event handler
// ----------------------------------------------------------------
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
        ESP_LOGI(TAG, "Open http://" IPSTR "/capture in your browser", IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}
 
// ----------------------------------------------------------------
// WiFi init — open network (no password)
// ----------------------------------------------------------------
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
 
    // Open network: no password, auth mode OPEN
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
 
    // Block until connected or failed
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE, portMAX_DELAY);
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to %s", WIFI_SSID);
    } else {
        ESP_LOGE(TAG, "Failed to connect to %s", WIFI_SSID);
    }
}

// ----------------------------------------------------------------
// LCD
// ----------------------------------------------------------------

// GUI setup function
static lv_disp_t *gui_setup(void) {
  // LCD backlight control pin configuration
  ESP_LOGI(TAG, "Turn off LCD backlight");
  gpio_config_t bk_gpio_config = {.mode = GPIO_MODE_OUTPUT,
                                  .pin_bit_mask = 1ULL
                                                  << EXAMPLE_PIN_NUM_BK_LIGHT};
  ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));

  // Configure and initialize the SPI bus for LCD communication
  ESP_LOGI(TAG, "Initialize SPI bus");
  spi_bus_config_t bus_config = {
      .sclk_io_num = EXAMPLE_PIN_NUM_SCLK,
      .mosi_io_num = EXAMPLE_PIN_NUM_MOSI,
      .miso_io_num = EXAMPLE_PIN_NUM_MISO,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
      .max_transfer_sz = EXAMPLE_LCD_H_RES * 80 * sizeof(uint16_t),
  };
  ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &bus_config, SPI_DMA_CH_AUTO));

  // Set up the panel IO (SPI interface to the LCD)
  ESP_LOGI(TAG, "Install panel IO");
  esp_lcd_panel_io_handle_t io_handle = NULL;
  esp_lcd_panel_io_spi_config_t io_config = {
      .dc_gpio_num = EXAMPLE_PIN_NUM_LCD_DC,
      .cs_gpio_num = EXAMPLE_PIN_NUM_LCD_CS,
      .pclk_hz = EXAMPLE_LCD_PIXEL_CLOCK_HZ,
      .lcd_cmd_bits = EXAMPLE_LCD_CMD_BITS,
      .lcd_param_bits = EXAMPLE_LCD_PARAM_BITS,
      .spi_mode = 0,
      .trans_queue_depth = 10,
  };
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST,
                                           &io_config, &io_handle));
  // Set up the LCD panel driver (ILI9341)
  ESP_LOGI(TAG, "Install ILI9341 panel driver");
  esp_lcd_panel_handle_t panel_handle = NULL;
  esp_lcd_panel_dev_config_t panel_config = {
      .reset_gpio_num = EXAMPLE_PIN_NUM_LCD_RST,
      .flags.reset_active_high = 1,
      .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
      .bits_per_pixel = 16,
  };
  ESP_ERROR_CHECK(
      esp_lcd_new_panel_ili9341(io_handle, &panel_config, &panel_handle));
  ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle)); // Reset the panel
  ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));  // Initialize the panel
  ESP_ERROR_CHECK(
      esp_lcd_panel_disp_on_off(panel_handle, true)); // Turn on display

  // Turn on the LCD backlight
  ESP_LOGI(TAG, "Turn on LCD backlight");
  gpio_set_level(EXAMPLE_PIN_NUM_BK_LIGHT, EXAMPLE_LCD_BK_LIGHT_ON_LEVEL);

  // LVGL initialization using port component
  ESP_LOGI(TAG, "Initialize LVGL library");
  const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
  lvgl_port_init(&lvgl_cfg);

  const lvgl_port_display_cfg_t disp_cfg = {
      .io_handle = io_handle,
      .panel_handle = panel_handle,
      .buffer_size = EXAMPLE_LCD_H_RES * EXAMPLE_LVGL_DRAW_BUF_LINES,
      .double_buffer = true,
      .hres = EXAMPLE_LCD_H_RES,
      .vres = EXAMPLE_LCD_V_RES,
      .monochrome = false,
      .flags = {.swap_bytes = true},
      .rotation = {
          .swap_xy = false,
          .mirror_x = true,
          .mirror_y = true,
      }};
  lv_disp_t *disp = lvgl_port_add_disp(&disp_cfg);

  return disp;
}

// ----------------------------------------------------------------
// app_main
// ----------------------------------------------------------------
void app_main(void)
{
    // NVS is required by WiFi driver
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
 
    // Init camera first (before WiFi to avoid memory pressure)
    if (camera_init() != ESP_OK) {
        ESP_LOGE(TAG, "Aborting: camera failed");
        return;
    }
 
    // Connect to WiFi
    wifi_init();
 
    // Start HTTP server
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;

    gpio_config_t io_conf = {
      .pin_bit_mask =  (1ULL << PZ),
      .mode = GPIO_MODE_INPUT
    };
    gpio_config(&io_conf);

    disp = gui_setup();
    lv_obj_t *scr = lv_disp_get_scr_act(disp);
    lv_obj_clean(scr);

    // --- UI elements ---
    // Hit or not
    lv_obj_t *label_raw = lv_label_create(scr);
    lv_obj_align(label_raw, LV_ALIGN_TOP_MID, 0, 20);

    // Hit counter
    lv_obj_t *label_hits = lv_label_create(scr);
    lv_obj_align(label_hits, LV_ALIGN_TOP_MID, 0, 60);

    // Status indicator
    lv_obj_t *label_status = lv_label_create(scr);
    lv_obj_align(label_status, LV_ALIGN_TOP_MID, 0, 100);
    lv_label_set_text(label_status, "Status: Idle");

    char buf[128];
    bool in_hit = false;
    TickType_t last_hit_tick = 0;
    while (1) {
        TickType_t now = xTaskGetTickCount();
        bool hit_detected = false;
        raw = gpio_get_level(PZ);
        // if (raw > 250) {
        if (gpio_get_level(PZ)){
            // Only count as a new hit if outside debounce window
            if (!in_hit || (now - last_hit_tick) > pdMS_TO_TICKS(PIEZO_DEBOUNCE_MS)) {
                hit_count++;
                hit_detected = true;
                in_hit = true;
                last_hit_tick = now;
                ESP_LOGI(TAG, "Hit #%d | Raw: %d", hit_count, raw);

                if (httpd_start(&server, &config) == ESP_OK) {
                    httpd_uri_t capture_uri = {
                        .uri     = "/capture",
                        .method  = HTTP_GET,
                        .handler = jpg_httpd_handler,
                        .user_ctx = NULL
                    };
                    httpd_register_uri_handler(server, &capture_uri);
                    ESP_LOGI(TAG, "HTTP server started on port 80");
                } else {
                    ESP_LOGE(TAG, "Failed to start HTTP server");
                }
            }
        } else {
            // Reset in_hit once signal drops back below threshold
            if (in_hit && (now - last_hit_tick) > pdMS_TO_TICKS(PIEZO_DEBOUNCE_MS)) {
                in_hit = false;
            }
        }

        lvgl_port_lock(0);
        snprintf(buf, sizeof(buf), "Raw: %4d", raw);
        lv_label_set_text(label_raw, buf);
        snprintf(buf, sizeof(buf), "Hits: %d", hit_count);
        lv_label_set_text(label_hits, buf);
        lv_label_set_text(label_status, hit_detected ? "Status: HIT!" : "Status: Idle");
        lvgl_port_unlock();
        vTaskDelay(pdMS_TO_TICKS(10)); 
    }
}