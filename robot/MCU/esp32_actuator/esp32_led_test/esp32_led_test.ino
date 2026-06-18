/*
 * ESP32 3 LED blink test
 *
 * Wiring:
 *   LED1 -> GPIO18
 *   LED2 -> TX2 / GPIO17
 *   LED3 -> RX2 / GPIO16
 */

static const int LED1_PIN = 18;
static const int LED2_PIN = 17; // TX2
static const int LED3_PIN = 16; // RX2

static const unsigned long STEP_DELAY_MS = 300;
static const unsigned long ALL_DELAY_MS = 500;

void setup() {
  pinMode(LED1_PIN, OUTPUT);
  pinMode(LED2_PIN, OUTPUT);
  pinMode(LED3_PIN, OUTPUT);

  digitalWrite(LED1_PIN, LOW);
  digitalWrite(LED2_PIN, LOW);
  digitalWrite(LED3_PIN, LOW);
}

void blinkOne(int pin) {
  digitalWrite(pin, HIGH);
  delay(STEP_DELAY_MS);
  digitalWrite(pin, LOW);
  delay(STEP_DELAY_MS);
}

void setAll(int state) {
  digitalWrite(LED1_PIN, state);
  digitalWrite(LED2_PIN, state);
  digitalWrite(LED3_PIN, state);
}

void loop() {
  blinkOne(LED1_PIN);
  blinkOne(LED2_PIN);
  blinkOne(LED3_PIN);

  setAll(HIGH);
  delay(ALL_DELAY_MS);
  setAll(LOW);
  delay(ALL_DELAY_MS);
}
