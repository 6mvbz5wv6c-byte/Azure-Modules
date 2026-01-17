/**
 * @file gnss_debug.cpp
 * @brief GNSS Location Debug Test Program
 *
 * Tests multiple methods for acquiring location on the Sequans GM02SP:
 * 1. GNSS Cold Start - No prior data
 * 2. GNSS Warm Start - With almanac
 * 3. GNSS Hot Start - With recent ephemeris
 * 4. Different sensitivity modes
 * 5. Cell tower location via AT+SQNCELLLOCATE
 *
 * Compile with: -DGNSS_DEBUG_MODE in platformio.ini
 */

#ifdef GNSS_DEBUG_MODE

#include <Arduino.h>
#include <WalterModem.h>
#include "config.h"

// Test configuration
#define TEST_TIMEOUT_SEC        120     // Max time per GNSS test
#define CELL_LOC_TIMEOUT_SEC    60      // Cell location timeout
#define SETTLE_TIME_MS          5000    // Time between tests

// Expected location (Springdale, AR - for accuracy comparison)
#define EXPECTED_LAT    36.1867
#define EXPECTED_LON    -94.1288

// Test result structure
struct TestResult {
    const char* testName;
    bool success;
    float latitude;
    float longitude;
    float altitude;
    float accuracy;
    uint8_t satellites;
    uint32_t timeToFixMs;
    float distanceError;  // km from expected
};

// Store results for all tests
#define MAX_TESTS 10
TestResult results[MAX_TESTS];
int resultCount = 0;

// Forward declarations
void runAllTests();
bool testGnssColdStart();
bool testGnssWarmStart();
bool testGnssHotStart();
bool testGnssHighSensitivity();
bool testGnssLowSensitivity();
bool testCellTowerLocation();
bool testCellTowerAT();
void printResults();
float haversineDistance(float lat1, float lon1, float lat2, float lon2);

// GNSS callback data
volatile bool gnssFixReceived = false;
volatile bool gnssFixValid = false;
WalterModemGNSSFix lastGnssFix;

void gnssCallback(const WalterModemGNSSFix* fix, void* args) {
    if (fix) {
        memcpy((void*)&lastGnssFix, fix, sizeof(WalterModemGNSSFix));
        gnssFixValid = (fix->estimatedConfidence > 0 && fix->estimatedConfidence < 5000);
        gnssFixReceived = true;
    }
}

void setup() {
    Serial.begin(115200);
    delay(2000);

    Serial.println("\n\n");
    Serial.println("╔══════════════════════════════════════════════════════════════╗");
    Serial.println("║           GNSS LOCATION DEBUG TEST PROGRAM                   ║");
    Serial.println("║           Sequans GM02SP / Walter ESP32-S3                   ║");
    Serial.println("╚══════════════════════════════════════════════════════════════╝");
    Serial.println();
    Serial.printf("Expected location: Springdale, AR (%.4f, %.4f)\n", EXPECTED_LAT, EXPECTED_LON);
    Serial.println();

    // Initialize modem
    Serial.println("[Init] Initializing Walter modem...");
    if (!WalterModem::begin(MODEM_SERIAL)) {
        Serial.println("[Init] ERROR: Failed to initialize modem!");
        while(1) delay(1000);
    }
    Serial.println("[Init] Modem initialized OK");

    // Register GNSS callback
    WalterModem::gnssSetEventHandler(gnssCallback, nullptr);
    Serial.println("[Init] GNSS callback registered");

    // Run all tests
    runAllTests();

    // Print summary
    printResults();
}

void loop() {
    // Nothing to do - tests complete
    delay(10000);
    Serial.println("[Done] Tests complete. Reset to run again.");
}

