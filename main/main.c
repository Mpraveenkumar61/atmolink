#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_http_server.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "dht22.h"          /* your proven driver */

/* ── User config ── */
#define WIFI_SSID        "ssid"
#define WIFI_PASSWORD  "psw"
#define OWM_API_KEY    "yourapikey"
#define DHT_GPIO         GPIO_NUM_4

/* Eluru coordinates */
#define CITY_LAT  "16.7107"
#define CITY_LON  "81.0952"

/* OLED I2C */
#define I2C_MASTER_SDA   GPIO_NUM_21
#define I2C_MASTER_SCL   GPIO_NUM_22
#define I2C_MASTER_NUM   I2C_NUM_0
#define I2C_MASTER_FREQ  400000
#define SSD1306_ADDR     0x3C

/* WiFi */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define WIFI_MAX_RETRY     5

static const char *TAG = "atmolink";
static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;

/* ── Shared data ── */
typedef struct {
    float temperature;
    float humidity;
    bool  valid;
} sensor_data_t;

typedef struct {
    float owm_temp;
    float owm_humidity;
    char  owm_desc[64];
    bool  owm_valid;
} owm_data_t;

static sensor_data_t g_sensor = {0};
static owm_data_t    g_owm    = {0};
static SemaphoreHandle_t g_mutex;

/* ── HTTP buffer for OWM ── */
static char http_buf[2048];
static int  http_len = 0;

/* ═══════════════════════════════════════
   SSD1306 OLED driver
   ═══════════════════════════════════════ */
