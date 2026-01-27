/**
 * @file walter_feels_display.ino
 * @brief Walter Feels with OLED display - continuous sensor monitoring
 * 
 * Modified from original DPTechnics example to add SSD1306 128x64 OLED display
 * at I2C address 0x3C showing all sensor readings on one page with continuous updates.
 * 
 * Original copyright (C) 2023, DPTechnics bv - All rights reserved.
 */

#include <HardwareSerial.h>
#include "WalterFeels.h"
#include <WalterModem.h>
#include <Arduino.h>
#include "esp_mac.h"
#include "hdc1080.h"
#include "lps22hb.h"
#include "ltc4015.h"
#include "scd30.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// OLED Display Configuration
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDR 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

#define CO2_EN_PIN 13
#define CO2_SDA_PIN 12
#define CO2_SCL_PIN 11

#define MODEM_SOCKET_PROFILE 1
#define SERV_ADDR "walterdemo.quickspot.io"
#define SERV_PORT 1999
#define PACKET_SIZE 51
//#define CELLULAR_APN "eapn1.net"
#define CELLULAR_APN "onomondo"
#define RADIO_TECHNOLOGY WALTER_MODEM_RAT_LTEM
#define MAX_GNSS_CONFIDENCE 100.0

// Sensor update interval (milliseconds)
#define SENSOR_UPDATE_INTERVAL 2000

WalterModem modem;
volatile bool gnssFixRcvd = false;
WalterModemGNSSFix latestGnssFix = {};
uint8_t out_buf[PACKET_SIZE] = { 0 };
uint8_t in_buf[1500] = { 0 };

HDC1080 hdc1080;
LPS22HB lps22hb(Wire);
LTC4015 ltc4015;
SCD30 scd30;

bool co2_sensor_installed = false;
bool display_initialized = false;
unsigned long lastSensorUpdate = 0;

// Global sensor values for display
struct SensorData {
  float temp;
  float hum;
  float pressure;
  uint16_t co2ppm;
  uint16_t inputVoltage;
  int16_t inputCurrent;     // signed values
  uint16_t systemVoltage;
  uint16_t batteryVoltage;
  int16_t chargeCurrent;    // signed values
  uint16_t chargeCount;
  uint16_t chargeStatus;
  uint16_t chargerState;
  float latitude;
  float longitude;
  uint8_t satCount;
  float rsrp;
  float rsrq;
  uint8_t band;
} sensorData;

/**
 * @brief Update OLED display with all sensor values
 * Compact layout for 128x64 (21 chars × 8 lines at 6x8 font)
 */
void updateDisplay() {
  if (!display_initialized) return;
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  
  // Line 0: Temp & Humidity
  display.printf("T:%.1fC H:%.0f%%", sensorData.temp, sensorData.hum);
  
  // Line 1: Pressure & CO2
  display.setCursor(0, 8);
  display.printf("P:%.0fhPa CO2:%d", sensorData.pressure, sensorData.co2ppm);
  
  // Line 2: Battery voltage & charge current (show + for charging, - for discharging)
  display.setCursor(0, 16);
  display.printf("Vb:%dmV Ib:%+dmA", sensorData.batteryVoltage, sensorData.chargeCurrent);
 
  // // Line 2: Battery voltage & charge current
  // display.setCursor(0, 16);
  // display.printf("Vb:%dmV Ic:%dmA", sensorData.batteryVoltage, sensorData.chargeCurrent);
  
  // Line 3: Input voltage & current (solar)
  display.setCursor(0, 24);
  display.printf("Vi:%dmV Ii:%dmA", sensorData.inputVoltage, sensorData.inputCurrent);
  
  // Line 4: System voltage & charge %
  float batteryCharge = (float)sensorData.chargeCount * 100.0f / 65535.0f;
  display.setCursor(0, 32);
  display.printf("Vs:%dmV Q:%.1f%%", sensorData.systemVoltage, batteryCharge);
  
  // Line 5: Charger status
  display.setCursor(0, 40);
  display.printf("ChgSt:%04X S:%04X", sensorData.chargeStatus, sensorData.chargerState);
  
  // Line 6: GPS coordinates
  display.setCursor(0, 48);
  display.printf("%.4f,%.4f", sensorData.latitude, sensorData.longitude);
  
  // Line 7: Sats, RSRP, Band
  display.setCursor(0, 56);
  display.printf("Sat:%d RSRP:%.0f B:%d", sensorData.satCount, sensorData.rsrp, sensorData.band);
  
  display.display();
}