void runAllTests() {
    Serial.println("\n========== STARTING TEST SUITE ==========\n");

    // Test 1: Cell tower location first (doesn't need GNSS radio switch)
    Serial.println("──────────────────────────────────────────");
    Serial.println("TEST 1: Cell Tower Location (AT+SQNCELLLOCATE)");
    Serial.println("──────────────────────────────────────────");
    testCellTowerLocation();
    delay(SETTLE_TIME_MS);

    // Test 2: Another cell location method
    Serial.println("\n──────────────────────────────────────────");
    Serial.println("TEST 2: Cell Tower AT Command Query");
    Serial.println("──────────────────────────────────────────");
    testCellTowerAT();
    delay(SETTLE_TIME_MS);

    // Test 3: GNSS Cold Start (no prior data)
    Serial.println("\n──────────────────────────────────────────");
    Serial.println("TEST 3: GNSS Cold Start");
    Serial.println("──────────────────────────────────────────");
    testGnssColdStart();
    delay(SETTLE_TIME_MS);

    // Test 4: GNSS Hot Start (should be faster now)
    Serial.println("\n──────────────────────────────────────────");
    Serial.println("TEST 4: GNSS Hot Start (using prior fix)");
    Serial.println("──────────────────────────────────────────");
    testGnssHotStart();
    delay(SETTLE_TIME_MS);

    // Test 5: GNSS High Sensitivity Mode
    Serial.println("\n──────────────────────────────────────────");
    Serial.println("TEST 5: GNSS High Sensitivity Mode");
    Serial.println("──────────────────────────────────────────");
    testGnssHighSensitivity();
    delay(SETTLE_TIME_MS);

    // Test 6: GNSS Warm Start
    Serial.println("\n──────────────────────────────────────────");
    Serial.println("TEST 6: GNSS Warm Start");
    Serial.println("──────────────────────────────────────────");
    testGnssWarmStart();
    delay(SETTLE_TIME_MS);

    Serial.println("\n========== TEST SUITE COMPLETE ==========\n");
}

bool prepareModemForGnss() {
    Serial.println("  [Prep] Setting modem to MINIMUM state for GNSS...");
    if (!WalterModem::setOpState(WALTER_MODEM_OPSTATE_MINIMUM)) {
        Serial.println("  [Prep] ERROR: Failed to set MINIMUM state");
        return false;
    }
    delay(2000);
    Serial.println("  [Prep] Modem ready for GNSS");
    return true;
}

bool restoreModemForLte() {
    Serial.println("  [Restore] Returning modem to NO_RF state...");
    WalterModem::setOpState(WALTER_MODEM_OPSTATE_NO_RF);
    delay(1000);
    return true;
}

bool waitForGnssFix(uint32_t timeoutSec, TestResult* result) {
    gnssFixReceived = false;
    gnssFixValid = false;

    Serial.println("  [GNSS] Requesting fix (gnssPerformAction)...");
    if (!WalterModem::gnssPerformAction()) {
        Serial.println("  [GNSS] ERROR: gnssPerformAction() failed");
        return false;
    }

    Serial.println("  [GNSS] Waiting for satellites...");
    uint32_t startTime = millis();
    uint32_t lastDot = 0;

    while (!gnssFixReceived) {
        delay(100);

        uint32_t elapsed = millis() - startTime;

        // Progress indicator every 5 seconds
        if (elapsed - lastDot >= 5000) {
            Serial.printf("  [GNSS] Waiting... %u sec\n", elapsed / 1000);
            lastDot = elapsed;
        }

        // Timeout check
        if (elapsed > (timeoutSec * 1000)) {
            Serial.println("  [GNSS] TIMEOUT - no fix received");
            result->success = false;
            result->timeToFixMs = elapsed;
            return false;
        }
    }

    uint32_t ttf = millis() - startTime;
    Serial.printf("  [GNSS] Fix received in %u ms\n", ttf);

    // Check fix quality
    if (!gnssFixValid) {
        Serial.printf("  [GNSS] Fix INVALID - confidence=%.1f\n", lastGnssFix.estimatedConfidence);
        result->success = false;
        result->timeToFixMs = ttf;
        return false;
    }

    // Record successful result
    result->success = true;
    result->latitude = lastGnssFix.latitude;
    result->longitude = lastGnssFix.longitude;
    result->altitude = lastGnssFix.height;
    result->accuracy = lastGnssFix.estimatedConfidence;
    result->satellites = lastGnssFix.satCount;
    result->timeToFixMs = ttf;
    result->distanceError = haversineDistance(EXPECTED_LAT, EXPECTED_LON,
                                               lastGnssFix.latitude, lastGnssFix.longitude);

    Serial.printf("  [GNSS] SUCCESS: %.6f, %.6f\n", result->latitude, result->longitude);
    Serial.printf("  [GNSS] Alt=%.1fm, Acc=%.1fm, Sats=%u, Error=%.2fkm\n",
                 result->altitude, result->accuracy, result->satellites, result->distanceError);

    return true;
}

