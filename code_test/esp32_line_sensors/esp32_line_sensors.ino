// ESP32 5-channel line follower PID test
//
// Line sensor pins from the line sensor test:
// IN1 -> VP  (GPIO36) leftmost
// IN2 -> VN  (GPIO39)
// IN3 -> D34 (GPIO34) center
// IN4 -> D35 (GPIO35)
// IN5 -> D32 (GPIO32) rightmost
//
// L298N pins from the MCU schematic:
// IN_M1 -> GPIO23, IN_M2 -> GPIO22, IN_M3 -> GPIO19, IN_M4 -> GPIO18
// Assumption: M1/M2 drive left motor, M3/M4 drive right motor.
// If direction is reversed, flip LEFT_REVERSED / RIGHT_REVERSED below.

static const int LINE_PINS[5] = {36, 39, 34, 35, 32};
static const int LINE_WEIGHTS[5] = {-2000, -1000, 0, 1000, 2000};

static const int LEFT_FORWARD_PIN = 23;   // IN_M1
static const int LEFT_BACKWARD_PIN = 22;  // IN_M2
static const int RIGHT_FORWARD_PIN = 19;  // IN_M3
static const int RIGHT_BACKWARD_PIN = 18; // IN_M4

static const bool LEFT_REVERSED = false;
static const bool RIGHT_REVERSED = false;

static const int PWM_FREQ = 5000;
static const int PWM_RESOLUTION = 8;
static const int PWM_MAX = 255;

// Your sensors: black line ~= 0, white background ~= 4095.
// PID uses normalized line strength 0..1000 from per-sensor calibration.
int blackCal[5] = {0, 0, 0, 0, 0};
int whiteCal[5] = {4095, 4095, 4095, 4095, 4095};
int detectThreshold = 200;

// Tune these first: baseSpeed, kp, then kd. Keep ki at 0 unless needed.
int baseSpeed = 200;
int minRunSpeed = 55;
int maxSpeed = 210;
int bridgeSpeed = 90;
int searchSpeed = 120;
float kp = 0.070f;
float ki = 0.000f;
float kd = 0.018f;

bool motorsEnabled = false;
unsigned long lastPidUs = 0;
unsigned long lastPrint = 0;
unsigned long lastLineSeenMs = 0;
const unsigned long printIntervalMs = 200;
unsigned long bridgeLineGapMs = 120;
unsigned long searchLineGapMs = 900;

float integral = 0.0f;
float lastError = 0.0f;
float lastSeenError = 0.0f;
int lastSearchDir = 1;
int lastLeftSpeed = 0;
int lastRightSpeed = 0;

struct LineRead {
  int raw[5];
  int strength[5];
  int active[5];
  int activeCount;
  long position;
};

void setupMotorPin(int pin) {
  pinMode(pin, OUTPUT);
  ledcAttach(pin, PWM_FREQ, PWM_RESOLUTION);
  ledcWrite(pin, 0);
}

void setup() {
  Serial.begin(115200);
  delay(100);

  analogReadResolution(12);
  for (int i = 0; i < 5; i++) {
    pinMode(LINE_PINS[i], INPUT);
    analogSetPinAttenuation(LINE_PINS[i], ADC_11db);
  }

  setupMotorPin(LEFT_FORWARD_PIN);
  setupMotorPin(LEFT_BACKWARD_PIN);
  setupMotorPin(RIGHT_FORWARD_PIN);
  setupMotorPin(RIGHT_BACKWARD_PIN);
  stopMotors();

  Serial.println("ESP32 PID Line Follower Test");
  Serial.println("Logic: black line=0, white background=4095");
  Serial.println("Commands: r=start, x=stop, tNNN=detect threshold 0..1000, bNNN=base, pX=kp, iX=ki, dX=kd");
  Serial.println("Recovery tune: gNNN=bridge ms, sNNN=search ms, vNNN=bridge speed, qNNN=search speed");
  Serial.println("Calibration: cw=sample white, cb=sample black, ca=auto sweep white+black for 3s, cr=reset 0/4095");
  Serial.print("DetectThreshold="); Serial.print(detectThreshold);
  Serial.print(" base="); Serial.print(baseSpeed);
  Serial.print(" kp="); Serial.print(kp, 4);
  Serial.print(" ki="); Serial.print(ki, 4);
  Serial.print(" kd="); Serial.println(kd, 4);
}

