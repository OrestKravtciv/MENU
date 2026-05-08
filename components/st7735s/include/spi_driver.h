#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"


#define SPI_HOST_ID SPI2_HOST
#define SPI_SCLK_PIN 12
#define SPI_MOSI_PIN 11
#define SPI_MISO_PIN -1
#define SPI_CS_PIN   10
#define SPI_DC_PIN   8
#define SPI_RST_PIN  9
#define SPI_CLOCK_HZ 40000000

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t spi_driver_init(void);
esp_err_t spi_driver_deinit(void);
esp_err_t spi_driver_write_cmd(uint8_t cmd);
esp_err_t spi_driver_write_data(const uint8_t *data, size_t len);
esp_err_t spi_driver_write_bytes(const uint8_t *data, size_t len, bool is_data);
esp_err_t spi_driver_write_framebuffer(const uint8_t *data, size_t len);
esp_err_t spi_driver_wait_framebuffer_done(void);

#ifdef __cplusplus
}
#endif
