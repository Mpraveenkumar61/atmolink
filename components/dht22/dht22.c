#include "dht22.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#define TAG "DHT22"

static int wait_for_level(gpio_num_t pin, int level, int timeout_us) {
    int t = 0;
    while (gpio_get_level(pin) == level) {
        if (++t > timeout_us) return -1;
        esp_rom_delay_us(1);
    }
    return t;
}

esp_err_t dht22_read(gpio_num_t pin, dht22_data_t *data) {
    uint8_t bits[5] = {0};
    int i, j = 0;

    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    gpio_set_level(pin, 0);
    vTaskDelay(pdMS_TO_TICKS(20)); // pull low for 20ms
    gpio_set_level(pin, 1);
    esp_rom_delay_us(40);
    gpio_set_direction(pin, GPIO_MODE_INPUT);

    if (wait_for_level(pin, 0, 80) < 0) return ESP_FAIL;
    if (wait_for_level(pin, 1, 80) < 0) return ESP_FAIL;

    for (i = 0; i < 40; i++) {
        if (wait_for_level(pin, 0, 50) < 0) return ESP_FAIL;
        int t = wait_for_level(pin, 1, 70);
        if (t < 0) return ESP_FAIL;
        bits[i/8] <<= 1;
        if (t > 30) bits[i/8] |= 1;
    }

    if (((bits[0] + bits[1] + bits[2] + bits[3]) & 0xFF) != bits[4]) {
        ESP_LOGE(TAG, "Checksum error");
        return ESP_FAIL;
    }

    data->humidity = ((bits[0] << 8) + bits[1]) * 0.1;
    data->temperature = (((bits[2] & 0x7F) << 8) + bits[3]) * 0.1;
    if (bits[2] & 0x80) data->temperature *= -1;

    return ESP_OK;
}