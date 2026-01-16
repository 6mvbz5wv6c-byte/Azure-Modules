// Rewrite of walter modem library example to use hardware serial defined UART channel
// Supports AT commands from RASPBERRY PI GPIO pins. PPP mode tested.
// Copyright (c) Logan Joseph Stover December 10 2025. All rights reserved.
// This source code is proprietary and confidential.
// Unauthorized copying, modification, distribution, or use of this file, via any medium, is strictly prohibited.
// For licensing inquiries, contact: stover@protonmail.com

#include <Arduino.h>
#include "driver/gpio.h"
#include <HardwareSerial.h>

#define WALTER_MODEM_PIN_RX    14   // GM02SP TX0 -> ESP RX
#define WALTER_MODEM_PIN_TX    48   // GM02SP RX0 -> ESP TX
#define WALTER_MODEM_PIN_RTS   21
#define WALTER_MODEM_PIN_CTS   47
#define WALTER_MODEM_PIN_RESET 45

#define ModemSerial Serial2

#define HOST_UART_RX 12 // to Pi TXD
#define HOST_UART_TX 11 // to Pi RXD

HardwareSerial gpioSerial(1);

// Function declarations
void pumpHostToModem();
void pumpModemToHost();

static void _modem_reset()
{
  gpio_hold_dis((gpio_num_t) WALTER_MODEM_PIN_RESET);
  digitalWrite(WALTER_MODEM_PIN_RESET, LOW);
  delay(1000);
  digitalWrite(WALTER_MODEM_PIN_RESET, HIGH);
  gpio_hold_en((gpio_num_t) WALTER_MODEM_PIN_RESET);
}

void setup() {
  // Optional USB debug
  Serial.begin(115200);
  Serial.println("Walter PPP bridge starting");

  pinMode(WALTER_MODEM_PIN_RX,    INPUT);
  pinMode(WALTER_MODEM_PIN_TX,    OUTPUT);
  pinMode(WALTER_MODEM_PIN_CTS,   INPUT);
  pinMode(WALTER_MODEM_PIN_RTS,   OUTPUT);
  pinMode(WALTER_MODEM_PIN_RESET, OUTPUT);

  pinMode(HOST_UART_RX, INPUT);
  pinMode(HOST_UART_TX, OUTPUT);

  ModemSerial.setRxBufferSize(2048);
  ModemSerial.setTxBufferSize(2048);
  ModemSerial.begin(
    115200,
    SERIAL_8N1,
    WALTER_MODEM_PIN_RX,
    WALTER_MODEM_PIN_TX,
    false,
    WALTER_MODEM_PIN_RTS,
    WALTER_MODEM_PIN_CTS
  );

  gpioSerial.setRxBufferSize(2048);
  gpioSerial.setTxBufferSize(2048);
  gpioSerial.begin(
    115200,
    SERIAL_8N1,
    HOST_UART_RX,
    HOST_UART_TX
  );

  _modem_reset();
}

void loop() {
  pumpHostToModem();
  pumpModemToHost();
}

void pumpHostToModem() {
  uint8_t buf[64];
  while (gpioSerial.available()) {
    size_t n = gpioSerial.read(buf, sizeof(buf));
    if (n) {
      ModemSerial.write(buf, n);
    }
  }
}

void pumpModemToHost() {
  uint8_t buf[64];
  while (ModemSerial.available()) {
    size_t n = ModemSerial.read(buf, sizeof(buf));
    if (n) {
      gpioSerial.write(buf, n);
    }
  }
}
