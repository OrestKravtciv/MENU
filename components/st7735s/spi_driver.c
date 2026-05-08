#include "spi_driver.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/semphr.h"

static const char *TAG = "spi_driver";
static spi_device_handle_t spi_handle = NULL;
static SemaphoreHandle_t dma_semaphore = NULL;
static SemaphoreHandle_t framebuffer_done_semaphore = NULL;
static volatile int framebuffer_dma_chunks_remaining = 0;
static int framebuffer_queued_chunks = 0;
static volatile bool framebuffer_send_in_progress = false;
static int framebuffer_transaction_user_marker = 0;
static spi_transaction_t *framebuffer_transactions = NULL;
#define SPI_MAX_TRANSFER_BYTES 20480
#define SPI_POLLING_THRESHOLD_BYTES 16

static void IRAM_ATTR dma_transfer_done(spi_transaction_t *trans)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    if (trans->user == &framebuffer_transaction_user_marker) {
        // Фреймбуферна транзакція — тільки лічильник
        if (framebuffer_dma_chunks_remaining > 0) {
            framebuffer_dma_chunks_remaining--;
            if (framebuffer_dma_chunks_remaining == 0) {
                framebuffer_send_in_progress = false;
                xSemaphoreGiveFromISR(framebuffer_done_semaphore,
                                      &xHigherPriorityTaskWoken);
            }
        }
    } else {
        // ✅ Звичайна транзакція — сигналізуємо spi_driver_transmit
        xSemaphoreGiveFromISR(dma_semaphore, &xHigherPriorityTaskWoken);
    }

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
        .user = (void*)is_data,
        .length = len * 8,
        .tx_buffer = data,
    };

    if (len <= SPI_POLLING_THRESHOLD_BYTES) {
        return spi_device_polling_transmit(spi_handle, &trans);
    }

    ESP_LOGI(TAG, "Using DMA for SPI transfer: len=%zu", len);

    esp_err_t err = spi_device_queue_trans(spi_handle, &trans, portMAX_DELAY);
    if (err != ESP_OK) {
        return err;
    }

    if (xSemaphoreTake(dma_semaphore, pdMS_TO_TICKS(5000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    spi_transaction_t *ret_trans;
    err = spi_device_get_trans_result(spi_handle, &ret_trans, portMAX_DELAY);
    return err;
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
        .max_transfer_sz = SPI_MAX_TRANSFER_BYTES + 8,
    };

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = SPI_CLOCK_HZ,
        .mode = 0,
        .spics_io_num = SPI_CS_PIN,
        .queue_size = 7,
        .post_cb = dma_transfer_done,
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
        .pin_bit_mask = (1UL << SPI_CS_PIN) | (1ULL << SPI_DC_PIN) | (1ULL << SPI_RST_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
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
        spi_bus_remove_device(spi_handle);
        spi_handle = NULL;
        spi_bus_free(SPI_HOST_ID);
        return ESP_ERR_NO_MEM;
    }

    framebuffer_done_semaphore = xSemaphoreCreateBinary();
    if (framebuffer_done_semaphore == NULL) {
        ESP_LOGE(TAG, "Failed to create framebuffer done semaphore");
        vSemaphoreDelete(dma_semaphore);
        spi_bus_remove_device(spi_handle);
        spi_handle = NULL;
        spi_bus_free(SPI_HOST_ID);
        return ESP_ERR_NO_MEM;
    }

    // Mark the completed state available until a framebuffer send begins.
    xSemaphoreGive(framebuffer_done_semaphore);

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

    if (dma_semaphore != NULL) {
        vSemaphoreDelete(dma_semaphore);
        dma_semaphore = NULL;
    }

    if (framebuffer_done_semaphore != NULL) {
        vSemaphoreDelete(framebuffer_done_semaphore);
        framebuffer_done_semaphore = NULL;
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


esp_err_t spi_driver_write_framebuffer(const uint8_t *data, size_t len)
{
    if (spi_handle == NULL) {
        ESP_LOGE(TAG, "SPI device not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    gpio_set_level(SPI_DC_PIN, 1);

    if (framebuffer_send_in_progress) {
        ESP_LOGE(TAG, "Framebuffer send already in progress");
        return ESP_ERR_INVALID_STATE;
    }

    // Calculate number of chunks
    size_t num_chunks = 0;
    size_t offset = 0;
    while (offset < len) {
        size_t chunk_len = (len - offset > SPI_MAX_TRANSFER_BYTES)
                           ? SPI_MAX_TRANSFER_BYTES : (len - offset);
        num_chunks++;
        offset += chunk_len;
    }

    // ✅ Allocate ALL transaction descriptors upfront on the heap.
    // Stack allocations inside the loop would go out of scope while
    // DMA is still reading them, causing corruption.
    spi_transaction_t *transactions = calloc(num_chunks, sizeof(spi_transaction_t));
    if (transactions == NULL) {
        ESP_LOGE(TAG, "Failed to allocate transaction descriptors");
        return ESP_ERR_NO_MEM;
    }

    framebuffer_dma_chunks_remaining = num_chunks;
    framebuffer_queued_chunks = num_chunks;
    framebuffer_send_in_progress = true;

    offset = 0;
    for (size_t i = 0; i < num_chunks; i++) {
        size_t chunk_len = (len - offset > SPI_MAX_TRANSFER_BYTES)
                           ? SPI_MAX_TRANSFER_BYTES : (len - offset);

        // ✅ Fill the pre-allocated slot — not a local variable
        transactions[i].user      = &framebuffer_transaction_user_marker;
        transactions[i].flags     = 0;
        transactions[i].length    = chunk_len * 8;
        transactions[i].tx_buffer = data + offset;
        transactions[i].rx_buffer = NULL;

        esp_err_t err = spi_device_queue_trans(spi_handle,
                                               &transactions[i],
                                               portMAX_DELAY);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to queue chunk %zu", i);
            framebuffer_send_in_progress = false;
            framebuffer_dma_chunks_remaining = 0;
            free(transactions);
            return err;
        }

        offset += chunk_len;
    }

    // ✅ Store the pointer so it can be freed after DMA finishes.
    // Do NOT free here — DMA is still reading these descriptors.
    framebuffer_transactions = transactions;

    return ESP_OK;
}

esp_err_t spi_driver_wait_framebuffer_done(void)
{
    if (xSemaphoreTake(framebuffer_done_semaphore, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(TAG, "Timeout waiting for framebuffer send completion");
        return ESP_ERR_TIMEOUT;
    }

    for (int i = 0; i < framebuffer_queued_chunks; i++) {
        spi_transaction_t *ret_trans;
        spi_device_get_trans_result(spi_handle, &ret_trans, portMAX_DELAY);
    }

    if (framebuffer_transactions != NULL) {
        free(framebuffer_transactions);
        framebuffer_transactions = NULL;
    }
    
    return ESP_OK;
}