#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FRAMEBUFFER_WIDTH  128
#define FRAMEBUFFER_HEIGHT 160
#define FRAMEBUFFER_SIZE   (FRAMEBUFFER_WIDTH * FRAMEBUFFER_HEIGHT)

typedef struct {
    uint16_t *buffer_a;
    uint16_t *buffer_b;
    uint16_t *draw_buffer;
    uint16_t *send_buffer;
    SemaphoreHandle_t dma_semaphore;
} framebuffer_t;

esp_err_t framebuffer_init(framebuffer_t *fb);
void framebuffer_deinit(framebuffer_t *fb);
void framebuffer_swap(framebuffer_t *fb);
void framebuffer_clear(framebuffer_t *fb, uint16_t color);
void framebuffer_draw_pixel(framebuffer_t *fb, uint16_t x, uint16_t y, uint16_t color);
void framebuffer_fill_rect(framebuffer_t *fb, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);

#ifdef __cplusplus
}
#endif