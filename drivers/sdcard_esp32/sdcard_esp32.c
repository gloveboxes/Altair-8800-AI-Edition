/**
 * @file sdcard_esp32.c
 * @brief ESP32-S3 SD card driver implementation for Altair 8800 emulator
 *
 * Uses ESP-IDF's SDMMC or SDSPI peripheral with board-specific pin mappings.
 */

#include "sdcard_esp32.h"
#include <string.h>
#include <sys/statvfs.h>
#include "esp_log.h"
#include "esp_vfs_fat.h"
#if CONFIG_ALTAIR_STORAGE_INTERNAL_FLASH
#include "wear_levelling.h"
#endif
#include "sdmmc_cmd.h"
#if ALTAIR_SD_CARD_USE_SDMMC
#include "driver/sdmmc_host.h"
#endif
#if ALTAIR_SD_CARD_USE_SDSPI
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#endif
#include "driver/gpio.h"

static const char* TAG = "SDCARD_ESP32";

#if !CONFIG_ALTAIR_STORAGE_INTERNAL_FLASH
static sdmmc_card_t* s_card = NULL;
#endif
static bool s_mounted = false;
#if CONFIG_ALTAIR_STORAGE_INTERNAL_FLASH
static wl_handle_t s_wl_handle = WL_INVALID_HANDLE;
#endif

bool sdcard_esp32_init(void)
{
    esp_err_t ret;

    if (s_mounted) {
        ESP_LOGI(TAG, "SD card already mounted; skipping re-init");
        return true;
    }

    ESP_LOGI(TAG, "Initializing disk image storage for %s...", ALTAIR_BOARD_NAME);

#if CONFIG_ALTAIR_STORAGE_INTERNAL_FLASH
    ESP_LOGI(TAG, "  Transport: internal flash FAT partition");

    esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = CONFIG_WL_SECTOR_SIZE,
    };

    ret = esp_vfs_fat_spiflash_mount_rw_wl(SDCARD_MOUNT_POINT, "storage",
                                           &mount_config, &s_wl_handle);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount internal flash storage (%s)", esp_err_to_name(ret));
        return false;
    }

    s_mounted = true;
    ESP_LOGI(TAG, "Internal flash storage mounted successfully at %s", SDCARD_MOUNT_POINT);
    return true;
#else

#if ALTAIR_SD_CARD_USE_SDMMC
    ESP_LOGI(TAG, "  Transport: SDMMC");
    ESP_LOGI(TAG, "  CLK: GPIO%d", SDMMC_PIN_CLK);
    ESP_LOGI(TAG, "  CMD: GPIO%d", SDMMC_PIN_CMD);
    ESP_LOGI(TAG, "  D0:  GPIO%d", SDMMC_PIN_D0);
    if (SDMMC_PIN_D3 >= 0) {
        ESP_LOGI(TAG, "  D3:  GPIO%d", SDMMC_PIN_D3);
    }
    if (SDMMC_BUS_WIDTH >= 4) {
        ESP_LOGI(TAG, "  D1:  GPIO%d", SDMMC_PIN_D1);
        ESP_LOGI(TAG, "  D2:  GPIO%d", SDMMC_PIN_D2);
    }
    ESP_LOGI(TAG, "  Bus width: %d-bit", SDMMC_BUS_WIDTH);
#elif ALTAIR_SD_CARD_USE_SDSPI
    ESP_LOGI(TAG, "  Transport: SDSPI");
    ESP_LOGI(TAG, "  MOSI: GPIO%d", SDSPI_PIN_MOSI);
    ESP_LOGI(TAG, "  MISO: GPIO%d", SDSPI_PIN_MISO);
    ESP_LOGI(TAG, "  SCLK: GPIO%d", SDSPI_PIN_SCLK);
    ESP_LOGI(TAG, "  CS:   GPIO%d", SDSPI_PIN_CS);
    ESP_LOGI(TAG, "  Max clock: %d kHz", SDSPI_MAX_FREQ_KHZ);
#elif ALTAIR_SD_CARD_USE_NONE
    ESP_LOGE(TAG, "  Transport: none configured for this board profile");
    ESP_LOGE(TAG, "  GPIO35/36/37/38 are FSPI flash/PSRAM signals on ESP32-S3 modules with Octal PSRAM.");
    ESP_LOGE(TAG, "  Do not connect an SD card to those pins; choose non-FSPI GPIOs for SDSPI.");
    return false;
#endif

#if ALTAIR_SD_CARD_USE_SDMMC || ALTAIR_SD_CARD_USE_SDSPI
    // Options for mounting the filesystem
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 4,
        /* 0 == use the card's existing cluster size; avoids allocating a
         * larger work area than necessary, which matters on boards without
         * PSRAM where the LCD framebuffer competes for internal DRAM. */
        .allocation_unit_size = 0,
    };
#endif

