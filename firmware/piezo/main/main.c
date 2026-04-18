#include <stdio.h>
#include <stdbool.h>
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_lcd_panel_io.h"a
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "pin_config.h"
#include "esp32s3_box_lcd_config.h"

static const char *TAG = "final_project";
static lv_disp_t *disp;

// --- Piezo config ---
#define PZ                  42
#define PIEZO_DEBOUNCE_MS   200

// --- Motor config ---
#define MOTOR_PWM_PIN       11
#define MOTOR_DIR_PIN       12
#define MOTOR_RUN_MS        3000    // how long motor runs after a hit

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