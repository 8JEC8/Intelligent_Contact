#include <SPI.h>
#include <LoRa.h>
#include <WiFi.h>
#include <WebSocketsServer.h>
#include <WebServer.h>
#include "esp_task_wdt.h"
#include <LittleFS.h>
#define LORA_FREQ 433E6
#define LORA_SS   10    // CS
#define LORA_DIO0 42   // DIO0
#define WDT_TIMEOUT 10   // seconds

//////////////////////////////////////////////////////////////////////////////////////////////////// HTML embebido
const char webpage[] PROGMEM = R"rawliteral(

)rawliteral";

const char* ssid = "CI_Cavs";
const char* password = "12345678";

WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);

uint8_t controllerClient = 255; // 255 = no hay controlador aún

TaskHandle_t watchdogTaskHandle = NULL;

int expectedChunks = 0;
int receivedChunks = 0;
bool receivingImage = false;

String msgToSend = "";                    // Fila de mensaje de monitor serial
bool msgQueued = false;

unsigned long retryInterval = 100;        // Default LoRa SHORT: ms entre intervalos de envío
unsigned long ackTimeout = 500;           // Default LoRa MID: 500ms espera en reintento
unsigned long chunkTimeout = 1500;        // Default LoRa MID: 1.5 segundos para reenviar solicitud de chunk

unsigned long msgStartTime = 0;           // Cuando el mensaje fue enviado
unsigned long lastSendAttempt = 0;        // Cuando el mensaje se intentó enviar
unsigned long lastChunkRequestTime = 0;   // Cuando el chunk se solicitó por última vez

float csvInt = 1.5;

// Estados LoRa
enum LoRaRange { SHORT, MID, LONG };
LoRaRange currentRange = SHORT;  // Iniciar SHORT

// RSSI Umbral
const int shortToMid = -65;   // SHORT -> MID
const int midToShort  = -45;  // MID -> SHORT
const int midToLong   = -110; // MID -> LONG
const int longToMid   = -90;  // LONG -> MID

// Promedio RSSI
const int rssiSampleCount = 4;    // Muestras en promediado
int rssiSamples[rssiSampleCount] = {0};
int sampleIndex = 0;
int sampleTotal = 0;
int sampleFilled = 0;

void setup() {
  Serial.begin(115200);
  while (!Serial);

  if (!LittleFS.begin()) {
    Serial.println("ERR_LittleFS");
  }
  
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

  // Start AP
  esp_log_level_set("wifi", ESP_LOG_NONE);
  WiFi.softAP(ssid, password);
  IPAddress IP = WiFi.softAPIP();
  Serial.print("\nConectar a Dashboard: http://");
  Serial.println(IP);

  server.on("/", HTTP_GET, []() {
    server.send_P(200, "text/html", webpage);
  });


  /* SERVIDOR */

  server.begin();

  webSocket.begin();

  webSocket.onEvent(onWebSocketEvent);

  SPI.begin(13, 12, 11, 10);
  LoRa.setPins(LORA_SS, -1, LORA_DIO0);

  if (!LoRa.begin(LORA_FREQ)) {
    Serial.println("AP_ERR_LoRa_FAILED");
    while (true);
  }

  LoRa.setTxPower(20);
  LoRa.setSpreadingFactor(7);     // Spreading Factor
  LoRa.setSignalBandwidth(250E3); // BW
  LoRa.setCodingRate4(5);         // Coding Rate
  LoRa.setSyncWord(0x88);         // Sync word
  LoRa.setPreambleLength(6);      // Preamble:
  LoRa.enableCrc();               // CRC

  Serial.println("Contacto Súper-Mega-Hiper-Inteligente: Comunicación LoRa Habillitada");
  Serial.println("'.COMLIST' para Lista de Comandos Disponibles");
}

