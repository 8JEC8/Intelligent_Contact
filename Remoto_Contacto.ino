#include <SPI.h>
#include <LoRa.h>
#include "esp_task_wdt.h"
#include <Wire.h>
#include <ZMPT101B.h>

#define PIN_VOLTAGE 19
#define SENSITIVITY 400.0f
#define CALIBRATION_FACTOR 1.38
ZMPT101B voltageSensor(PIN_VOLTAGE, 60.0);

#define LORA_FREQ 433E6
#define LORA_SS   10    // CS
#define LORA_DIO0 42    // DIO0
#define WDT_TIMEOUT 10  // seconds
#define RELAY_PIN 5     // Relay Pin

float voltageSum = 0.0;
float voltageValue = 0.0;

int sampleCount = 0;

unsigned long lastSampleTime = 0;

const int NUM_MUESTRAS = 5;
const int SAMPLE_INTERVAL = 100;

bool receivingTel = false;
bool wasReceivingTel = false;       // Recordar estado previo
TaskHandle_t watchdogTaskHandle = NULL;

void setup() {
  Serial.begin(115200);
  while (!Serial);

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

  const esp_task_wdt_config_t wdt_config = {
        .timeout_ms = WDT_TIMEOUT * 1000,
        .idle_core_mask = (1 << 0) | (1 << 1),
        .trigger_panic = true
  };
  esp_task_wdt_init(&wdt_config);

  esp_task_wdt_add(NULL);
  Serial.println("WATCHDOG_LOOP_ENABLED");

  xTaskCreatePinnedToCore(
    watchdogTask,           // task function
    "watchdogTask",         // name
    4096,                   // stack size
    NULL,                   // parameter
    1,                      // priority
    &watchdogTaskHandle,    // task handle
    0                       // core 0 (low priority)
  );

  SPI.begin();
  LoRa.setPins(LORA_SS, -1, LORA_DIO0);

  if (!LoRa.begin(LORA_FREQ)) {
    Serial.println("LoRa_ERROR_init");
    while (true);
  }

  LoRa.setTxPower(20);
  LoRa.setSpreadingFactor(7);
  LoRa.setSignalBandwidth(250E3);
  LoRa.setCodingRate4(5);
  LoRa.setSyncWord(0x88);
  LoRa.setPreambleLength(6);
  LoRa.enableCrc();
}

void loop() {
  esp_task_wdt_reset();

  // Passive voltage sampling
  if (millis() - lastSampleTime >= SAMPLE_INTERVAL) {

    lastSampleTime = millis();

    float voltage = voltageSensor.getRmsVoltage();

    voltageSum += voltage;
    sampleCount++;

    // Rolling average
    if (sampleCount >= NUM_MUESTRAS) {

      voltageValue = voltageSum / NUM_MUESTRAS;

      // Reset for next rolling average
      voltageSum = 0;
      sampleCount = 0;
    }
  }
  
  // LoRa check
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    String received = "";
    while (LoRa.available()) {
      received += (char)LoRa.read();
    }
    received.trim();

    // Ack
    LoRa.beginPacket();
    LoRa.print("ACK_" + received);
    LoRa.endPacket();
    

    if (received == "SRA" || received == "MRA" || received == "LRA") {
      delay(250);  // 250 para acabar ACK
    }

    delay(50);
    handleRequest(received);
  }
}

void handleRequest(String cmd) {
  if (cmd == "GO") {
    receivingTel = true;
    LoRa.beginPacket();
    LoRa.print("INFO_ON");
    LoRa.endPacket();
  }

  else if (cmd == "STP") {
    receivingTel = false;
    LoRa.beginPacket();
    LoRa.print("INFO_OFF");
    LoRa.endPacket();
  }
  
  else if (cmd == "ON") {
    Serial.println("Contacto Encendido");
    digitalWrite(RELAY_PIN, HIGH);
    LoRa.beginPacket();
    LoRa.print("CONT_ON");
    LoRa.endPacket();
  }

  else if (cmd == "OFF") {
    Serial.println("Contacto Apagado");
    digitalWrite(RELAY_PIN, LOW);
    LoRa.beginPacket();
    LoRa.print("CONT_OFF");
    LoRa.endPacket();
  }

  else if (cmd == "VOLT") {
    LoRa.beginPacket();
    LoRa.print(voltageValue);
    LoRa.endPacket();
  }

  else if (cmd == "CORR") {
    Serial.println("*Obtener con Sensor de Corriente*");
    LoRa.beginPacket();
    LoRa.print("*Corriente*");
    LoRa.endPacket();
  }

  else if (cmd == "FASE") {
    Serial.println("*Obtener con Sensor de V y I*");
    LoRa.beginPacket();
    LoRa.print("*Fase*");
    LoRa.endPacket();
  }

  else if (cmd == "SRA") {
    LoRa.setSpreadingFactor(7);
    LoRa.setSignalBandwidth(250E3);
    LoRa.setCodingRate4(5);
    LoRa.setPreambleLength(6);
    Serial.println("INT1.5");
  }

  else if (cmd == "MRA") {
    LoRa.setSpreadingFactor(9);
    LoRa.setSignalBandwidth(125E3);
    LoRa.setCodingRate4(6);
    LoRa.setPreambleLength(8);
    Serial.println("INT2.5");
  }

  else if (cmd == "LRA") {
    LoRa.setSpreadingFactor(11);
    LoRa.setSignalBandwidth(125E3);
    LoRa.setCodingRate4(8);
    LoRa.setPreambleLength(10);
    Serial.println("INT5.0");
  }

  else if (cmd == "RECONT") {
    delay(200);
    ESP.restart();
  }
}

void watchdogTask(void *parameter) {
  esp_task_wdt_add(NULL);
  Serial.println("WATCHDOG_TASK_ENABLED");

  while (true) {
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