bool testGnssColdStart() {
    TestResult* r = &results[resultCount++];
    r->testName = "GNSS Cold Start";
    r->success = false;

    if (!prepareModemForGnss()) return false;

    Serial.println("  [Config] Configuring for COLD/WARM start...");
    if (!WalterModem::gnssConfig(WALTER_MODEM_GNSS_SENS_MODE_HIGH,
                                  WALTER_MODEM_GNSS_ACQ_MODE_COLD_WARM_START)) {
        Serial.println("  [Config] WARNING: gnssConfig failed, trying defaults");
        WalterModem::gnssConfig();
    }
    delay(500);

    bool success = waitForGnssFix(TEST_TIMEOUT_SEC, r);

    restoreModemForLte();
    return success;
}

bool testGnssWarmStart() {
    TestResult* r = &results[resultCount++];
    r->testName = "GNSS Warm Start";
    r->success = false;

    if (!prepareModemForGnss()) return false;

    Serial.println("  [Config] Configuring for COLD/WARM start...");
    WalterModem::gnssConfig(WALTER_MODEM_GNSS_SENS_MODE_HIGH,
                            WALTER_MODEM_GNSS_ACQ_MODE_COLD_WARM_START);
    delay(500);

    bool success = waitForGnssFix(TEST_TIMEOUT_SEC, r);

    restoreModemForLte();
    return success;
}

bool testGnssHotStart() {
    TestResult* r = &results[resultCount++];
    r->testName = "GNSS Hot Start";
    r->success = false;

    if (!prepareModemForGnss()) return false;

    Serial.println("  [Config] Configuring for HOT start...");
    if (!WalterModem::gnssConfig(WALTER_MODEM_GNSS_SENS_MODE_HIGH,
                                  WALTER_MODEM_GNSS_ACQ_MODE_HOT_START)) {
        Serial.println("  [Config] WARNING: Hot start config failed, using cold/warm");
        WalterModem::gnssConfig(WALTER_MODEM_GNSS_SENS_MODE_HIGH,
                                WALTER_MODEM_GNSS_ACQ_MODE_COLD_WARM_START);
    }
    delay(500);

    bool success = waitForGnssFix(TEST_TIMEOUT_SEC, r);

    restoreModemForLte();
    return success;
}

bool testGnssHighSensitivity() {
    TestResult* r = &results[resultCount++];
    r->testName = "GNSS High Sens";
    r->success = false;

    if (!prepareModemForGnss()) return false;

    Serial.println("  [Config] Configuring HIGH sensitivity mode...");
    WalterModem::gnssConfig(WALTER_MODEM_GNSS_SENS_MODE_HIGH,
                            WALTER_MODEM_GNSS_ACQ_MODE_COLD_WARM_START);
    delay(500);

    bool success = waitForGnssFix(TEST_TIMEOUT_SEC, r);

    restoreModemForLte();
    return success;
}

bool testGnssLowSensitivity() {
    TestResult* r = &results[resultCount++];
    r->testName = "GNSS Low Sens";
    r->success = false;

    if (!prepareModemForGnss()) return false;

    Serial.println("  [Config] Configuring LOW sensitivity mode...");
    WalterModem::gnssConfig(WALTER_MODEM_GNSS_SENS_MODE_LOW,
                            WALTER_MODEM_GNSS_ACQ_MODE_COLD_WARM_START);
    delay(500);

    bool success = waitForGnssFix(TEST_TIMEOUT_SEC, r);

    restoreModemForLte();
    return success;
}

