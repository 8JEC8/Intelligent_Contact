#include <math.h>

// =============================
// CONFIGURACION
// =============================

// Pin ADC del ESP32
const int pinI = 2;

// ADC ESP32
const float Vref = 3.3;
const float ADCmax = 4095.0;

// Muestreo
const int N = 1000;
const float fs = 4000.0;

// Sensor CT
const float Rb = 100.0;      // Burden resistor
const float Nct = 1000.0;   // Relacion CT
float Ki = 1.0;
float calibracion = 0.60;

// =============================
// SETUP
// =============================

void setup() {

  Serial.begin(115200);

  delay(2000);

  // Configuracion ADC ESP32
  analogReadResolution(12); // 0-4095

  // Atenuacion para medir hasta 3.3V
  analogSetPinAttenuation(pinI, ADC_11db);

  Serial.println("ESP32 Current Sensor Ready");
}

// =============================
// LOOP
// =============================

void loop() {


  float IrmsFinal = medirIrmsPromedio();

  Serial.print("Irms [A]: ");
  Serial.println(IrmsFinal, 5);

  delay(500);
}

float medirIrmsPromedio() {

  float sumaPromedio = 0;

  // Tomar 5 mediciones RMS
  for (int k = 0; k < 5; k++) {

    float meanAdc = 0.0;

    // =============================
    // OFFSET
    // =============================

    for (int n = 0; n < N; n++) {

      int raw = analogRead(pinI);

      float v = (raw * Vref) / ADCmax;

      meanAdc += v;

      delayMicroseconds((int)(1000000.0 / fs));
    }

    meanAdc /= N;

    // =============================
    // RMS
    // =============================

    float sumI2 = 0.0;

    for (int n = 0; n < N; n++) {

      int raw = analogRead(pinI);

      float v = (raw * Vref) / ADCmax;

      float vac = v - meanAdc;

      float iSec = vac / Rb;

      float iPrim = Ki * Nct * iSec;

      sumI2 += iPrim * iPrim;

      delayMicroseconds((int)(1000000.0 / fs));
    }

    float Irms = sqrt(sumI2 / N);

    // Correccion offset
    Irms =( Irms - 0.10)*calibracion;

    if (Irms < 0.01)
      Irms = 0;

    sumaPromedio += Irms;
  }

  // Regresar promedio de las 5 mediciones
  return sumaPromedio / 5.0;
}