/**
 * @brief Display status message during operations
 */
void displayStatus(const char* line1, const char* line2 = nullptr) {
  if (!display_initialized) return;
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 24);
  display.println(line1);
  if (line2) {
    display.println(line2);
  }
  display.display();
}

/**
 * @brief Read all sensors and update sensorData struct
 */
void readAllSensors() {
  // Environmental sensors
  sensorData.temp = hdc1080.readTemperature();
  sensorData.hum = hdc1080.readHumidity();
  sensorData.pressure = lps22hb.readPressure();
  
  // CO2 sensor
  if (co2_sensor_installed) {
    sensorData.co2ppm = scd30.getCO2();
  }
  
  // Power management / charger
  sensorData.chargeStatus = ltc4015.read_word(LTC4015_REG_CHARGE_STATUS);
  sensorData.chargerState = ltc4015.read_word(LTC4015_REG_CHARGER_STATE);
  sensorData.inputVoltage = (uint16_t)(ltc4015.get_input_voltage() * 1000);
  sensorData.inputCurrent = (int16_t)(ltc4015.get_input_current() * 1000);   // SIGNED
  sensorData.systemVoltage = (uint16_t)(ltc4015.get_system_voltage() * 1000);
  sensorData.batteryVoltage = (uint16_t)(ltc4015.get_battery_voltage() * 1000);
  sensorData.chargeCurrent = (int16_t)(ltc4015.get_charge_current() * 1000); // SIGNED
  sensorData.chargeCount = ltc4015.get_qcount();
}

bool lteConnected() {
  WalterModemNetworkRegState regState = modem.getNetworkRegState();
  return (regState == WALTER_MODEM_NETWORK_REG_REGISTERED_HOME ||
          regState == WALTER_MODEM_NETWORK_REG_REGISTERED_ROAMING);
}

bool waitForNetwork(int timeout_sec = 300) {
  Serial.print("Connecting to the network...");
  displayStatus("Connecting LTE...");
  int time = 0;
  while(!lteConnected()) {
    Serial.print(".");
    delay(1000);
    time++;
    if(time > timeout_sec) return false;
  }
  Serial.println("\nConnected to the network");
  return true;
}

bool lteDisconnect() {
  if(modem.setOpState(WALTER_MODEM_OPSTATE_MINIMUM)) {
    Serial.println("Set operational state to MINIMUM");
  } else {
    Serial.println("Error: Could not set operational state to MINIMUM");
    return false;
  }
  WalterModemNetworkRegState regState = modem.getNetworkRegState();
  while(regState != WALTER_MODEM_NETWORK_REG_NOT_SEARCHING) {
    delay(100);
    regState = modem.getNetworkRegState();
  }
  Serial.println("Disconnected from the network");
  return true;
}

bool lteConnect() {
  displayStatus("LTE Connecting...");
  
  if(!modem.setOpState(WALTER_MODEM_OPSTATE_NO_RF)) {
    Serial.println("Error: Could not set operational state to NO RF");
    return false;
  }
  if(!modem.definePDPContext(1, CELLULAR_APN)) {
    Serial.println("Error: Could not create PDP context");
    return false;
  }
  if(!modem.setOpState(WALTER_MODEM_OPSTATE_FULL)) {
    Serial.println("Error: Could not set operational state to FULL");
    return false;
  }
  if(!modem.setNetworkSelectionMode(WALTER_MODEM_NETWORK_SEL_MODE_AUTOMATIC)) {
    Serial.println("Error: Could not set network selection mode");
    return false;
  }
  return waitForNetwork();
}

