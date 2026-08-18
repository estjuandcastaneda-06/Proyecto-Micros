/* ============================================================
   COCINA INTELIGENTE - ESP32 #2
   IMPORTANTE SOBRE EL MQ-2 (sensor de gas):
   - Necesita un tiempo de precalentamiento (aprox 20-60 segundos)
     para que sus lecturas se estabilicen. Los primeros segundos
     tras encenderlo, ignora lo que marque.
   - Calibra el umbral (GAS_UMBRAL) viendo el Monitor Serial con
     el sensor ya caliente y en aire limpio: toma el valor que
     marca ahi y ponle un margen arriba (por ejemplo, +500).
   ============================================================ */

#include <ESP32Servo.h>


#define SERVO_PERSIANA_PIN 13
#define SWITCH_PERSIANA      5
#define SERVO_PUERTA_PIN 25
#define BOTON_PUERTA     18
#define MQ2_AOUT         34   // Salida analogica (ADC1, solo lectura)
#define MQ2_DOUT         35   // Salida digital del comparador (opcional)
#define BUZZER           4
#define LED_GAS          19


#define GAS_UMBRAL          300  // (rango ADC ESP32: 0-4095)
#define MUESTRAS_GAS        5     

const int ANGULO_PERSIANA_CERRADA = 0;
const int ANGULO_PERSIANA_ABIERTA = 90;

const int ANGULO_PUERTA_CERRADA = 0;
const int ANGULO_PUERTA_ABIERTA = 90;

const unsigned long DEBOUNCE_MS = 300;

Servo servoPersiana;
Servo servoPuerta;

bool puertaAbierta = false;
unsigned long lastDebouncePuerta = 0;

// Estado ya confirmado (tras el debounce) del switch de la persiana y del boton de la puerta
int estadoPersianaEstable = HIGH;
int estadoPuertaEstable = HIGH;
// Ultima lectura cruda de cada uno (para detectar cuando cambia)
int lecturaPersianaAnterior = HIGH;
int lecturaPuertaAnterior = HIGH;
unsigned long lastDebouncePersiana = 0;

// Guarda si el servo de la persiana esta actualmente encendido/enganchado,
bool servoPersianaActivo = false;

void setup() {
  Serial.begin(115200);

  pinMode(SWITCH_PERSIANA, INPUT_PULLUP);
  pinMode(BOTON_PUERTA, INPUT_PULLUP);

  pinMode(MQ2_DOUT, INPUT);
  pinMode(BUZZER, OUTPUT);
  pinMode(LED_GAS, OUTPUT);
  digitalWrite(BUZZER, LOW);
  digitalWrite(LED_GAS, LOW);

  // La persiana arranca apagada/sin torque hasta que el switch diga lo contrario
  servoPersiana.setPeriodHertz(50);

  servoPuerta.setPeriodHertz(50);
  servoPuerta.attach(SERVO_PUERTA_PIN, 500, 2400);
  servoPuerta.write(ANGULO_PUERTA_CERRADA);

  Serial.println("ESP32 #2 listo: Persiana (servo) / Puerta (servo) / Gas");
  Serial.println("MQ-2 precalentando... espera unos segundos antes de confiar en la lectura.");
}

void loop() {
  // LOW = persiana activada / abierta
  // HIGH = persiana apagada / cerrada
  int lecturaPersiana = digitalRead(SWITCH_PERSIANA);
  if (lecturaPersiana != lecturaPersianaAnterior) {
    lastDebouncePersiana = millis();
  }
  if (millis() - lastDebouncePersiana > DEBOUNCE_MS) {
    if (lecturaPersiana != estadoPersianaEstable) {
      estadoPersianaEstable = lecturaPersiana;

      if (estadoPersianaEstable == LOW) {
        // Switch en posicion "activada": encender el servo y moverlo
        Serial.println("Persiana: switch activado, moviendo...");
        if (!servoPersianaActivo) {
          servoPersiana.attach(SERVO_PERSIANA_PIN, 500, 2400);
          servoPersianaActivo = true;
        }
        servoPersiana.write(ANGULO_PERSIANA_ABIERTA);
      } else {
        // Switch en posicion "apagada": quitar el torque
        Serial.println("Persiana: switch apagado.");
        if (servoPersianaActivo) {
          servoPersiana.detach();
          servoPersianaActivo = false;
        }
      }
    }
  }
  lecturaPersianaAnterior = lecturaPersiana;

  // ---- Sensor de gas MQ-2 (chequeo continuo, cada vuelta del loop) ----
  long sumaGas = 0;
  for (int i = 0; i < MUESTRAS_GAS; i++) {
    sumaGas += analogRead(MQ2_AOUT);
  }
  int valorGas = sumaGas / MUESTRAS_GAS;

  Serial.print("MQ2 valor: ");
  Serial.println(valorGas);

  if (valorGas > GAS_UMBRAL) {
    digitalWrite(BUZZER, HIGH);
    digitalWrite(LED_GAS, HIGH);
    Serial.print("¡FUGA DE GAS DETECTADA! Valor: ");
    Serial.println(valorGas);
  } else {
    digitalWrite(BUZZER, LOW);
    digitalWrite(LED_GAS, LOW);
  }
}
