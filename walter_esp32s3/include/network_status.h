/**
 * @file network_status.h
 * @brief LTE network status and signal quality monitoring
 *
 * Queries the Sequans GM02SP modem for signal quality metrics:
 * - RSRP: Reference Signal Received Power (signal strength)
 * - RSRQ: Reference Signal Received Quality (signal quality)
 * - SINR/CINR: Signal to Interference plus Noise Ratio (throughput capacity)
 * - RSSI: Received Signal Strength Indicator (total received power)
 */

#ifndef NETWORK_STATUS_H
#define NETWORK_STATUS_H

#include <Arduino.h>
#include <WalterModem.h>
#include "config.h"

// =============================================================================
// SIGNAL QUALITY THRESHOLDS (for reference)
// =============================================================================
// RSRP: -80 dBm (excellent) to -140 dBm (no signal)
// RSRQ: -3 dB (excellent) to -20 dB (poor)
// SINR: 20+ dB (excellent), 0 dB (poor), negative = bad
// RSSI: -65 dBm (excellent) to -110 dBm (poor)

// =============================================================================
// NETWORK STATUS STRUCTURE
// =============================================================================

struct LteSignalQuality {
    int16_t rsrp;           // Reference Signal Received Power (dBm), -140 to -44
    int16_t rsrq;           // Reference Signal Received Quality (dB), -20 to -3
    int16_t sinr;           // Signal to Interference+Noise Ratio (dB), -10 to 30+
    int16_t rssi;           // Received Signal Strength Indicator (dBm)
    bool valid;             // True if values are valid
    uint32_t lastUpdateMs;  // millis() when last updated
};

struct LteCellInfo {
    char operatorName[32];  // Network operator name
    uint16_t mcc;           // Mobile Country Code
    uint16_t mnc;           // Mobile Network Code
    uint32_t cellId;        // Cell ID
    uint16_t tac;           // Tracking Area Code
    uint8_t band;           // LTE band number
    char rat[16];           // Radio Access Technology (LTE-M, NB-IoT)
};

// =============================================================================
// NETWORK STATUS MANAGER CLASS
// =============================================================================

class NetworkStatusManager {
public:
    NetworkStatusManager();

    /**
     * @brief Initialize network status monitoring
     * Call after WalterModem::begin()
     */
    bool begin();

    /**
     * @brief Update signal quality metrics from modem
     * Call periodically (every 30-60 seconds recommended)
     * @return true if successful
     */
    bool updateSignalQuality();

    /**
     * @brief Update cell/operator information
     * Call less frequently (on connect or every few minutes)
     * @return true if successful
     */
    bool updateCellInfo();

    /**
     * @brief Get current signal quality
     */
    const LteSignalQuality& getSignalQuality() const { return _signal; }

    /**
     * @brief Get cell information
     */
    const LteCellInfo& getCellInfo() const { return _cell; }

    /**
     * @brief Check if signal quality is valid and recent
     * @param maxAgeMs Maximum age in milliseconds
     */
    bool hasValidSignal(uint32_t maxAgeMs = 120000) const;

    /**
     * @brief Get RSRP value (-999 if invalid)
     */
    int16_t getRsrp() const { return _signal.valid ? _signal.rsrp : -999; }

    /**
     * @brief Get RSRQ value (-999 if invalid)
     */
    int16_t getRsrq() const { return _signal.valid ? _signal.rsrq : -999; }

    /**
     * @brief Get SINR value (-999 if invalid)
     */
    int16_t getSinr() const { return _signal.valid ? _signal.sinr : -999; }

    /**
     * @brief Get RSSI value (-999 if invalid)
     */
    int16_t getRssi() const { return _signal.valid ? _signal.rssi : -999; }

    /**
     * @brief Get signal strength as human-readable string
     */
    const char* getSignalStrengthText() const;

    /**
     * @brief Format signal quality as JSON fragment for payloads
     * @param buffer Output buffer
     * @param bufferLen Buffer size
     * @return Number of characters written
     */
    size_t toJsonFragment(char* buffer, size_t bufferLen) const;

private:
    LteSignalQuality _signal;
    LteCellInfo _cell;
    bool _initialized;
};

// Global instance
extern NetworkStatusManager networkStatus;

#endif // NETWORK_STATUS_H
