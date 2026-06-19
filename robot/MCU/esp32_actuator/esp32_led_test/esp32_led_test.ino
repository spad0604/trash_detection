/*
 * ESP32 3 LED serial control test
 *
 * Wiring:
 *   LED1 -> GPIO18
 *   LED2 -> TX2 / GPIO17
 *   LED3 -> RX2 / GPIO16
 *
 * Serial commands:
 *   1 -> turn LED1 on for 5 seconds
 *   2 -> turn LED2 on for 5 seconds
 *   3 -> turn LED3 on for 5 seconds
 */

static const int LED1_PIN = 18;
static const int LED2_PIN = 17; // TX2
static const int LED3_PIN = 16; // RX2
static const int LED_PINS[3] = {LED1_PIN, LED2_PIN, LED3_PIN};

static const uint32_t SERIAL_BAUD = 115200;
static const unsigned long LED_ON_MS = 5000;

int activeLedIndex = -1;
unsigned long activeLedUntilMs = 0;

void allLedsOff() {
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_PINS[i], LOW);
  }
}

void turnLedOnFor5s(int ledIndex) {
  allLedsOff();
  activeLedIndex = ledIndex;
  activeLedUntilMs = millis() + LED_ON_MS;
  digitalWrite(LED_PINS[ledIndex], HIGH);

  Serial.print("LED");
  Serial.print(ledIndex + 1);
  Serial.println(":ON");
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(300);

  for (int i = 0; i < 3; i++) {
    pinMode(LED_PINS[i], OUTPUT);
  }
  allLedsOff();

  Serial.println("STATUS:LED_TEST_READY");
  Serial.println("SEND: 1, 2, or 3");
}

void loop() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c >= '1' && c <= '3') {
      turnLedOnFor5s(c - '1');
    }
  }

  if (activeLedIndex >= 0 && millis() >= activeLedUntilMs) {
    allLedsOff();
    Serial.print("LED");
    Serial.print(activeLedIndex + 1);
    Serial.println(":OFF");
    activeLedIndex = -1;
  }
}
