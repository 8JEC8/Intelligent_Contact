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
const float Rb = 100.0;
const float Nct = 1000.0;

float Ki = 1.0;
float calibracion = 0.60;

// =============================
// VARIABLES NON-BLOCKING
// =============================

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

// =============================
// SETUP
// =============================

void setup() {

  Serial.begin(115200);

  delay(2000);

  // Configuracion ADC ESP32
  analogReadResolution(12);

  // Atenuacion para medir hasta 3.3V
  analogSetPinAttenuation(pinI, ADC_11db);

  Serial.println("ESP32 Current Sensor Ready");
}

// =============================
// LOOP
// =============================

void loop() {

  // =====================================
  // NON-BLOCKING CURRENT SAMPLING
  // =====================================

  if (micros() - lastCurrentSample >= CURRENT_SAMPLE_US) {

    lastCurrentSample = micros();

    int raw = analogRead(pinI);

    float v = (raw * Vref) / ADCmax;

    // =====================================
    // OFFSET PHASE
    // =====================================

    if (measuringOffset) {

      meanAdc += v;

      sampleIndex++;

      if (sampleIndex >= N) {

        meanAdc /= N;

        sampleIndex = 0;

        measuringOffset = false;
      }
    }

    // =====================================
    // RMS PHASE
    // =====================================

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

        // =====================================
        // FINAL AVERAGE OF 5 RMS MEASUREMENTS
        // =====================================

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

  // =====================================
  // SERIAL COMMAND
  // =====================================

  if (Serial.available()) {

    String cmd = Serial.readStringUntil('\n');

    cmd.trim();

    if (cmd == "C") {

      Serial.print("Irms [A]: ");
      Serial.println(currentValue, 5);
    }
  }
}
