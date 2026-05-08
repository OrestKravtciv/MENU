#include "framebuffer.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "framebuffer";

static inline uint16_t framebuffer_color_to_be(uint16_t color) {
    return (uint16_t)((color >> 8) | (color << 8));
}

esp_err_t framebuffer_init(framebuffer_t *fb) {
    if (fb == NULL) return ESP_ERR_INVALID_ARG;
    
    // Allocate buffers in DMA-capable memory
    fb->buffer_a = (uint16_t *)heap_caps_malloc(FRAMEBUFFER_SIZE * sizeof(uint16_t), 
                                                MALLOC_CAP_DMA);
    fb->buffer_b = (uint16_t *)heap_caps_malloc(FRAMEBUFFER_SIZE * sizeof(uint16_t), 
                                                MALLOC_CAP_DMA);
    
    if (fb->buffer_a == NULL || fb->buffer_b == NULL) {
        ESP_LOGE(TAG, "Failed to allocate framebuffers");
        return ESP_ERR_NO_MEM;
    }
    
    fb->draw_buffer = fb->buffer_a;
    fb->send_buffer = fb->buffer_b;
    
    fb->dma_semaphore = xSemaphoreCreateBinary();
    if (fb->dma_semaphore == NULL) {
        ESP_LOGE(TAG, "Failed to create DMA semaphore");
        return ESP_ERR_NO_MEM; 
    }
    
    // Initialize semaphores
    xSemaphoreGive(fb->dma_semaphore);
    
    return ESP_OK;
}

void framebuffer_deinit(framebuffer_t *fb) {
    if (fb == NULL) return;
    
    if (fb->dma_semaphore) {
        vSemaphoreDelete(fb->dma_semaphore);
    }
    
    if (fb->buffer_a) {
        heap_caps_free(fb->buffer_a);
    }
    
    if (fb->buffer_b) {
        heap_caps_free(fb->buffer_b);
    }
}

void framebuffer_swap(framebuffer_t *fb) {
    if (fb == NULL) return;
    
    // Swap buffers
    uint16_t *temp = fb->draw_buffer;
    fb->draw_buffer = fb->send_buffer;
    fb->send_buffer = temp;
}

void framebuffer_clear(framebuffer_t *fb, uint16_t color) {
    if (fb == NULL || fb->draw_buffer == NULL) return;
    
    uint16_t be_color = framebuffer_color_to_be(color);
    for (size_t i = 0; i < FRAMEBUFFER_SIZE; i++) {
        fb->draw_buffer[i] = be_color;
    }
}

void framebuffer_draw_pixel(framebuffer_t *fb, uint16_t x, uint16_t y, uint16_t color) {
    if (fb == NULL || fb->draw_buffer == NULL) return;
    if (x >= FRAMEBUFFER_WIDTH || y >= FRAMEBUFFER_HEIGHT) return;
    
    size_t index = (y * FRAMEBUFFER_WIDTH) + x;
    fb->draw_buffer[index] = framebuffer_color_to_be(color);
}

void framebuffer_fill_rect(framebuffer_t *fb, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
    if (fb == NULL || fb->draw_buffer == NULL) return;
    
    for (uint16_t dy = 0; dy < h; dy++) {
        for (uint16_t dx = 0; dx < w; dx++) {
            uint16_t px = x + dx;
            uint16_t py = y + dy;
            if (px < FRAMEBUFFER_WIDTH && py < FRAMEBUFFER_HEIGHT) {
                framebuffer_draw_pixel(fb, px, py, color);
            }
        }
    }
}