bool checkAssistanceStatus(walter_modem_rsp_t* rsp, bool* updateAlmanac = nullptr,
                           bool* updateEphemeris = nullptr) {
  if(!modem.gnssGetAssistanceStatus(rsp) ||
     rsp->type != WALTER_MODEM_RSP_DATA_TYPE_GNSS_ASSISTANCE_DATA) {
    return false;
  }
  if(updateAlmanac) *updateAlmanac = false;
  if(updateEphemeris) *updateEphemeris = false;

  auto report = [](const char* name, const auto& data, bool* updateFlag) {
    Serial.printf("%s data is ", name);
    if(data.available) {
      Serial.printf("available, update in %ds\r\n", data.timeToUpdate);
      if(updateFlag) *updateFlag = (data.timeToUpdate <= 0);
    } else {
      Serial.println("not available.");
      if(updateFlag) *updateFlag = true;
    }
  };

  report("Almanac", rsp->data.gnssAssistance.almanac, updateAlmanac);
  report("Ephemeris", rsp->data.gnssAssistance.realtimeEphemeris, updateEphemeris);
  return true;
}

bool validateGNSSClock(walter_modem_rsp_t* rsp) {
  modem.gnssGetUTCTime(rsp);
  if(rsp->data.clock.epochTime > 4) return true;

  Serial.println("Clock invalid, LTE sync required");
  if(!lteConnected() && !lteConnect()) return false;

  for(int i = 0; i < 5; ++i) {
    modem.gnssGetUTCTime(rsp);
    if(rsp->data.clock.epochTime > 4) {
      Serial.printf("Clock synced: %" PRIi64 "\r\n", rsp->data.clock.epochTime);
      return true;
    }
    delay(2000);
  }
  Serial.println("Error: Clock sync failed");
  return false;
}

bool updateGNSSAssistance(walter_modem_rsp_t* rsp) {
  bool updateAlmanac = false;
  bool updateEphemeris = false;

  if(!checkAssistanceStatus(rsp, &updateAlmanac, &updateEphemeris)) return false;
  if(!updateAlmanac && !updateEphemeris) return true;

  if(!lteConnected() && !lteConnect()) return false;

  if(updateAlmanac && !modem.gnssUpdateAssistance(WALTER_MODEM_GNSS_ASSISTANCE_TYPE_ALMANAC)) {
    Serial.println("Could not update almanac");
    return false;
  }
  if(updateEphemeris &&
     !modem.gnssUpdateAssistance(WALTER_MODEM_GNSS_ASSISTANCE_TYPE_REALTIME_EPHEMERIS)) {
    Serial.println("Could not update ephemeris");
    return false;
  }
  return checkAssistanceStatus(rsp);
}

void gnssEventHandler(const WalterModemGNSSFix* fix, void* args) {
  memcpy(&latestGnssFix, fix, sizeof(WalterModemGNSSFix));
  
  uint8_t goodSatCount = 0;
  for(int i = 0; i < latestGnssFix.satCount; ++i) {
    if(latestGnssFix.sats[i].signalStrength >= 30) ++goodSatCount;
  }
  Serial.printf("\nGNSS: Conf:%.02f Lat:%.06f Lon:%.06f Sats:%d Good:%d\r\n",
                latestGnssFix.estimatedConfidence, latestGnssFix.latitude,
                latestGnssFix.longitude, latestGnssFix.satCount, goodSatCount);
  
  // Update display data
  sensorData.latitude = latestGnssFix.latitude;
  sensorData.longitude = latestGnssFix.longitude;
  sensorData.satCount = latestGnssFix.satCount;
  
  gnssFixRcvd = true;
}