static const uint8_t font5x7[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, {0x00,0x00,0x5F,0x00,0x00},
    {0x00,0x07,0x00,0x07,0x00}, {0x14,0x7F,0x14,0x7F,0x14},
    {0x24,0x2A,0x7F,0x2A,0x12}, {0x23,0x13,0x08,0x64,0x62},
    {0x36,0x49,0x55,0x22,0x50}, {0x00,0x05,0x03,0x00,0x00},
    {0x00,0x1C,0x22,0x41,0x00}, {0x00,0x41,0x22,0x1C,0x00},
    {0x08,0x2A,0x1C,0x2A,0x08}, {0x08,0x08,0x3E,0x08,0x08},
    {0x00,0x50,0x30,0x00,0x00}, {0x08,0x08,0x08,0x08,0x08},
    {0x00,0x60,0x60,0x00,0x00}, {0x20,0x10,0x08,0x04,0x02},
    {0x3E,0x51,0x49,0x45,0x3E}, {0x00,0x42,0x7F,0x40,0x00},
    {0x42,0x61,0x51,0x49,0x46}, {0x21,0x41,0x45,0x4B,0x31},
    {0x18,0x14,0x12,0x7F,0x10}, {0x27,0x45,0x45,0x45,0x39},
    {0x3C,0x4A,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36}, {0x06,0x49,0x49,0x29,0x1E},
    {0x00,0x36,0x36,0x00,0x00}, {0x00,0x56,0x36,0x00,0x00},
    {0x00,0x08,0x14,0x22,0x41}, {0x14,0x14,0x14,0x14,0x14},
    {0x41,0x22,0x14,0x08,0x00}, {0x02,0x01,0x51,0x09,0x06},
    {0x32,0x49,0x79,0x41,0x3E}, {0x7E,0x11,0x11,0x11,0x7E},
    {0x7F,0x49,0x49,0x49,0x36}, {0x3E,0x41,0x41,0x41,0x22},
    {0x7F,0x41,0x41,0x22,0x1C}, {0x7F,0x49,0x49,0x49,0x41},
    {0x7F,0x09,0x09,0x09,0x01}, {0x3E,0x41,0x49,0x49,0x7A},
    {0x7F,0x08,0x08,0x08,0x7F}, {0x00,0x41,0x7F,0x41,0x00},
    {0x20,0x40,0x41,0x3F,0x01}, {0x7F,0x08,0x14,0x22,0x41},
    {0x7F,0x40,0x40,0x40,0x40}, {0x7F,0x02,0x04,0x02,0x7F},
    {0x7F,0x04,0x08,0x10,0x7F}, {0x3E,0x41,0x41,0x41,0x3E},
    {0x7F,0x09,0x09,0x09,0x06}, {0x3E,0x41,0x51,0x21,0x5E},
    {0x7F,0x09,0x19,0x29,0x46}, {0x46,0x49,0x49,0x49,0x31},
    {0x01,0x01,0x7F,0x01,0x01}, {0x3F,0x40,0x40,0x40,0x3F},
    {0x1F,0x20,0x40,0x20,0x1F}, {0x3F,0x40,0x38,0x40,0x3F},
    {0x63,0x14,0x08,0x14,0x63}, {0x07,0x08,0x70,0x08,0x07},
    {0x61,0x51,0x49,0x45,0x43}, {0x00,0x7F,0x41,0x41,0x00},
    {0x02,0x04,0x08,0x10,0x20}, {0x00,0x41,0x41,0x7F,0x00},
    {0x04,0x02,0x01,0x02,0x04}, {0x40,0x40,0x40,0x40,0x40},
    {0x00,0x01,0x02,0x04,0x00}, {0x20,0x54,0x54,0x54,0x78},
    {0x7F,0x48,0x44,0x44,0x38}, {0x38,0x44,0x44,0x44,0x20},
    {0x38,0x44,0x44,0x48,0x7F}, {0x38,0x54,0x54,0x54,0x18},
    {0x08,0x7E,0x09,0x01,0x02}, {0x08,0x54,0x54,0x54,0x3C},
    {0x7F,0x08,0x04,0x04,0x78}, {0x00,0x44,0x7D,0x40,0x00},
    {0x20,0x40,0x44,0x3D,0x00}, {0x7F,0x10,0x28,0x44,0x00},
    {0x00,0x41,0x7F,0x40,0x00}, {0x7C,0x04,0x18,0x04,0x78},
    {0x7C,0x08,0x04,0x04,0x78}, {0x38,0x44,0x44,0x44,0x38},
    {0x7C,0x14,0x14,0x14,0x08}, {0x08,0x14,0x14,0x18,0x7C},
    {0x7C,0x08,0x04,0x04,0x08}, {0x48,0x54,0x54,0x54,0x20},
    {0x04,0x3F,0x44,0x40,0x20}, {0x3C,0x40,0x40,0x40,0x7C},
    {0x1C,0x20,0x40,0x20,0x1C}, {0x3C,0x40,0x30,0x40,0x3C},
    {0x44,0x28,0x10,0x28,0x44}, {0x0C,0x50,0x50,0x50,0x3C},
    {0x44,0x64,0x54,0x4C,0x44}, {0x00,0x08,0x36,0x41,0x00},
    {0x00,0x00,0x7F,0x00,0x00}, {0x00,0x41,0x36,0x08,0x00},
    {0x08,0x08,0x2A,0x1C,0x08}, {0x08,0x1C,0x2A,0x08,0x08},
};

static uint8_t oled_buf[128 * 8];

static esp_err_t i2c_master_init(void) {
    i2c_config_t conf = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = I2C_MASTER_SDA,
        .scl_io_num       = I2C_MASTER_SCL,
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ,
    };
    esp_err_t err = i2c_param_config(I2C_MASTER_NUM, &conf);
    if (err != ESP_OK) return err;
    return i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);
}

static void oled_cmd(uint8_t cmd) {
    uint8_t buf[2] = {0x00, cmd};
    i2c_master_write_to_device(I2C_MASTER_NUM, SSD1306_ADDR, buf, 2, pdMS_TO_TICKS(10));
}

static void oled_init(void) {
    vTaskDelay(pdMS_TO_TICKS(100));
    uint8_t seq[] = {
        0xAE, 0xD5,0x80, 0xA8,0x3F, 0xD3,0x00, 0x40,
        0x8D,0x14, 0x20,0x00, 0xA1, 0xC8, 0xDA,0x12,
        0x81,0xCF, 0xD9,0xF1, 0xDB,0x40, 0xA4, 0xA6, 0xAF
    };
    for (int i = 0; i < (int)sizeof(seq); i++) oled_cmd(seq[i]);
    memset(oled_buf, 0, sizeof(oled_buf));
}

