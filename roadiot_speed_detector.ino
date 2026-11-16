#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ============================================================================
// 1. CALIBRATION & USER CONFIGURATION
// ============================================================================
// Measure the exact center-to-center distance between IR Sensor 1 and IR Sensor 2
const float SENSOR_DISTANCE_CM = 20.0;   // Default: 20.0 cm

// Overspeed alert threshold in cm/s (50.0 cm/s = 1.80 km/h)
// Tip: At 20 cm spacing, crossing in < 0.40 s triggers OVERSPEED
const float SPEED_LIMIT_CM_S   = 50.0;

// Detection timing bounds (calibrates how fast/slow a car can be detected)
// - MIN_TRAVEL_US: Minimum valid travel time between sensors.
//   15000 us (15 ms) allows speeds up to 1333 cm/s (48 km/h) across 20 cm,
//   while filtering out electrical noise / IR bounce.
// - TIMING_TIMEOUT_MS: Max wait time for Sensor 2 after Sensor 1 triggers.
//   3000 ms (3.0 s) allows slow cars down to ~6.7 cm/s before resetting.
const unsigned long MIN_TRAVEL_US           = 15000UL; // 15 ms
const unsigned long TIMING_TIMEOUT_MS       = 3000UL;  // 3.0 seconds
const unsigned long RESULT_HOLD_MS          = 1500UL;  // Hold speed/alert 1.5 s
const unsigned long SENSOR_CLEAR_TIMEOUT_MS = 2000UL;  // Avoid hang if sensor blocked

// Most LM393 IR obstacle modules output LOW when a car is detected.
// Change to HIGH if your sensor module outputs HIGH on detection.
const int IR_TRIGGER_STATE = LOW;

// Set to true for a passive buzzer (uses tone()), false for an active buzzer.
const bool BUZZER_IS_PASSIVE = true;

// ============================================================================
// 2. PIN DEFINITIONS (Matches Circuit Table)
// ============================================================================
const uint8_t IR_START_PIN  = 2;   // D2 (INT0) - IR Sensor 1 OUT
const uint8_t IR_STOP_PIN   = 3;   // D3 (INT1) - IR Sensor 2 OUT
const uint8_t RED_LED_PIN   = 8;   // D8        - Overspeed Red LED (via 220 ohm)
const uint8_t GREEN_LED_PIN = 9;   // D9        - Normal Green LED (via 220 ohm)
const uint8_t BUZZER_PIN    = 10;  // D10       - Buzzer (+)

// ============================================================================
// 3. OLED DISPLAY SETUP (GME12864-4 128x64 I2C SSD1306)
// ============================================================================
#define SCREEN_WIDTH   128
#define SCREEN_HEIGHT  64
#define OLED_RESET     -1

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ============================================================================
// 4. STATE MACHINE & INTERRUPT DATA
// ============================================================================
enum TrapState : uint8_t {
  STATE_READY    = 0,
  STATE_TIMING   = 1,
  STATE_CAPTURED = 2,
  STATE_COOLDOWN = 3
};

volatile uint8_t trapState = STATE_READY;
volatile unsigned long startTimeUs = 0;
volatile unsigned long stopTimeUs  = 0;

bool timingScreenDrawn = false;
bool oledAvailable     = false;
uint8_t oledAddress    = 0;

// ============================================================================
// INTERRUPT SERVICE ROUTINES (ISRs)
// ============================================================================
void onStartSensor() {
  if (trapState == STATE_READY) {
    startTimeUs = micros();
    trapState = STATE_TIMING;
  }
}

void onStopSensor() {
  if (trapState == STATE_TIMING) {
    unsigned long now = micros();
    if ((now - startTimeUs) >= MIN_TRAVEL_US) {
      stopTimeUs = now;
      trapState = STATE_CAPTURED;
    }
  }
}