void loop() {
  handleSerialCommands();

  LineRead line = readLineSensors();
  runPid(line);
  printDebug(line);
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

void runPid(const LineRead &line) {
  unsigned long nowMs = millis();
  unsigned long nowUs = micros();
  float dt = (lastPidUs == 0) ? 0.01f : (nowUs - lastPidUs) / 1000000.0f;
  lastPidUs = nowUs;
  if (dt <= 0.0f || dt > 0.2f) {
    dt = 0.01f;
  }

  if (!motorsEnabled) {
    stopMotors();
    integral = 0.0f;
    lastError = 0.0f;
    lastLeftSpeed = 0;
    lastRightSpeed = 0;
    return;
  }

  if (line.activeCount == 0) {
    recoverLostLine(nowMs);
    return;
  }

  lastLineSeenMs = nowMs;
  float error = (float)line.position;
  lastSeenError = error;
  if (error > 150.0f) {
    lastSearchDir = 1;
  } else if (error < -150.0f) {
    lastSearchDir = -1;
  }

  integral += error * dt;
  integral = constrain(integral, -5000.0f, 5000.0f);
  float derivative = (error - lastError) / dt;
  lastError = error;

  float correction = kp * error + ki * integral + kd * derivative;

  int leftSpeed = (int)(baseSpeed + correction);
  int rightSpeed = (int)(baseSpeed - correction);

  leftSpeed = clampRunSpeed(leftSpeed);
  rightSpeed = clampRunSpeed(rightSpeed);

  setMotorSpeeds(leftSpeed, rightSpeed);
  lastLeftSpeed = leftSpeed;
  lastRightSpeed = rightSpeed;
}

void recoverLostLine(unsigned long nowMs) {
  unsigned long lostMs = (lastLineSeenMs == 0) ? searchLineGapMs + 1 : nowMs - lastLineSeenMs;
  integral = 0.0f;

  if (lostMs <= bridgeLineGapMs) {
    int leftSpeed = clampRecoverySpeed(lastLeftSpeed, bridgeSpeed);
    int rightSpeed = clampRecoverySpeed(lastRightSpeed, bridgeSpeed);
    if (leftSpeed == 0 && rightSpeed == 0) {
      leftSpeed = bridgeSpeed;
      rightSpeed = bridgeSpeed;
    }
    setMotorSpeeds(leftSpeed, rightSpeed);
    return;
  }

  if (lostMs <= searchLineGapMs) {
    int dir = lastSearchDir;
    if (lastSeenError > 100.0f || lastSeenError < -100.0f) {
      dir = (lastSeenError > 0.0f) ? 1 : -1;
    }

    // Positive error means line was last seen on the right, so rotate right.
    if (dir > 0) {
      setMotorSpeeds(searchSpeed, -searchSpeed);
    } else {
      setMotorSpeeds(-searchSpeed, searchSpeed);
    }
    return;
  }

  stopMotors();
  lastLeftSpeed = 0;
  lastRightSpeed = 0;
}

int clampRecoverySpeed(int speed, int fallbackAbsSpeed) {
  if (speed == 0) {
    return 0;
  }
  int sign = speed > 0 ? 1 : -1;
  int magnitude = abs(speed);
  magnitude = constrain(magnitude, minRunSpeed, fallbackAbsSpeed);
  return sign * magnitude;
}

int clampRunSpeed(int speed) {
  if (speed == 0) {
    return 0;
  }
  if (speed > 0) {
    return constrain(speed, minRunSpeed, maxSpeed);
  }
  return constrain(speed, -maxSpeed, -minRunSpeed);
}

void setMotorSpeeds(int leftSpeed, int rightSpeed) {
  if (LEFT_REVERSED) {
    leftSpeed = -leftSpeed;
  }
  if (RIGHT_REVERSED) {
    rightSpeed = -rightSpeed;
  }

  driveMotor(LEFT_FORWARD_PIN, LEFT_BACKWARD_PIN, leftSpeed);
  driveMotor(RIGHT_FORWARD_PIN, RIGHT_BACKWARD_PIN, rightSpeed);
}

void driveMotor(int forwardPin, int backwardPin, int speed) {
  speed = constrain(speed, -PWM_MAX, PWM_MAX);

  if (speed > 0) {
    ledcWrite(forwardPin, speed);
    ledcWrite(backwardPin, 0);
  } else if (speed < 0) {
    ledcWrite(forwardPin, 0);
    ledcWrite(backwardPin, -speed);
  } else {
    ledcWrite(forwardPin, 0);
    ledcWrite(backwardPin, 0);
  }
}

void stopMotors() {
  ledcWrite(LEFT_FORWARD_PIN, 0);
  ledcWrite(LEFT_BACKWARD_PIN, 0);
  ledcWrite(RIGHT_FORWARD_PIN, 0);
  ledcWrite(RIGHT_BACKWARD_PIN, 0);
}

void handleSerialCommands() {
  if (!Serial.available()) {
    return;
  }

  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  cmd.toLowerCase();
  if (cmd.length() == 0) {
    return;
  }

  if (cmd == "r") {
    motorsEnabled = true;
    integral = 0.0f;
    lastError = 0.0f;
    lastPidUs = micros();
    lastLineSeenMs = millis();
    Serial.println("RUN");
  } else if (cmd == "x") {
    motorsEnabled = false;
    stopMotors();
    Serial.println("STOP");
  } else if (cmd.startsWith("t")) {
    int value = cmd.substring(1).toInt();
    if (value >= 0 && value <= 1000) {
      detectThreshold = value;
      Serial.print("detectThreshold="); Serial.println(detectThreshold);
    }
  } else if (cmd == "cw") {
    sampleSurfaceCalibration(false);
  } else if (cmd == "cb") {
    sampleSurfaceCalibration(true);
  } else if (cmd == "ca") {
    autoSweepCalibration();
  } else if (cmd == "cr") {
    resetCalibration();
  } else if (cmd.startsWith("b")) {
    int value = cmd.substring(1).toInt();
    if (value >= 0 && value <= PWM_MAX) {
      baseSpeed = value;
      Serial.print("baseSpeed="); Serial.println(baseSpeed);
    }
  } else if (cmd.startsWith("p")) {
    kp = cmd.substring(1).toFloat();
    Serial.print("kp="); Serial.println(kp, 4);
  } else if (cmd.startsWith("i")) {
    ki = cmd.substring(1).toFloat();
    Serial.print("ki="); Serial.println(ki, 4);
  } else if (cmd.startsWith("d")) {
    kd = cmd.substring(1).toFloat();
    Serial.print("kd="); Serial.println(kd, 4);
  } else if (cmd.startsWith("g")) {
    int value = cmd.substring(1).toInt();
    if (value >= 0 && value <= 1000) {
      bridgeLineGapMs = value;
      Serial.print("bridgeLineGapMs="); Serial.println(bridgeLineGapMs);
    }
  } else if (cmd.startsWith("s")) {
    int value = cmd.substring(1).toInt();
    if (value >= 100 && value <= 5000) {
      searchLineGapMs = value;
      Serial.print("searchLineGapMs="); Serial.println(searchLineGapMs);
    }
  } else if (cmd.startsWith("v")) {
    int value = cmd.substring(1).toInt();
    if (value >= 0 && value <= PWM_MAX) {
      bridgeSpeed = value;
      Serial.print("bridgeSpeed="); Serial.println(bridgeSpeed);
    }
  } else if (cmd.startsWith("q")) {
    int value = cmd.substring(1).toInt();
    if (value >= 0 && value <= PWM_MAX) {
      searchSpeed = value;
      Serial.print("searchSpeed="); Serial.println(searchSpeed);
    }
  } else {
    Serial.println("Unknown command");
  }
}

void sampleSurfaceCalibration(bool blackSurface) {
  const int samples = 120;
  long sum[5] = {0, 0, 0, 0, 0};

  motorsEnabled = false;
  stopMotors();

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

  motorsEnabled = false;
  stopMotors();
  Serial.println("Auto calibration: move sensors across black line and white background for 3 seconds");

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

void printDebug(const LineRead &line) {
  unsigned long now = millis();
  if (now - lastPrint < printIntervalMs) {
    return;
  }
  lastPrint = now;

  Serial.print(motorsEnabled ? "RUN " : "STOP ");
  Serial.print("pos="); Serial.print(line.position);
  Serial.print(" active="); Serial.print(line.activeCount);
  Serial.print(" th="); Serial.print(detectThreshold);
  if (motorsEnabled && line.activeCount == 0) {
    unsigned long lostMs = (lastLineSeenMs == 0) ? 0 : millis() - lastLineSeenMs;
    Serial.print(" lostMs="); Serial.print(lostMs);
    if (lostMs <= bridgeLineGapMs) {
      Serial.print(" BRIDGE");
    } else if (lostMs <= searchLineGapMs) {
      Serial.print(" SEARCH");
    } else {
      Serial.print(" STOP_LOST");
    }
  }
  Serial.print(" | ");

  for (int i = 0; i < 5; i++) {
    Serial.print("IN"); Serial.print(i + 1);
    Serial.print(":"); Serial.print(line.raw[i]);
    Serial.print("/"); Serial.print(line.strength[i]);
    Serial.print("("); Serial.print(line.active[i]); Serial.print(") ");
  }

  if (line.activeCount == 0) {
    Serial.print("NO_LINE");
  }
  Serial.println();
}
