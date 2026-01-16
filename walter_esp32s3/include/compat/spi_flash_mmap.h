/**
 * @file spi_flash_mmap.h
 * @brief Compatibility shim for WalterModem library
 *
 * The WalterModem library expects spi_flash_mmap.h in a location
 * that differs across ESP-IDF versions. This shim provides the
 * necessary includes for Arduino ESP32 framework compatibility.
 *
 * In ESP-IDF 4.4.x with Arduino framework, the mmap functions
 * are declared in esp_spi_flash.h. In newer ESP-IDF versions,
 * they moved to esp_flash_mmap.h or spi_flash_mmap.h.
 */

#ifndef _SPI_FLASH_MMAP_H_COMPAT_
#define _SPI_FLASH_MMAP_H_COMPAT_

// Include the ESP-IDF header that contains the mmap definitions
// This header defines spi_flash_mmap_memory_t and related types
#include "esp_spi_flash.h"

// No custom definitions needed - esp_spi_flash.h provides everything
// The original definitions were causing conflicts because:
// - SPI_FLASH_MMAP_DATA is an enum value, not a macro
// - The #ifndef check doesn't work for enum values
// - esp_spi_flash.h is already included via Arduino.h before this check

#endif // _SPI_FLASH_MMAP_H_COMPAT_