// ============================================================================
// I2C & OLED INITIALIZATION + DISPLAY HELPERS
// ============================================================================
bool initOledDisplay() {
  Wire.begin();
  Wire.setClock(100000UL); // 100 kHz standard mode is most reliable on breadboards
#ifdef WIRE_HAS_TIMEOUT
  Wire.setWireTimeout(3000, true); // Prevent lockup if SDA/SCL wire is loose
#endif

  // Scan I2C bus to diagnose OLED wiring and auto-detect 0x3C vs 0x3D
  Serial.println(F("Scanning I2C bus for GME12864-4 OLED..."));
  uint8_t foundCount = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    uint8_t err = Wire.endTransmission();
    if (err == 0) {
      Serial.print(F("  -> Found I2C device at 0x"));
      if (addr < 16) Serial.print('0');
      Serial.println(addr, HEX);
      foundCount++;
      if (addr == 0x3C || addr == 0x3D) {
        oledAddress = addr;
      }
    }
  }

  if (foundCount == 0) {
    Serial.println(F("ERROR: No I2C device detected!"));
    Serial.println(F("  Check OLED wiring: SDA->A4, SCL->A5, VCC->5V, GND->GND."));
    Serial.println(F("  Also verify VCC/GND pin order printed on your GME12864-4 board."));
    return false;
  }

  // Fallback to 0x3C if another non-standard address was found
  uint8_t candidates[2] = { (uint8_t)(oledAddress ? oledAddress : 0x3C), 0x3D };
  for (uint8_t i = 0; i < 2; i++) {
    uint8_t addr = candidates[i];
    if (display.begin(SSD1306_SWITCHCAPVCC, addr)) {
      oledAddress = addr;
      Serial.print(F("OLED initialized successfully at 0x"));
      Serial.println(oledAddress, HEX);
      return true;
    }
  }

  Serial.println(F("ERROR: OLED begin() failed (try SH1106 driver if 0x3C/0x3D found)."));
  return false;
}

void showBootScreen() {
  if (!oledAvailable) return;
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(16, 14);
  display.println(F("ROAD-IOT"));
  display.setTextSize(1);
  display.setCursor(20, 40);
  display.println(F("Speed Trap Booting"));
  display.display();
  delay(700);
}

void showReadyScreen() {
  if (!oledAvailable) return;
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(F("ROAD-IOT SPEED TRAP"));
  display.drawLine(0, 11, 127, 11, SSD1306_WHITE);

  display.setTextSize(2);
  display.setCursor(28, 20);
  display.println(F("READY"));

  display.setTextSize(1);
  display.setCursor(0, 46);
  display.print(F("Dist: "));
  display.print(SENSOR_DISTANCE_CM, 0);
  display.print(F("cm Lim: "));
  display.print(SPEED_LIMIT_CM_S, 0);
  display.display();
}

void showMeasuringScreen() {
  if (!oledAvailable) return;
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(F("SENSOR 1 TRIGGERED"));
  display.drawLine(0, 11, 127, 11, SSD1306_WHITE);

  display.setTextSize(2);
  display.setCursor(6, 26);
  display.println(F("TIMING..."));
  display.display();
}

void showTimeoutScreen() {
  if (!oledAvailable) return;
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(2);
  display.setCursor(16, 16);
  display.println(F("TIMEOUT"));

  display.setTextSize(1);
  display.setCursor(14, 42);
  display.println(F("Resetting trap..."));
  display.display();
}