void loop() {
  unsigned long now = millis();

  esp_task_wdt_reset();

  webSocket.loop();
  server.handleClient();

  // Recepción LORA
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    String received = "";
    while (LoRa.available()) received += (char)LoRa.read();
    received.trim();

    // Filtrar basura
    bool valid = true;
    for (unsigned int i = 0; i < received.length(); i++) {
      char c = received[i];
      if ((c < 32 || c > 126) && c != '\n' && c != '\r') {
        valid = false;
        break;
      }
    }

    if (!valid) {
      Serial.println("NOISE_RECEIVED");
      return;  // Descartar antes de actualizar RSSI
    }
    
    // Obtener RSSI
    int rssi = LoRa.packetRssi();  // RSSI de mensaje entrante
    sampleTotal -= rssiSamples[sampleIndex];    // Quitar T4
    rssiSamples[sampleIndex] = rssi;            // Guardar T0
    sampleTotal += rssiSamples[sampleIndex];    // Sumar al total

    sampleIndex = (sampleIndex + 1) % rssiSampleCount;    // Circular buffer
    if (sampleFilled < rssiSampleCount) sampleFilled++;

    int avgRssi = sampleTotal / sampleFilled;  // Promediar

    Serial.println("  RECV_CONTACT_" + String(rssi) + "," + String(avgRssi) + "," + received);

    if (msgQueued && received.startsWith("ACK_")) {
      String ackFor = received.substring(4);
      if (ackFor == msgToSend) {
          msgQueued = false;
          msgToSend = "";

          if (ackFor == "SRA") LoRaShort();
          else if (ackFor == "MRA") LoRaMid();
          else if (ackFor == "LRA") LoRaLong();
      }

      if (ackFor.startsWith("INT")) {
        String lenINT = ackFor.substring(3);
        float newInt = lenINT.toFloat();
        if (newInt > 0) csvInt = newInt;
        Serial.println("  SET_CSV_INTERVAL_" + String(csvInt));
        webSocket.broadcastTXT("CSVINT_" + String(csvInt));
      }
    }

    // Cambio dinámico dependiendo de Thresholds
    switch (currentRange) {
        case SHORT:
            if (avgRssi < shortToMid) {
                Serial.println("AUTOSWITCH_MID");
                queueMessage("MRA");
            }
            break;

        case MID:
            if (avgRssi > midToShort) {
                Serial.println("AUTOSWITCH_SHORT");
                queueMessage("SRA");
            } else if (avgRssi < midToLong) {
                Serial.println("AUTOSWITCH_LONG");
                queueMessage("LRA");
            }
            break;

        case LONG:
            if (avgRssi > longToMid) {
                Serial.println("AUTOSWITCH_MID");
                queueMessage("MRA");
            }
            break;
    }

    if (received.startsWith("M,")) {
      webSocket.broadcastTXT(received);
    }

    else if (received.indexOf(',') != -1) {
      String webTelemetry = String(avgRssi) + "," + received;
      webSocket.broadcastTXT(webTelemetry);
    }

    else if (received.startsWith("CONT_ON")) {
      webSocket.broadcastTXT("RELAY_ON");
    }

    else if (received.startsWith("CONT_OFF")) {
      webSocket.broadcastTXT("RELAY_OFF");
    }

  }

  // REINTENTAR mensaje en cola con Timeout
  if (msgQueued) {
      // Mandar y reintentar
      if (now - lastSendAttempt >= retryInterval && LoRa.beginPacket()) {
          LoRa.print(msgToSend);
          LoRa.endPacket();
          lastSendAttempt = now;

          // Comenzar timeout al mandar primera vez
          if (msgStartTime == 0) msgStartTime = now;

          Serial.println("SENT_EST_" + msgToSend);
      }

      // Timeout de no recibir ACK
      if (msgStartTime > 0 && now - msgStartTime >= ackTimeout) {
          Serial.println("MSG_TIMEOUT");
          msgQueued = false;
          msgToSend = "";
          msgStartTime = 0;
      }
    }
  
  // Mensajes Serial
  handleSerial();
}

void handleSerial() {
  while (Serial.available()) {
    String serialInput = Serial.readStringUntil('\n');
    serialInput.trim();

    if (serialInput.length() == 0) continue;

    if (serialInput.equalsIgnoreCase(".COMLIST")) {
      printCommandList();
    }

    else if (serialInput.equalsIgnoreCase(".RESETSTA")) {
      delay(200);
      ESP.restart();
    }

    else if (serialInput.startsWith(".INTERVAL")) {
      String intervalStr = serialInput.substring(9);
      intervalStr.trim();
      float intervalVal = intervalStr.toFloat();

      if (intervalVal > 0) {
        queueMessage("INT" + String(intervalVal));
      } else {
        Serial.println("CSV_INTERVAL_INVALID");
      }
    }

    else if (serialInput.equalsIgnoreCase(".FORCESHORT")) {
      LoRaShort();
    }
    else if (serialInput.equalsIgnoreCase(".FORCEMID")) {
      LoRaMid();
    }
    else if (serialInput.equalsIgnoreCase(".FORCELONG")) {
      LoRaLong();
    }

    else if (serialInput.equalsIgnoreCase(".ON")) queueMessage("ON");
    else if (serialInput.equalsIgnoreCase(".OFF")) queueMessage("OFF");
    else if (serialInput.equalsIgnoreCase(".V")) queueMessage("VOLT");
    else if (serialInput.equalsIgnoreCase(".C")) queueMessage("CORR");
    else if (serialInput.equalsIgnoreCase(".F")) queueMessage("FASE");
    else if (serialInput.equalsIgnoreCase(".START")) queueMessage("GO");
    else if (serialInput.equalsIgnoreCase(".STOP")) queueMessage("STP");
    else if (serialInput.equalsIgnoreCase(".RECONT")) queueMessage("RECONT");
    else  {
      Serial.println("UNKNOWN_IGNORED");
    }
  }
}

