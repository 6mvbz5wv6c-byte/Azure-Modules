/**
 * @file network_status.cpp
 * @brief LTE network status and signal quality monitoring implementation
 *
 * Uses WalterModem::getCellInformation() to query the Sequans GM02SP modem
 * for LTE signal quality metrics (RSRP, RSRQ, SINR, RSSI).
 */

#include "network_status.h"

// Global instance
NetworkStatusManager networkStatus;

// =============================================================================
// CONSTRUCTOR
// =============================================================================

NetworkStatusManager::NetworkStatusManager()
    : _initialized(false)
{
    // Initialize signal quality to invalid
    _signal.rsrp = -999;
    _signal.rsrq = -999;
    _signal.sinr = -999;
    _signal.rssi = -999;
    _signal.valid = false;
    _signal.lastUpdateMs = 0;

    // Initialize cell info
    memset(_cell.operatorName, 0, sizeof(_cell.operatorName));
    _cell.mcc = 0;
    _cell.mnc = 0;
    _cell.cellId = 0;
    _cell.tac = 0;
    _cell.band = 0;
    memset(_cell.rat, 0, sizeof(_cell.rat));
}

// =============================================================================
// INITIALIZATION
// =============================================================================

bool NetworkStatusManager::begin() {
    LOG_PRINTLN("[NetStatus] Initializing network status monitoring...");
    _initialized = true;
    LOG_PRINTLN("[NetStatus] Network status monitoring initialized");
    return true;
}

// =============================================================================
// SIGNAL QUALITY UPDATE
// =============================================================================

bool NetworkStatusManager::updateSignalQuality() {
    if (!_initialized) {
        LOG_PRINTLN("[NetStatus] ERROR: Not initialized");
        return false;
    }

    // Allocate response on heap (WalterModemRsp can be large)
    WalterModemRsp* rsp = (WalterModemRsp*)malloc(sizeof(WalterModemRsp));
    if (rsp == nullptr) {
        LOG_PRINTLN("[NetStatus] ERROR: Failed to allocate response buffer");
        return false;
    }

    bool success = false;

    // Get cell information including signal quality
    // Use SERVING_CELL_WITH_CINR (type 9) to get SINR/CINR values
    if (WalterModem::getCellInformation(WALTER_MODEM_SQNMONI_REPORTS_SERVING_CELL_WITH_CINR, rsp)) {
        // Extract signal quality metrics from cellInformation
        // Values are floats in the structure
        _signal.rsrp = (int16_t)rsp->data.cellInformation.rsrp;
        _signal.rsrq = (int16_t)rsp->data.cellInformation.rsrq;
        _signal.sinr = (int16_t)rsp->data.cellInformation.cinr;  // CINR = SINR
        _signal.rssi = (int16_t)rsp->data.cellInformation.rssi;
        _signal.valid = true;
        _signal.lastUpdateMs = millis();

        // Also update cell info while we have it
        if (rsp->data.cellInformation.netName != nullptr) {
            strncpy(_cell.operatorName, rsp->data.cellInformation.netName, sizeof(_cell.operatorName) - 1);
        }
        _cell.mcc = rsp->data.cellInformation.cc;
        _cell.mnc = rsp->data.cellInformation.nc;
        _cell.cellId = rsp->data.cellInformation.cid;
        _cell.tac = rsp->data.cellInformation.tac;
        _cell.band = rsp->data.cellInformation.band;

        LOG_PRINTF("[NetStatus] Signal: RSRP=%d dBm, RSRQ=%d dB, SINR=%d dB, RSSI=%d dBm\n",
                  _signal.rsrp, _signal.rsrq, _signal.sinr, _signal.rssi);
        LOG_PRINTF("[NetStatus] Cell: %s (MCC=%u, MNC=%u, CID=%lu, Band=%u)\n",
                  _cell.operatorName, _cell.mcc, _cell.mnc,
                  (unsigned long)_cell.cellId, _cell.band);

        success = true;
    } else {
        LOG_PRINTLN("[NetStatus] WARNING: getCellInformation failed - modem may not be connected");
        // Don't invalidate existing data, just note the failure
    }

    free(rsp);
    return success;
}

// =============================================================================
// CELL INFO UPDATE
// =============================================================================

bool NetworkStatusManager::updateCellInfo() {
    // getCellInformation already retrieves cell info, so just call updateSignalQuality
    return updateSignalQuality();
}

// =============================================================================
// UTILITY FUNCTIONS
// =============================================================================

bool NetworkStatusManager::hasValidSignal(uint32_t maxAgeMs) const {
    if (!_signal.valid) {
        return false;
    }
    return (millis() - _signal.lastUpdateMs) < maxAgeMs;
}

const char* NetworkStatusManager::getSignalStrengthText() const {
    if (!_signal.valid) {
        return "Unknown";
    }

    // Classify based on RSRP (primary LTE signal strength indicator)
    // Reference: 3GPP TS 36.133
    if (_signal.rsrp >= -80) {
        return "Excellent";
    } else if (_signal.rsrp >= -90) {
        return "Good";
    } else if (_signal.rsrp >= -100) {
        return "Fair";
    } else if (_signal.rsrp >= -110) {
        return "Poor";
    } else {
        return "Very Poor";
    }
}

size_t NetworkStatusManager::toJsonFragment(char* buffer, size_t bufferLen) const {
    if (!_signal.valid) {
        return snprintf(buffer, bufferLen,
            "\"lte\":{\"valid\":false,\"rssi\":-999}");
    }

    return snprintf(buffer, bufferLen,
        "\"lte\":{"
        "\"valid\":true,"
        "\"rsrp\":%d,"
        "\"rsrq\":%d,"
        "\"sinr\":%d,"
        "\"rssi\":%d,"
        "\"quality\":\"%s\","
        "\"operator\":\"%s\","
        "\"band\":%u,"
        "\"cellId\":%lu,"
        "\"ageMs\":%lu"
        "}",
        _signal.rsrp,
        _signal.rsrq,
        _signal.sinr,
        _signal.rssi,
        getSignalStrengthText(),
        _cell.operatorName,
        _cell.band,
        (unsigned long)_cell.cellId,
        (unsigned long)(millis() - _signal.lastUpdateMs)
    );
}
