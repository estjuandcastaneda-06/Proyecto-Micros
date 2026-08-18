/* ============================================================
   COCINA INTELIGENTE - ESP32 #2
   Controla: Persiana automatizada (SERVOMOTOR + switch de palanca),
   Puerta automatizada (SERVOMOTOR + boton) y Sensor de Fuga de
   Gas MQ-2 con alarma (buzzer + LED).
   ============================================================
   LIBRERIAS NECESARIAS (Arduino IDE > Programa > Incluir Libreria
   > Administrar Librerias):
     - "ESP32Servo" (Kevin Harrington) -> para el servo de la
       puerta y el servo de la persiana
   ============================================================
   PLACA: Herramientas > Placa > ESP32 Arduino > ESP32 Dev Module
   ============================================================
   IMPORTANTE SOBRE LOS SERVOS (puerta y persiana):
   - Cable rojo -> 5V, cable marron/negro -> GND, cable naranja
     (señal) -> el GPIO indicado abajo.
   - *** MUY IMPORTANTE ***: dale a cada servo (o a ambos juntos)
     su PROPIA fuente de 5V, separada del 5V que alimenta al ESP32.
     El pico de corriente al arrancar el servo puede hundir el
     voltaje y reiniciar el ESP32 solo (brownout), causando que el
     switch parezca "no servir" o que el servo se mueva erratico.
     Solo hay que unir los GND de ambas fuentes entre si.
   - Si puedes, pon un capacitor electrolitico de 220-470uF entre
     el + y el - de la alimentacion del servo, lo mas cerca posible
     de el, para absorber ese pico de corriente.
   ============================================================
   IMPORTANTE SOBRE EL MQ-2 (sensor de gas):
   - Necesita un tiempo de precalentamiento (aprox 20-60 segundos)
     para que sus lecturas se estabilicen. Los primeros segundos
     tras encenderlo, ignora lo que marque.
   - El valor analogico SUBE cuando hay mas gas/humo (no baja).
   - Calibra el umbral (GAS_UMBRAL) viendo el Monitor Serial con
     el sensor ya caliente y en aire limpio: toma el valor que
     marca ahi y ponle un margen arriba (por ejemplo, +500).
   ============================================================
   IMPORTANTE SOBRE LA PERSIANA (switch de palanca fija):
   - Como es un switch que se queda fijo en una posicion (no un
     pulsador que vuelve solo), el codigo ahora LEE DIRECTAMENTE
     la posicion del switch en cada vuelta del loop, en vez de
     contar "toggles". Esto es mas robusto: sin importar rebotes
     o reinicios del ESP32, el servo siempre termina en la
     posicion que corresponde a como este el switch fisicamente.
   ============================================================ */

#include <ESP32Servo.h>

// ---------------- PERSIANA: SERVOMOTOR ----------------
#define SERVO_PERSIANA_PIN 13
#define SWITCH_PERSIANA      5

// ---------------- PUERTA: SERVOMOTOR ----------------
#define SERVO_PUERTA_PIN 25
#define BOTON_PUERTA     18

// ---------------- SENSOR DE GAS MQ-2 ----------------
#define MQ2_AOUT         34   // Salida analogica (ADC1, solo lectura)
#define MQ2_DOUT         35   // Salida digital del comparador (opcional)
#define BUZZER           4
#define LED_GAS          19

// ---------------- CONFIGURACION ----------------
#define GAS_UMBRAL          300  // Ajustar viendo el Monitor Serial en aire limpio (rango ADC ESP32: 0-4095)
#define MUESTRAS_GAS        5     // Cantidad de lecturas para promediar y evitar falsos positivos por ruido

const int ANGULO_PERSIANA_CERRADA = 0;
const int ANGULO_PERSIANA_ABIERTA = 90;

const int ANGULO_PUERTA_CERRADA = 0;
const int ANGULO_PUERTA_ABIERTA = 90;

const unsigned long DEBOUNCE_MS = 300;

Servo servoPersiana;
Servo servoPuerta;

bool puertaAbierta = false;
unsigned long lastDebouncePuerta = 0;

// Estado ya confirmado (tras el debounce) del switch de la persiana
// y del boton de la puerta
int estadoPersianaEstable = HIGH;
int estadoPuertaEstable = HIGH;
// Ultima lectura cruda de cada uno (para detectar cuando cambia)
int lecturaPersianaAnterior = HIGH;
int lecturaPuertaAnterior = HIGH;
unsigned long lastDebouncePersiana = 0;

// Guarda si el servo de la persiana esta actualmente encendido/enganchado,
// para no llamar attach()/detach() de mas en cada vuelta del loop
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
  // ---- Switch persiana: LECTURA DIRECTA de posicion (no toggle) ----
  // LOW (switch hacia GND) = persiana activada / abierta
  // HIGH (switch en la otra posicion) = persiana apagada
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

  // ---- Boton puerta (toggle abrir/cerrar, servo, con debounce correcto) ----
  int lecturaPuerta = digitalRead(BOTON_PUERTA);
  if (lecturaPuerta != lecturaPuertaAnterior) {
    lastDebouncePuerta = millis();
  }
  if (millis() - lastDebouncePuerta > DEBOUNCE_MS) {
    if (lecturaPuerta != estadoPuertaEstable) {
      estadoPuertaEstable = lecturaPuerta;
      if (estadoPuertaEstable == LOW) {
        puertaAbierta = !puertaAbierta;
        Serial.println(puertaAbierta ? "Abriendo puerta..." : "Cerrando puerta...");
        servoPuerta.write(puertaAbierta ? ANGULO_PUERTA_ABIERTA : ANGULO_PUERTA_CERRADA);
      }
    }
  }
  lecturaPuertaAnterior = lecturaPuerta;

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
