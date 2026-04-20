#include <stdio.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "driver/gpio.h"
#include "esp_camera.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "mdns.h"
 
#include "esp_http_client.h"

// ----------------------------------------------------------------
// TAG for log messages
// ----------------------------------------------------------------
static const char *TAG = "camera";
 
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

#define MAX_HTTP_OUTPUT_BUFFER 1024

char *TARGT_HOST_URL = "http://api.open-meteo.com/v1/forecast?latitude=36.00&longitude=-78.93&current=temperature_2m,relative_humidity_2m,wind_speed_10m&temperature_unit=fahrenheit&wind_speed_unit=ms";

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
// mDNS setup - allows camera to be accessed as doorbell.local
// ----------------------------------------------------------------
void mdns_init_service(void)
{
    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set("doorbell"));
    ESP_ERROR_CHECK(mdns_instance_name_set("Smart Doorbell Camera"));
    
    // Advertise HTTP service
    ESP_ERROR_CHECK(mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0));
    
    ESP_LOGI(TAG, "mDNS configured: doorbell.local");
}

// ----------------------------------------------------------------
// Flask Server Configuration
// ----------------------------------------------------------------
#define FLASK_SERVER_IP     "67.159.65.184"
#define FLASK_SERVER_PORT   80
#define FLASK_ENDPOINT      "/api/camera/append_logentry"

typedef struct {
    uint8_t *buf;
    size_t len;
} http_response_t;

// HTTP event handler for Flask POST
static esp_err_t flask_http_event_handler(esp_http_client_event_t *evt)
{
    http_response_t *resp = (http_response_t *)evt->user_data;
    
    switch (evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGE(TAG, "HTTP Client Error");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGI(TAG, "HTTP Connected to Flask");
            break;
        case HTTP_EVENT_ON_DATA:
            if (!esp_http_client_is_chunked_response(evt->client)) {
                if (resp->buf == NULL) {
                    resp->buf = (uint8_t *)malloc(evt->data_len + 1);
                    resp->len = 0;
                }
                if (resp->buf && resp->len + evt->data_len <= 1024) {
                    memcpy(resp->buf + resp->len, evt->data, evt->data_len);
                    resp->len += evt->data_len;
                }
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            if (resp->buf) {
                resp->buf[resp->len] = '\0';
                ESP_LOGI(TAG, "Flask Response: %s", (char *)resp->buf);
            }
            break;
        case HTTP_EVENT_DISCONNECTED:
            if (resp->buf) {
                free(resp->buf);
                resp->buf = NULL;
                resp->len = 0;
            }
            break;
        default:
            break;
    }
    return ESP_OK;
}

// Capture and send image to Flask server
void capture_and_send_to_flask(void)
{
    camera_fb_t *fb = NULL;
    time_t now = time(NULL);
    char timestamp_str[32];
    char flask_url[256];
    
    snprintf(timestamp_str, sizeof(timestamp_str), "%ld", now);
    snprintf(flask_url, sizeof(flask_url), "http://%s:%d%s", FLASK_SERVER_IP, FLASK_SERVER_PORT, FLASK_ENDPOINT);
    
    ESP_LOGI(TAG, "Capturing image for Flask...");
    fb = esp_camera_fb_get();
    if (!fb) {
        ESP_LOGE(TAG, "Camera capture failed");
        return;
    }
    
    ESP_LOGI(TAG, "Captured: %uKB | Timestamp: %s", fb->len / 1024, timestamp_str);
    
    esp_http_client_config_t config = {
        .url = flask_url,
        .method = HTTP_POST,
        .timeout_ms = 10000,
        .event_handler = flask_http_event_handler,
    };
    
    http_response_t resp = {0};
    config.user_data = &resp;
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to init HTTP client for Flask");
        esp_camera_fb_return(fb);
        return;
    }
    
    // Set headers
    esp_http_client_set_header(client, "X-Timestamp", timestamp_str);
    esp_http_client_set_header(client, "Content-Type", "image/jpeg");
    
    // Send raw JPEG data
    esp_http_client_set_post_field(client, (char *)fb->buf, fb->len);
    
    ESP_LOGI(TAG, "POSTing image to Flask...");
    esp_err_t err = esp_http_client_perform(client);
    
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "Flask HTTP Status: %d", status_code);
        if (status_code == 201 || status_code == 200) {
            ESP_LOGI(TAG, "Image successfully sent to Flask!");
        } else {
            ESP_LOGW(TAG, "Flask returned status: %d", status_code);
        }
    } else {
        ESP_LOGE(TAG, "Flask POST failed: %s", esp_err_to_name(err));
    }
    
    esp_http_client_cleanup(client);
    esp_camera_fb_return(fb);
}

// ----------------------------------------------------------------
// HTTP handler for /trigger_capture endpoint
// ----------------------------------------------------------------
esp_err_t trigger_capture_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Trigger capture received!");
    
    // Capture and send to Flask in a separate task to not block HTTP server
    xTaskCreate(
        (TaskFunction_t)capture_and_send_to_flask,
        "capture_task",
        4096,
        NULL,
        5,
        NULL
    );
    
    // Respond immediately
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\": \"capture triggered\"}", -1);
    return ESP_OK;
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
 
    // Init mDNS — makes camera accessible as doorbell.local
    mdns_init_service();
 
    // Start HTTP server
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
 
    if (httpd_start(&server, &config) == ESP_OK) {
        // Register /capture endpoint
        httpd_uri_t capture_uri = {
            .uri     = "/capture",
            .method  = HTTP_GET,
            .handler = jpg_httpd_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &capture_uri);
        
        // Register /trigger_capture endpoint (called by piezo device)
        httpd_uri_t trigger_uri = {
            .uri     = "/trigger_capture",
            .method  = HTTP_POST,
            .handler = trigger_capture_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &trigger_uri);
        
        ESP_LOGI(TAG, "HTTP server started on port 80");
        ESP_LOGI(TAG, "  - GET /capture for live stream");
        ESP_LOGI(TAG, "  - POST /trigger_capture to capture & send to Flask");
    } else {
        ESP_LOGE(TAG, "Failed to start HTTP server");
    }
}