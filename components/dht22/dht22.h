#ifndef DHT22_H
#define DHT22_H

#include "driver/gpio.h"
#include "esp_err.h"

typedef struct {
    float temperature;
    float humidity;
} dht22_data_t;

esp_err_t dht22_read(gpio_num_t pin, dht22_data_t *data);

#endif