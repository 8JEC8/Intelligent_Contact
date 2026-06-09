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

// ===== Sistema de validación de carga =====

enum LoadType {
  NONE,
  FOCO,
  VENTILADOR,
  FOCO2,
  CUSTOM
};

LoadType selectedLoad = NONE;

unsigned long loadValidTimer = 0;
unsigned long LOAD_TIMEOUT = 7000; // 15 segundos

bool systemEnabled = true;

// FOCO
float focoCurrentMin = 0.5;
float focoCurrentMax = 0.7;

float focoPFMin = 0.90;
float focoPFMax = 1.00;

// VENTILADOR
float ventCurrentMin = 0.1;
float ventCurrentMax = 0.26;

float ventPFMin = 0.64;
float ventPFMax = 0.75;

//  FOCO 2
float foco2CurrentMin=0.28;
float foco2CurrentMax=0.38;

float foco2PFMin=0.9;
float foco2PFMax=1.00;

// Carga personalizada
float customCurrentMin=0.0;
float customCurrentMax=0.0;

float customPFMin=0.0;
float customPFMax=0.0;

float customCurrent=0.0;
float customPF=0.0;

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

    if(!LittleFS.begin(true)){
    Serial.println("LittleFS ERROR");
    return;
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

    File file = LittleFS.open("/index.html", "r");

    if(!file){
        server.send(404, "text/plain", "index.html NOT FOUND");
        return;
    }

    server.streamFile(file, "text/html");

    file.close();

  });

  server.serveStatic("/", LittleFS, "/");

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

      if (ackFor.startsWith("ON")) {
        loadValidTimer = millis();
        systemEnabled = true;
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

      // ===== EXTRAER DATOS =====

    int firstComma = received.indexOf(',');
    int secondComma = received.indexOf(',', firstComma + 1);
    int lastComma = received.lastIndexOf(',');

    // Corriente = segundo valor
    float currentValue = received.substring(firstComma + 1, secondComma).toFloat();

    // FP = último valor
    float pfValue = received.substring(lastComma + 1).toFloat();

    if (isExpectedLoad(currentValue, pfValue)) {

        loadValidTimer = millis();

    } else {

        if (millis() - loadValidTimer > LOAD_TIMEOUT && currentValue!=0.0) {

            systemEnabled = false;

            queueMessage("OFF");

            Serial.println("Carga incorrecta -> Sistema apagado");
        }
    }
}
        else if (received.startsWith("CONT_ON")) {
          webSocket.broadcastTXT("RELAY_ON");
        }

        else if (received.startsWith("CONT_OFF")) {
          webSocket.broadcastTXT("RELAY_OFF");
        }

        else if (received.startsWith("INFO_ON")) {
          webSocket.broadcastTXT("READING_ON");
        }
    
        else if (received.startsWith("INFO_OFF")) {
          webSocket.broadcastTXT("READING_OFF");
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
    serialInput.toUpperCase();
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
    else if (serialInput.equalsIgnoreCase(".FOCO")) {

        Serial.println("Modo FOCO seleccionado");
        selectedLoad = FOCO;
        loadValidTimer = millis();

        systemEnabled = true;


    }

    else if (serialInput.equalsIgnoreCase(".VENT")) {

        Serial.println("Modo VENTILADOR seleccionado");
        selectedLoad = VENTILADOR;
        loadValidTimer = millis();

        systemEnabled = true;


    }

    else if (serialInput.equalsIgnoreCase(".FOCO2")) {

        Serial.println("Modo FOCO2 seleccionado");
        selectedLoad = FOCO2;
        loadValidTimer = millis();

        systemEnabled = true;


    }
    else if (serialInput.equalsIgnoreCase(".NONE")) {

        Serial.println("Modo SIN VALIDACION seleccionado");

        selectedLoad = NONE;

        loadValidTimer = millis();

        systemEnabled = true;
    }
    else if (serialInput.startsWith(".CUSTOM")){
        Serial.println("Modo CUSTOM seleccionado");
        float localCurrent=0.0;
        float localPF=0.0;    

        int firstSpace=serialInput.indexOf(' ');
        int secondSpace = serialInput.indexOf(' ',firstSpace+1);

        if (firstSpace > 0 && secondSpace >firstSpace){
            String currentString=serialInput.substring(firstSpace+1,secondSpace);
            String pfString=serialInput.substring(secondSpace +1);
            
            localCurrent= currentString.toFloat();
            localPF= pfString.toFloat();

             
            customCurrentMin=localCurrent-0.10;
            customCurrentMax=localCurrent+0.10;
            customPFMin=localPF-0.3;
            customPFMax=localPF+0.3;

        

            Serial.println("Corriente = " + String(localCurrent)+ "  FP = "+String(localPF));
            
            selectedLoad=CUSTOM;      
            loadValidTimer=millis();
            systemEnabled=true;
        }
        else{
            Serial.println("Formato invalido. Use .CUSTOM <corriente> <fp>");
        }
    }
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

      switch(currentRange){

          case SHORT:
              webSocket.sendTXT(
              client_num,
              "LORA_MODE_SHORT");
              break;

          case MID:
              webSocket.sendTXT(
              client_num,
              "LORA_MODE_MID");
              break;

          case LONG:
              webSocket.sendTXT(
              client_num,
              "LORA_MODE_LONG");
              break;
      }

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
        else if(msg== "LORA_SHORT"){
            LoRaShort();
        }
        else if(msg=="LORA_MID"){
            LoRaMid();
        }
        else if(msg=="LORA_LONG"){
            LoRaLong();
        }

        else if(msg == "LOAD_FOCO"){

            selectedLoad = FOCO;
            loadValidTimer = millis();
            systemEnabled = true;

            Serial.println("WEB -> FOCO");
        }

        else if(msg == "LOAD_VENT"){

            selectedLoad = VENTILADOR;
            loadValidTimer = millis();
            systemEnabled = true;

            Serial.println("WEB -> VENTILADOR");
        }

        else if(msg == "LOAD_FOCO2"){

            selectedLoad = FOCO2;
            loadValidTimer = millis();
            systemEnabled = true;

            Serial.println("WEB -> FOCO2");
        }

        else if(msg == "LOAD_NONE"){

            selectedLoad = NONE;
            loadValidTimer = millis();
            systemEnabled = true;

            Serial.println("WEB -> NONE");
        }

        else if(msg.startsWith("LOAD_CUSTOM_")){

            int firstUnderscore =
            msg.indexOf('_', 12);

            String currentStr =
            msg.substring(12, firstUnderscore);

            String pfStr =
            msg.substring(firstUnderscore + 1);

            float current =
            currentStr.toFloat();

            float pf =
            pfStr.toFloat();

            customCurrentMin = current - 0.10;
            customCurrentMax = current + 0.10;

            customPFMin = pf - 0.30;
            customPFMax = pf + 0.30;

            selectedLoad = CUSTOM;

            loadValidTimer = millis();

            systemEnabled = true;

            Serial.println(
            "WEB -> CUSTOM");
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
  LOAD_TIMEOUT = 6000;
  webSocket.broadcastTXT("LORA_MODE_SHORT");
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
  LOAD_TIMEOUT = 8000;
  webSocket.broadcastTXT("LORA_MODE_MID");
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
  LOAD_TIMEOUT = 10000;
  webSocket.broadcastTXT("LORA_MODE_LONG");
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

bool isExpectedLoad(float current, float pf) {

switch(selectedLoad) {

    case NONE: 
        return true;
    case FOCO:
      return (
        current >= focoCurrentMin &&
        current <= focoCurrentMax &&
        pf >= focoPFMin &&
        pf <= focoPFMax
      );

    case VENTILADOR:
      return (
        current >= ventCurrentMin &&
        current <= ventCurrentMax &&
        pf >= ventPFMin &&
        pf <= ventPFMax
      );
    case FOCO2:
        return (
        current >= foco2CurrentMin &&
        current <= foco2CurrentMax &&
        pf >= foco2PFMin &&
        pf <= foco2PFMax
       );
    case CUSTOM:
        return (
        current >= customCurrentMin &&
        current <= customCurrentMax &&
        pf >= customPFMin &&
        pf <= customPFMax
       );
    default:
      return true;
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
