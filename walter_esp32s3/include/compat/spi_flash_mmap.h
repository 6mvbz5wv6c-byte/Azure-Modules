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

// In ESP-IDF 4.4.x with Arduino framework, the mmap functions
// are declared in esp_spi_flash.h
#include "esp_spi_flash.h"

// If the above doesn't provide spi_flash_mmap_handle_t, define it
#ifndef SPI_FLASH_MMAP_DATA
// Memory mapping handle type
typedef uint32_t spi_flash_mmap_handle_t;

// Memory mapping regions
typedef enum {
    SPI_FLASH_MMAP_DATA,    // Map to data memory
    SPI_FLASH_MMAP_INST,    // Map to instruction memory
} spi_flash_mmap_memory_t;
#endif

#endif // _SPI_FLASH_MMAP_H_COMPAT_
