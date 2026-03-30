// ------ ESP-IDF and LVGL includes ------
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
// #include <driver/adc.h> // ADC driver
#include "esp_adc/adc_oneshot.h"
#include <stdio.h>

static const char *TAG = "lab2_task3";
static lv_disp_t *disp;

// Board-specific pin and display configuration
#include "esp32s3_box_lcd_config.h"

#define BUTTON_PIN      11
#define DEBOUNCE_DELAY  50

static adc_oneshot_unit_handle_t adc1_handle;

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

// Function to read temperature from TMP36 sensor
static float read_temperature() { 
    int raw;
    ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, ADC_CHANNEL_8, &raw));
    
    float Vout = (raw * 3600.0) / 4095.0;
    float celcius = (Vout - 500) / 10.0;
    float fahrenheit = (celcius * (9.0/5.0)) + 32;

    return fahrenheit;
}

static volatile int64_t last_isr_us = 0;
volatile bool read_requested = false;

// ISR handler - only updates LCD display with current sensor reading
static void IRAM_ATTR button_isr_handler(void *arg) {
    // debouncing
    int64_t now = esp_timer_get_time();
    if (now - last_isr_us < DEBOUNCE_DELAY * 1000) {
        return;
    }
    last_isr_us = now;
    read_requested = true;
}

void app_main(void) {
    // Initialize GUI and get display handle
    disp = gui_setup();
    // set up display
    lvgl_port_lock(0);
    lv_obj_t *scr = lv_disp_get_scr_act(disp);
    lv_obj_clean(scr);
    lv_obj_t *label1 = lv_label_create(scr);
    lv_label_set_text(label1, "Press button");
    lv_obj_align(label1, LV_ALIGN_CENTER, 0, 0);
    lvgl_port_unlock();

    // Initialize the ADC Unit Handle
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc1_handle));

    // Configure ADC Channel (attached to the handle)
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten = ADC_ATTEN_DB_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_8, &config));

    // Configure pin for button
    gpio_config_t io_conf_button = {
      .pin_bit_mask = (1ULL << BUTTON_PIN),
      .mode = GPIO_MODE_INPUT,
      .intr_type = GPIO_INTR_POSEDGE,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_ENABLE
    };
    gpio_config(&io_conf_button);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(BUTTON_PIN, button_isr_handler, NULL);

    while(1){
        if (read_requested){
            float temp_f = read_temperature();            
            char new_text[512];
            snprintf(new_text, sizeof(new_text), "Temp: %.1f °F\n", temp_f);

            lvgl_port_lock(0);
            lv_label_set_text(label1, new_text);
            lv_obj_set_style_text_font(label1, &lv_font_montserrat_24, 0);
            lvgl_port_unlock();
            read_requested = false;
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
