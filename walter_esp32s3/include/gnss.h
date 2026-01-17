/**
 * @file gnss.h
 * @brief GNSS location tracking for Walter ESP32-S3
 *
 * Uses the Sequans GM02SP modem's integrated GNSS receiver.
 * Note: LTE and GNSS cannot operate simultaneously - the modem
 * must switch modes to acquire a GNSS fix.
 *
 * Location is persisted to NVS (non-volatile storage) so the device
 * can report its last known location even after reboot.
 */

#ifndef GNSS_H
#define GNSS_H

#include <Arduino.h>
#include <WalterModem.h>
#include <Preferences.h>
#include "config.h"

// NVS namespace for GNSS storage
#define GNSS_NVS_NAMESPACE  "gnss"

// =============================================================================
// GNSS LOCATION STRUCTURE
// =============================================================================

struct GnssLocation {
    bool valid;              // True if we have a valid fix (fresh or from NVS)
    bool stale;              // True if location is from NVS (not fresh this session)
    float latitude;          // Decimal degrees, negative = South
    float longitude;         // Decimal degrees, negative = West
    float altitude;          // Meters above sea level
    float confidence;        // Estimated accuracy in meters
    uint8_t satelliteCount;  // Number of satellites used
    uint32_t fixTimeMs;      // millis() when fix was acquired (0 if from NVS)
    uint32_t fixTimestamp;   // Unix timestamp of fix (if available)
};

// =============================================================================
// GNSS MANAGER CLASS
// =============================================================================

class GnssManager {
public:
    GnssManager();

    /**
     * @brief Initialize GNSS subsystem
     * @return true if successful
     */
    bool begin();

    /**
     * @brief Attempt to get a GNSS fix
     *
     * This will temporarily disable LTE if connected. The modem must be
     * in MINIMUM operational state for GNSS to work.
     *
     * @param timeoutSec Maximum time to wait for fix (seconds)
     * @param maxAttempts Number of fix attempts
     * @return true if valid fix obtained
     */
    bool acquireFix(uint32_t timeoutSec = GNSS_FIX_TIMEOUT_SEC,
                    uint8_t maxAttempts = GNSS_FIX_MAX_ATTEMPTS);

    /**
     * @brief Get the current stored location
     * @return Reference to location struct
     */
    const GnssLocation& getLocation() const { return _location; }

    /**
     * @brief Check if we have a valid location
     */
    bool hasValidLocation() const { return _location.valid; }

    /**
     * @brief Check if it's time for a periodic GNSS update
     * @return true if update interval has elapsed
     */
    bool needsUpdate() const;

    /**
     * @brief Get location age in milliseconds
     */
    uint32_t getLocationAgeMs() const;

    /**
     * @brief Format location as JSON fragment for payloads
     * @param buffer Output buffer
     * @param bufferLen Buffer size
     * @return Number of characters written
     */
    size_t toJsonFragment(char* buffer, size_t bufferLen) const;

    /**
     * @brief GNSS event handler callback (called by modem driver)
     * Must not block or call modem methods!
     */
    static void gnssEventHandler(const WalterModemGNSSFix* fix, void* args);

    /**
     * @brief Check if location is stale (loaded from NVS, not fresh)
     */
    bool isLocationStale() const { return _location.stale; }

    /**
     * @brief Check if we have any location (valid fresh or valid stale)
     */
    bool hasAnyLocation() const { return _location.valid; }

    /**
     * @brief Load location from NVS (called automatically in begin())
     * @return true if valid location was loaded from NVS
     */
    bool loadFromNvs();

    /**
     * @brief Save current location to NVS
     * @return true if successful
     */
    bool saveToNvs();

private:
    GnssLocation _location;
    volatile bool _fixReceived;
    volatile bool _fixValid;
    Preferences _prefs;

    // Temporary storage for callback
    static GnssManager* _instance;
    WalterModemGNSSFix _pendingFix;
};

// Global instance
extern GnssManager gnssManager;

#endif // GNSS_H
