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
#include "esp32s3_box_lcd_config.h"

static const char *TAG = "final_project";
static lv_disp_t *disp;
#define PZ 42
#define PIEZO_DEBOUNCE_MS   200
int raw = 0;
int hit_count = 0;

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

    // Hit counter
    lv_obj_t *label_hits = lv_label_create(scr);
    lv_obj_align(label_hits, LV_ALIGN_TOP_MID, 0, 40);

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
        if (gpio_get_level(PZ)){
            // Only count as a new hit if outside debounce window
            if (!in_hit || (now - last_hit_tick) > pdMS_TO_TICKS(PIEZO_DEBOUNCE_MS)) {
                hit_count++;
                hit_detected = true;
                in_hit = true;
                last_hit_tick = now;
                
                ESP_LOGI(TAG, "Hit #%d | Raw: %d", hit_count, raw);
            }
        } else {
            // Reset in_hit once signal drops back below threshold
            if (in_hit && (now - last_hit_tick) > pdMS_TO_TICKS(PIEZO_DEBOUNCE_MS)) {
                in_hit = false;
            }
        }

        lvgl_port_lock(0);
        snprintf(buf, sizeof(buf), "Hits:    %d", hit_count);
        lv_label_set_text(label_hits, buf);
        lv_label_set_text(label_status, hit_detected ? "Status: HIT!" : "Status: Idle");
        lvgl_port_unlock();
        vTaskDelay(pdMS_TO_TICKS(10)); 
    }
}