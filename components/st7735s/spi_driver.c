#include "spi_driver.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/semphr.h"

static const char *TAG = "spi_driver";
static spi_device_handle_t spi_handle = NULL;
static SemaphoreHandle_t dma_semaphore = NULL;
static volatile bool dma_transfer_complete = false;

#define SPI_HOST_ID SPI2_HOST
#define SPI_SCLK_PIN 18
#define SPI_MOSI_PIN 23
#define SPI_MISO_PIN -1
#define SPI_CS_PIN   5
#define SPI_DC_PIN   21
#define SPI_RST_PIN  22
#define SPI_CLOCK_HZ 10000000

static void IRAM_ATTR dma_transfer_done(spi_transaction_t *trans) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(dma_semaphore, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

static esp_err_t spi_driver_transmit(const uint8_t *data, size_t len, bool is_data)
{
    if (spi_handle == NULL) {
        ESP_LOGE(TAG, "SPI device not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    gpio_set_level(SPI_DC_PIN, is_data ? 1 : 0);

    spi_transaction_t trans = {
        .flags = 0,
        .length = len * 8,
        .tx_buffer = data,
    };

    return spi_device_polling_transmit(spi_handle, &trans);
}

esp_err_t spi_driver_init(void)
{
    esp_err_t err;

    spi_bus_config_t buscfg = {
        .mosi_io_num = SPI_MOSI_PIN,
        .miso_io_num = SPI_MISO_PIN,
        .sclk_io_num = SPI_SCLK_PIN,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = SPI_CLOCK_HZ,
        .mode = 0,
        .spics_io_num = SPI_CS_PIN,
        .queue_size = 7,
        .flags = SPI_DEVICE_HALFDUPLEX,
    };

    err = spi_bus_initialize(SPI_HOST_ID, &buscfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(err));
        return err;
    }

    err = spi_bus_add_device(SPI_HOST_ID, &devcfg, &spi_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_add_device failed: %s", esp_err_to_name(err));
        spi_bus_free(SPI_HOST_ID);
        return err;
    }

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << SPI_DC_PIN) | (1ULL << SPI_RST_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config failed: %s", esp_err_to_name(err));
        spi_bus_remove_device(spi_handle);
        spi_handle = NULL;
        spi_bus_free(SPI_HOST_ID);
        return err;
    }

    gpio_set_level(SPI_DC_PIN, 1);
    gpio_set_level(SPI_RST_PIN, 1);

    dma_semaphore = xSemaphoreCreateBinary();
    if (dma_semaphore == NULL) {
        ESP_LOGE(TAG, "Failed to create DMA semaphore");
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t spi_driver_deinit(void)
{
    esp_err_t err = ESP_OK;

    if (spi_handle != NULL) {
        err = spi_bus_remove_device(spi_handle);
        spi_handle = NULL;
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "spi_bus_remove_device failed: %s", esp_err_to_name(err));
            return err;
        }
    }

    err = spi_bus_free(SPI_HOST_ID);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_free failed: %s", esp_err_to_name(err));
    }

    return err;
}

esp_err_t spi_driver_write_cmd(uint8_t cmd)
{
    return spi_driver_transmit(&cmd, 1, false);
}

esp_err_t spi_driver_write_data(const uint8_t *data, size_t len)
{
    return spi_driver_transmit(data, len, true);
}

esp_err_t spi_driver_write_bytes(const uint8_t *data, size_t len, bool is_data)
{
    return spi_driver_transmit(data, len, is_data);
}


esp_err_t spi_driver_write_framebuffer(const uint8_t *data, size_t len) {
    if (spi_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    gpio_set_level(SPI_DC_PIN, 1); // Data mode

    spi_transaction_t trans = {
        .flags = SPI_TRANS_MODE_QIO, // Use QIO mode for faster transfer
        .length = len * 8,
        .tx_buffer = data,
        .rx_buffer = NULL,
    };

    // For large transfers, use DMA
    if (len > 64) { // Threshold for DMA vs polling
        dma_transfer_complete = false;
        esp_err_t err = spi_device_queue_trans(spi_handle, &trans, portMAX_DELAY);
        if (err != ESP_OK) return err;
        
        // Wait for completion
        if (xSemaphoreTake(dma_semaphore, pdMS_TO_TICKS(1000)) != pdTRUE) {
            return ESP_ERR_TIMEOUT;
        }
        return ESP_OK;
    } else {
        return spi_device_polling_transmit(spi_handle, &trans);
    }
}