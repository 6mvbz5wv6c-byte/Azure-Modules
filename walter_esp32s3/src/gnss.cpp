/**
 * @file gnss.cpp
 * @brief GNSS location tracking implementation
 *
 * The Sequans GM02SP modem cannot do LTE and GNSS simultaneously.
 * This module handles the mode switching required for GNSS fixes.
 */

#include "gnss.h"

// Static instance for callback
GnssManager* GnssManager::_instance = nullptr;

// Global instance
GnssManager gnssManager;

// =============================================================================
// CONSTRUCTOR
// =============================================================================

GnssManager::GnssManager()
    : _fixReceived(false)
    , _fixValid(false)
{
    _location.valid = false;
    _location.latitude = 0.0f;
    _location.longitude = 0.0f;
    _location.altitude = 0.0f;
    _location.confidence = 9999.0f;
    _location.satelliteCount = 0;
    _location.fixTimeMs = 0;
    _location.fixTimestamp = 0;

    _instance = this;
}

// =============================================================================
// INITIALIZATION
// =============================================================================

bool GnssManager::begin() {
    LOG_PRINTLN("[GNSS] Initializing GNSS subsystem...");

    // Set event handler for GNSS fixes
    WalterModem::gnssSetEventHandler(gnssEventHandler, this);
    LOG_PRINTLN("[GNSS] Event handler registered");

    // Configure GNSS with default settings
    // Note: gnssConfig() uses COLD_WARM_START mode by default
    if (!WalterModem::gnssConfig()) {
        LOG_PRINTLN("[GNSS] WARNING: Could not configure GNSS");
        // Continue anyway - might work with defaults
    }

    LOG_PRINTLN("[GNSS] GNSS subsystem initialized");
    return true;
}

// =============================================================================
// GNSS FIX ACQUISITION
// =============================================================================

bool GnssManager::acquireFix(uint32_t timeoutSec, uint8_t maxAttempts) {
    LOG_PRINTLN("[GNSS] Starting GNSS fix acquisition...");
    LOG_PRINTF("[GNSS] Timeout: %u sec, Max attempts: %u\n", timeoutSec, maxAttempts);

    // Put modem in MINIMUM state for GNSS (disables LTE)
    LOG_PRINTLN("[GNSS] Setting modem to MINIMUM state for GNSS...");
    if (!WalterModem::setOpState(WALTER_MODEM_OPSTATE_MINIMUM)) {
        LOG_PRINTLN("[GNSS] ERROR: Failed to set MINIMUM state");
        return false;
    }

    // Small delay for mode switch
    delay(500);

    // Reconfigure for hot start if we have a previous fix
    if (_location.valid) {
        WalterModem::gnssConfig(WALTER_MODEM_GNSS_SENS_MODE_HIGH,
                                WALTER_MODEM_GNSS_ACQ_MODE_HOT_START);
    } else {
        // Cold/warm start for first fix
        WalterModem::gnssConfig(WALTER_MODEM_GNSS_SENS_MODE_HIGH,
                                WALTER_MODEM_GNSS_ACQ_MODE_COLD_WARM_START);
    }

    bool success = false;
    uint32_t startTime = millis();
    uint32_t totalTimeoutMs = timeoutSec * 1000;

    for (uint8_t attempt = 1; attempt <= maxAttempts; attempt++) {
        LOG_PRINTF("[GNSS] Fix attempt %u/%u...\n", attempt, maxAttempts);

        _fixReceived = false;
        _fixValid = false;

        // Request a GNSS fix (uses previously configured settings)
        if (!WalterModem::gnssPerformAction()) {
            LOG_PRINTLN("[GNSS] ERROR: Failed to request GNSS fix");
            delay(1000);
            continue;
        }

        // Wait for fix with timeout
        uint32_t attemptStart = millis();
        uint32_t attemptTimeout = (totalTimeoutMs - (millis() - startTime)) / (maxAttempts - attempt + 1);
        attemptTimeout = max(attemptTimeout, (uint32_t)10000);  // At least 10 seconds

        LOG_PRINTF("[GNSS] Waiting for fix (timeout: %u ms)...\n", attemptTimeout);

        while (!_fixReceived) {
            delay(500);
            LOG_PRINT(".");

            if (millis() - attemptStart > attemptTimeout) {
                LOG_PRINTLN(" timeout");
                break;
            }

            // Check total timeout
            if (millis() - startTime > totalTimeoutMs) {
                LOG_PRINTLN("[GNSS] Total timeout exceeded");
                break;
            }
        }

        if (_fixReceived) {
            LOG_PRINTLN(" fix received!");

            // Process the pending fix from callback
            if (_pendingFix.estimatedConfidence <= GNSS_MAX_CONFIDENCE) {
                _location.valid = true;
                _location.latitude = _pendingFix.latitude;
                _location.longitude = _pendingFix.longitude;
                _location.altitude = _pendingFix.height;  // WalterModem uses 'height' not 'altitude'
                _location.confidence = _pendingFix.estimatedConfidence;
                _location.satelliteCount = _pendingFix.satCount;
                _location.fixTimeMs = millis();
                _location.fixTimestamp = time(nullptr);

                LOG_PRINTF("[GNSS] Valid fix: %.6f, %.6f (alt: %.1fm, conf: %.1fm, sats: %u)\n",
                          _location.latitude, _location.longitude,
                          _location.altitude, _location.confidence,
                          _location.satelliteCount);

                success = true;
                break;
            } else {
                LOG_PRINTF("[GNSS] Fix rejected - confidence %.1fm > %.1fm threshold\n",
                          _pendingFix.estimatedConfidence, GNSS_MAX_CONFIDENCE);
            }
        }

        // Check total timeout
        if (millis() - startTime > totalTimeoutMs) {
            LOG_PRINTLN("[GNSS] Total timeout exceeded");
            break;
        }
    }

    if (!success) {
        LOG_PRINTLN("[GNSS] Failed to acquire valid GNSS fix");
    }

    // Return modem to NO_RF state (ready for LTE configuration)
    LOG_PRINTLN("[GNSS] Returning modem to NO_RF state...");
    WalterModem::setOpState(WALTER_MODEM_OPSTATE_NO_RF);

    return success;
}

