#include <SPI.h>
#include <LoRa.h>
#include "esp_task_wdt.h"
#include <Wire.h>
#include <ZMPT101B.h>
#include <math.h>
#include <Adafruit_NeoPixel.h>

#define LED_PIN    6
#define LED_COUNT  8
Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

#define LORA_FREQ 433E6
#define LORA_SS   10    // CS
#define LORA_DIO0 42    // DIO0
#define WDT_TIMEOUT 10  // seconds
#define RELAY_PIN 5     // Relay Pin

#define PIN_VOLTAGE 17
#define SENSITIVITY 400.0f
#define CALIBRATION_FACTOR 1.38
ZMPT101B voltageSensor(PIN_VOLTAGE, 60.0);

#define PIN_CURRENT 18
// ADC ESP32
const float Vref = 3.3;
const float ADCmax = 4095.0;

// Muestreo
const int N = 1000;
const float fs = 4000.0;

// Sensor CT
const float Rb = 100.0;
const float Nct = 1000.0;

float Ki = 1.0;
float calibracion = 0.60;

float currentValue = 0.0;
float meanAdc = 0.0;
float sumI2 = 0.0;
float sumaPromedio = 0.0;
int sampleIndex = 0;
int promedioIndex = 0;

unsigned long lastCurrentSample = 0;

const unsigned long CURRENT_SAMPLE_US =
  (unsigned long)(1000000.0 / fs);

bool measuringOffset = true;

float voltageSum = 0.0;
float voltageValue = 0.0;

int sampleCount = 0;

unsigned long lastSampleTime = 0;

const int NUM_MUESTRAS = 5;
const int SAMPLE_INTERVAL = 100;

bool receivingTel = false;

unsigned long lastTelemetryTime = 0;
const unsigned long TELEMETRY_INTERVAL = 1500;

TaskHandle_t watchdogTaskHandle = NULL;

void setup() {
  Serial.begin(115200);
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_CURRENT, ADC_11db);
  while (!Serial);

  strip.begin();
  strip.setBrightness(20);
  strip.clear();
  strip.show();

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);
  updateRelayLEDs();

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

  if (micros() - lastCurrentSample >= CURRENT_SAMPLE_US) {

    lastCurrentSample = micros();
    int raw = analogRead(PIN_CURRENT);
    float v = (raw * Vref) / ADCmax;

    // OFFSET PHASE
    if (measuringOffset) {
      meanAdc += v;
      sampleIndex++;

      if (sampleIndex >= N) {
        meanAdc /= N;
        sampleIndex = 0;
        measuringOffset = false;
      }
    }

    // RMS PHASE
    else {
      float vac = v - meanAdc;
      float iSec = vac / Rb;
      float iPrim = Ki * Nct * iSec;
      sumI2 += iPrim * iPrim;
      sampleIndex++;

      if (sampleIndex >= N) {

        float Irms = sqrt(sumI2 / N);
        // Correccion offset
        Irms = (Irms - 0.10) * calibracion;

        if (Irms < 0.01)
          Irms = 0;

        sumaPromedio += Irms;
        promedioIndex++;

        if (promedioIndex >= 5) {

          currentValue = sumaPromedio / 5.0;

          sumaPromedio = 0;
          promedioIndex = 0;
        }

        // Reset cycle

        meanAdc = 0.0;
        sumI2 = 0.0;

        sampleIndex = 0;

        measuringOffset = true;
      }
    }
  }

  if (receivingTel &&
      millis() - lastTelemetryTime >= TELEMETRY_INTERVAL) {

    lastTelemetryTime = millis();

    String payload = buildTelemetryCSV();

    LoRa.beginPacket();
    LoRa.print(payload);
    LoRa.endPacket();
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

String buildTelemetryCSV() {

  float powerValue = 0;

  if (voltageValue > 0 && currentValue > 0) {
    powerValue = voltageValue * currentValue;
  }

  String payload = "";
  payload.reserve(64);

  payload += String(voltageValue, 3);
  payload += ",";

  payload += String(currentValue, 3);
  payload += ",";

  payload += String(powerValue, 3);
  payload += ",";

  payload += String(10000);

  return payload;
}

void handleRequest(String cmd) {
  if (cmd == "GO") {
    LoRa.beginPacket();
    LoRa.print("INFO_ON");
    LoRa.endPacket();

    receivingTel = true;

    lastTelemetryTime = millis();
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
    updateRelayLEDs();
    LoRa.beginPacket();
    LoRa.print("CONT_ON");
    LoRa.endPacket();
  }

  else if (cmd == "OFF") {
    Serial.println("Contacto Apagado");
    digitalWrite(RELAY_PIN, LOW);
    updateRelayLEDs();
    LoRa.beginPacket();
    LoRa.print("CONT_OFF");
    LoRa.endPacket();
  }

  else if (cmd == "VOLT") {
    LoRa.beginPacket();
    LoRa.print(voltageValue, 3);
    LoRa.endPacket();
  }

  else if (cmd == "CORR") {
    LoRa.beginPacket();
    LoRa.print(currentValue, 3);
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

void updateRelayLEDs() {
  if (digitalRead(RELAY_PIN) == HIGH) {
    // GREEN = relay ON
    for (int i = 0; i < LED_COUNT; i++) {
      strip.setPixelColor(i, strip.Color(0, 25, 0));
    }
  }

  else {
    // RED = relay OFF
    for (int i = 0; i < LED_COUNT; i++) {
      strip.setPixelColor(i, strip.Color(25, 0, 0));
    }
  }

  strip.show();
}

void watchdogTask(void *parameter) {
  esp_task_wdt_add(NULL);
  Serial.println("WATCHDOG_TASK_ENABLED");

  while (true) {
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
