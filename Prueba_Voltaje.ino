#include <Wire.h>
#include <ZMPT101B.h>

#define PIN_VOLTAGE 27

#define SENSITIVITY 400.0f
#define CALIBRATION_FACTOR 1.38

#define NUM_MUESTRAS 5  // Número de lecturas para promedio

ZMPT101B voltageSensor(PIN_VOLTAGE, 60.0);

void setup() {
  Serial.begin(115200);

  analogReadResolution(12); // ADC de 12 bits en ESP32
  voltageSensor.setSensitivity(SENSITIVITY);
}

void sensor() {
  float sumaVoltaje = 0.0;

  // Tomar 5 mediciones
  for (int i = 0; i < NUM_MUESTRAS; i++) {
    float voltage = (voltageSensor.getRmsVoltage() * CALIBRATION_FACTOR)-4;
    sumaVoltaje += voltage;

    delay(100); // pequeña pausa entre mediciones
  }

  // Promedio
  float voltajePromedio = sumaVoltaje / NUM_MUESTRAS;

  // Mostrar resultado final
  Serial.println("----- PROMEDIO VOLTAJE -----");
  Serial.print("Voltaje RMS Promedio: ");
  Serial.print(voltajePromedio, 2);
  Serial.println(" V");
  Serial.println("----------------------------");
}

void loop() {
  sensor();
  delay(1000);
}
