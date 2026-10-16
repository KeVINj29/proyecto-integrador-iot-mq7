# Arquitectura Eléctrica, Acondicionamiento de Señales y Desacoplamiento de Potencia

**Materia:** Internet de las Cosas  
**Docente:** Andrés Salomón Enríquez Ochoa  
**Sistema:** Prototipo IoT de Automatización en Lazo Cerrado para Monóxido de Carbono (CO)

---

## 1. Justificación y Cálculo del Acondicionamiento de Señal (Sensor MQ-7)

El sensor electroquímico de gas **MQ-7** requiere un voltaje de alimentación analógica de $V_{CC} = 5.0\,\text{V}$ para calentar adecuadamente su resistencia interna ($R_H$) y generar la respuesta de óxido metálico ($\text{SnO}_2$). Su salida analógica ($A_{OUT}$) entrega una señal proporcional a la concentración de monóxido de carbono en un rango de $0.0\,\text{V}$ a $5.0\,\text{V}$.

### Problema de Compatibilidad de Voltaje
Los pines del Convertidor Analógico-Digital (ADC) del microcontrolador **ESP32** tienen un límite absoluto de tensión en sus compuertas de entrada de $3.3\,\text{V}$ (tolerancia máxima recomendada de $3.6\,\text{V}$). Conectar directamente la salida de $5.0\,\text{V}$ provocaría saturación del ADC y la degradación térmica irreversible por sobretensión en el canal `ADC1_CH6` (GPIO 34).

### Diseño del Divisor Resistivo de Tensión
Se implementó una red de atenuación pasiva constituida por:
* Resistencia superior ($R_1$): $10\,\text{k}\Omega$ ($\pm 1\%$, película metálica).
* Resistencia inferior ($R_2$): $20\,\text{k}\Omega$ ($\pm 1\%$, película metálica).

La función de transferencia del divisor está dada por la ecuación:

$$V_{out} = V_{in} \cdot \left( \frac{R_2}{R_1 + R_2} \right)$$

Evaluando bajo la condición más crítica (saturación del sensor a $V_{in} = 5.0\,\text{V}$):

$$V_{out} = 5.0\,\text{V} \cdot \left( \frac{20\,\text{k}\Omega}{10\,\text{k}\Omega + 20\,\text{k}\Omega} \right) = 5.0\,\text{V} \cdot \left( \frac{20}{30} \right) = 5.0\,\text{V} \cdot \frac{2}{3} \approx 3.333\,\text{V}$$

#### Impedancia de Entrada y Consumo de Corriente
* Corriente de carga consumida por el divisor: $I = \frac{5.0\,\text{V}}{30\,\text{k}\Omega} \approx 0.166\,\text{mA}$, perfectamente soportable por la etapa amplificadora del MQ-7.
* Impedancia equivalente de Thévenin vista por el pin GPIO 34: $R_{Th} = R_1 \parallel R_2 = \frac{10\,\text{k}\Omega \cdot 20\,\text{k}\Omega}{30\,\text{k}\Omega} \approx 6.67\,\text{k}\Omega$. Al ser menor a $10\,\text{k}\Omega$, garantiza una carga rápida del condensador de muestreo y retención (Sample & Hold) interno del ADC del ESP32, mitigando errores de no linealidad.

---

## 2. Desacoplamiento de Potencia y Aislamiento Galvánico

Para accionar el extractor de aire industrial ($12\,\text{V}$ DC, carga inductiva) se utiliza un módulo relevador comercial de 1 canal con optoacoplador integrado (**SRD-05VDC-SL-C**).

### Aislamiento Galvánico
El módulo dispone de un optoacoplador (típicamente **EL817** o **PC817**) que separa eléctricamente el circuito lógico del microcontrolador respecto a la bobina del relevador:
1. **Lado Lógico:** La señal de control parte del **GPIO 26** del ESP32 hacia el pin `IN` del módulo. Funciona con lógica negativa (*Active-LOW*).
2. **Lado de Actuación Inductiva:** El contacto común (`COM`) y normalmente abierto (`NO`) conmutan la línea positiva de una fuente de alimentación independiente conmutada de $12\,\text{V}$ DC ($2\,\text{A}$).
3. **Punto Crítico de Aislamiento:** Las tierras lógicas (**GND** del ESP32 / 5V) y la tierra de la etapa de potencia (**GND** de la fuente de 12V) se mantienen **físicamente separadas en los contactos de salida**, impidiendo que transitorios de conmutación viajen hacia el microcontrolador.

