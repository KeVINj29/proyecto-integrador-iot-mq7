/**
 * =========================================================================================
 * PROYECTO INTEGRADOR: SISTEMA IOT INDUSTRIAL DE CONTROL EN LAZO CERRADO Y TELEMETRÍA (CO)
 * MATERIA: Internet de las Cosas
 * DOCENTE: Andrés Salomón Enríquez Ochoa
 * HARDWARE: ESP32 DevKit V1 (38 pines / 30 pines) + MQ-7 + Módulo Relevador 5V (Optoacoplado)
 * =========================================================================================
 * 
 * LÓGICA DE CONTROL EN LAZO CERRADO (CLOSED-LOOP CONTROL):
 * - Control autónomo e histéresis:
 *     - Setpoint Alto (Alarma/Extracción ON): CO >= 35.0 %
 *     - Setpoint Bajo (Extracción OFF):       CO <= 25.0 %
 *     - Alerta Crítica (Notificación nube):   CO >  60.0 %
 * - Acondicionamiento de señal: Divisor de tensión 10k / 20k (5V -> 3.3V) a GPIO 34 (ADC1_CH6).
 * - Filtrado digital: Filtro de Media Móvil (Moving Average Filter) de 10 muestras continuas.
 * - Concurrencia no bloqueante: Máquina de estados gobernada por millis(), garantizando que la
 *   detección y actuación sobre el relevador continúe ejecutándose aun ante caídas de Wi-Fi o MQTT.
 * =========================================================================================
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// ==========================================
// CONFIGURACIÓN DE PINES (GPIO)
// ==========================================
#define PIN_SENSOR_MQ7     34   // ADC1_CH6: Solo entrada analógica, inmune a interferencias de Wi-Fi ADC2
#define PIN_RELE_EXTRACTOR 26   // Salida Digital: Control de bobina del relevador (Activo en LOW)
#define PIN_LED_ESTADO      2   // LED Onboard ESP32: Indicador de estado/latido (Heartbeat)

// ==========================================
// PARÁMETROS DE CONTROL Y UMBRALES
// ==========================================
const float SETPOINT_ACTIVACION   = 35.0; // Umbral de activación del extractor (Histeresis Superior) [%]
const float SETPOINT_DESACTIVACION = 25.0; // Umbral de apagado del extractor (Histeresis Inferior) [%]
const float UMBRAL_ALERTA_CRITICA  = 60.0; // Umbral de peligro inminente para la salud humana [%]

// El relevador ópticamente aislado (SRD-05VDC-SL-C) conmuta en lógica inversa (Active-LOW)
#define RELE_ACTIVADO    LOW
#define RELE_DESACTIVADO HIGH

// ==========================================
// CREDENCIALES DE RED Y BROKER MQTT
// ==========================================
const char* WIFI_SSID       = "IoT_Industrial_Network";
const char* WIFI_PASS       = "SeguridadIoT2026*";

const char* MQTT_BROKER     = "192.168.1.100"; // IP estática asignada a la Raspberry Pi 4
const int   MQTT_PORT       = 1883;
const char* MQTT_USER       = "iot_operator";   // Usuario configurado en Mosquitto ACL
const char* MQTT_PASS       = "M0squitt0_Pass#2026";
const char* MQTT_CLIENT_ID  = "ESP32_Planta_CO_01";
const char* MQTT_TOPIC_PUB  = "planta/seguridad/co";
const char* MQTT_TOPIC_CMD  = "planta/seguridad/co/cmd";

// ==========================================
// OBJETOS Y TEMPORIZADORES (millis)
// ==========================================
WiFiClient espClient;
PubSubClient mqttClient(espClient);

unsigned long ultimoMuestreoADC      = 0;
const unsigned long INTERVALO_ADC    = 50;   // Muestreo analógico a 20 Hz (cada 50 ms)

unsigned long ultimaPublicacionMQTT  = 0;
const unsigned long INTERVALO_MQTT   = 2000; // Publicación periódica cada 2000 ms (2 segundos)

unsigned long ultimoIntentoWiFi      = 0;
const unsigned long INTERVALO_RECON_WIFI = 5000; // Intento de reconexión Wi-Fi cada 5s si se pierde

unsigned long ultimoIntentoMQTT      = 0;
const unsigned long INTERVALO_RECON_MQTT = 3000; // Intento de reconexión MQTT cada 3s si Wi-Fi está OK

// ==========================================
// BUFFER DEL FILTRO DE MEDIA MÓVIL
// ==========================================
const int NUM_MUESTRAS = 10;
int muestrasADC[NUM_MUESTRAS];
int indiceMuestra = 0;
long sumaMuestras = 0;
bool bufferLleno = false;

// Variables de estado del proceso
int adcRawFiltrado = 0;
float concentracionCO = 0.0;
bool estadoExtractor = false; // false = Apagado, true = Encendido
bool alertaCriticaActiva = false;

// ==========================================
// DECLARACIÓN DE FUNCIONES AUXILIARES
// ==========================================
void actualizarFiltroADC();
void ejecutarLazoCerrado();
void verificarConectividadRed();
void publicarTelemetriaJSON();
void callbackMqtt(char* topic, byte* payload, unsigned int length);

// ==========================================
// SETUP
// ==========================================
void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println(F("\n========================================================"));
  Serial.println(F(" INICIALIZANDO CONTROLADOR INDUSTRIAL DE MONÓXIDO (CO)  "));
  Serial.println(F(" PROYECTO INTEGRADOR IoT - DOCENTE: ANDRÉS SALOMÓN E. O."));
  Serial.println(F("========================================================"));

  // Configuración de pines de hardware
  pinMode(PIN_RELE_EXTRACTOR, OUTPUT);
  digitalWrite(PIN_RELE_EXTRACTOR, RELE_DESACTIVADO); // Asegurar extractor apagado al arranque
  estadoExtractor = false;

  pinMode(PIN_LED_ESTADO, OUTPUT);
  digitalWrite(PIN_LED_ESTADO, LOW);

  // Configuración del Conversor Analógico-Digital (ADC)
  // Atenuación de 11 dB: Rango dinámico completo de 0V a ~3.1V / 3.3V
  analogSetAttenuation(ADC_11db);
  analogReadResolution(12); // Resolución estándar de 12 bits (0 a 4095)

  // Inicializar buffer de promedio móvil
  for (int i = 0; i < NUM_MUESTRAS; i++) {
    muestrasADC[i] = 0;
  }

  // Pre-llenar buffer con lecturas iniciales para evitar picos espurios
  for (int i = 0; i < NUM_MUESTRAS; i++) {
    int lectura = analogRead(PIN_SENSOR_MQ7);
    muestrasADC[i] = lectura;
    sumaMuestras += lectura;
    delay(10);
  }
  indiceMuestra = 0;
  bufferLleno = true;
  adcRawFiltrado = sumaMuestras / NUM_MUESTRAS;

  Serial.println(F("[HARDWARE] Periféricos GPIO, ADC y Relevador inicializados con éxito."));

  // Configuración de red Wi-Fi
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("[WIFI] Conectando a red: %s ...\n", WIFI_SSID);

  // Configuración de cliente MQTT
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(callbackMqtt);
  mqttClient.setBufferSize(512); // Buffer holgado para JSON serializado

  Serial.println(F("[SISTEMA] Modo autónomo iniciado. Entrando al bucle principal de control."));
}

// ==========================================
// LOOP PRINCIPAL (NON-BLOCKING)
// ==========================================
void loop() {
  unsigned long tiempoActual = millis();

  // 1. TAREA DE ALTA PRIORIDAD: Muestreo y Acondicionamiento de Señal (Cada 50ms)
  if (tiempoActual - ultimoMuestreoADC >= INTERVALO_ADC) {
    ultimoMuestreoADC = tiempoActual;
    actualizarFiltroADC();
    ejecutarLazoCerrado();
  }

  // 2. TAREA DE RED: Mantenimiento resiliente de Wi-Fi y MQTT (No bloqueante)
  verificarConectividadRed();

  if (mqttClient.connected()) {
    mqttClient.loop();
  }

  // 3. TAREA DE TELEMETRÍA: Serialización y Publicación JSON (Cada 2000ms)
  if (tiempoActual - ultimaPublicacionMQTT >= INTERVALO_MQTT) {
    ultimaPublicacionMQTT = tiempoActual;
    publicarTelemetriaJSON();
  }
}

// ==========================================
// FILTRADO DIGITAL: MEDIA MÓVIL
// ==========================================
void actualizarFiltroADC() {
  int lecturaActual = analogRead(PIN_SENSOR_MQ7);

  // Algoritmo circular O(1) de promedio móvil
  sumaMuestras -= muestrasADC[indiceMuestra];
  muestrasADC[indiceMuestra] = lecturaActual;
  sumaMuestras += lecturaActual;

  indiceMuestra = (indiceMuestra + 1) % NUM_MUESTRAS;
  adcRawFiltrado = (int)(sumaMuestras / NUM_MUESTRAS);

  // Escalado a porcentaje normalizado de saturación de CO (0.0% a 100.0%)
  // 0 -> 0%, 4095 (3.3V máximo permitido por el divisor) -> 100.0%
  concentracionCO = ((float)adcRawFiltrado / 4095.0) * 100.0;
  if (concentracionCO > 100.0) concentracionCO = 100.0;
  if (concentracionCO < 0.0) concentracionCO = 0.0;

  // Actualizar bandera de alarma crítica
  alertaCriticaActiva = (concentracionCO > UMBRAL_ALERTA_CRITICA);
}

// ==========================================
// LÓGICA DE CONTROL EN LAZO CERRADO (AUTÓNOMA)
// ==========================================
void ejecutarLazoCerrado() {
  // Control tipo Bang-Bang con Banda Muerta (Histéresis) para proteger los contactos mecánicos del relevador
  if (!estadoExtractor && concentracionCO >= SETPOINT_ACTIVACION) {
    // Encendido por superación de setpoint alto
    digitalWrite(PIN_RELE_EXTRACTOR, RELE_ACTIVADO);
    digitalWrite(PIN_LED_ESTADO, HIGH);
    estadoExtractor = true;
    Serial.printf("[LAZO CERRADO] >>> DISPARO ON: CO (%.2f%%) >= Setpoint (%.1f%%). Relevador ACTIVO.\n", 
                  concentracionCO, SETPOINT_ACTIVACION);
  } 
  else if (estadoExtractor && concentracionCO <= SETPOINT_DESACTIVACION) {
    // Apagado por restitución de aire seguro bajo setpoint inferior
    digitalWrite(PIN_RELE_EXTRACTOR, RELE_DESACTIVADO);
    digitalWrite(PIN_LED_ESTADO, LOW);
    estadoExtractor = false;
    Serial.printf("[LAZO CERRADO] <<< CORTE OFF: CO (%.2f%%) <= Setpoint (%.1f%%). Relevador DESACTIVADO.\n", 
                  concentracionCO, SETPOINT_DESACTIVACION);
  }
}

// ==========================================
// GESTIÓN RESILIENTE DE COMUNICACIONES
// ==========================================
void verificarConectividadRed() {
  unsigned long tiempo = millis();

  // Gestión no bloqueante de conexión Wi-Fi
  if (WiFi.status() != WL_CONNECTED) {
    if (tiempo - ultimoIntentoWiFi >= INTERVALO_RECON_WIFI) {
      ultimoIntentoWiFi = tiempo;
      Serial.println(F("[WIFI] Enlace caído. Reintentando asociación a AP sin bloquear control..."));
      WiFi.reconnect();
    }
    return; // Si no hay Wi-Fi, no intentar MQTT
  }

  // Gestión no bloqueante de broker Mosquitto MQTT
  if (!mqttClient.connected()) {
    if (tiempo - ultimoIntentoMQTT >= INTERVALO_RECON_MQTT) {
      ultimoIntentoMQTT = tiempo;
      Serial.println(F("[MQTT] Broker desconectado. Intentando conexión con credenciales ACL..."));

      // Conexión no bloqueante: se evalúa una sola vez por intervalo
      if (mqttClient.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS)) {
        Serial.println(F("[MQTT] ¡Conexión exitosa al broker Mosquitto de la Raspberry Pi!"));
        mqttClient.subscribe(MQTT_TOPIC_CMD);
      } else {
        Serial.printf("[MQTT] Falló conexión. Código rc=%d. El lazo cerrado continúa operando localmente.\n", 
                      mqttClient.state());
      }
    }
  }
}

// ==========================================
// SERIALIZACIÓN Y PUBLICACIÓN DE TELEMETRÍA JSON
// ==========================================
void publicarTelemetriaJSON() {
  // Construcción de documento JSON optimizado
  StaticJsonDocument<384> doc;

  doc["nodo"]           = MQTT_CLIENT_ID;
  doc["sensor"]         = "MQ-7 (CO Industrial)";
  doc["adc_raw"]        = adcRawFiltrado;
  doc["co_pct"]         = serialized(String(concentracionCO, 2)); // 2 decimales de precisión
  doc["setpoint_on"]    = SETPOINT_ACTIVACION;
  doc["setpoint_off"]   = SETPOINT_DESACTIVACION;
  doc["actuador"]       = estadoExtractor ? "ENCENDIDO" : "APAGADO";
  doc["alerta_critica"] = alertaCriticaActiva;
  doc["uptime_ms"]      = millis();
  doc["wifi_rssi"]      = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;

  char jsonBuffer[384];
  size_t n = serializeJson(doc, jsonBuffer);

  // Monitor Serial de diagnóstico
  if (mqttClient.connected()) {
    bool pubOk = mqttClient.publish(MQTT_TOPIC_PUB, jsonBuffer);
    if (pubOk) {
      Serial.printf("[MQTT PUB] %s\n", jsonBuffer);
    } else {
      Serial.println(F("[MQTT PUB ERROR] Error al despachar paquete al socket."));
    }
  } else {
    // Si no hay red, imprimir en monitor serial para auditoría local
    Serial.printf("[OFFLINE - CONTROL LOCAL ACTIVO] %s\n", jsonBuffer);
  }
}

// ==========================================
// CALLBACK MQTT (COMANDOS REMOTOS)
// ==========================================
void callbackMqtt(char* topic, byte* payload, unsigned int length) {
  char mensaje[length + 1];
  memcpy(mensaje, payload, length);
  mensaje[length] = '\0';

  Serial.printf("[MQTT RECV] Tópico: %s | Payload: %s\n", topic, mensaje);

  // Comando remoto de forzado de emergencia o diagnóstico
  if (strcmp(mensaje, "FORCE_ON") == 0) {
    digitalWrite(PIN_RELE_EXTRACTOR, RELE_ACTIVADO);
    estadoExtractor = true;
    Serial.println(F("[REMOTO] Relevador forzado a ON vía comando MQTT."));
  } else if (strcmp(mensaje, "FORCE_OFF") == 0) {
    digitalWrite(PIN_RELE_EXTRACTOR, RELE_DESACTIVADO);
    estadoExtractor = false;
    Serial.println(F("[REMOTO] Relevador forzado a OFF vía comando MQTT."));
  }
}