static void oled_flush(void) {
    oled_cmd(0x21); oled_cmd(0); oled_cmd(127);
    oled_cmd(0x22); oled_cmd(0); oled_cmd(7);
    for (int i = 0; i < 128*8; i += 16) {
        uint8_t chunk[17];
        chunk[0] = 0x40;
        memcpy(chunk+1, oled_buf+i, 16);
        i2c_master_write_to_device(I2C_MASTER_NUM, SSD1306_ADDR, chunk, 17, pdMS_TO_TICKS(50));
    }
}

static void oled_putchar(int col, int page, char c) {
    if (c < 32 || c > 127) c = ' ';
    const uint8_t *g = font5x7[c - 32];
    int base = page * 128 + col;
    if (base + 5 > 128*8) return;
    for (int i = 0; i < 5; i++) oled_buf[base + i] = g[i];
    oled_buf[base + 5] = 0x00;
}

static void oled_print(int col, int page, const char *str) {
    while (*str && col < 122) {
        oled_putchar(col, page, *str++);
        col += 6;
    }
}

static void oled_clear(void) { memset(oled_buf, 0, sizeof(oled_buf)); }

/* ═══════════════════════════════════════
   WiFi
   ═══════════════════════════════════════ */
static void wifi_event_handler(void *arg, esp_event_base_t base,
                                int32_t id, void *data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < WIFI_MAX_RETRY) { esp_wifi_connect(); s_retry_num++; }
        else xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "IP: " IPSTR, IP2STR(&ev->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static bool wifi_init_sta(void) {
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                        wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                        wifi_event_handler, NULL, NULL);
    wifi_config_t wc = {
        .sta = { .ssid = WIFI_SSID, .password = WIFI_PASSWORD,
                 .threshold.authmode = WIFI_AUTH_WPA_WPA2_PSK }
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                        pdFALSE, pdFALSE, portMAX_DELAY);
    return (bits & WIFI_CONNECTED_BIT) != 0;
}

/* ═══════════════════════════════════════
   OWM fetch
   ═══════════════════════════════════════ */
static esp_err_t http_evt(esp_http_client_event_t *evt) {
    if (evt->event_id == HTTP_EVENT_ON_DATA &&
        !esp_http_client_is_chunked_response(evt->client)) {
        if (http_len + evt->data_len < (int)sizeof(http_buf) - 1) {
            memcpy(http_buf + http_len, evt->data, evt->data_len);
            http_len += evt->data_len;
            http_buf[http_len] = '\0';
        }
    }
    return ESP_OK;
}

static float parse_float(const char *json, const char *key) {
    char s[64]; snprintf(s, sizeof(s), "\"%s\":", key);
    const char *p = strstr(json, s);
    if (!p) return -999.0f;
    p += strlen(s);
    while (*p == ' ') p++;
    return strtof(p, NULL);
}

static void parse_str(const char *json, const char *key, char *out, size_t n) {
    char s[64]; snprintf(s, sizeof(s), "\"%s\":\"", key);
    const char *p = strstr(json, s);
    if (!p) { strncpy(out, "N/A", n); return; }
    p += strlen(s); size_t i = 0;
    while (*p && *p != '"' && i < n-1) out[i++] = *p++;
    out[i] = '\0';
}