bool attemptGNSSFix() {
  walter_modem_rsp_t rsp = {};
  displayStatus("GNSS Fix...");

  if(!validateGNSSClock(&rsp)) return false;
  updateGNSSAssistance(&rsp);  // Continue even if fails

  if(lteConnected() && !lteDisconnect()) return false;

  if(latestGnssFix.estimatedConfidence <= MAX_GNSS_CONFIDENCE) {
    modem.gnssConfig(WALTER_MODEM_GNSS_SENS_MODE_HIGH, WALTER_MODEM_GNSS_ACQ_MODE_HOT_START);
  }

  const int maxAttempts = 5;
  for(int attempt = 0; attempt < maxAttempts; ++attempt) {
    gnssFixRcvd = false;
    if(!modem.gnssPerformAction()) return false;

    Serial.printf("GNSS attempt %d/%d\r\n", attempt + 1, maxAttempts);
    char statusMsg[22];
    snprintf(statusMsg, sizeof(statusMsg), "GNSS %d/%d", attempt + 1, maxAttempts);
    displayStatus(statusMsg);

    while(!gnssFixRcvd) {
      Serial.print(".");
      delay(500);
    }

    if(latestGnssFix.estimatedConfidence <= MAX_GNSS_CONFIDENCE) {
      Serial.println("Valid GNSS fix obtained");
      return true;
    }
    Serial.printf("Confidence %.02f too low\r\n", latestGnssFix.estimatedConfidence);
  }
  return false;
}

void setup_charger() {
  ltc4015.initialize(3, 4);
  ltc4015.suspend_charging();
  ltc4015.enable_force_telemetry();
  delay(1000);
  ltc4015.start_charging();
  ltc4015.enable_mppt();
  ltc4015.enable_coulomb_counter();
}

static void myURCHandler(const walter_modem_urc_event_t* ev, void* args) {
  Serial.printf("URC at %lld\n", ev->timestamp);
  switch(ev->type) {
  case WM_URC_TYPE_SOCKET:
    if(ev->socket.event == WALTER_MODEM_SOCKET_EVENT_RING) {
      Serial.printf("Socket Ring: profile %d, len %u\n", ev->socket.profileId, ev->socket.dataLen);
      if(modem.socketReceiveMessage(ev->socket.profileId, in_buf, ev->socket.dataLen)) {
        for(int i = 0; i < ev->socket.dataLen; i++) Serial.printf("%c", in_buf[i]);
        Serial.println();
      }
    } else if(ev->socket.event == WALTER_MODEM_SOCKET_EVENT_DISCONNECTED) {
      Serial.printf("Socket closed: profile %d", ev->socket.profileId);
    }
    break;
  default:
    break;
  }
}

