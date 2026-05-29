// ESP32 5-channel line sensor read-only test
//
// Purpose:
// - Verify the 5 line sensors alone, without motors or PID.
// - Show raw ADC, normalized strength, active flags, and whether the robot
//   currently "sees" a line.
//
// Pin map from the working line test:
// IN1 -> GPIO36
// IN2 -> GPIO39
// IN3 -> GPIO34
// IN4 -> GPIO35
// IN5 -> GPIO32
//
// Sensor logic for this project:
// - Black line ~= 0
// - White background ~= 4095
//
// Commands over Serial:
//   tNNN  set detect threshold 0..1000
//   cw    sample white surface
//   cb    sample black surface
//   ca    auto calibration for 3 seconds
//   cr    reset calibration to black=0 white=4095
//   p     print calibration now

#include <Arduino.h>

static const int LINE_PINS[5] = {36, 39, 34, 35, 32};
static const int LINE_WEIGHTS[5] = {-2000, -1000, 0, 1000, 2000};

int blackCal[5] = {0, 0, 0, 0, 0};
int whiteCal[5] = {4095, 4095, 4095, 4095, 4095};
int detectThreshold = 200;

unsigned long lastPrintMs = 0;
const unsigned long printIntervalMs = 150;

struct LineRead {
  int raw[5];
  int strength[5];
  int active[5];
  int activeCount;
  long position;
};

int normalizedLineStrength(int sensorIndex, int rawValue) {
  int black = blackCal[sensorIndex];
  int white = whiteCal[sensorIndex];
  int span = white - black;
  if (span < 50) {
    span = 50;
  }

  long strength = ((long)white - rawValue) * 1000L / span;
  if (strength < 0) strength = 0;
  if (strength > 1000) strength = 1000;
  return (int)strength;
}

LineRead readLineSensors() {
  LineRead line;
  line.activeCount = 0;
  long weightedSum = 0;
  long strengthSum = 0;

  for (int i = 0; i < 5; i++) {
    int value = analogRead(LINE_PINS[i]);
    int strength = normalizedLineStrength(i, value);

    line.raw[i] = value;
    line.strength[i] = strength;
    line.active[i] = strength >= detectThreshold;

    if (line.active[i]) {
      line.activeCount++;
      weightedSum += (long)LINE_WEIGHTS[i] * strength;
      strengthSum += strength;
    }
  }

  line.position = (strengthSum > 0) ? (weightedSum / strengthSum) : 0;
  return line;
}

void printCalibration() {
  Serial.print("black=");
  for (int i = 0; i < 5; i++) {
    Serial.print(blackCal[i]);
    Serial.print(i == 4 ? " " : ",");
  }
  Serial.print("white=");
  for (int i = 0; i < 5; i++) {
    Serial.print(whiteCal[i]);
    Serial.print(i == 4 ? "\n" : ",");
  }
}

void sampleSurfaceCalibration(bool blackSurface) {
  const int samples = 120;
  long sum[5] = {0, 0, 0, 0, 0};

  for (int s = 0; s < samples; s++) {
    for (int i = 0; i < 5; i++) {
      sum[i] += analogRead(LINE_PINS[i]);
    }
    delay(4);
  }

  for (int i = 0; i < 5; i++) {
    int avg = (int)(sum[i] / samples);
    if (blackSurface) {
      blackCal[i] = avg;
    } else {
      whiteCal[i] = avg;
    }
  }

  Serial.println(blackSurface ? "Black calibration saved" : "White calibration saved");
  printCalibration();
}

void autoSweepCalibration() {
  int minRaw[5] = {4095, 4095, 4095, 4095, 4095};
  int maxRaw[5] = {0, 0, 0, 0, 0};

  Serial.println("Auto calibration: sweep over black line and white background for 3 seconds");

  unsigned long start = millis();
  while (millis() - start < 3000) {
    for (int i = 0; i < 5; i++) {
      int value = analogRead(LINE_PINS[i]);
      if (value < minRaw[i]) minRaw[i] = value;
      if (value > maxRaw[i]) maxRaw[i] = value;
    }
    delay(5);
  }

  for (int i = 0; i < 5; i++) {
    blackCal[i] = minRaw[i];
    whiteCal[i] = maxRaw[i];
  }

  Serial.println("Auto calibration saved");
  printCalibration();
}

void resetCalibration() {
  for (int i = 0; i < 5; i++) {
    blackCal[i] = 0;
    whiteCal[i] = 4095;
  }
  Serial.println("Calibration reset to black=0 white=4095");
}

void handleSerialCommands() {
  if (!Serial.available()) {
    return;
  }

  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  cmd.toLowerCase();

  if (cmd.startsWith("t")) {
    int value = cmd.substring(1).toInt();
    if (value >= 0 && value <= 1000) {
      detectThreshold = value;
      Serial.print("detectThreshold=");
      Serial.println(detectThreshold);
    }
  } else if (cmd == "cw") {
    sampleSurfaceCalibration(false);
  } else if (cmd == "cb") {
    sampleSurfaceCalibration(true);
  } else if (cmd == "ca") {
    autoSweepCalibration();
  } else if (cmd == "cr") {
    resetCalibration();
  } else if (cmd == "p") {
    printCalibration();
  }
}

void printLine(const LineRead &line) {
  unsigned long now = millis();
  if (now - lastPrintMs < printIntervalMs) {
    return;
  }
  lastPrintMs = now;

  Serial.print("LINE=");
  Serial.print(line.activeCount > 0 ? 1 : 0);
  Serial.print(" active=");
  Serial.print(line.activeCount);
  Serial.print(" pos=");
  Serial.print(line.position);
  Serial.print(" th=");
  Serial.print(detectThreshold);
  Serial.print(" | ");

  for (int i = 0; i < 5; i++) {
    Serial.print("IN");
    Serial.print(i + 1);
    Serial.print(":");
    Serial.print(line.raw[i]);
    Serial.print("/");
    Serial.print(line.strength[i]);
    Serial.print("(");
    Serial.print(line.active[i]);
    Serial.print(") ");
  }

  if (line.activeCount == 0) {
    Serial.print("NO_LINE");
  }
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  delay(100);

  analogReadResolution(12);
  for (int i = 0; i < 5; i++) {
    pinMode(LINE_PINS[i], INPUT);
    analogSetPinAttenuation(LINE_PINS[i], ADC_11db);
  }

  Serial.println("ESP32 5-channel line sensor read-only test");
  Serial.println("Black line=0, white background=4095");
  Serial.println("Commands: tNNN, cw, cb, ca, cr, p");
  printCalibration();
}

void loop() {
  handleSerialCommands();
  LineRead line = readLineSensors();
  printLine(line);
}
