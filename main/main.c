#include <stdio.h>
#include "esp_log.h"
#include "spi_driver.h"
#include "st7735s.h"
#include "framebuffer.h"

// Embedded bitmap
extern const uint8_t game2_bmp_start[] asm("_binary_game2_bmp_start");
extern const uint8_t game2_bmp_end[] asm("_binary_game2_bmp_end");

static const char *TAG = "MAIN";

// BMP header structure (simplified)
typedef struct {
    uint16_t bfType;
    uint32_t bfSize;
    uint16_t bfReserved1;
    uint16_t bfReserved2;
    uint32_t bfOffBits;
} __attribute__((packed)) BITMAPFILEHEADER;

typedef struct {
    uint32_t biSize;
    int32_t biWidth;
    int32_t biHeight;
    uint16_t biPlanes;
    uint16_t biBitCount;
    uint32_t biCompression;
    uint32_t biSizeImage;
    int32_t biXPelsPerMeter;
    int32_t biYPelsPerMeter;
    uint32_t biClrUsed;
    uint32_t biClrImportant;
} __attribute__((packed)) BITMAPINFOHEADER;

// Function to convert RGB888 to RGB565
uint16_t rgb888_to_rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Initializing SPI driver...");
    esp_err_t err = spi_driver_init();

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI driver: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "Initializing ST7735S display...");
    err = st7735s_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize ST7735S: %s", esp_err_to_name(err));
        spi_driver_deinit();
        return;
    }

    ESP_LOGI(TAG, "Initializing framebuffer...");
    framebuffer_t fb;
    err = framebuffer_init(&fb);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize framebuffer: %s", esp_err_to_name(err));
        spi_driver_deinit();
        return;
    }

    // Load and display bitmap
    const uint8_t *bmp_data = game2_bmp_start;
    size_t bmp_size = game2_bmp_end - game2_bmp_start;

    if (bmp_size < sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER)) {
        ESP_LOGE(TAG, "BMP file too small");
        return;
    }

    BITMAPFILEHEADER *file_header = (BITMAPFILEHEADER *)bmp_data;
    BITMAPINFOHEADER *info_header = (BITMAPINFOHEADER *)(bmp_data + sizeof(BITMAPFILEHEADER));

    if (file_header->bfType != 0x4D42) { // 'BM'
        ESP_LOGE(TAG, "Not a BMP file");
        return;
    }

    int32_t width = info_header->biWidth;
    int32_t height = info_header->biHeight;
    uint16_t bit_count = info_header->biBitCount;

    ESP_LOGI(TAG, "BMP: %dx%d, %d bits", width, height, bit_count);

    if (bit_count != 24 && bit_count != 32) {
        ESP_LOGE(TAG, "Only 24-bit and 32-bit BMP supported");
        return;
    }

    uint8_t bytes_per_pixel = bit_count / 8;
    const uint8_t *pixel_data = bmp_data + file_header->bfOffBits;

    while (1) {
        // Clear framebuffer
        framebuffer_clear(&fb, 0x0000);

        // Scale and display BMP to fullscreen 128x160.
        // If the BMP is 160x128, rotate it to portrait orientation.
        bool rotate90 = (width == FRAMEBUFFER_HEIGHT && height == FRAMEBUFFER_WIDTH);

        for (int y = 0; y < FRAMEBUFFER_HEIGHT; y++) {
            for (int x = 0; x < FRAMEBUFFER_WIDTH; x++) {
                int src_x;
                int src_y;

                if (!rotate90) {
                    // Rotate the landscape BMP into portrait mode (clockwise).
                    src_x = (y * width) / FRAMEBUFFER_HEIGHT;
                    src_y = height - 1 - ((x * height) / FRAMEBUFFER_WIDTH);
                } else {
                    src_x = (x * width) / FRAMEBUFFER_WIDTH;
                    src_y = (y * height) / FRAMEBUFFER_HEIGHT;
                }

                // BMP is bottom-up
                int bmp_y = height - 1 - src_y;
                int index = (bmp_y * width + src_x) * bytes_per_pixel;
                uint8_t b = pixel_data[index];
                uint8_t g = pixel_data[index + 1];
                uint8_t r = pixel_data[index + 2];
                uint16_t color = rgb888_to_rgb565(r, g, b);
                framebuffer_draw_pixel(&fb, x, y, color);
            }
        }

        framebuffer_swap(&fb);
        err = st7735s_write_framebuffer(&fb, fb.send_buffer, FRAMEBUFFER_SIZE);
        if (err != ESP_OK) { ESP_LOGE(TAG, "Frame 1 queue failed"); return; }

        // Поки DMA шле кадр 1 — малюємо кадр 2 в draw_buffer (інший буфер)
        framebuffer_clear(&fb, 0xF800); // червоний

        // ✅ Чекаємо завершення кадру 1 перед будь-якими діями з SPI
        err = spi_driver_wait_framebuffer_done();
        if (err != ESP_OK) { ESP_LOGE(TAG, "Frame 1 wait failed"); return; }

        // --- Кадр 2: червоний ---
        framebuffer_swap(&fb);
        err = st7735s_write_framebuffer(&fb, fb.send_buffer, FRAMEBUFFER_SIZE);
        if (err != ESP_OK) { ESP_LOGE(TAG, "Frame 2 queue failed"); return; }

        // ✅ Чекаємо і кадр 2 — інакше DMA зруйнує стек і дасть білий екран
        err = spi_driver_wait_framebuffer_done();
        if (err != ESP_OK) { ESP_LOGE(TAG, "Frame 2 wait failed"); return; }

        ESP_LOGI(TAG, "Both frames displayed successfully");

    }

}