// =============================================================================
// EVENT HANDLER (CALLBACK)
// =============================================================================

void GnssManager::gnssEventHandler(const WalterModemGNSSFix* fix, void* args) {
    // This is called from modem driver context - must not block!
    GnssManager* manager = static_cast<GnssManager*>(args);

    if (fix && manager) {
        // Copy fix data for later processing
        memcpy(&manager->_pendingFix, fix, sizeof(WalterModemGNSSFix));
        manager->_fixValid = (fix->estimatedConfidence > 0);
        manager->_fixReceived = true;
    }
}

// =============================================================================
// UTILITY FUNCTIONS
// =============================================================================

bool GnssManager::needsUpdate() const {
#if GNSS_UPDATE_INTERVAL_MS > 0
    if (!_location.valid) {
        return true;  // Always try if we don't have a fix
    }
    return (millis() - _location.fixTimeMs) > GNSS_UPDATE_INTERVAL_MS;
#else
    return false;  // Periodic updates disabled
#endif
}

uint32_t GnssManager::getLocationAgeMs() const {
    if (!_location.valid) {
        return UINT32_MAX;
    }
    return millis() - _location.fixTimeMs;
}

size_t GnssManager::toJsonFragment(char* buffer, size_t bufferLen) const {
    if (!_location.valid) {
        return snprintf(buffer, bufferLen,
            "\"gnss\":{\"valid\":false}");
    }

    return snprintf(buffer, bufferLen,
        "\"gnss\":{"
        "\"valid\":true,"
        "\"lat\":%.6f,"
        "\"lon\":%.6f,"
        "\"alt\":%.1f,"
        "\"accuracy\":%.1f,"
        "\"sats\":%u,"
        "\"ageMs\":%lu"
        "}",
        _location.latitude,
        _location.longitude,
        _location.altitude,
        _location.confidence,
        _location.satelliteCount,
        (unsigned long)getLocationAgeMs()
    );
}
