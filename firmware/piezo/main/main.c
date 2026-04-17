#include <stdio.h>
#include "driver/gpio.h"       // GPIO driver for controlling LCD backlight
#include "driver/spi_master.h" // SPI master driver for LCD communication
#include "esp_err.h"           // Error handling macros and types
#include "esp_lcd_panel_io.h"  // LCD panel IO abstraction
#include "esp_lcd_panel_ops.h" // LCD panel operations (reset, mirror, etc.)
#include "esp_log.h"           // Logging utilities
#include "esp_lvgl_port.h"     // ESP LVGL port component
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h" // LVGL graphics library
#include "pin_config.h"
#include "esp_adc/adc_oneshot.h"

#include "esp32s3_box_lcd_config.h"

static const char *TAG = "final_project";
static lv_disp_t *disp;
#define PZ 42
#define PIEZO_DEBOUNCE_MS   200
int raw = 0;
int hit_count = 0;
int peak_value = 0;

adc_oneshot_unit_handle_t adc1_handle;

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

static void init_adc() {
        // 1. Initialize the ADC Unit Handle
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc1_handle));

    // 2. Configure the Channel (attached to the handle)
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_12, // Input voltage divided into 2**12 discrete levels
        .atten = ADC_ATTEN_DB_12, // Controls the input voltage range that maps to the ADC's full-scale output (0 mv to 4400 mv)
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_9, &config));

    // 3. Read using the handle
    adc_oneshot_read(adc1_handle, ADC_CHANNEL_9, &raw);
    printf("New Raw: %d\n", raw);
}

// static void IRAM_ATTR button_isr(void *arg){
//   int64_t now = esp_timer_get_time();
//   bool button_on = gpio_get_level(BUTTON);
//   if (now - last_isr_us < 50000) {
//     return;
//   }
//   last_isr_us = now;
//   gpio_set_level(BUZZER, button_on);
  
// }

void app_main(void) {
    init_adc();
    gpio_config_t io_conf = {
      .pin_bit_mask =  (1ULL << PZ),
      .mode = GPIO_MODE_INPUT
    };
    gpio_config(&io_conf);

    disp = gui_setup();
    lv_obj_t *scr = lv_disp_get_scr_act(disp);
    lv_obj_clean(scr);

    // --- UI elements ---
    // Raw ADC value
    lv_obj_t *label_raw = lv_label_create(scr);
    lv_obj_align(label_raw, LV_ALIGN_TOP_MID, 0, 20);

    // Peak value since last reset
    lv_obj_t *label_peak = lv_label_create(scr);
    lv_obj_align(label_peak, LV_ALIGN_TOP_MID, 0, 60);

    // Hit counter
    lv_obj_t *label_hits = lv_label_create(scr);
    lv_obj_align(label_hits, LV_ALIGN_TOP_MID, 0, 100);

    // Status indicator
    lv_obj_t *label_status = lv_label_create(scr);
    lv_obj_align(label_status, LV_ALIGN_TOP_MID, 0, 140);
    lv_label_set_text(label_status, "Status: Idle");

    char buf[128];
    bool in_hit = false;
    TickType_t last_hit_tick = 0;
    while (1) {
        // adc_oneshot_read(adc1_handle, ADC_CHANNEL_9, &raw);

        TickType_t now = xTaskGetTickCount();
        bool hit_detected = false;
        raw = gpio_get_level(PZ);
        // if (raw > 250) {
        if (gpio_get_level(PZ)){
            // Track the peak during a hit window
            if (raw > peak_value) {
                peak_value = raw;
            }

            // Only count as a new hit if outside debounce window
            if (!in_hit || (now - last_hit_tick) > pdMS_TO_TICKS(PIEZO_DEBOUNCE_MS)) {
                hit_count++;
                hit_detected = true;
                in_hit = true;
                last_hit_tick = now;
                
                ESP_LOGI(TAG, "Hit #%d | Raw: %d | Peak: %d", hit_count, raw, peak_value);
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
        snprintf(buf, sizeof(buf), "Peak:    %4d", peak_value);
        lv_label_set_text(label_peak, buf);
        snprintf(buf, sizeof(buf), "Hits:    %d", hit_count);
        lv_label_set_text(label_hits, buf);
        lv_label_set_text(label_status, hit_detected ? "Status: HIT!" : "Status: Idle");
        lvgl_port_unlock();
        vTaskDelay(pdMS_TO_TICKS(10)); 
    }
}