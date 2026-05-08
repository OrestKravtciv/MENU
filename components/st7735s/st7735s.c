#include "st7735s.h"
#include "spi_driver.h"
#include "framebuffer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include <stdbool.h>

static const char *TAG = "ST7735S";
static bool current_window_valid = false;
static uint16_t current_window_x0;
static uint16_t current_window_y0;
static uint16_t current_window_x1;
static uint16_t current_window_y1;

static bool window_equals(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    return current_window_valid
        && current_window_x0 == x0
        && current_window_y0 == y0
        && current_window_x1 == x1
        && current_window_y1 == y1;
}

esp_err_t st7735s_init(void) {
    esp_err_t err;
    
    // Hardware reset
    gpio_set_level(SPI_RST_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(SPI_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Software reset
    err = spi_driver_write_cmd(ST7735S_SWRESET);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(150));
    
    // Sleep out
    err = spi_driver_write_cmd(ST7735S_SLPOUT);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(150));

    // Memory access control (RGB order, etc.)
    err = spi_driver_write_cmd(ST7735S_MADCTL);
    if (err != ESP_OK) return err;
    uint8_t madctl_data = 0x00; // RGB order, default orientation
    err = spi_driver_write_data(&madctl_data, 1);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(10));

    // Set color mode to 16-bit
    err = spi_driver_write_cmd(ST7735S_COLMOD);
    if (err != ESP_OK) return err;
    uint8_t colmod_data = 0x05; // 16-bit color
    err = spi_driver_write_data(&colmod_data, 1);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(10));

    // Display OFF
    err = spi_driver_write_cmd(0x28); // Display OFF command
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Display ON
    err = spi_driver_write_cmd(ST7735S_DISPON); // Display ON command
    vTaskDelay(pdMS_TO_TICKS(100));
    
    return ESP_OK;
}

esp_err_t st7735s_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    esp_err_t err;
    
    // Column address set
    err = spi_driver_write_cmd(ST7735S_CASET);
    if (err != ESP_OK) return err;
    uint8_t caset_data[] = {x0 >> 8, x0 & 0xFF, x1 >> 8, x1 & 0xFF};
    err = spi_driver_write_data(caset_data, 4);
    if (err != ESP_OK) return err;
    
    // Row address set
    err = spi_driver_write_cmd(ST7735S_RASET);
    if (err != ESP_OK) return err;
    uint8_t raset_data[] = {y0 >> 8, y0 & 0xFF, y1 >> 8, y1 & 0xFF};
    err = spi_driver_write_data(raset_data, 4);
    if (err != ESP_OK) return err;

    current_window_valid = true;
    current_window_x0 = x0;
    current_window_y0 = y0;
    current_window_x1 = x1;
    current_window_y1 = y1;
    
    return ESP_OK;
}

esp_err_t st7735s_write_framebuffer(framebuffer_t *fb, const uint16_t *framebuffer, size_t len) {
    esp_err_t err;
    
    // Set full screen window only if it changed
    if (!window_equals(0, 0, ST7735S_WIDTH - 1, ST7735S_HEIGHT - 1)) {
        err = st7735s_set_window(0, 0, ST7735S_WIDTH - 1, ST7735S_HEIGHT - 1);
        if (err != ESP_OK) return err;
    }
    
    // Memory write command
    err = spi_driver_write_cmd(ST7735S_RAMWR);
    if (err != ESP_OK) return err;
    
    // `framebuffer` already stores pixel words in big-endian order so it can be sent directly.
    size_t byte_len = len * 2;
    return spi_driver_write_framebuffer((const uint8_t *)framebuffer, byte_len);
}