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

// Muestreo corriente RMS
const int N = 1000;
const float fs = 10000.0;

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
unsigned long TELEMETRY_INTERVAL = 1500;

TaskHandle_t watchdogTaskHandle = NULL;

// =====================================================
// FACTOR DE POTENCIA — variables agregadas
// =====================================================

const int N_FP = 2000;           // Muestras para captura FP
const float fs_FP = 5000.0;      // Frecuencia de muestreo FP

float muestrasVoltajeFP[N_FP];
float muestrasCorrienteFP[N_FP];

const int NUM_FP = 10;
float historialFP[NUM_FP] = {0};
int indiceFP = 0;

float factorPotencia = 0.0;

// FP se recalcula cada FP_INTERVAL ms (no bloqueante)
const unsigned long FP_INTERVAL = 125;
unsigned long lastFPTime = 0;

// Estado de la máquina de estados del cálculo FP
enum FPState { FP_IDLE, FP_OFFSET, FP_SAMPLING, FP_CALC };
FPState fpState = FP_IDLE;

int fpSampleIdx = 0;
float fpOffsetV = 0.0;
float fpOffsetI = 0.0;
unsigned long fpLastSampleUs = 0;
const unsigned long FP_SAMPLE_US = (unsigned long)(1000000.0 / fs_FP);

// =====================================================
// SETUP
// =====================================================

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
    watchdogTask,
    "watchdogTask",
    4096,
    NULL,
    1,
    &watchdogTaskHandle,
    0
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

// =====================================================
// LOOP
// =====================================================

void loop() {
  esp_task_wdt_reset();

  // --- Voltaje RMS (sin cambios) ---
  if (millis() - lastSampleTime >= SAMPLE_INTERVAL) {
    lastSampleTime = millis();

    float voltage = voltageSensor.getRmsVoltage();
    voltageSum += voltage;
    sampleCount++;

    if (sampleCount >= NUM_MUESTRAS) {
      voltageValue = voltageSum / NUM_MUESTRAS;
      voltageSum = 0;
      sampleCount = 0;
    }
  }

  // --- Corriente RMS (sin cambios) ---
  if (micros() - lastCurrentSample >= CURRENT_SAMPLE_US) {
    lastCurrentSample = micros();
    int raw = analogRead(PIN_CURRENT);
    float v = (raw * Vref) / ADCmax;

    if (measuringOffset) {
      meanAdc += v;
      sampleIndex++;
      if (sampleIndex >= N) {
        meanAdc /= N;
        sampleIndex = 0;
        measuringOffset = false;
      }
    } else {
      float vac = v - meanAdc;
      float iSec = vac / Rb;
      float iPrim = Ki * Nct * iSec;
      sumI2 += iPrim * iPrim;
      sampleIndex++;

      if (sampleIndex >= N) {
        float Irms = sqrt(sumI2 / N);
        Irms = (Irms - 0.10) * calibracion;
        if (Irms < 0.01) Irms = 0;

        sumaPromedio += Irms;
        promedioIndex++;

        if (promedioIndex >= 5) {
          currentValue = sumaPromedio / 5.0;
          sumaPromedio = 0;
          promedioIndex = 0;
        }

        meanAdc = 0.0;
        sumI2 = 0.0;
        sampleIndex = 0;
        measuringOffset = true;
      }
    }
  }

  // --- Factor de potencia no bloqueante ---
  calcularFP();

  // --- Telemetría LoRa (sin cambios) ---
  if (receivingTel &&
      millis() - lastTelemetryTime >= TELEMETRY_INTERVAL) {
    lastTelemetryTime = millis();

    String payload = buildTelemetryCSV();

    LoRa.beginPacket();
    LoRa.print(payload);
    LoRa.endPacket();
  }

  // --- LoRa receive (sin cambios) ---
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    String received = "";
    while (LoRa.available()) {
      received += (char)LoRa.read();
    }
    received.trim();

    LoRa.beginPacket();
    LoRa.print("ACK_" + received);
    LoRa.endPacket();

    if (received == "SRA" || received == "MRA" || received == "LRA") {
      delay(250);
    }

    delay(50);
    handleRequest(received);
  }
}

// =====================================================
// FACTOR DE POTENCIA — máquina de estados no bloqueante
// =====================================================

