/**
 * @file sdcard_esp32.h
 * @brief ESP32-S3 SD card driver for Altair 8800 emulator
 *
 * Provides SD card initialization and FAT mounting for the ESP32-S3.
 */

#ifndef _SDCARD_ESP32_H_
#define _SDCARD_ESP32_H_

#include <stdbool.h>
#include <stdint.h>

#include "board_config.h"

#define ALTAIR_STORAGE_MOUNT_POINT "/sdcard"

#if ALTAIR_SD_CARD_USE_SDMMC
#define SDMMC_PIN_CLK   ALTAIR_SDMMC_PIN_CLK
#define SDMMC_PIN_CMD   ALTAIR_SDMMC_PIN_CMD
#define SDMMC_PIN_D0    ALTAIR_SDMMC_PIN_D0
#define SDMMC_PIN_D1    ALTAIR_SDMMC_PIN_D1
#define SDMMC_PIN_D2    ALTAIR_SDMMC_PIN_D2
#define SDMMC_PIN_D3    ALTAIR_SDMMC_PIN_D3
#define SDMMC_BUS_WIDTH ALTAIR_SDMMC_BUS_WIDTH
#endif

#if ALTAIR_SD_CARD_USE_SDSPI
#define SDSPI_HOST      ALTAIR_SDSPI_HOST
#define SDSPI_PIN_MOSI  ALTAIR_SDSPI_PIN_MOSI
#define SDSPI_PIN_MISO  ALTAIR_SDSPI_PIN_MISO
#define SDSPI_PIN_SCLK  ALTAIR_SDSPI_PIN_SCLK
#define SDSPI_PIN_CS    ALTAIR_SDSPI_PIN_CS
#define SDSPI_MAX_FREQ_KHZ ALTAIR_SDSPI_MAX_FREQ_KHZ
#endif

// Mount point for file-backed disk images. Kept for existing disk path macros.
#define SDCARD_MOUNT_POINT ALTAIR_STORAGE_MOUNT_POINT

/**
 * @brief Initialize the SD card interface and mount FAT filesystem
 * 
 * @return true on success, false on failure
 */
bool sdcard_esp32_init(void);

/**
 * @brief Unmount SD card and free resources
 */
void sdcard_esp32_deinit(void);

/**
 * @brief Get total size of SD card in bytes
 * 
 * @return Total size in bytes, or 0 if not mounted
 */
uint64_t sdcard_esp32_get_total_bytes(void);

/**
 * @brief Get used space on SD card in bytes
 * 
 * @return Used space in bytes, or 0 if not mounted
 */
uint64_t sdcard_esp32_get_used_bytes(void);

/**
 * @brief Check if SD card is mounted
 * 
 * @return true if mounted, false otherwise
 */
bool sdcard_esp32_is_mounted(void);

#endif // _SDCARD_ESP32_H_
