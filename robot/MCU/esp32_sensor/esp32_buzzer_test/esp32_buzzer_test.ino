/*
 * ESP32 buzzer test
 *
 * Wiring:
 *   Buzzer signal -> GPIO22 / D22
 *
 * For an active buzzer module, HIGH should make it beep.
 */

static const int BUZZER_PIN = 22;
static const uint32_t SERIAL_BAUD = 115200;
static const unsigned long ON_MS = 500;
static const unsigned long OFF_MS = 500;

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(300);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  Serial.println("STATUS:BUZZER_TEST_READY");
}

void loop() {
  Serial.println("BUZZER:ON");
  digitalWrite(BUZZER_PIN, HIGH);
  delay(ON_MS);

  Serial.println("BUZZER:OFF");
  digitalWrite(BUZZER_PIN, LOW);
  delay(OFF_MS);
}