#if ALTAIR_SD_CARD_USE_SDMMC
    // Configure SDMMC host
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = 400;

    // Configure SDMMC slot with board-specific custom pins
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();

    // Set the GPIO pins for SDMMC
    slot_config.clk = SDMMC_PIN_CLK;
    slot_config.cmd = SDMMC_PIN_CMD;
    slot_config.d0 = SDMMC_PIN_D0;

    slot_config.d1 = SDMMC_PIN_D1;
    slot_config.d2 = SDMMC_PIN_D2;
    slot_config.d3 = SDMMC_PIN_D3;
    slot_config.width = SDMMC_BUS_WIDTH;

    // Enable internal pullups on the bus lines
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    gpio_set_pull_mode(SDMMC_PIN_CMD, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(SDMMC_PIN_D0, GPIO_PULLUP_ONLY);
    if (SDMMC_PIN_D1 >= 0) {
        gpio_set_pull_mode(SDMMC_PIN_D1, GPIO_PULLUP_ONLY);
    }
    if (SDMMC_PIN_D2 >= 0) {
        gpio_set_pull_mode(SDMMC_PIN_D2, GPIO_PULLUP_ONLY);
    }
    if (SDMMC_PIN_D3 >= 0) {
        gpio_set_pull_mode(SDMMC_PIN_D3, GPIO_PULLUP_ONLY);
    }

    ESP_LOGI(TAG, "Mounting SD card filesystem at %s...", SDCARD_MOUNT_POINT);

    ret = esp_vfs_fat_sdmmc_mount(SDCARD_MOUNT_POINT, &host, &slot_config,
                                  &mount_config, &s_card);
#elif ALTAIR_SD_CARD_USE_SDSPI
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SDSPI_HOST;
    host.max_freq_khz = SDSPI_MAX_FREQ_KHZ;

    spi_bus_config_t bus_config = {
        .mosi_io_num = SDSPI_PIN_MOSI,
        .miso_io_num = SDSPI_PIN_MISO,
        .sclk_io_num = SDSPI_PIN_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    ret = spi_bus_initialize(host.slot, &bus_config, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus (%s)", esp_err_to_name(ret));
        return false;
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = SDSPI_PIN_CS;
    slot_config.host_id = host.slot;

    ret = esp_vfs_fat_sdspi_mount(SDCARD_MOUNT_POINT, &host, &slot_config,
                                  &mount_config, &s_card);
#endif

    if (ret != ESP_OK) {
#if ALTAIR_SD_CARD_USE_SDSPI
        spi_bus_free(host.slot);
#endif
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem. Check if SD card is formatted as FAT32.");
        } else if (ret == ESP_ERR_NO_MEM) {
            ESP_LOGE(TAG, "Failed to allocate memory for SD card.");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SD card (%s). "
                     "Make sure the card is inserted and the %s pin mapping is correct.",
                     esp_err_to_name(ret), ALTAIR_BOARD_NAME);
        }
        return false;
    }

    s_mounted = true;

    // Print card info
    ESP_LOGI(TAG, "SD card mounted successfully!");
    sdmmc_card_print_info(stdout, s_card);

    return true;
#endif
}

void sdcard_esp32_deinit(void)
{
    if (s_mounted) {
#if CONFIG_ALTAIR_STORAGE_INTERNAL_FLASH
        esp_vfs_fat_spiflash_unmount_rw_wl(SDCARD_MOUNT_POINT, s_wl_handle);
        s_wl_handle = WL_INVALID_HANDLE;
#else
        esp_vfs_fat_sdcard_unmount(SDCARD_MOUNT_POINT, s_card);
#if ALTAIR_SD_CARD_USE_SDSPI
        spi_bus_free(SDSPI_HOST);
#endif
        s_card = NULL;
#endif
        s_mounted = false;
    ESP_LOGI(TAG, "Disk image storage unmounted");
    }
}

uint64_t sdcard_esp32_get_total_bytes(void)
{
#if CONFIG_ALTAIR_STORAGE_INTERNAL_FLASH
    if (!s_mounted) {
        return 0;
    }

    struct statvfs fs_info;
    if (statvfs(SDCARD_MOUNT_POINT, &fs_info) != 0) {
        return 0;
    }

    return (uint64_t)fs_info.f_blocks * fs_info.f_frsize;
#else
    if (!s_mounted || !s_card) {
        return 0;
    }
    
    // Calculate total size from card info
    // sector_size is typically 512 bytes
    uint64_t total = (uint64_t)s_card->csd.capacity * s_card->csd.sector_size;
    return total;
#endif
}

uint64_t sdcard_esp32_get_used_bytes(void)
{
#if CONFIG_ALTAIR_STORAGE_INTERNAL_FLASH
    if (!s_mounted) {
        return 0;
    }

    struct statvfs fs_info;
    if (statvfs(SDCARD_MOUNT_POINT, &fs_info) != 0) {
        return 0;
    }

    uint64_t total = (uint64_t)fs_info.f_blocks * fs_info.f_frsize;
    uint64_t free_bytes = (uint64_t)fs_info.f_bfree * fs_info.f_frsize;
    return total - free_bytes;
#else
    if (!s_mounted) {
        return 0;
    }

    FATFS* fs;
    DWORD free_clusters;
    
    if (f_getfree(SDCARD_MOUNT_POINT, &free_clusters, &fs) != FR_OK) {
        return 0;
    }

    uint64_t total_sectors = (fs->n_fatent - 2) * fs->csize;
    uint64_t free_sectors = free_clusters * fs->csize;
    uint64_t used_sectors = total_sectors - free_sectors;
    
    // Multiply by sector size (typically 512)
    return used_sectors * fs->ssize;
#endif
}

bool sdcard_esp32_is_mounted(void)
{
    return s_mounted;
}