void showSpeedScreen(float speedCmS, float speedKmH, float elapsedSec, bool isOverspeed) {
  if (!oledAvailable) return;
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  if (isOverspeed) {
    display.fillRect(0, 0, 128, 13, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
    display.setTextSize(1);
    display.setCursor(16, 3);
    display.print(F("!! OVERSPEED !!"));
    display.setTextColor(SSD1306_WHITE);
  } else {
    display.setTextSize(1);
    display.setCursor(24, 2);
    display.print(F("SPEED NORMAL"));
    display.drawLine(0, 12, 127, 12, SSD1306_WHITE);
  }

  // Primary readout in cm/s
  display.setTextSize(2);
  display.setCursor(2, 20);
  display.print(speedCmS, 1);
  display.setTextSize(1);
  display.print(F(" cm/s"));

  // Secondary readout in km/h + travel time
  display.setTextSize(1);
  display.setCursor(2, 46);
  display.print(speedKmH, 2);
  display.print(F(" km/h  t="));
  display.print(elapsedSec, 2);
  display.print(F("s"));
  display.display();
}

// ============================================================================
// ALERT & BUZZER HELPERS
// ============================================================================
void setBuzzer(bool on, uint16_t freqHz = 2000) {
  if (BUZZER_IS_PASSIVE) {
    if (on) {
      tone(BUZZER_PIN, freqHz);
    } else {
      noTone(BUZZER_PIN);
    }
  } else {
    digitalWrite(BUZZER_PIN, on ? HIGH : LOW);
  }
}

void runOverspeedAlert(unsigned long durationMs) {
  digitalWrite(GREEN_LED_PIN, LOW);
  unsigned long alertStart = millis();

  while (millis() - alertStart < durationMs) {
    digitalWrite(RED_LED_PIN, HIGH);
    setBuzzer(true, 2200);
    delay(150);

    digitalWrite(RED_LED_PIN, LOW);
    setBuzzer(false);
    delay(100);
  }

  digitalWrite(RED_LED_PIN, LOW);
  setBuzzer(false);
}

void runNormalAlert(unsigned long durationMs) {
  digitalWrite(RED_LED_PIN, LOW);
  setBuzzer(false);
  digitalWrite(GREEN_LED_PIN, HIGH);
  delay(durationMs);
  digitalWrite(GREEN_LED_PIN, LOW);
}

void waitForSensorsClear() {
  unsigned long waitStart = millis();
  bool warned = false;

  while (digitalRead(IR_START_PIN) == IR_TRIGGER_STATE ||
         digitalRead(IR_STOP_PIN) == IR_TRIGGER_STATE) {
    if (!warned && (millis() - waitStart >= SENSOR_CLEAR_TIMEOUT_MS)) {
      Serial.println(F("WARN: An IR sensor is still blocked! Adjust sensor potentiometer if stuck."));
      warned = true;
    }
    delay(20);
  }
  delay(80); // Short settle delay before re-arming
}

void printSensorCalibrationStatus() {
  int s1 = digitalRead(IR_START_PIN);
  int s2 = digitalRead(IR_STOP_PIN);
  Serial.print(F("IR1 (D2): "));
  Serial.print(s1 == IR_TRIGGER_STATE ? F("BLOCKED") : F("CLEAR"));
  Serial.print(F(" | IR2 (D3): "));
  Serial.println(s2 == IR_TRIGGER_STATE ? F("BLOCKED") : F("CLEAR"));
}

// ============================================================================
// ARDUINO SETUP & MAIN LOOP
// ============================================================================
void setup() {
  // 9600 baud matches Arduino IDE Serial Monitor's default baud rate
  Serial.begin(9600);
  delay(200);

  Serial.println();
  Serial.println(F("========================================"));
  Serial.println(F("   ROAD-IOT TOY CAR SPEED DETECTOR"));
  Serial.println(F("========================================"));

  pinMode(IR_START_PIN, INPUT);
  pinMode(IR_STOP_PIN, INPUT);
  pinMode(RED_LED_PIN, OUTPUT);
  pinMode(GREEN_LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  digitalWrite(RED_LED_PIN, LOW);
  digitalWrite(GREEN_LED_PIN, LOW);
  setBuzzer(false);

  oledAvailable = initOledDisplay();
  if (oledAvailable) {
    showBootScreen();
    showReadyScreen();
  }

  printSensorCalibrationStatus();
  Serial.print(F("Calibration -> Distance: "));
  Serial.print(SENSOR_DISTANCE_CM, 1);
  Serial.print(F(" cm | Limit: "));
  Serial.print(SPEED_LIMIT_CM_S, 1);
  Serial.println(F(" cm/s"));

  // Clear any pending hardware interrupt flags before attaching ISRs
#if defined(__AVR_ATmega328P__)
  EIFR = (1 << INTF0) | (1 << INTF1);
#endif

  int interruptEdge = (IR_TRIGGER_STATE == LOW) ? FALLING : RISING;
  attachInterrupt(digitalPinToInterrupt(IR_START_PIN), onStartSensor, interruptEdge);
  attachInterrupt(digitalPinToInterrupt(IR_STOP_PIN), onStopSensor, interruptEdge);

  Serial.println(F("Trap READY. Roll toy car past Sensor 1 -> Sensor 2."));
  Serial.println();
}

void loop() {
  uint8_t currentState;
  noInterrupts();
  currentState = trapState;
  interrupts();

  if (currentState == STATE_TIMING) {
    if (!timingScreenDrawn) {
      Serial.println(F("[IR1] Car detected -> Timer started..."));
      showMeasuringScreen();
      timingScreenDrawn = true;
    }

    noInterrupts();
    unsigned long tStart = startTimeUs;
    interrupts();

    if ((micros() - tStart) >= (TIMING_TIMEOUT_MS * 1000UL)) {
      noInterrupts();
      trapState = STATE_COOLDOWN;
      interrupts();

      Serial.println(F("[TIMEOUT] Car did not reach Sensor 2 in time. Resetting."));
      showTimeoutScreen();
      delay(1000);
      waitForSensorsClear();

      timingScreenDrawn = false;
      showReadyScreen();

#if defined(__AVR_ATmega328P__)
      EIFR = (1 << INTF0) | (1 << INTF1);
#endif
      noInterrupts();
      trapState = STATE_READY;
      interrupts();
    }
  } else if (currentState == STATE_CAPTURED) {
    unsigned long tStart, tStop;
    noInterrupts();
    tStart = startTimeUs;
    tStop  = stopTimeUs;
    trapState = STATE_COOLDOWN;
    interrupts();

    float elapsedSeconds = (float)(tStop - tStart) / 1000000.0f;
    float speedCmS = SENSOR_DISTANCE_CM / elapsedSeconds;
    float speedMS  = speedCmS / 100.0f;
    float speedKmH = speedMS * 3.6f;
    bool isOverspeed = (speedCmS > SPEED_LIMIT_CM_S);

    // Formatted Serial Monitor output: Speed + Alert message
    Serial.println(F("----------------------------------------"));
    Serial.print(F("Travel Time : "));
    Serial.print(elapsedSeconds, 3);
    Serial.println(F(" s"));

    Serial.print(F("Speed       : "));
    Serial.print(speedCmS, 1);
    Serial.print(F(" cm/s  ("));
    Serial.print(speedKmH, 2);
    Serial.println(F(" km/h)"));

    Serial.print(F("Speed Limit : "));
    Serial.print(SPEED_LIMIT_CM_S, 1);
    Serial.println(F(" cm/s"));

    if (isOverspeed) {
      Serial.println(F("Alert       : [OVERSPEED ALERT!] Red LED & Buzzer ON!"));
    } else {
      Serial.println(F("Alert       : [NORMAL] Speed within limit."));
    }
    Serial.println(F("----------------------------------------"));

    // Update GME12864-4 OLED display with speed & alert status
    showSpeedScreen(speedCmS, speedKmH, elapsedSeconds, isOverspeed);

    if (isOverspeed) {
      runOverspeedAlert(RESULT_HOLD_MS);
    } else {
      runNormalAlert(RESULT_HOLD_MS);
    }

    waitForSensorsClear();
    timingScreenDrawn = false;
    showReadyScreen();

#if defined(__AVR_ATmega328P__)
    EIFR = (1 << INTF0) | (1 << INTF1);
#endif
    noInterrupts();
    trapState = STATE_READY;
    interrupts();
  }
}
