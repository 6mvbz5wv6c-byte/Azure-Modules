/**
 * @file spi_flash_mmap.h
 * @brief Compatibility shim for WalterModem library
 *
 * The WalterModem library expects spi_flash_mmap.h in a location
 * that differs across ESP-IDF versions. This shim provides the
 * necessary includes for Arduino ESP32 framework compatibility.
 */

#ifndef _SPI_FLASH_MMAP_H_COMPAT_
#define _SPI_FLASH_MMAP_H_COMPAT_

#include "esp_spi_flash.h"

#endif // _SPI_FLASH_MMAP_H_COMPAT_
