
// Motor speeds chronoed on a fully charged 3s LiHV battery and 1g Worker HE darts
const int motorMin = 1350;  // Motor Minimum speed 1350 = ~100fps
const int motorMid = 1525;  // Motor Medium speed  1525 = ~130fps
const int motorMax = 2000;  // Motor Maximum speed 2000 = >200fps
const int preRev = 1040;    // Motor Pre-rev speed

int solenoidOn = 55;            // Solenoid  On Delay, default 55ms
int solenoidOff = 55;           // Solenoid  Off Delay, default 55ms
const int solenoidDelay = 240;  // Delay before firing solenoid as motors spool up from cold
const int delayReduction = 70;  // How many ms is taken off the solenoidDelay when Pre-rev mode is active

// Libraries
#include <Servo.h>

// Switches
#define REV D1
#define TRIGGER D2
#define SELECT_1 D3
#define SELECT_2 D4
#define REV_1 D5
#define REV_2 D6

//Trigger and burst states
int triggerState = LOW;
int lastTriggerState = HIGH;
int triggerReading;
int fireDelay;
int triggerDelay;
unsigned long debounceTime = 0;  // Last time the output pin was toggled
unsigned long debounce = 200UL;  // Debounce time

// Solenoid
#define MOSFET D0

// ESC controls
Servo ESC1;
Servo ESC2;
Servo ESC3;
Servo ESC4;

// ESC values
int escSpeed;
int escLow = 1000;
int escRevdown;

void setup() {
  pinMode(MOSFET, OUTPUT);
  pinMode(REV, INPUT_PULLUP);
  pinMode(TRIGGER, INPUT_PULLUP);
  pinMode(SELECT_1, INPUT_PULLUP);
  pinMode(SELECT_2, INPUT_PULLUP);
  pinMode(REV_1, INPUT_PULLUP);
  pinMode(REV_2, INPUT_PULLUP);
  ESC1.attach(D7, 900, motorMax);
  ESC2.attach(D8, 900, motorMax);
  ESC3.attach(D9, 900, motorMax);
  ESC4.attach(D10, 900, motorMax);
  ESC1.write(1000);
  ESC2.write(1000);
  ESC3.write(1000);
  ESC4.write(1000);
  delay(3000);
  fireDelay = solenoidDelay;
  Serial.begin(9600);
}
// Semi auto
void semiAuto() {
  triggerState = digitalRead(TRIGGER);
  if (triggerState != lastTriggerState) {
    if ((triggerState == LOW)) {
      digitalWrite(MOSFET, HIGH);
      delay(solenoidOn);
      digitalWrite(MOSFET, LOW);
    } else {
      digitalWrite(MOSFET, LOW);
    }
    delay(20);
    lastTriggerState = triggerState;
  }
}
// Full auto
void fullAuto() {
  if (digitalRead(TRIGGER) == LOW) {
    digitalWrite(MOSFET, HIGH);
    delay(solenoidOn);
    digitalWrite(MOSFET, LOW);
    delay(solenoidOff);
    if (digitalRead(TRIGGER) == HIGH) {
      digitalWrite(MOSFET, LOW);
    }
  }
}
// Rev flywheels
void revUp() {
  while (digitalRead(REV) == LOW) {  // Rev trigger pressed
    revMode();
    ESC1.write(escSpeed);
    ESC2.write(escSpeed);
    ESC3.write(escSpeed);
    ESC4.write(escSpeed);
    delay(triggerDelay);  // Do not fire until solenoid delay has elapsed
    selectFire();
    triggerDelay = 0;
    if (digitalRead(REV) == HIGH) {  // Rev trigger released
      revDown();
    }
  }
}
// Power down flywheels
void revDown() {
  digitalWrite(MOSFET, LOW);
  for (escRevdown = escSpeed; escRevdown >= escLow; escRevdown -= 12) {  // Gradually rev down motors
    ESC1.write(escRevdown);
    ESC2.write(escRevdown);
    ESC3.write(escRevdown);
    ESC4.write(escRevdown);
    if (digitalRead(REV) == LOW) {  // Rev trigger pressed
      revUp();
    }
    delay(20);
  }
}
// Rev speed control
void revMode() {
  //Check Select Rev Switch
  if (digitalRead(REV_1) == HIGH && digitalRead(REV_2) == LOW) {  // Low Rev
    escSpeed = motorMin;
  } else if (digitalRead(REV_1) == HIGH && digitalRead(REV_2) == HIGH) {  // Med Rev
    escSpeed = motorMid;
  } else if (digitalRead(REV_1) == LOW && digitalRead(REV_2) == HIGH) {  // Max Rev
    escSpeed = motorMax;
  }
}
// Check select fire switch
void selectFire() {
  if (digitalRead(SELECT_1) == HIGH && digitalRead(SELECT_2) == LOW) {  // Full Auto
    fullAuto();
  } else if (digitalRead(SELECT_1) == HIGH && digitalRead(SELECT_2) == HIGH) {  // Semi Auto
    semiAuto();
  }
}
// Pre-rev mode, toggle trigger switch
void idleMode() {
  triggerReading = digitalRead(TRIGGER);
  if (triggerReading == LOW && lastTriggerState == HIGH && millis() - debounceTime > debounce) {  // Trigger pressed after debounce time
    if (escLow == preRev) {
      escLow = 1000;
      fireDelay = solenoidDelay;
    } else {
      escLow = preRev;
      fireDelay = solenoidDelay - delayReduction;
    }
    debounceTime = millis();
  }
  lastTriggerState = triggerReading;
}
void loop() {
  if (digitalRead(SELECT_1) == LOW) {  // Safety On
    idleMode();
  }
  if (digitalRead(SELECT_1) == !LOW) {  // Safety Off
    triggerDelay = fireDelay;
    revUp();
  }
  ESC1.write(escLow);
  ESC2.write(escLow);
  ESC3.write(escLow);
  ESC4.write(escLow);
  digitalWrite(MOSFET, LOW);
}