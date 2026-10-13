/*
 * Low-Cost IoT Health Monitoring System
 * Board : Arduino UNO
 * Sensors: Pulse Sensor (analog) + DS18B20 (1-Wire temperature)
 * Display: 16x2 LCD with I2C backpack (PCF8574)
 * Output : Onboard LED blinks with each detected heartbeat
 *
 * Libraries (Arduino IDE > Library Manager):
 *   - PulseSensor Playground
 *   - OneWire
 *   - DallasTemperature
 *   - LiquidCrystal I2C (Frank de Brabander)
 *
 * Note: PulseSensor Playground uses Timer2 on the UNO, so avoid PWM on pins 3 and 11.
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#define USE_ARDUINO_INTERRUPTS true
#include <PulseSensorPlayground.h>

// ---------- Pin configuration ----------
const int PULSE_PIN    = A0;   // Pulse sensor signal
const int LED_PIN      = 13;   // Heartbeat indicator LED (onboard)
const int ONE_WIRE_PIN = 2;    // DS18B20 data (needs 4.7k pull-up to 5V)

// ---------- Tuning ----------
const int PULSE_THRESHOLD = 550;            // Signal level that counts as a beat; tune per person/sensor
const unsigned long TEMP_INTERVAL = 1000;   // Read temperature every 1 s
const unsigned long LCD_INTERVAL  = 500;    // Refresh display every 0.5 s
const unsigned long BEAT_TIMEOUT  = 3000;   // No beat for 3 s -> assume finger removed
const int MIN_BPM = 40;                     // Ignore readings outside a sane range
const int MAX_BPM = 200;
const int AVG_SAMPLES = 4;                  // Average the last 4 beats to steady the display

// ---------- Objects ----------
LiquidCrystal_I2C lcd(0x27, 16, 2);         // Try 0x3F if the display stays blank
OneWire oneWire(ONE_WIRE_PIN);
DallasTemperature tempSensor(&oneWire);
PulseSensorPlayground pulseSensor;

// ---------- State ----------
int bpm = 0;
int bpmHistory[AVG_SAMPLES];
int bpmCount = 0;
int bpmIndex = 0;
unsigned long lastBeatTime = 0;

float tempC = 0.0;
bool tempValid = false;
unsigned long lastTempRead = 0;
unsigned long lastLcdUpdate = 0;

void setup() {
  Serial.begin(9600);

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Health Monitor");
  lcd.setCursor(0, 1);
  lcd.print("Starting...");

  tempSensor.begin();
  tempSensor.setWaitForConversion(false);  // Non-blocking so beat detection isn't delayed
  tempSensor.requestTemperatures();

  pulseSensor.analogInput(PULSE_PIN);
  pulseSensor.blinkOnPulse(LED_PIN);       // LED flashes in sync with each heartbeat
  pulseSensor.setThreshold(PULSE_THRESHOLD);

  if (!pulseSensor.begin()) {
    lcd.clear();
    lcd.print("Pulse sensor");
    lcd.setCursor(0, 1);
    lcd.print("init failed!");
    while (true) { }                        // Halt: nothing useful to do
  }

  delay(1500);
  lcd.clear();
}

void loop() {
  unsigned long now = millis();

  // --- Heart rate: update only when a new beat is detected ---
  if (pulseSensor.sawStartOfBeat()) {
    int reading = pulseSensor.getBeatsPerMinute();
    if (reading >= MIN_BPM && reading <= MAX_BPM) {
      bpmHistory[bpmIndex] = reading;
      bpmIndex = (bpmIndex + 1) % AVG_SAMPLES;
      if (bpmCount < AVG_SAMPLES) bpmCount++;

      long sum = 0;
      for (int i = 0; i < bpmCount; i++) sum += bpmHistory[i];
      bpm = sum / bpmCount;
      lastBeatTime = now;
    }
  }

  // --- If no beat for a while, clear the old value instead of showing it forever ---
  if (bpm > 0 && now - lastBeatTime > BEAT_TIMEOUT) {
    bpm = 0;
    bpmCount = 0;
    bpmIndex = 0;
  }

  // --- Temperature: read result, then request the next conversion ---
  if (now - lastTempRead >= TEMP_INTERVAL) {
    lastTempRead = now;
    float t = tempSensor.getTempCByIndex(0);
    // -127 = disconnected, 85.0 = sensor's power-on default (no real reading yet)
    if (t != DEVICE_DISCONNECTED_C && t != 85.0 && t > -55.0 && t < 125.0) {
      tempC = t;
      tempValid = true;
    } else {
      tempValid = false;
    }
    tempSensor.requestTemperatures();
  }

  // --- Display + serial output ---
  if (now - lastLcdUpdate >= LCD_INTERVAL) {
    lastLcdUpdate = now;

    lcd.setCursor(0, 0);
    if (bpm > 0) {
      lcd.print("BPM: ");
      lcd.print(bpm);
      lcd.print("      ");                  // Clear leftover characters
    } else {
      lcd.print("BPM: -- no pulse");        // Exactly 16 characters
    }

    lcd.setCursor(0, 1);
    lcd.print("Temp: ");
    if (tempValid) { lcd.print(tempC, 1); lcd.print((char)223); lcd.print("C"); }
    else           { lcd.print("ERR"); }
    lcd.print("    ");

    Serial.print("BPM: ");
    if (bpm > 0) Serial.print(bpm); else Serial.print("--");
    Serial.print(" | Temp: ");
    if (tempValid) Serial.println(tempC, 1); else Serial.println("sensor error");
  }
}