bool testCellTowerLocation() {
    TestResult* r = &results[resultCount++];
    r->testName = "Cell Tower Loc";
    r->success = false;

    Serial.println("  [Cell] Testing cell tower triangulation...");
    Serial.println("  [Cell] Note: Requires LTE connection and location service");

    // First ensure we have LTE connection
    Serial.println("  [Cell] Setting modem to FULL state...");
    WalterModem::setOpState(WALTER_MODEM_OPSTATE_NO_RF);
    delay(1000);

    // Define PDP context
    Serial.println("  [Cell] Defining PDP context...");
    WalterModem::definePDPContext(1, LTE_APN);

    // Go to full operational state
    WalterModem::setOpState(WALTER_MODEM_OPSTATE_FULL);

    // Wait for network registration
    Serial.println("  [Cell] Waiting for network registration (max 30 sec)...");
    uint32_t startTime = millis();
    WalterModemNetworkRegState regState;

    while (millis() - startTime < 30000) {
        regState = WalterModem::getNetworkRegState();
        if (regState == WALTER_MODEM_NETWORK_REG_REGISTERED_HOME ||
            regState == WALTER_MODEM_NETWORK_REG_REGISTERED_ROAMING) {
            Serial.println("  [Cell] Network registered!");
            break;
        }
        delay(1000);
        Serial.print(".");
    }
    Serial.println();

    if (regState != WALTER_MODEM_NETWORK_REG_REGISTERED_HOME &&
        regState != WALTER_MODEM_NETWORK_REG_REGISTERED_ROAMING) {
        Serial.println("  [Cell] ERROR: Network registration failed");
        return false;
    }

    // Try to get cell location using AT+SQNCELLLOCATE
    // This requires cloud assistance from Sequans/carrier
    Serial.println("  [Cell] Attempting AT+SQNCELLLOCATE...");
    Serial.println("  [Cell] (This may not be supported by all carriers)");

    // The WalterModem library may not have direct support for this
    // We'll try raw AT command if available

    // For now, get cell info which gives us cell ID for manual lookup
    WalterModemRsp* rsp = (WalterModemRsp*)malloc(sizeof(WalterModemRsp));
    if (rsp && WalterModem::getCellInformation(WALTER_MODEM_SQNMONI_REPORTS_SERVING_CELL, rsp)) {
        Serial.println("  [Cell] Cell info retrieved:");
        Serial.printf("  [Cell] MCC=%u, MNC=%u, CID=%lu, TAC=%u\n",
                     rsp->data.cellInformation.cc,
                     rsp->data.cellInformation.nc,
                     (unsigned long)rsp->data.cellInformation.cid,
                     rsp->data.cellInformation.tac);
        Serial.printf("  [Cell] Band=%u, EARFCN=%u\n",
                     rsp->data.cellInformation.band,
                     rsp->data.cellInformation.earfcn);

        // Cell tower location lookup would require external service
        // The modem itself doesn't provide lat/lon from cell info
        Serial.println("  [Cell] Note: Cell ID can be used with external lookup service");
        Serial.println("  [Cell] (opencellid.org, Google Geolocation API, etc.)");

        // Store cell info for reference
        r->success = false;  // We got cell info but not actual location
        r->timeToFixMs = millis() - startTime;
    }
    free(rsp);

    return false;  // Cell tower location not directly available
}

bool testCellTowerAT() {
    TestResult* r = &results[resultCount++];
    r->testName = "Cell AT Query";
    r->success = false;

    Serial.println("  [AT] Querying cell information via AT commands...");

    // Get detailed cell info
    WalterModemRsp* rsp = (WalterModemRsp*)malloc(sizeof(WalterModemRsp));
    if (!rsp) return false;

    uint32_t startTime = millis();

    // Query serving cell with CINR
    if (WalterModem::getCellInformation(WALTER_MODEM_SQNMONI_REPORTS_SERVING_CELL_WITH_CINR, rsp)) {
        Serial.println("  [AT] Serving cell info:");
        Serial.printf("  [AT] Operator: %s\n",
                     rsp->data.cellInformation.netName ? rsp->data.cellInformation.netName : "N/A");
        Serial.printf("  [AT] MCC: %u, MNC: %u\n",
                     rsp->data.cellInformation.cc, rsp->data.cellInformation.nc);
        Serial.printf("  [AT] Cell ID: %lu (0x%lX)\n",
                     (unsigned long)rsp->data.cellInformation.cid,
                     (unsigned long)rsp->data.cellInformation.cid);
        Serial.printf("  [AT] TAC: %u, Band: %u\n",
                     rsp->data.cellInformation.tac, rsp->data.cellInformation.band);
        Serial.printf("  [AT] RSRP: %.1f dBm, RSRQ: %.1f dB\n",
                     rsp->data.cellInformation.rsrp, rsp->data.cellInformation.rsrq);
        Serial.printf("  [AT] RSSI: %.1f dBm, CINR: %.1f dB\n",
                     rsp->data.cellInformation.rssi, rsp->data.cellInformation.cinr);

        r->timeToFixMs = millis() - startTime;

        // Provide OpenCellID lookup URL
        Serial.println("\n  [AT] To look up cell location manually:");
        Serial.printf("  [AT] https://opencellid.org/cell/get?key=YOUR_KEY&mcc=%u&mnc=%u&lac=%u&cellid=%lu\n",
                     rsp->data.cellInformation.cc,
                     rsp->data.cellInformation.nc,
                     rsp->data.cellInformation.tac,
                     (unsigned long)rsp->data.cellInformation.cid);
    } else {
        Serial.println("  [AT] ERROR: Failed to get cell information");
    }

    free(rsp);
    return false;  // Info only, not actual lat/lon
}