static void fetch_owm(void) {
    char url[256];
    snprintf(url, sizeof(url),
        "http://api.openweathermap.org/data/2.5/weather"
        "?lat=%s&lon=%s&appid=%s&units=metric",
        CITY_LAT, CITY_LON, OWM_API_KEY);
    http_len = 0;
    memset(http_buf, 0, sizeof(http_buf));
    esp_http_client_config_t cfg = {
        .url = url, .event_handler = http_evt, .timeout_ms = 10000
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (esp_http_client_perform(c) == ESP_OK &&
        esp_http_client_get_status_code(c) == 200) {
        xSemaphoreTake(g_mutex, portMAX_DELAY);
        g_owm.owm_temp     = parse_float(http_buf, "temp");
        g_owm.owm_humidity = parse_float(http_buf, "humidity");
        parse_str(http_buf, "description", g_owm.owm_desc, sizeof(g_owm.owm_desc));
        g_owm.owm_valid    = true;
        xSemaphoreGive(g_mutex);
        ESP_LOGI(TAG, "OWM: %.1f C  %.0f%%  %s",
                 g_owm.owm_temp, g_owm.owm_humidity, g_owm.owm_desc);
    }
    esp_http_client_cleanup(c);
}

/* ═══════════════════════════════════════
   HTTP web server
   ═══════════════════════════════════════ */
static const char *HTML_HEADER =
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<meta http-equiv='refresh' content='5'>"
    "<title>AtmoLink — Eluru</title>"
    "<style>"
    "body{font-family:sans-serif;background:#111;color:#eee;margin:0;padding:20px}"
    "h1{color:#4fc3f7;margin-bottom:4px}"
    "h2{color:#80cbc4;margin-top:28px}"
    ".grid{display:grid;grid-template-columns:1fr 1fr;gap:16px;max-width:480px}"
    ".card{background:#1e1e1e;border-radius:12px;padding:20px;text-align:center}"
    ".val{font-size:2.4em;font-weight:700;color:#4fc3f7}"
    ".lbl{font-size:.85em;color:#aaa;margin-top:4px}"
    ".owm{background:#1a2e1a;border-radius:12px;padding:16px;"
          "max-width:480px;margin-top:16px}"
    ".owm .val{color:#81c784}"
    ".delta{margin-top:8px;color:#aaa;font-size:.85em}"
    ".ts{font-size:.75em;color:#555;margin-top:24px}"
    "</style></head><body>";

static esp_err_t root_handler(httpd_req_t *req) {
    static char buf[3072];   // static = not on stack
    char tb[16], hb[16];

    xSemaphoreTake(g_mutex, portMAX_DELAY);
    float t  = g_sensor.temperature;
    float h  = g_sensor.humidity;
    bool  sv = g_sensor.valid;
    float ot = g_owm.owm_temp;
    float oh = g_owm.owm_humidity;
    char  od[64]; strncpy(od, g_owm.owm_desc, sizeof(od));
    bool  ov = g_owm.owm_valid;
    xSemaphoreGive(g_mutex);

    if (sv) { snprintf(tb,sizeof(tb),"%.1f&deg;C",t); snprintf(hb,sizeof(hb),"%.1f%%",h); }
    else     { strcpy(tb,"--"); strcpy(hb,"--"); }

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send_chunk(req, HTML_HEADER, HTTPD_RESP_USE_STRLEN);

    snprintf(buf, sizeof(buf),
        "<h1>AtmoLink</h1>"
        "<p style='color:#666;font-size:.85em'>Eluru &middot; auto-refresh 5s</p>"
        "<h2>DHT22 &mdash; live</h2>"
        "<div class='grid'>"
        "<div class='card'><div class='val'>%s</div><div class='lbl'>Temperature</div></div>"
        "<div class='card'><div class='val'>%s</div><div class='lbl'>Humidity</div></div>"
        "</div>", tb, hb);
    httpd_resp_send_chunk(req, buf, HTTPD_RESP_USE_STRLEN);

    if (ov) {
        snprintf(buf, sizeof(buf),
            "<h2>OpenWeatherMap &mdash; Eluru reference</h2>"
            "<div class='owm'>"
            "<div class='grid'>"
            "<div class='card'><div class='val'>%.1f&deg;C</div>"
              "<div class='lbl'>OWM temp</div></div>"
            "<div class='card'><div class='val'>%.1f%%</div>"
              "<div class='lbl'>OWM humidity</div></div>"
            "</div>"
            "<div style='margin-top:10px;color:#aaa'>%s</div>"
            "<div class='delta'>DHT22 vs OWM &mdash; "
              "&Delta;T = %.1f&deg;C &nbsp; &Delta;H = %.1f%%</div>"
            "</div>",
            ot, oh, od,
            sv ? t - ot : 0.0f,
            sv ? h - oh : 0.0f);
        httpd_resp_send_chunk(req, buf, HTTPD_RESP_USE_STRLEN);
    }

    httpd_resp_send_chunk(req,
        "<div class='ts'>AtmoLink &mdash; ESP32 + ESP-IDF + DHT22</div>"
        "</body></html>", HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static httpd_handle_t start_webserver(void) {
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = 80;
    cfg.stack_size = 16384; 
    httpd_handle_t srv = NULL;
    if (httpd_start(&srv, &cfg) == ESP_OK) {
        httpd_uri_t uri = { .uri="/", .method=HTTP_GET, .handler=root_handler };
        httpd_register_uri_handler(srv, &uri);
        ESP_LOGI(TAG, "Web server up on port 80");
    }
    return srv;
}

/* ═══════════════════════════════════════
   FreeRTOS tasks
   ═══════════════════════════════════════ */
static void task_dht(void *pv) {
    /* Setup pin once */
    gpio_set_pull_mode(DHT_GPIO, GPIO_PULLUP_ONLY);
    gpio_set_direction(DHT_GPIO, GPIO_MODE_INPUT);
    ESP_LOGI(TAG, "GPIO%d idle: %d (must be 1)", DHT_GPIO,
             gpio_get_level(DHT_GPIO));

    vTaskDelay(pdMS_TO_TICKS(2000)); /* let DHT22 stabilise after power-on */

    dht22_data_t raw;
    while (1) {
        esp_err_t err = dht22_read(DHT_GPIO, &raw);
        xSemaphoreTake(g_mutex, portMAX_DELAY);
        if (err == ESP_OK) {
            g_sensor.temperature = raw.temperature;
            g_sensor.humidity    = raw.humidity;
            g_sensor.valid       = true;
            ESP_LOGI(TAG, "DHT22: %.1f C  %.1f%%",
                     raw.temperature, raw.humidity);
        } else {
            ESP_LOGW(TAG, "DHT22 err: %s", esp_err_to_name(err));
        }
        xSemaphoreGive(g_mutex);
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}

static void task_oled(void *pv) {
    char line[24];
    while (1) {
        xSemaphoreTake(g_mutex, portMAX_DELAY);
        float t  = g_sensor.temperature;
        float h  = g_sensor.humidity;
        bool  sv = g_sensor.valid;
        float ot = g_owm.owm_temp;
        bool  ov = g_owm.owm_valid;
        xSemaphoreGive(g_mutex);

        oled_clear();
        oled_print(0, 0, "   AtmoLink");
        oled_print(0, 1, "-------------");

        if (sv) {
            snprintf(line, sizeof(line), "T: %.1f C", t);
            oled_print(0, 2, line);
            snprintf(line, sizeof(line), "H: %.1f%%", h);
            oled_print(0, 3, line);
        } else {
            oled_print(0, 2, "DHT22: wait..");
        }

        oled_print(0, 4, "OWM ref:");
        if (ov) {
            snprintf(line, sizeof(line), "T: %.1f C", ot);
            oled_print(0, 5, line);
            if (sv) {
                snprintf(line, sizeof(line), "dT: %.1f C", t - ot);
                oled_print(0, 6, line);
            }
        } else {
            oled_print(0, 5, "Fetching..");
        }

        oled_flush();
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

static void task_owm(void *pv) {
    vTaskDelay(pdMS_TO_TICKS(5000)); /* wait for WiFi to fully settle */
    while (1) {
        fetch_owm();
        vTaskDelay(pdMS_TO_TICKS(600000)); /* every 10 min */
    }
}

/* ═══════════════════════════════════════
   app_main
   ═══════════════════════════════════════ */
void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    g_mutex = xSemaphoreCreateMutex();

    ESP_ERROR_CHECK(i2c_master_init());
    oled_init();
    oled_print(0, 0, "AtmoLink");
    oled_print(0, 1, "Booting...");
    oled_flush();

    bool connected = wifi_init_sta();
    oled_clear();
    oled_print(0, 0, "AtmoLink");
    oled_print(0, 1, connected ? "WiFi OK" : "WiFi FAIL");
    oled_flush();

    if (connected) start_webserver();

// NEW — bigger stacks
    xTaskCreate(task_dht,  "dht",  4096, NULL, 5, NULL);
    xTaskCreate(task_oled, "oled", 8192, NULL, 4, NULL);   // was 4096
    if (connected)
        xTaskCreate(task_owm, "owm", 12288, NULL, 3, NULL); // was 8192
}