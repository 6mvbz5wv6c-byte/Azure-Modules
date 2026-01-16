/**
 * @file gnss.cpp
 * @brief GNSS location tracking implementation
 *
 * The Sequans GM02SP modem cannot do LTE and GNSS simultaneously.
 * This module handles the mode switching required for GNSS fixes.
 *
 * GNSS acquisition times:
 * - Cold start: 30-60 seconds (up to 12+ minutes in poor conditions)
 * - Warm start: 15-30 seconds
 * - Hot start: 1-10 seconds
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

    LOG_PRINTLN("[GNSS] GNSS subsystem initialized");
    return true;
}

// =============================================================================
// GNSS FIX ACQUISITION
// =============================================================================

bool GnssManager::acquireFix(uint32_t timeoutSec, uint8_t maxAttempts) {
    LOG_PRINTLN("[GNSS] ========== GNSS FIX ACQUISITION ==========");
    LOG_PRINTF("[GNSS] Total timeout: %u sec, Max attempts: %u\n", timeoutSec, maxAttempts);
    LOG_PRINTLN("[GNSS] Note: Cold start can take 30-60+ seconds");

    // Step 1: Set modem to MINIMUM state for GNSS (disables LTE radio)
    LOG_PRINTLN("[GNSS] Step 1: Setting modem to MINIMUM state...");
    if (!WalterModem::setOpState(WALTER_MODEM_OPSTATE_MINIMUM)) {
        LOG_PRINTLN("[GNSS] ERROR: Failed to set MINIMUM state");
        return false;
    }
    LOG_PRINTLN("[GNSS] MINIMUM state set successfully");

    // Wait for modem to stabilize after state change
    LOG_PRINTLN("[GNSS] Waiting for modem to stabilize (2 seconds)...");
    delay(2000);

    // Step 2: Configure GNSS acquisition mode
    LOG_PRINTLN("[GNSS] Step 2: Configuring GNSS...");
    WalterModemGNSSAcqMode acqMode;
    if (_location.valid) {
        acqMode = WALTER_MODEM_GNSS_ACQ_MODE_HOT_START;
        LOG_PRINTLN("[GNSS] Using HOT START (have previous fix)");
    } else {
        acqMode = WALTER_MODEM_GNSS_ACQ_MODE_COLD_WARM_START;
        LOG_PRINTLN("[GNSS] Using COLD/WARM START (no previous fix)");
    }

    if (!WalterModem::gnssConfig(WALTER_MODEM_GNSS_SENS_MODE_HIGH, acqMode)) {
        LOG_PRINTLN("[GNSS] WARNING: gnssConfig failed, trying with defaults...");
        if (!WalterModem::gnssConfig()) {
            LOG_PRINTLN("[GNSS] ERROR: gnssConfig failed completely");
            // Continue anyway - might work
        }
    }
    LOG_PRINTLN("[GNSS] GNSS configured");

    // Wait a bit after configuration
    delay(500);

    bool success = false;
    uint32_t startTime = millis();
    uint32_t totalTimeoutMs = timeoutSec * 1000;

    // Calculate per-attempt timeout (at least 30 seconds for cold start)
    uint32_t perAttemptTimeout = totalTimeoutMs / maxAttempts;
    perAttemptTimeout = max(perAttemptTimeout, (uint32_t)30000);  // At least 30 seconds per attempt
    LOG_PRINTF("[GNSS] Per-attempt timeout: %u ms\n", perAttemptTimeout);

    for (uint8_t attempt = 1; attempt <= maxAttempts; attempt++) {
        LOG_PRINTF("[GNSS] ---- Fix attempt %u/%u ----\n", attempt, maxAttempts);

        _fixReceived = false;
        _fixValid = false;

        // Step 3: Request a GNSS fix
        LOG_PRINTLN("[GNSS] Requesting GNSS fix (gnssPerformAction)...");
        if (!WalterModem::gnssPerformAction()) {
            LOG_PRINTLN("[GNSS] ERROR: gnssPerformAction() returned false");
            LOG_PRINTLN("[GNSS] This usually means the modem is not ready for GNSS");
            LOG_PRINTLN("[GNSS] Waiting 3 seconds before retry...");
            delay(3000);
            continue;
        }
        LOG_PRINTLN("[GNSS] GNSS fix request accepted, waiting for satellites...");

        // Step 4: Wait for fix with timeout
        uint32_t attemptStart = millis();
        uint32_t dotCounter = 0;

        while (!_fixReceived) {
            delay(1000);  // Check every second
            dotCounter++;

            // Print progress every 5 seconds
            if (dotCounter % 5 == 0) {
                LOG_PRINTF("[GNSS] Still waiting... (%u sec)\n", dotCounter);
            } else {
                LOG_PRINT(".");
            }

            // Check per-attempt timeout
            if (millis() - attemptStart > perAttemptTimeout) {
                LOG_PRINTLN("\n[GNSS] Attempt timeout reached");
                break;
            }

            // Check total timeout
            if (millis() - startTime > totalTimeoutMs) {
                LOG_PRINTLN("\n[GNSS] Total timeout exceeded");
                break;
            }
        }

        if (_fixReceived) {
            LOG_PRINTLN("\n[GNSS] Fix data received from modem!");
            LOG_PRINTF("[GNSS] Raw data: lat=%.6f, lon=%.6f, height=%.1f, conf=%.1f, sats=%u\n",
                      _pendingFix.latitude, _pendingFix.longitude,
                      _pendingFix.height, _pendingFix.estimatedConfidence,
                      _pendingFix.satCount);

            // Check if fix meets confidence threshold
            if (_pendingFix.estimatedConfidence <= GNSS_MAX_CONFIDENCE &&
                _pendingFix.estimatedConfidence > 0) {
                _location.valid = true;
                _location.latitude = _pendingFix.latitude;
                _location.longitude = _pendingFix.longitude;
                _location.altitude = _pendingFix.height;
                _location.confidence = _pendingFix.estimatedConfidence;
                _location.satelliteCount = _pendingFix.satCount;
                _location.fixTimeMs = millis();
                _location.fixTimestamp = time(nullptr);

                LOG_PRINTF("[GNSS] VALID FIX: %.6f, %.6f (alt: %.1fm, accuracy: %.1fm, sats: %u)\n",
                          _location.latitude, _location.longitude,
                          _location.altitude, _location.confidence,
                          _location.satelliteCount);

                success = true;
                break;
            } else {
                LOG_PRINTF("[GNSS] Fix rejected - confidence %.1fm exceeds %.1fm threshold\n",
                          _pendingFix.estimatedConfidence, GNSS_MAX_CONFIDENCE);
            }
        }

        // Check total timeout before next attempt
        if (millis() - startTime > totalTimeoutMs) {
            LOG_PRINTLN("[GNSS] Total timeout exceeded, stopping attempts");
            break;
        }

        // Brief pause between attempts
        if (attempt < maxAttempts) {
            LOG_PRINTLN("[GNSS] Waiting 2 seconds before next attempt...");
            delay(2000);
        }
    }

    if (!success) {
        LOG_PRINTLN("[GNSS] Failed to acquire valid GNSS fix");
    }

    // Step 5: Return modem to NO_RF state (ready for LTE configuration)
    LOG_PRINTLN("[GNSS] Returning modem to NO_RF state...");
    if (!WalterModem::setOpState(WALTER_MODEM_OPSTATE_NO_RF)) {
        LOG_PRINTLN("[GNSS] WARNING: Failed to set NO_RF state");
    }
    delay(1000);  // Wait for state change

    LOG_PRINTLN("[GNSS] ==========================================");
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
