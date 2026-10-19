#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// --- 1. CONFIGURACIÓN DE RED Y BROKER MQTT ---
const char* WIFI_SSID = "S26 ultra Sebas";
const char* WIFI_PASS = "18092005.";

const char* MQTT_SERVER    = "10.230.217.43"; 
const int   MQTT_PORT      = 1883;
const char* MQTT_CLIENT_ID = "ESP32_Planta_MQ7";
const char* MQTT_USER      = "esp32_nodo";
const char* MQTT_PASS      = "iot_pass_2026";

const char* TOPIC_TELEMETRIA = "planta/seguridad/mq7/telemetria";
const char* TOPIC_ESTATUS    = "planta/seguridad/mq7/estatus";

WiFiClient espClient;
PubSubClient mqttClient(espClient);

unsigned long tUltimaTelemetria = 0;
unsigned long tUltimoReintentoRed = 0;

// Temporizador exclusivo para el apagado (Off-Delay)
unsigned long tInicioApagado = 0;
const unsigned long TIEMPO_APAGADO_ESTABLE_MS = 3000; // 3 segundos continuos de aire limpio

// --- 2. PINES Y UMBRALES DE DISPARO ---
const int PIN_MQ7 = 32;     // Pin AO mediante divisor de tensión
const int PIN_RELAY = 26;   // Control al módulo de relés

const int RELAY_ON  = LOW;  
const int RELAY_OFF = HIGH; 

const float SETPOINT_ALTO  = 30.0; // Enciende DE INMEDIATO si co_pct >= 30%
const float SETPOINT_BAJO  = 20.0; // Apaga si co_pct <= 20% durante 3s continuos
const float UMBRAL_CRITICO = 50.0; 
const float FACTOR_DIVISOR = 1.0;

bool extractorEncendido = false;
bool alertaCritica = false;

int obtenerAdcSuavizado() {
  long suma = 0;
  for (int i = 0; i < 32; i++) {
    suma += analogRead(PIN_MQ7);
    delayMicroseconds(200);
  }
  return (int)(suma / 32);
}

void gestionarRedYBroker() {
  if (mqttClient.connected()) {
    mqttClient.loop();
    yield();
    return;
  }

  unsigned long ahora = millis();
  if (ahora - tUltimoReintentoRed < 5000) return;
  tUltimoReintentoRed = ahora;

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[ROL 2] Conectando a Hotspot...");
    WiFi.reconnect();
    yield();
    return;
  }

  Serial.print("[ROL 2] Conectando al Broker Mosquitto... ");
  const char* lwtPayload = "{\"nodo\":\"ESP32_Extractor_CO\",\"conexion\":\"OFFLINE_CORTE_ENERGIA\"}";

  if (mqttClient.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS, TOPIC_ESTATUS, 1, true, lwtPayload)) {
    Serial.println("¡CONECTADO!");
    mqttClient.publish(TOPIC_ESTATUS, "{\"nodo\":\"ESP32_Extractor_CO\",\"conexion\":\"ONLINE\"}", true);
  } else {
    Serial.print("Fallo, rc=");
    Serial.println(mqttClient.state());
  }
  yield();
}

void publicarTelemetriaJSON(int adcRaw, float concentracionCO) {
  unsigned long ahora = millis();
  if (ahora - tUltimaTelemetria < 1000) return; // Enviar telemetría cada 1s para mejor respuesta
  tUltimaTelemetria = ahora;

  JsonDocument doc;
  doc["nodo"] = "ESP32_Extractor_CO";
  doc["sensor"] = "MQ-7";
  doc["adc_raw"] = adcRaw;
  doc["co_pct"] = round(concentracionCO * 10.0) / 10.0;
  doc["setpoint"] = SETPOINT_ALTO;
  doc["umbral_critico"] = UMBRAL_CRITICO;
  doc["actuador"] = extractorEncendido ? "ON" : "OFF";
  doc["alerta_critica"] = alertaCritica;
  doc["rssi_dbm"] = WiFi.RSSI();
  doc["uptime_seg"] = ahora / 1000;

  char bufferJSON[256];
  serializeJson(doc, bufferJSON);

  if (mqttClient.connected()) {
    mqttClient.publish(TOPIC_TELEMETRIA, bufferJSON, false);
    Serial.print("[TX MQTT] ");
    Serial.println(bufferJSON);
  } else {
    Serial.print("[OFFLINE] ");
    Serial.println(bufferJSON);
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_MQ7, INPUT);

  pinMode(PIN_RELAY, OUTPUT);
  digitalWrite(PIN_RELAY, RELAY_OFF);

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setKeepAlive(10);
  mqttClient.setSocketTimeout(3);
}

void loop() {
  int adcPromedio = obtenerAdcSuavizado();
  float nivelCO = ((adcPromedio / 4095.0) * 100.0) * FACTOR_DIVISOR;
  if (nivelCO > 100.0) nivelCO = 100.0;

  unsigned long ahora = millis();

  // --- LÓGICA DE CONTROL INDUSTRIAL ---
  if (!extractorEncendido) {
    // Encendido instantáneo ante cualquier ráfaga de gas
    if (nivelCO >= SETPOINT_ALTO) {
      extractorEncendido = true;
      digitalWrite(PIN_RELAY, RELAY_ON);
      tInicioApagado = 0;
      Serial.printf("\n>>> [DISPARO] Gas detectado: %.1f%% -> EXTRACTOR ON <<<\n\n", nivelCO);
    }
  } else {
    // Apagado con retardo de seguridad (Off-Delay)
    if (nivelCO <= SETPOINT_BAJO) {
      if (tInicioApagado == 0) {
        tInicioApagado = ahora; // Empieza a cronometrar que el aire esté limpio
      } else if (ahora - tInicioApagado >= TIEMPO_APAGADO_ESTABLE_MS) {
        extractorEncendido = false;
        digitalWrite(PIN_RELAY, RELAY_OFF);
        tInicioApagado = 0;
        Serial.printf("\n>>> [NORMAL] Aire limpio: %.1f%% -> EXTRACTOR OFF <<<\n\n", nivelCO);
      }
    } else {
      tInicioApagado = 0; // Si el gas sube mientras intentaba apagar, se mantiene prendido
    }
  }

  alertaCritica = (nivelCO >= UMBRAL_CRITICO);

  gestionarRedYBroker();
  publicarTelemetriaJSON(adcPromedio, nivelCO);
  delay(100);
}