void printResults() {
    Serial.println("\n");
    Serial.println("╔══════════════════════════════════════════════════════════════════════════════╗");
    Serial.println("║                           TEST RESULTS SUMMARY                                ║");
    Serial.println("╠══════════════════════════════════════════════════════════════════════════════╣");
    Serial.println("║ Test Name        │ Result │ Lat       │ Lon        │ Acc(m) │ Time(s) │ Err  ║");
    Serial.println("╠══════════════════════════════════════════════════════════════════════════════╣");

    int passCount = 0;
    int failCount = 0;

    for (int i = 0; i < resultCount; i++) {
        TestResult* r = &results[i];

        if (r->success) {
            passCount++;
            Serial.printf("║ %-16s │  PASS  │ %9.5f │ %10.5f │ %6.1f │ %7.1f │ %4.1f ║\n",
                         r->testName,
                         r->latitude,
                         r->longitude,
                         r->accuracy,
                         r->timeToFixMs / 1000.0,
                         r->distanceError);
        } else {
            failCount++;
            Serial.printf("║ %-16s │  FAIL  │    ---    │     ---    │   ---  │ %7.1f │  --- ║\n",
                         r->testName,
                         r->timeToFixMs / 1000.0);
        }
    }

    Serial.println("╠══════════════════════════════════════════════════════════════════════════════╣");
    Serial.printf("║ TOTAL: %d tests │ PASS: %d │ FAIL: %d                                         ║\n",
                 resultCount, passCount, failCount);
    Serial.println("╚══════════════════════════════════════════════════════════════════════════════╝");

    // Print recommendations
    Serial.println("\n[Recommendations]");
    if (passCount == 0) {
        Serial.println("  - No GNSS fixes obtained. Check:");
        Serial.println("    1. Is the GNSS antenna connected and has clear sky view?");
        Serial.println("    2. Are you indoors? GNSS needs sky visibility");
        Serial.println("    3. Try moving device near a window or outside");
        Serial.println("    4. Cold start can take 5-12 minutes in poor conditions");
    } else {
        // Find best result
        float bestTime = 999999;
        const char* bestTest = "None";
        for (int i = 0; i < resultCount; i++) {
            if (results[i].success && results[i].timeToFixMs < bestTime) {
                bestTime = results[i].timeToFixMs;
                bestTest = results[i].testName;
            }
        }
        Serial.printf("  - Best method: %s (%.1f seconds)\n", bestTest, bestTime / 1000.0);
        Serial.println("  - For fastest fixes, use Hot Start after initial Cold Start");
    }
}

// Haversine formula to calculate distance between two lat/lon points
float haversineDistance(float lat1, float lon1, float lat2, float lon2) {
    const float R = 6371.0;  // Earth radius in km
    float dLat = radians(lat2 - lat1);
    float dLon = radians(lon2 - lon1);
    float a = sin(dLat/2) * sin(dLat/2) +
              cos(radians(lat1)) * cos(radians(lat2)) *
              sin(dLon/2) * sin(dLon/2);
    float c = 2 * atan2(sqrt(a), sqrt(1-a));
    return R * c;
}

#endif // GNSS_DEBUG_MODE
