// =========================================================================
// CÓDIGO EXCLUSIVO ROL 1: PRUEBA FÍSICA Y LAZO CERRADO LOCAL (SIN WI-FI)
// =========================================================================

// 1. PINES CONECTADOS EN LA PROTOBOARD
const int PIN_SENSOR_MQ7 = 34; // Cable que viene de la Fila 15 (entre resistencias)
const int PIN_RELEVADOR  = 26; // Cable que va al pin IN1 del módulo relevador

// 2. LÓGICA DE TU RELEVADOR (SRD-05VDC-SL-C de 2 canales)
// Este módulo enciende el foco rojo y hace "clic" cuando recibe 0V (LOW)
const int ENCENDER_VENTILADOR = LOW;
const int APAGAR_VENTILADOR   = HIGH;

// 3. TUS LÍMITES DE AUTOMATIZACIÓN (Puedes ajustarlos tras ver el Monitor Serial)
const float LIMITE_PARA_ENCENDER = 35.0; // Si el gas sube de 35%, prende el ventilador
const float LIMITE_PARA_APAGAR   = 25.0; // Cuando baje de 25%, apaga el ventilador

bool ventiladorPrendido = false;

void setup() {
  // Inicia la pantalla de texto en la computadora a 115200 de velocidad
  Serial.begin(115200);

  // Configura los pines
  pinMode(PIN_SENSOR_MQ7, INPUT);
  pinMode(PIN_RELEVADOR, OUTPUT);

  // Regla de seguridad: Asegurar que el ventilador inicie APAGADO al enchufar
  digitalWrite(PIN_RELEVADOR, APAGAR_VENTILADOR);

  // Ajuste interno del ESP32 para leer bien de 0V a 3.3V (0 a 4095)
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  Serial.println("====================================================");
  Serial.println("  PRUEBA DE HARDWARE ROL 1: SENSOR MQ-7 Y RELEVADOR ");
  Serial.println("====================================================");
}

void loop() {
  // PASO 1: Tomamos 20 lecturas rápidas para evitar saltos falsos de voltaje
  long sumaLecturas = 0;
  for (int i = 0; i < 20; i++) {
    sumaLecturas += analogRead(PIN_SENSOR_MQ7);
    delay(10); // Pequeña pausa de 10 milisegundos entre muestras
  }
  
  // Calculamos el promedio (de 0 a 4095) y lo pasamos a porcentaje (0% a 100%)
  float lecturaCruda = sumaLecturas / 20.0;
  float porcentajeGas = (lecturaCruda / 4095.0) * 100.0;
  float voltajeEnPin  = (lecturaCruda / 4095.0) * 3.3; // Para verificar seguridad (<3.3V)

  // PASO 2: Lazo Cerrado (El ESP32 toma la decisión solito)
  if (porcentajeGas >= LIMITE_PARA_ENCENDER && !ventiladorPrendido) {
    ventiladorPrendido = true;
    digitalWrite(PIN_RELEVADOR, ENCENDER_VENTILADOR);
    Serial.println(">>> ¡GAS DETECTADO! -> RELEVADOR ACTIVADO (CLIC) <<<");
  } 
  else if (porcentajeGas <= LIMITE_PARA_APAGAR && ventiladorPrendido) {
    ventiladorPrendido = false;
    digitalWrite(PIN_RELEVADOR, APAGAR_VENTILADOR);
    Serial.println(">>> AIRE LIMPIO -> RELEVADOR DESACTIVADO <<<");
  }

  // PASO 3: Mostrar datos en la computadora para que calibres tu sensor
  Serial.print("Lectura Cruda (0-4095): ");
  Serial.print((int)lecturaCruda);
  Serial.print(" | Voltaje en Pin 34: ");
  Serial.print(voltajeEnPin, 2);
  Serial.print("V | Nivel Gas: ");
  Serial.print(porcentajeGas, 1);
  Serial.print("% | Ventilador: ");
  Serial.println(ventiladorPrendido ? "ENCENDIDO [ON]" : "APAGADO [OFF]");

  delay(800); // Actualiza la pantalla cada segundo aproximadamente
}