### Supresión de Picos Inductivos (Diodo Flyback 1N4007)
Todo motor DC de extracción industrial genera una fuerza contraelectromotriz (Back-EMF, descrita por $V_L = -L \frac{di}{dt}$) al abrir los contactos mecánicos del relevador.
* Se instala en antiparalelo directamente sobre las terminales de alimentación del ventilador un diodo rectificador **1N4007** (Cátodo a $+12\,\text{V}$, Ánodo a $\text{GND}_{12V}$).
* Este diodo ofrece un camino de recirculación segura para la energía residual del campo magnético del inductor, extinguiendo el arco eléctrico en los contactos del relevador y erradicando picos de voltaje que inducirían *brownouts* o congelamientos en el ESP32.

---

## 3. Diagrama Esquemático de Conexión (Mermaid)

```mermaid
graph TD
    subgraph ETAPA_ALIMENTACION ["Etapa de Fuentes de Alimentación"]
        F5V["Fuente Regulada 5V DC (Lógica / Sensores)"]
        F12V["Fuente de Potencia 12V DC (Extractor)"]
    end

    subgraph ETAPA_CONTROL ["Etapa de Control Embebido (ESP32)"]
        ESP32["ESP32 DevKit V1"]
        GPIO34["GPIO 34 (ADC1_CH6)"]
        GPIO26["GPIO 26 (DO)"]
        GND_ESP["GND"]
        VIN_ESP["VIN (5V)"]
    end

    subgraph ETAPA_SENSOR ["Etapa Sensora Acondicionada"]
        MQ7["Sensor MQ-7 (Monóxido de Carbono)"]
        R1["Resistencia R1 (10 kΩ)"]
        R2["Resistencia R2 (20 kΩ)"]
    end

    subgraph ETAPA_RELE ["Etapa de Aislamiento y Relevador"]
        RELE["Módulo Relevador 5V (SRD-05VDC-SL-C)"]
        VCC_R["VCC (5V)"]
        GND_R["GND"]
        IN_R["IN (Active LOW)"]
        COM_R["COM (Común)"]
        NO_R["NO (Normalmente Abierto)"]
    end

    subgraph ETAPA_POTENCIA ["Etapa de Actuación Industrial"]
        MOTOR["Extractor Industrial 12V DC"]
        DIODO["Diodo Flyback 1N4007 (Antiparalelo)"]
    end

    %% Conexiones Alimentacion Logica
    F5V -->|5V| VIN_ESP
    F5V -->|5V| MQ7
    F5V -->|5V| VCC_R
    F5V -->|GND Común Lógico| GND_ESP
    GND_ESP --- GND_R
    GND_ESP --- R2

    %% Divisor de Tension Sensor
    MQ7 -->|Salida AOUT (0-5V)| R1
    R1 --> GPIO34
    GPIO34 --- R2
    R2 --- GND_ESP

    %% Control de Relevador
    GPIO26 -->|Señal de Disparo Lógica Inversa| IN_R

    %% Circuito de Potencia 12V (Aislado)
    F12V -->|+12V| COM_R
    NO_R --> MOTOR
    MOTOR -->|Retorno 12V| F12V
    DIODO -.->|Cátodo a +12V / Ánodo a Retorno| MOTOR
```

---

## 4. Diagrama Esquemático en Formato ASCII