void queueMessage(String msg) {
  msgToSend = msg;
  msgQueued = true;
  lastSendAttempt = 0;  // Enviar inmediatamente mensaje en cola
  msgStartTime = 0;
}

void broadcastControlState() {
  String msg = "CTRL_" + String(controllerClient);
  webSocket.broadcastTXT(msg);
}

void onWebSocketEvent(
  uint8_t client_num,
  WStype_t type,
  uint8_t * payload,
  size_t length
){

  switch(type){

    /* ========================= */
    /* CLIENTE CONECTADO */
    /* ========================= */

    case WStype_CONNECTED: {

      Serial.printf(
      "CLIENT_%u_CONNECTED\n",
      client_num);

      webSocket.sendTXT(
      client_num,
      "ASSIGN_ID_" + String(client_num));

      webSocket.sendTXT(
      client_num,
      "CTRL_" + String(controllerClient));

      /* ========================= */
      /* PRIMER CLIENTE */
      /* ========================= */

      if(controllerClient == 255){

        controllerClient = client_num;

        broadcastControlState();

      }

      break;
    }

    /* ========================= */
    /* CLIENTE DESCONECTADO */
    /* ========================= */

    case WStype_DISCONNECTED: {

      Serial.printf(
      "CLIENT_%u_DISCONNECTED\n",
      client_num);

      if(client_num == controllerClient){

        controllerClient = 255;

        broadcastControlState();

      }

      break;
    }

    /* ========================= */
    /* MENSAJES */
    /* ========================= */

    case WStype_TEXT: {

      String msg =
      String((char*)payload);

      msg.trim();

      /* ========================= */
      /* TOMAR CONTROL */
      /* ========================= */

      if(msg == "REQUEST_CONTROL"){

        if(controllerClient == 255){

          controllerClient = client_num;

          broadcastControlState();

        }

        return;
      }

      /* ========================= */
      /* LIBERAR CONTROL */
      /* ========================= */

      if(msg == "RELEASE_CONTROL"){

        if(client_num == controllerClient){

          controllerClient = 255;

          broadcastControlState();

        }

        return;
      }
      /* ========================= */
      /* SOLO CONTROLADOR */
      /* ========================= */

      if(client_num == controllerClient){

        if(msg == "ON"){

          queueMessage("ON");

        }

        else if(msg == "OFF"){

          queueMessage("OFF");

        }

        else if(msg == "GO"){

          queueMessage("GO");

        }

        else if(msg == "STOP"){

          queueMessage("STP");

        }

      }
      break;
    }

    default:
    break;

  }

}

void LoRaShort() {
  Serial.println("LoRa_CHANGE_SF7");
  LoRa.idle();
  delay(150);
  LoRa.setSpreadingFactor(7);
  LoRa.setSignalBandwidth(250E3);
  LoRa.setCodingRate4(5);
  LoRa.setPreambleLength(6);
  delay(50);
  retryInterval = 100;
  ackTimeout = 500;
  currentRange = SHORT;
  webSocket.broadcastTXT("CSVINT_1.5");
  webSocket.broadcastTXT("CK_200");
}

void LoRaMid() {
  Serial.println("LoRa_CHANGE_SF9");
  LoRa.idle();
  delay(150);
  LoRa.setSpreadingFactor(9);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(6);
  LoRa.setPreambleLength(8);
  delay(50);
  retryInterval = 300;
  ackTimeout = 1500;
  currentRange = MID;
  webSocket.broadcastTXT("CSVINT_2.5");
  webSocket.broadcastTXT("CK_128");
}

void LoRaLong() {
  Serial.println("LoRa_CHANGE_SF11");
  LoRa.idle();
  delay(150);
  LoRa.setSpreadingFactor(11);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(8);
  LoRa.setPreambleLength(10);
  delay(50);
  retryInterval = 1500;
  ackTimeout = 6000;
  currentRange = LONG;
  webSocket.broadcastTXT("CSVINT_5.0");
  webSocket.broadcastTXT("CK_64");
}

void printCommandList() {
  Serial.println("  Lista de Comandos:");
  Serial.println("    '.RESETSTA'     : Reiniciar ESP de Estación Terrestre");
  Serial.println("    '.ON'           : Habilitar Contacto Remoto");
  Serial.println("    '.OFF'          : Apagar Contacto Remoto");
  Serial.println("    '.V'            : Obtener lectura de Voltaje INSTANTANEO");
  Serial.println("    '.C'            : Obtener lectura de Corriente INSTANTANEA");
  Serial.println("    '.F'            : Obtener lectura de Fase INSTANTANEA");
  Serial.println("    '.START'        : Inicia envío de telemetría");
  Serial.println("    '.STOP'         : Detiene envío de telemetría");
  Serial.println("    '.RECONT'        : Resetear Contacto");
}

void watchdogTask(void *parameter) {
  esp_task_wdt_add(NULL);
  Serial.println("WATCHDOG_TASK_ENABLED");

 while (true) {
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