void calcularFP() {

  switch (fpState) {

    // Esperar intervalo antes de iniciar nueva medición
    case FP_IDLE:
      if (millis() - lastFPTime >= FP_INTERVAL) {
        fpOffsetV = 0.0;
        fpOffsetI = 0.0;
        fpSampleIdx = 0;
        fpState = FP_OFFSET;
      }
      break;

    // Calcular offset con 500 muestras (una por iteración de loop)
    case FP_OFFSET:
      if (fpSampleIdx < 500) {
        fpOffsetV += analogRead(PIN_VOLTAGE);
        fpOffsetI += analogRead(PIN_CURRENT);
        fpSampleIdx++;
      } else {
        fpOffsetV /= 500.0;
        fpOffsetI /= 500.0;
        fpSampleIdx = 0;
        fpLastSampleUs = micros();
        fpState = FP_SAMPLING;
      }
      break;

    // Tomar N_FP muestras sincronizadas a fs_FP
    case FP_SAMPLING:
      if (fpSampleIdx < N_FP) {
        if (micros() - fpLastSampleUs >= FP_SAMPLE_US) {
          fpLastSampleUs += FP_SAMPLE_US;

          int rawV = analogRead(PIN_VOLTAGE);
          int rawI = analogRead(PIN_CURRENT);

          muestrasVoltajeFP[fpSampleIdx] = rawV - fpOffsetV;

          float adcI = rawI - fpOffsetI;
          float voltSensor = (adcI * Vref) / ADCmax;
          float iSec = voltSensor / Rb;
          muestrasCorrienteFP[fpSampleIdx] = Ki * Nct * iSec;

          fpSampleIdx++;
        }
      } else {
        fpState = FP_CALC;
      }
      break;

    // Calcular FP a partir de las muestras (no bloqueante, corre en un ciclo de loop)
    case FP_CALC: {
      // Buscar cruce por cero ascendente — voltaje
      int ceroV = -1;
      for (int i = 1; i < N_FP; i++) {
        if (muestrasVoltajeFP[i - 1] < 0 && muestrasVoltajeFP[i] >= 0) {
          ceroV = i;
          break;
        }
      }

      // Buscar cruce por cero ascendente — corriente
      int ceroI = -1;
      for (int i = 1; i < N_FP; i++) {
        if (muestrasCorrienteFP[i - 1] < 0 && muestrasCorrienteFP[i] >= 0) {
          ceroI = i;
          break;
        }
      }

      if (ceroV != -1 && ceroI != -1) {
        int diferenciaMuestras = (ceroI - ceroV);
        float desfaseTiempo = (diferenciaMuestras / fs_FP)-0.00135;
        float angulo = (desfaseTiempo * 2.0 * PI * 60.0);
        float fpInstantaneo = abs(cos(angulo));

        if (fpInstantaneo > 1.0) fpInstantaneo = 1.0;
        if (fpInstantaneo < 0.0) fpInstantaneo = 0.0;

        historialFP[indiceFP] = fpInstantaneo;
        indiceFP++;
        if (indiceFP >= NUM_FP) indiceFP = 0;

        float sumaFP = 0;
        for (int i = 0; i < NUM_FP; i++) sumaFP += historialFP[i];
        factorPotencia = sumaFP / NUM_FP;
      } else {
        factorPotencia = 0.0;
      }

      lastFPTime = millis();
      fpState = FP_IDLE;
      break;
    }
  }
}

// =====================================================
// TELEMETRÍA — FP reemplaza el 10000 placeholder
// =====================================================

String buildTelemetryCSV() {

  // ---------- PROTECTION ----------
  if (voltageValue < 20.0) {
    currentValue = 0.0;
  }

  // ---------- POWER CALCULATIONS ----------
  float apparentPower = 0.0;
  float realPower = 0.0;
  float reactivePower = 0.0;

  if (voltageValue > 0.0 &&
      currentValue > 0.0 &&
      factorPotencia > 0.0) {

    // Apparent Power (VA)
    apparentPower = voltageValue * currentValue;

    // Safe local PF copy
    float pf = factorPotencia;

    if (pf > 1.0) pf = 1.0;
    if (pf < 0.0) pf = 0.0;

    // Real Power (W)
    realPower = apparentPower * pf;

    // Phase angle from PF
    float phi = acos(pf);

    // Reactive Power (VAR)
    reactivePower = apparentPower * sin(phi);
  }

  // ---------- CSV BUILD ----------
  String payload;
  payload.reserve(128);

  payload += String(voltageValue, 3);
  payload += ",";

  payload += String(currentValue, 3);
  payload += ",";

  payload += String(apparentPower, 3);
  payload += ",";

  payload += String(realPower, 3);
  payload += ",";

  payload += String(reactivePower, 3);
  payload += ",";

  payload += String(factorPotencia, 3);

  return payload;
}

// =====================================================
// HANDLE REQUEST (sin cambios excepto caso "FP")
// =====================================================

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
 float corrDisplay = (voltageValue < 20.0) ? 0.0 : currentValue;
    LoRa.beginPacket();
    LoRa.print(currentValue, 3);
    LoRa.endPacket();
  }

  // Comando FP agregado — on-demand desde el receptor
  else if (cmd == "FASE") {
    LoRa.beginPacket();
    LoRa.print(factorPotencia, 3);
    LoRa.endPacket();
  }
/*
  else if (cmd == "FASE") {
    Serial.println("*Obtener con Sensor de V y I*");
    LoRa.beginPacket();
    LoRa.print("*Fase*");
    LoRa.endPacket();
  }
*/
  else if (cmd == "SRA") {
    LoRa.setSpreadingFactor(7);
    LoRa.setSignalBandwidth(250E3);
    LoRa.setCodingRate4(5);
    LoRa.setPreambleLength(6);
    TELEMETRY_INTERVAL = 1500;
  }

  else if (cmd == "MRA") {
    LoRa.setSpreadingFactor(9);
    LoRa.setSignalBandwidth(125E3);
    LoRa.setCodingRate4(6);
    LoRa.setPreambleLength(8);
    TELEMETRY_INTERVAL = 2500;
  }

  else if (cmd == "LRA") {
    LoRa.setSpreadingFactor(11);
    LoRa.setSignalBandwidth(125E3);
    LoRa.setCodingRate4(8);
    LoRa.setPreambleLength(10);
    TELEMETRY_INTERVAL = 5000;
  }

  else if (cmd == "RECONT") {
    delay(200);
    ESP.restart();
  }
}

// =====================================================
// LEDS Y WATCHDOG (sin cambios)
// =====================================================

void updateRelayLEDs() {
  if (digitalRead(RELAY_PIN) == HIGH) {
    for (int i = 0; i < LED_COUNT; i++) {
      strip.setPixelColor(i, strip.Color(0, 25, 0));
    }
  } else {
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