```text
=========================================================================================================
                                     DIAGRAMA ELECTRÓNICO DETALLADO
=========================================================================================================

  +5V DC (Fuente Lógica)
   o-----------------------+-----------------------------+
                           |                             |
                           | (Pin VCC)                   | (Pin VCC)
                     +-----------+                 +-----------+
                     |  SENSOR   |                 |  MÓDULO   |
                     |   MQ-7    |                 | RELEVADOR |
                     +-----------+                 |  5V (SL)  |
                           |                       |           |
                           | (Pin AOUT: 0-5V)      |   (IN)    |<--------[GPIO 26 (DO Active-LOW)]
                           |                       +-----------+                 (ESP32)
                           |                             |  |
                           R1 (10kΩ, 1%)                 |  +--------------------+
                           |                             |                       |
                           +---------> [GPIO 34 (ADC)]   | (Pin GND)             |
                           |           (ESP32 0-3.3V)    |                       |
                           R2 (20kΩ, 1%)                 |                       |
                           |                             |                       |
   o-----------------------+-----------------------------+                       |
  GND (Tierra Lógica Común ESP32 / Sensor / Rele)                                |
                                                                                 |
================================================================================ | ======================
                                 ZONA DE AISLAMIENTO GALVÁNICO                   |
================================================================================ | ======================
                                                                                 |
  +12V DC (Fuente Potencia Industrial)                                           |
   o----------------------------------------------------+ (Contacto COM)         |
                                                        |                        |
                                                    +---+---+                    |
                                                    | RELE  |                    |
                                                    |  NO   |--------------------+
                                                    +-------+
                                                        |
                                                        +--------+ (Positivo Extractor)
                                                        |        |
                                                      +---+      |  Cátodo (Banda)
                                                      | M |    [ - ]
                                   Extractor Inductivo| O |    | D | Diodo 1N4007 (Flyback)
                                             12V DC   | T |    [ + ]
                                                      | O |      |  Ánodo
                                                      | R |      |
                                                      +---+      |
                                                        |        |
                                                        +--------+ (Negativo Retorno)
                                                        |
   o----------------------------------------------------+
  GND 12V DC (Tierra de Potencia Independiente)
=========================================================================================================
```

---

## 5. Tabla de Asignación de Terminales y Pines (Pinout)

| Componente | Terminal Física | Conexión en Sistema | Nivel Lógico / Tensión | Función Técnica |
| :--- | :--- | :--- | :--- | :--- |
| **ESP32 DevKit V1** | `VIN` | Salida 5V Regulada | 5.0 V DC | Alimentación del SoC ESP32 |
| **ESP32 DevKit V1** | `GND` | Barra de Tierra Lógica | 0.0 V (Ref) | Referencia del sistema embebido |
| **ESP32 DevKit V1** | `GPIO 34` | Nodo central Divisor ($R_1-R_2$) | 0.0 V - 3.3 V DC | Lectura analógica ADC1_CH6 (Atenuación 11dB) |
| **ESP32 DevKit V1** | `GPIO 26` | Pin `IN` del Módulo Relevador | 3.3 V / 0.0 V DC | Salida digital de control (LOW = Activar) |
| **Sensor MQ-7** | `VCC` | Riel 5V DC | 5.0 V DC | Calentador y puente detector SnO2 |
| **Sensor MQ-7** | `GND` | Riel GND Lógico | 0.0 V DC | Retorno del sensor |
| **Sensor MQ-7** | `AOUT` | Resistencia $R_1$ ($10\,\text{k}\Omega$) | 0.0 V - 5.0 V DC | Salida de tensión variable proporcional a CO |
| **Módulo Relevador**| `VCC` | Riel 5V DC | 5.0 V DC | Alimentación del circuito de bobina/opto |
| **Módulo Relevador**| `GND` | Riel GND Lógico | 0.0 V DC | Retorno del módulo |
| **Módulo Relevador**| `COM` | Borne Positivo Fuente 12V | +12.0 V DC | Entrada de tensión de potencia |
| **Módulo Relevador**| `NO` | Borne Positivo del Ventilador| +12.0 V DC (Conmutado)| Contacto normalmente abierto de cierre de lazo |
| **Ventilador 12V** | Borne Negativo | Borne Negativo Fuente 12V | 0.0 V DC Potencia | Retorno de potencia del motor DC |
| **Diodo 1N4007** | Cátodo / Ánodo | Borne Positivo / Borne Neg | Reversa a 12V | Recirculación de corriente inductiva de apagado |