void setup() {
  Serial.begin(115200);
  delay(5000);

  Serial.printf("\r\n=== WalterFeels + OLED Display (Continuous) ===\r\n\r\n");

  // Initialize I2C and display early
  WalterFeels::set3v3(true);
  WalterFeels::setI2cBusPower(true);
  
  if(!Wire.begin(WFEELS_PIN_I2C_SDA, WFEELS_PIN_I2C_SCL, 100000)) {
    Serial.println("I2C init failed");
    return;
  }

  // Initialize OLED
  if(!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("OLED init failed - check address");
    display_initialized = false;
  } else {
    Serial.println("OLED initialized");
    display_initialized = true;
    displayStatus("Walter Feels", "Initializing...");
  }

  esp_read_mac(out_buf, ESP_MAC_WIFI_STA);
  Serial.printf("MAC: %02X:%02X:%02X:%02X:%02X:%02X\r\n", 
                out_buf[0], out_buf[1], out_buf[2], out_buf[3], out_buf[4], out_buf[5]);

  displayStatus("Init modem...");
  if(modem.begin(&Serial2)) {
    Serial.println("Modem initialized");
  } else {
    Serial.println("Modem init failed");
    displayStatus("Modem FAIL", "Restarting...");
    delay(5000);
    ESP.restart();
  }

  modem.urcSetEventHandler(myURCHandler, NULL);

  // // CO2 sensor on second I2C bus
  // Wire1.begin(CO2_SDA_PIN, CO2_SCL_PIN);
  // delay(100);

  // co2_sensor_installed = scd30.begin(Wire1);
  // for(int i = 0; i < 10 && !co2_sensor_installed; ++i) {
  //   delay(500);
  //   co2_sensor_installed = scd30.begin(Wire1);
  // }

  // if(!co2_sensor_installed) {
  //   digitalWrite(CO2_EN_PIN, HIGH);
  //   Serial.println("No CO2 sensor");
  // } else {
  //   Serial.println("SCD30 CO2 sensor found");
  // }

  setup_charger();
  hdc1080.begin();
  lps22hb.begin();

  walter_modem_rsp_t rsp = {};

  if(modem.getRAT(&rsp) && rsp.data.rat != RADIO_TECHNOLOGY) {
    modem.setRAT(RADIO_TECHNOLOGY);
  }

  if(modem.getIdentity(&rsp)) {
    Serial.printf("IMEI: %s\r\n", rsp.data.identity.imei);
  }
  if(modem.getSIMCardID(&rsp)) {
    Serial.printf("ICCID: %s\r\n", rsp.data.simCardID.iccid);
  }
  if(modem.getSIMCardIMSI(&rsp)) {
    Serial.printf("IMSI: %s\r\n", rsp.data.imsi);
  }

  if(!modem.gnssConfig()) {
    Serial.println("GNSS config failed");
    displayStatus("GNSS FAIL");
    delay(1000);
    ESP.restart();
    return;
  }

  if(!modem.socketConfig(MODEM_SOCKET_PROFILE) ||
     !modem.socketConfigSecure(MODEM_SOCKET_PROFILE, false)) {
    Serial.println("Socket config failed");
    return;
  }

  modem.gnssSetEventHandler(gnssEventHandler, NULL);

  // Initial sensor read
  displayStatus("Reading sensors...");
  readAllSensors();

  Serial.printf("Sensors: T:%.02fC H:%.02f%% P:%.02fhPa CO2:%d\r\n",
                sensorData.temp, sensorData.hum, sensorData.pressure); //, sensorData.co2ppm);

  // Attempt GNSS fix
  attemptGNSSFix();

  // Connect LTE and get cell info
  if(lteConnect()) {
    if(modem.getCellInformation(WALTER_MODEM_SQNMONI_REPORTS_SERVING_CELL, &rsp)) {
      Serial.printf("Band %u, Op: %s, CID: %u\r\n", rsp.data.cellInformation.band,
                    rsp.data.cellInformation.netName, rsp.data.cellInformation.cid);
      Serial.printf("RSRP: %.2f, RSRQ: %.2f\r\n", rsp.data.cellInformation.rsrp,
                    rsp.data.cellInformation.rsrq);
      
      sensorData.rsrp = rsp.data.cellInformation.rsrp;
      sensorData.rsrq = rsp.data.cellInformation.rsrq;
      sensorData.band = rsp.data.cellInformation.band;
    }
  } else {
    Serial.println("LTE connect failed - continuing without network");
  }

  // Initial display update
  updateDisplay();
  
  Serial.println("Setup complete - entering continuous monitoring loop");
}

void loop() {
  unsigned long currentMillis = millis();
  
  // Update sensors at defined interval
  if (currentMillis - lastSensorUpdate >= SENSOR_UPDATE_INTERVAL) {
    lastSensorUpdate = currentMillis;
    
    // Read all sensors
    readAllSensors();
    
    // Update display
    updateDisplay();
    
    // Debug output to serial
    Serial.printf("T:%.1fC H:%.0f%% P:%.0fhPa CO2:%d Vbat:%dmV Ichg:%dmA\r\n",
                  sensorData.temp, sensorData.hum, sensorData.pressure,
                  sensorData.co2ppm, sensorData.batteryVoltage, sensorData.chargeCurrent);
  }
}
