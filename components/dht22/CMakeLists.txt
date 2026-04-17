idf_component_register(SRCS "dht22.c"
                        INCLUDE_DIRS "."
                        REQUIRES driver freertos esp_rom)