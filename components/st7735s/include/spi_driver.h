#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t spi_driver_init(void);
esp_err_t spi_driver_deinit(void);
esp_err_t spi_driver_write_cmd(uint8_t cmd);
esp_err_t spi_driver_write_data(const uint8_t *data, size_t len);
esp_err_t spi_driver_write_bytes(const uint8_t *data, size_t len, bool is_data);
esp_err_t spi_driver_write_framebuffer(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif
