#include <Arduino.h>
#include <BLEMidi.h>        
#include <Adafruit_NeoPixel.h> 
#include <WiFi.h>
#include <ESPAsyncWebServer.h> 
#include <AsyncTCP.h>          
#include <ArduinoJson.h>
#include <LittleFS.h>       
#include <DNSServer.h>      

// Controle de Debug Serial
#define DEBUG_SERIAL true

// --- Configurações Globais ---
uint8_t globalBrightness = 50; 
const int NUM_CC_LEDS_FOR_SNAPSHOT = 5;

// --- Configurações dos LEDs Endereçáveis ---
const int LED_PIN = 18;            
const int NUM_TOTAL_LEDS = 10;      
Adafruit_NeoPixel strip(NUM_TOTAL_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);
uint32_t colorOff = strip.Color(0, 0, 0);

// --- Nome do dispositivo BLE alvo ---
const std::string TARGET_DEVICE_NAME = "BlackBox_BLE"; 

// --- Definições dos Botões ---
const int NUM_BUTTONS = 10;
const int buttonPins[NUM_BUTTONS] = {13,12,14,27,26,25,33,32,15,4}; 

// --- Estrutura de Configuração do Botão ---
struct ButtonConfig {
  int mode;          
  uint8_t midiChannel; 
  uint8_t targetNumber;
  uint8_t ccValue;     
  uint8_t ledR, ledG, ledB;
  bool ccLedSnapshot[NUM_CC_LEDS_FOR_SNAPSHOT]; 
};
ButtonConfig buttonConfigs[NUM_BUTTONS];

// Estados de runtime
bool ccLedStates[NUM_CC_LEDS_FOR_SNAPSHOT] = {false}; 
int activePcLedIndex = 0;      
bool buttonActiveState[NUM_BUTTONS];
bool lastRawButtonState[NUM_BUTTONS];
unsigned long lastDebounceTime[NUM_BUTTONS];
const unsigned long debounceDelay = 25; 

// --- Wi-Fi AP & Web Server ---
const char* apSsid = "ESP32_MIDI_Config";
const char* apPassword = "midi12345"; 
AsyncWebServer server(80); 
const char* configFile = "/config.json"; 

// --- DNS Server para Captive Portal ---
DNSServer dnsServer;
const byte DNS_PORT = 53;

// --- BLE Client ---
bool tentandoConectarAGORA = false; 
unsigned long ultimaTentativaScan = 0;
const unsigned long intervaloScan = 1000; 

// Protótipos de Funções
void initializeFileSystem(); 
void loadConfiguration(); 
void saveConfiguration(); 
void updateAllLeds();
void tentarConectarAoServidor();
void setupWebServer();

// =======================
// FUNÇÕES DE SISTEMA DE ARQUIVOS (LittleFS) E CONFIGURAÇÃO
// =======================
void initializeFileSystem() {
  if (DEBUG_SERIAL) Serial.println("[FS] Tentando montar LittleFS...");
  if (!LittleFS.begin(true)) { 
    if (DEBUG_SERIAL) Serial.println("[FS] FALHA ao montar LittleFS!");
    return; 
  }
  if (DEBUG_SERIAL) Serial.println("[FS] LittleFS montado com sucesso.");
}

void loadConfiguration() {
  if (DEBUG_SERIAL) Serial.println("[CONFIG] Carregando config do " + String(configFile) + "...");
  if (LittleFS.exists(configFile)) {
    if (DEBUG_SERIAL) Serial.println("[CONFIG] Arquivo encontrado.");
    File file = LittleFS.open(configFile, "r");
    if (!file) {
      if (DEBUG_SERIAL) Serial.println("[CONFIG] FALHA ao abrir arquivo. Carregando/salvando padrões.");
      globalBrightness = 50; activePcLedIndex = 0; 
      for (int i = 0; i < NUM_BUTTONS; i++) {
        buttonConfigs[i].mode = (i < NUM_CC_LEDS_FOR_SNAPSHOT) ? 1 : 2; 
        buttonConfigs[i].midiChannel = 1;
        buttonConfigs[i].targetNumber = (i < NUM_CC_LEDS_FOR_SNAPSHOT) ? i : (i - NUM_CC_LEDS_FOR_SNAPSHOT); 
        buttonConfigs[i].ccValue = 0; 
        bool isDefaultPcActive = (i >= NUM_CC_LEDS_FOR_SNAPSHOT && (i - NUM_CC_LEDS_FOR_SNAPSHOT) == activePcLedIndex);
        buttonConfigs[i].ledR = isDefaultPcActive ? 0 : ((i < NUM_CC_LEDS_FOR_SNAPSHOT) ? 0 : 0);
        buttonConfigs[i].ledG = isDefaultPcActive ? 200 : ((i < NUM_CC_LEDS_FOR_SNAPSHOT) ? 0 : 0);
        buttonConfigs[i].ledB = isDefaultPcActive ? 0 : ((i < NUM_CC_LEDS_FOR_SNAPSHOT) ? 200 : 0);
        for(int k=0; k<NUM_CC_LEDS_FOR_SNAPSHOT; ++k) { buttonConfigs[i].ccLedSnapshot[k] = false; }
      }
      saveConfiguration(); 
      strip.setBrightness(globalBrightness); 
      if (DEBUG_SERIAL) Serial.printf("[LED] Brilho padrão (falha ao abrir): %d\n", globalBrightness);
      return;
    }

    StaticJsonDocument<4096> doc; // Aumentado para ccLedSnapshot e 10 botões
    DeserializationError error = deserializeJson(doc, file);
    file.close(); 

    if (error) {
      if (DEBUG_SERIAL) { Serial.print("[CONFIG] FALHA JSON: "); Serial.println(error.c_str()); }
      if (DEBUG_SERIAL) Serial.println("[CONFIG] Carregando padrões e salvando.");
      globalBrightness = 50; activePcLedIndex = 0;
      for (int i = 0; i < NUM_BUTTONS; i++) { 
        buttonConfigs[i].mode = (i < NUM_CC_LEDS_FOR_SNAPSHOT) ? 1 : 2; 
        buttonConfigs[i].midiChannel = 1;
        buttonConfigs[i].targetNumber = (i < NUM_CC_LEDS_FOR_SNAPSHOT) ? i : (i - NUM_CC_LEDS_FOR_SNAPSHOT); 
        buttonConfigs[i].ccValue = 0; 
        bool isDefaultPcActive = (i >= NUM_CC_LEDS_FOR_SNAPSHOT && (i - NUM_CC_LEDS_FOR_SNAPSHOT) == activePcLedIndex);
        buttonConfigs[i].ledR = isDefaultPcActive ? 0 : ((i < NUM_CC_LEDS_FOR_SNAPSHOT) ? 0 : 0);
        buttonConfigs[i].ledG = isDefaultPcActive ? 200 : ((i < NUM_CC_LEDS_FOR_SNAPSHOT) ? 0 : 0);
        buttonConfigs[i].ledB = isDefaultPcActive ? 0 : ((i < NUM_CC_LEDS_FOR_SNAPSHOT) ? 200 : 0);
        for(int k=0; k<NUM_CC_LEDS_FOR_SNAPSHOT; ++k) { buttonConfigs[i].ccLedSnapshot[k] = false; }
      }
      saveConfiguration();
    } else {
      globalBrightness = doc["globalBrightness"] | 50; 
      JsonArrayConst buttonsArray = doc["buttons"].as<JsonArrayConst>();
      int count = 0;
      if (!buttonsArray.isNull()) {
        for (JsonObjectConst obj : buttonsArray) {
          if (count < NUM_BUTTONS) {
            buttonConfigs[count].mode = obj["mode"] | 0; 
            buttonConfigs[count].midiChannel = obj["midiChannel"] | 1;
            buttonConfigs[count].targetNumber = obj["targetNumber"] | count;
            buttonConfigs[count].ccValue = obj["ccValue"] | 0;
            buttonConfigs[count].ledR = obj["ledR"] | 100;
            buttonConfigs[count].ledG = obj["ledG"] | 100;
            buttonConfigs[count].ledB = obj["ledB"] | 100;
            
            JsonArrayConst snapshot = obj["ccLedSnapshot"].as<JsonArrayConst>(); 
            if (!snapshot.isNull() && snapshot.size() == NUM_CC_LEDS_FOR_SNAPSHOT) {
              for (int k = 0; k < NUM_CC_LEDS_FOR_SNAPSHOT; ++k) {
                buttonConfigs[count].ccLedSnapshot[k] = snapshot[k] | false;
              }
            } else { 
              for(int k=0; k<NUM_CC_LEDS_FOR_SNAPSHOT; ++k) { buttonConfigs[count].ccLedSnapshot[k] = false; }
              if (DEBUG_SERIAL && (obj["mode"] | 0) == 2 && (snapshot.isNull() || snapshot.size() != NUM_CC_LEDS_FOR_SNAPSHOT)) {
                 Serial.printf("[CONFIG] Snapshot para Btn PC %d ausente/inválido no JSON (size: %d). Zerado.\n", count, snapshot.isNull() ? -1 : snapshot.size());
              }
            }
            if (DEBUG_SERIAL) Serial.printf("[CONFIG] Btn %d: M%d Ch%d Tgt%d Val%d RGB(%d,%d,%d) Snap:%d%d%d%d%d\n",
              count, buttonConfigs[count].mode, buttonConfigs[count].midiChannel, buttonConfigs[count].targetNumber,
              buttonConfigs[count].ccValue, buttonConfigs[count].ledR, buttonConfigs[count].ledG, buttonConfigs[count].ledB,
              buttonConfigs[count].ccLedSnapshot[0], buttonConfigs[count].ccLedSnapshot[1], buttonConfigs[count].ccLedSnapshot[2], buttonConfigs[count].ccLedSnapshot[3], buttonConfigs[count].ccLedSnapshot[4]);
            count++;
          }
        }
      }
      bool defaultsWereSetForMissingButtons = false;
      for (int i = count; i < NUM_BUTTONS; i++) { 
        if (DEBUG_SERIAL) Serial.printf("[CONFIG] Padrão para botão %d (JSON tinha %d configs).\n", i, count);
        buttonConfigs[i].mode = (i < NUM_CC_LEDS_FOR_SNAPSHOT) ? 1 : 2; 
        buttonConfigs[i].midiChannel = 1;
        buttonConfigs[i].targetNumber = (i < NUM_CC_LEDS_FOR_SNAPSHOT) ? i : (i - NUM_CC_LEDS_FOR_SNAPSHOT);
        buttonConfigs[i].ccValue = 0; 
        bool isDefaultPcActive = (i >= NUM_CC_LEDS_FOR_SNAPSHOT && (i - NUM_CC_LEDS_FOR_SNAPSHOT) == activePcLedIndex);
        buttonConfigs[i].ledR = isDefaultPcActive ? 0 : ((i < NUM_CC_LEDS_FOR_SNAPSHOT) ? 0 : 0);
        buttonConfigs[i].ledG = isDefaultPcActive ? 200 : ((i < NUM_CC_LEDS_FOR_SNAPSHOT) ? 0 : 0);
        buttonConfigs[i].ledB = isDefaultPcActive ? 0 : ((i < NUM_CC_LEDS_FOR_SNAPSHOT) ? 200 : 0);
        for(int k=0; k<NUM_CC_LEDS_FOR_SNAPSHOT; ++k) { buttonConfigs[i].ccLedSnapshot[k] = false; }
        defaultsWereSetForMissingButtons = true;
      }
      if (defaultsWereSetForMissingButtons || (buttonsArray.isNull() || (buttonsArray.size() < NUM_BUTTONS && buttonsArray.size() >= 0) ) ) {
          if(DEBUG_SERIAL) Serial.println("[CONFIG] (Re)salvando padrões devido a JSON incompleto/ausente ou botões preenchidos com padrão.");
          saveConfiguration(); 
      }
      if (DEBUG_SERIAL) Serial.printf("[CONFIG] Brilho Global: %d. Configs carregadas.\n", globalBrightness);
    }
  } else { 
    if (DEBUG_SERIAL) { Serial.print("[CONFIG] '"); Serial.print(configFile); Serial.println("' nao encontrado. Criando padrões.");}
    globalBrightness = 50; activePcLedIndex = 0; 
    for (int i = 0; i < NUM_BUTTONS; i++) {
      buttonConfigs[i].mode = (i < NUM_CC_LEDS_FOR_SNAPSHOT) ? 1 : 2; 
      buttonConfigs[i].midiChannel = 1;
      buttonConfigs[i].targetNumber = (i < NUM_CC_LEDS_FOR_SNAPSHOT) ? i : (i - NUM_CC_LEDS_FOR_SNAPSHOT); 
      buttonConfigs[i].ccValue = 0;
      bool isDefaultPcActive = (i >= NUM_CC_LEDS_FOR_SNAPSHOT && (i - NUM_CC_LEDS_FOR_SNAPSHOT) == activePcLedIndex);
      buttonConfigs[i].ledR = isDefaultPcActive ? 0 : ((i < NUM_CC_LEDS_FOR_SNAPSHOT) ? 0 : 0); 
      buttonConfigs[i].ledG = isDefaultPcActive ? 200 : ((i < NUM_CC_LEDS_FOR_SNAPSHOT) ? 0 : 0); 
      buttonConfigs[i].ledB = isDefaultPcActive ? 0 : ((i < NUM_CC_LEDS_FOR_SNAPSHOT) ? 200 : 0); 
      for(int k=0; k<NUM_CC_LEDS_FOR_SNAPSHOT; ++k) { buttonConfigs[i].ccLedSnapshot[k] = false; }
    }
    saveConfiguration(); 
  }
  strip.setBrightness(globalBrightness); 
  if (DEBUG_SERIAL) Serial.printf("[LED] Brilho na carga: %d\n", globalBrightness);
}

void saveConfiguration() {
  if (DEBUG_SERIAL) Serial.println("[CONFIG_SAVE] Salvando config no LittleFS...");
  File file = LittleFS.open(configFile, "w"); 
  if (!file) { 
    if (DEBUG_SERIAL) Serial.println("[CONFIG_SAVE] FALHA ao abrir arquivo para escrita.");
    return; 
  }
  StaticJsonDocument<4096> doc; 
  
  doc["globalBrightness"] = globalBrightness;
  JsonArray buttonsArray = doc.createNestedArray("buttons");

  for (int i = 0; i < NUM_BUTTONS; i++) { 
    JsonObject obj = buttonsArray.add<JsonObject>();
    obj["mode"] = buttonConfigs[i].mode; 
    obj["midiChannel"] = buttonConfigs[i].midiChannel;
    obj["targetNumber"] = buttonConfigs[i].targetNumber; 
    obj["ccValue"] = buttonConfigs[i].ccValue;
    obj["ledR"] = buttonConfigs[i].ledR; 
    obj["ledG"] = buttonConfigs[i].ledG; 
    obj["ledB"] = buttonConfigs[i].ledB;
    if (buttonConfigs[i].mode == 2) { 
        JsonArray snapshotArray = obj.createNestedArray("ccLedSnapshot");
        for (int k = 0; k < NUM_CC_LEDS_FOR_SNAPSHOT; ++k) {
            snapshotArray.add(buttonConfigs[i].ccLedSnapshot[k]);
        }
    }
  }
  size_t bytesWritten = serializeJsonPretty(doc, file); 
  file.close();
  if (bytesWritten > 0) {
    if (DEBUG_SERIAL) { Serial.print("[CONFIG_SAVE] Config salva ("); Serial.print(bytesWritten); Serial.println(" bytes).");}
  } else {
    if (DEBUG_SERIAL) Serial.println("[CONFIG_SAVE] FALHA ao escrever no arquivo (0 bytes).");
  }
}

// =======================
// LÓGICA DOS LEDS
// =======================
void updateAllLeds() { 
  for (int i = 0; i < NUM_BUTTONS; i++) {
    if (i < NUM_CC_LEDS_FOR_SNAPSHOT) { // LEDs de Botões CC (índices de LED 0-4)
        // Para LEDs CC, o estado é ccLedStates[i], e a cor vem de buttonConfigs[i]
        if (buttonConfigs[i].mode == 1 || buttonConfigs[i].mode == 0) { // Inclui modo desligado para que o toggle visual ainda funcione
             strip.setPixelColor(i, ccLedStates[i] ? strip.Color(buttonConfigs[i].ledR, buttonConfigs[i].ledG, buttonConfigs[i].ledB) : colorOff);
        } else { // Se o botão 0-4 está configurado como PC por engano, LED apagado
            strip.setPixelColor(i, colorOff);
        }
    } else { // LEDs de Botões PC (índices de LED 5-9)
        if (buttonConfigs[i].mode == 2) { // Se o botão está configurado como PC
            int pcButtonVisualIndex = i - NUM_CC_LEDS_FOR_SNAPSHOT; 
            strip.setPixelColor(i, (pcButtonVisualIndex == activePcLedIndex) ? strip.Color(buttonConfigs[i].ledR, buttonConfigs[i].ledG, buttonConfigs[i].ledB) : colorOff);
        } else { // Se o botão 5-9 não está em modo PC, ou modo desligado
            strip.setPixelColor(i, colorOff);
        }
    }
  }
  strip.show();
}

// =======================
// SETUP DO SERVIDOR WEB
// =======================
void setupWebServer() {
  if (DEBUG_SERIAL) Serial.println("[WEB_SETUP] Configurando rotas...");
  
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    if (DEBUG_SERIAL) Serial.println("[WEB] Request para /");
    if(LittleFS.exists("/index.html")){ request->send(LittleFS, "/index.html", "text/html", false); } 
    else { request->send(404, "text/plain", "/index.html nao encontrado."); }
  });

  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request){ 
    if (DEBUG_SERIAL) Serial.println("[WEB] Request para /style.css");
    if(LittleFS.exists("/style.css")){ request->send(LittleFS, "/style.css", "text/css"); } 
    else { request->send(404, "text/plain", "/style.css nao encontrado"); }
  }); 
  
  server.on("/config", HTTP_GET, [](AsyncWebServerRequest *request) { 
    if (DEBUG_SERIAL) Serial.println("[WEB] Request para /config (GET)");
    StaticJsonDocument<3072> docGet; 
    
    docGet["globalBrightness"] = globalBrightness;
    JsonArray buttonsArray = docGet.createNestedArray("buttons");

    for (int i = 0; i < NUM_BUTTONS; i++) { 
        JsonObject obj = buttonsArray.add<JsonObject>();
        obj["mode"] = buttonConfigs[i].mode; 
        obj["midiChannel"] = buttonConfigs[i].midiChannel;
        obj["targetNumber"] = buttonConfigs[i].targetNumber; 
        obj["ccValue"] = buttonConfigs[i].ccValue;
        obj["ledR"] = buttonConfigs[i].ledR; 
        obj["ledG"] = buttonConfigs[i].ledG; 
        obj["ledB"] = buttonConfigs[i].ledB;
        // Envia ccLedSnapshot para TODOS os botões; o JS só mostrará para modo PC
        JsonArray snapshotArray = obj.createNestedArray("ccLedSnapshot");
        for(int k=0; k<NUM_CC_LEDS_FOR_SNAPSHOT; ++k) {
            snapshotArray.add(buttonConfigs[i].ccLedSnapshot[k]);
        }
    }
    String output; 
    serializeJsonPretty(docGet, output); 
    if (DEBUG_SERIAL && output.length() < 800) { 
        Serial.println("[WEB] Enviando JSON para /config:"); 
        //Serial.println(output); 
    } else if (DEBUG_SERIAL) {
        Serial.println("[WEB] Enviando JSON para /config (omitido do log por tamanho).");
    }
    request->send(200, "application/json", output);
  });

  server.on("/save", HTTP_POST, 
    [](AsyncWebServerRequest *request){}, 
    nullptr, 
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) { 
      if (index == 0) { 
        if (DEBUG_SERIAL) Serial.printf("[WEB] /save POST: Recebendo corpo... total: %u bytes\n", total);
        request->_tempObject = new std::string(); 
      }
      ((std::string*)request->_tempObject)->append((char*)data, len);
      if (index + len == total) { 
        std::string* bodyStrPtr = (std::string*)request->_tempObject;
        String bodyStr = String(bodyStrPtr->c_str()); 
        delete bodyStrPtr; request->_tempObject = nullptr; 
        if (DEBUG_SERIAL) { Serial.printf("[WEB] Corpo JSON /save (%u bytes) recebido.\n", bodyStr.length());}
        
        StaticJsonDocument<4096> docSave; 
        DeserializationError error = deserializeJson(docSave, bodyStr);
        
        if (error) {
            request->send(400, "text/plain", "JSON Invalido.");
            if (DEBUG_SERIAL) { Serial.print("[WEB] Erro JSON /save: "); Serial.println(error.c_str()); }
            return;
        }

        globalBrightness = docSave["globalBrightness"] | 50; 
        strip.setBrightness(globalBrightness); 
        if (DEBUG_SERIAL) Serial.printf("[WEB_SAVE] Brilho global: %d\n", globalBrightness);

        JsonArrayConst array = docSave["buttons"].as<JsonArrayConst>(); 
        if (array.isNull() || array.size() != NUM_BUTTONS) {
            request->send(400, "text/plain", "Erro no array 'buttons'.");
            if (DEBUG_SERIAL) Serial.printf("[WEB_SAVE] Erro: array 'buttons' ausente/tamanho (%d) != %d\n", array.isNull() ? 0 : array.size(), NUM_BUTTONS);
            return;
        }
        int i = 0;
        for (JsonObjectConst obj : array) { 
            if (i < NUM_BUTTONS) { 
                buttonConfigs[i].mode = obj["mode"].as<int>();
                buttonConfigs[i].midiChannel = obj["midiChannel"].as<uint8_t>();
                buttonConfigs[i].targetNumber = obj["targetNumber"] | 0;
                buttonConfigs[i].ccValue = obj["ccValue"] | 0;      
                buttonConfigs[i].ledR = obj["ledR"] | 0;            
                buttonConfigs[i].ledG = obj["ledG"] | 0;
                buttonConfigs[i].ledB = obj["ledB"] | 0;
                
                // Zera o snapshot por padrão
                for (int k = 0; k < NUM_CC_LEDS_FOR_SNAPSHOT; ++k) buttonConfigs[i].ccLedSnapshot[k] = false;
                if (buttonConfigs[i].mode == 2) { // Se modo PC, tenta ler o snapshot
                    JsonArrayConst snapshot = obj["ccLedSnapshot"].as<JsonArrayConst>(); 
                    if (!snapshot.isNull() && snapshot.size() == NUM_CC_LEDS_FOR_SNAPSHOT) {
                        for (int k = 0; k < NUM_CC_LEDS_FOR_SNAPSHOT; ++k) {
                            buttonConfigs[i].ccLedSnapshot[k] = snapshot[k] | false;
                        }
                    } else {
                        if (DEBUG_SERIAL) Serial.printf("[WEB_SAVE] Snapshot Btn PC %d ausente/inválido. Zerado.\n", i);
                    }
                }
                if (DEBUG_SERIAL) Serial.printf("[CONFIG_UPDATE] Btn %d: M%d Ch%d Tgt%d Val%d RGB(%d,%d,%d) Snap:%d%d%d%d%d\n",
                  i, buttonConfigs[i].mode, buttonConfigs[i].midiChannel, buttonConfigs[i].targetNumber,
                  buttonConfigs[i].ccValue, buttonConfigs[i].ledR, buttonConfigs[i].ledG, buttonConfigs[i].ledB,
                  buttonConfigs[i].ccLedSnapshot[0],buttonConfigs[i].ccLedSnapshot[1],buttonConfigs[i].ccLedSnapshot[2],buttonConfigs[i].ccLedSnapshot[3],buttonConfigs[i].ccLedSnapshot[4]);
                i++;
            }
        }
        saveConfiguration(); 
        for(int j=0; j<NUM_CC_LEDS_FOR_SNAPSHOT; j++) ccLedStates[j] = false;
        activePcLedIndex = 0; 
        updateAllLeds();      
        request->send(200, "text/plain", "Configuracoes salvas!");
        if (DEBUG_SERIAL) Serial.println("[WEB] Configs salvas via /save.");
      }
    }
  );

  server.onNotFound([](AsyncWebServerRequest *request){
    if (DEBUG_SERIAL) { Serial.print("[WEB_NOT_FOUND] Rota: "); Serial.println(request->url()); }
    if (request->url() == "/favicon.ico") { request->send(204); } 
    else if (request->url().endsWith("generate_204") || request->url().endsWith("connecttest.txt") || request->url().endsWith("ncsi.txt")) { request->send(204); } 
    else { 
      if(LittleFS.exists("/index.html")){ request->send(LittleFS, "/index.html", "text/html", false); } 
      else { request->send(404, "text/plain", "index.html nao encontrado."); }
    }
  });
  if (DEBUG_SERIAL) Serial.println("[WEB_SETUP] Rotas configuradas.");
}

// =======================
// SETUP PRINCIPAL
// =======================
void setup() {
  delay(500); 
  Serial.begin(115200);
  delay(1000); 
  
  if (DEBUG_SERIAL) Serial.println("\n\n=== Pedal MIDI Config com Bancos PC (v1.2 - Snapshot Corrigido) ===");
  if (DEBUG_SERIAL) Serial.println("[SETUP] Início.");

  if (DEBUG_SERIAL) Serial.println("[SETUP] PASSO 1: LittleFS...");
  initializeFileSystem(); 
  if (DEBUG_SERIAL) Serial.println("[SETUP] PASSO 1: LittleFS OK.\n");

  if (DEBUG_SERIAL) Serial.println("[SETUP] PASSO 2: Configuração...");
  loadConfiguration(); 
  if (DEBUG_SERIAL) Serial.println("[SETUP] PASSO 2: Configuração OK.\n");

  if (DEBUG_SERIAL) Serial.println("[SETUP] PASSO 3: LEDs NeoPixel...");
  strip.begin();
  updateAllLeds();      
  if (DEBUG_SERIAL) Serial.println("[SETUP] PASSO 3: LEDs NeoPixel OK.\n");

  if (DEBUG_SERIAL) Serial.println("[SETUP] PASSO 4: Pinos dos Botões...");
  for (int i = 0; i < NUM_BUTTONS; i++) { 
      pinMode(buttonPins[i], INPUT_PULLUP);
      lastRawButtonState[i] = (digitalRead(buttonPins[i]) == LOW);
      buttonActiveState[i] = lastRawButtonState[i];
      lastDebounceTime[i] = 0;
  }
  if (DEBUG_SERIAL) Serial.println("[SETUP] PASSO 4: Pinos dos Botões OK.\n");
  
  if (DEBUG_SERIAL) Serial.println("[SETUP] PASSO 6: Wi-Fi AP...");
  WiFi.softAP(apSsid, apPassword);
  IPAddress myIP = WiFi.softAPIP(); 
  if (DEBUG_SERIAL) { Serial.print("  [WIFI] AP IP: http://"); Serial.println(myIP); }
  if (DEBUG_SERIAL) Serial.println("[SETUP] PASSO 6: Wi-Fi AP OK.\n");

  if (DEBUG_SERIAL) Serial.println("[DNS_CAPTIVE] Iniciando DNS Server...");
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError); 
  if (dnsServer.start(DNS_PORT, "*", myIP)) {
    if (DEBUG_SERIAL) Serial.println("[DNS_CAPTIVE] DNS Server iniciado.");
  } else {
    if (DEBUG_SERIAL) Serial.println("[DNS_CAPTIVE] FALHA DNS Server.");
  }
  
  if (DEBUG_SERIAL) Serial.println("[SETUP] PASSO 7: Web Server...");
  setupWebServer(); 
  server.begin();   
  if (DEBUG_SERIAL) Serial.println("[SETUP] PASSO 7: Web Server OK e INICIADO.\n"); 
  
  if (DEBUG_SERIAL) Serial.println("[SETUP] PASSO 5: Cliente BLE MIDI...");
  BLEMidiClient.begin("ESP32_MIDI_Pedal_Bancos"); 
  if (DEBUG_SERIAL) Serial.println("[SETUP] PASSO 5: Cliente BLE MIDI OK.\n");

  if (DEBUG_SERIAL) { Serial.print("[BLE_TARGET] Alvo BLE: '"); Serial.print(TARGET_DEVICE_NAME.c_str()); Serial.println("'"); }
  if (DEBUG_SERIAL) Serial.println("[SYSTEM] Setup COMPLETO.");
  if (DEBUG_SERIAL) Serial.println("======================================================");
}

// =======================
// LÓGICA DE CONEXÃO BLE (CLIENTE) 
// =======================
void tentarConectarAoServidor() {
  if (tentandoConectarAGORA || BLEMidiClient.isConnected()) { return; }
  if (DEBUG_SERIAL) Serial.println("[BLE_CONNECT] Escaneando...");
  tentandoConectarAGORA = true;
  int numDispositivosEncontrados = BLEMidiClient.scan(); 
  if (numDispositivosEncontrados == 0) {
    if (DEBUG_SERIAL) Serial.println("[BLE_CONNECT] Nenhum dispositivo MIDI encontrado.");
    tentandoConectarAGORA = false; return;
  }
  if (DEBUG_SERIAL) { Serial.print("[BLE_CONNECT] "); Serial.print(numDispositivosEncontrados); Serial.println(" dispositivo(s) MIDI encontrado(s). Tentando conectar ao primeiro...");}
  
  if (BLEMidiClient.connect(0)) { 
    if (DEBUG_SERIAL) Serial.println("[BLE_CONNECT] Conexão BLE estabelecida.");
  } else {
    if (DEBUG_SERIAL) Serial.println("[BLE_CONNECT] Falha ao conectar.");
  }
  tentandoConectarAGORA = false;
}

// =======================
// LOOP PRINCIPAL
// =======================
void loop() {
  dnsServer.processNextRequest(); 

  if (!BLEMidiClient.isConnected()) {
    if (!tentandoConectarAGORA && (millis() - ultimaTentativaScan > intervaloScan)) {
        tentarConectarAoServidor();
        ultimaTentativaScan = millis();
    }
  }

  for (int i = 0; i < NUM_BUTTONS; i++) {
    bool rawButtonState = (digitalRead(buttonPins[i]) == LOW);
    if (rawButtonState != lastRawButtonState[i]) { lastDebounceTime[i] = millis(); }
    if ((millis() - lastDebounceTime[i]) > debounceDelay) {
      if (rawButtonState != buttonActiveState[i]) {
        buttonActiveState[i] = rawButtonState;
        if (buttonActiveState[i]) { 
          ButtonConfig currentConfig = buttonConfigs[i]; 
          if (currentConfig.mode == 0) { 
            if (DEBUG_SERIAL) Serial.printf("[BTN %d] PRESS (Pino %d), MODO DESLIGADO.\n", i+1, buttonPins[i]);
            if (i < NUM_CC_LEDS_FOR_SNAPSHOT) { 
                ccLedStates[i] = !ccLedStates[i]; 
            } else { 
                activePcLedIndex = i - NUM_CC_LEDS_FOR_SNAPSHOT; 
                for (int k = 0; k < NUM_CC_LEDS_FOR_SNAPSHOT; ++k) {
                    ccLedStates[k] = currentConfig.ccLedSnapshot[k];
                }
            }
            updateAllLeds();
            continue; 
          }
          
          if (DEBUG_SERIAL) Serial.printf("[BTN %d] PRESS (Pino %d)\n", i+1, buttonPins[i]);
          uint8_t apiChannel = currentConfig.midiChannel - 1; 
          if (apiChannel > 15) { 
             if (DEBUG_SERIAL) Serial.printf("  [WARN] Canal MIDI %d invalido, usando canal 0 (MIDI Ch 1).\n", currentConfig.midiChannel);
             apiChannel = 0; 
          }

          if (BLEMidiClient.isConnected()) {
            if (currentConfig.mode == 1) { // MODO CC
              if (i < NUM_CC_LEDS_FOR_SNAPSHOT) { ccLedStates[i] = !ccLedStates[i]; } 
              if (DEBUG_SERIAL) Serial.printf("  -> MIDI CC: Ch Config=%d (API=%d), CC#=%d, Val=%d\n", currentConfig.midiChannel, apiChannel, currentConfig.targetNumber, currentConfig.ccValue);
              BLEMidiClient.controlChange(apiChannel, currentConfig.targetNumber, currentConfig.ccValue);
            } else if (currentConfig.mode == 2) { // MODO PC
              if (i >= NUM_CC_LEDS_FOR_SNAPSHOT) { 
                  activePcLedIndex = i - NUM_CC_LEDS_FOR_SNAPSHOT; 
                  for (int k = 0; k < NUM_CC_LEDS_FOR_SNAPSHOT; ++k) {
                      ccLedStates[k] = currentConfig.ccLedSnapshot[k]; 
                  }
                  if (DEBUG_SERIAL) Serial.printf("  Aplicando snapshot de LEDs CC para PC %d (Btn %d)\n", currentConfig.targetNumber, i+1);
              } 
              if (DEBUG_SERIAL) Serial.printf("  -> MIDI PC: Ch Config=%d (API=%d), PC#=%d\n", currentConfig.midiChannel, apiChannel, currentConfig.targetNumber);
              BLEMidiClient.programChange(apiChannel, currentConfig.targetNumber);
            }
          } else { 
             if (DEBUG_SERIAL) Serial.println("  -> BLE não conectado. MIDI não enviado.");
             if (currentConfig.mode == 1 && i < NUM_CC_LEDS_FOR_SNAPSHOT) { ccLedStates[i] = !ccLedStates[i]; }
             else if (currentConfig.mode == 2 && i >= NUM_CC_LEDS_FOR_SNAPSHOT) {
                 activePcLedIndex = i - NUM_CC_LEDS_FOR_SNAPSHOT;
                 for (int k = 0; k < NUM_CC_LEDS_FOR_SNAPSHOT; ++k) {
                    ccLedStates[k] = currentConfig.ccLedSnapshot[k];
                 }
             }
          }
          updateAllLeds(); 
        }
      }
    }
    lastRawButtonState[i] = rawButtonState;
  }
  delay(5); 
}