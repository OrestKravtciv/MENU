#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// ST7735S Commands
#define ST7735S_SWRESET 0x01
#define ST7735S_SLPOUT  0x11
#define ST7735S_NORON   0x13
#define ST7735S_INVOFF  0x20
#define ST7735S_DISPON  0x29
#define ST7735S_CASET   0x2A
#define ST7735S_RASET   0x2B
#define ST7735S_RAMWR   0x2C
#define ST7735S_COLMOD  0x3A
#define ST7735S_MADCTL  0x36

// Display dimensions
#define ST7735S_WIDTH  128
#define ST7735S_HEIGHT 160

esp_err_t st7735s_init(void);
esp_err_t st7735s_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
esp_err_t st7735s_write_framebuffer(const uint16_t *framebuffer, size_t len);

#ifdef __cplusplus
}
#endif