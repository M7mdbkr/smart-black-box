#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <TinyGPSPlus.h>
#include "FS.h"
#include "SD.h"
#include "SPI.h"
#include <SoftwareSerial.h>

// -----------------------------------------
// User settings (edit here)
// -----------------------------------------
const String PHONE_NUMBER = "+966567672890"; // emergency contact number (international format)
#define ACCIDENT_THRESHOLD 25.0 // impact force (m/s^2) required to count as an accident

// --- Gas/smoke sensor settings (MQ-135) ---
#define MQ135_PIN 34 // analog sensor pin
#define GAS_THRESHOLD 2500 // danger threshold (0 to 4095)
unsigned long lastGasAlert = 0;
#define GAS_ALERT_DELAY 60000 // send at most one fire alert every 60 seconds

// -----------------------------------------
// Pin connections (ESP32)
// -----------------------------------------
#define SD_CS_PIN 5
#define GPS_RX_PIN 16
#define GPS_TX_PIN 17
#define SIM800_RX_PIN 26
#define SIM800_TX_PIN 27
#define OBD_RX_PIN 32 // connects to OBD module's TX
#define OBD_TX_PIN 33 // connects to OBD module's RX

// -----------------------------------------
// Object definitions
// -----------------------------------------
Adafruit_MPU6050 mpu;
TinyGPSPlus gps;
HardwareSerial gpsSerial(2);
HardwareSerial sim800(1);
SoftwareSerial obdSerial(OBD_RX_PIN, OBD_TX_PIN);

// --- Accident-state variables ---
bool accidentDetected = false;
unsigned long normalStartTime = 0;
#define NORMAL_THRESHOLD 12.0
#define NORMAL_TIME 3000

// --- Black-box variables (OBD & gas) ---
long currentRPM = 0;
int currentSpeed = 0;
int currentTemp = 0;
int currentGasLevel = 0;
int obdStep = 0;
unsigned long lastObdUpdate = 0;
unsigned long lastGasUpdate = 0;

// ---------------------------------------------------------
// Simple test SMS (startup only)
// ---------------------------------------------------------
void sendSimpleSMS(String message) {
  Serial.println(">> Sending Simple SMS...");
  sim800.println("AT+CMGF=1");
  delay(200);
  sim800.print("AT+CMGS=\"");
  sim800.print(PHONE_NUMBER);
  sim800.println("\"");
  delay(200);
  // send the message text with no extras
  sim800.print(message);
  delay(200);
  sim800.write(26); // send command (Ctrl+Z)
  delay(3000);
  Serial.println(">> Simple SMS command sent.");
}

// ---------------------------------------------------------
// Full emergency alert SMS (includes all telemetry)
// ---------------------------------------------------------
void sendAlertSMS(String alertType, String date, String time, String lat, String lng) {
  Serial.println(">> Sending Alert SMS: " + alertType);
  sim800.println("AT+CMGF=1");
  delay(200);
  sim800.print("AT+CMGS=\"");
  sim800.print(PHONE_NUMBER);
  sim800.println("\"");
  delay(200);

  // --- full message body for emergencies ---
  sim800.print("ALARM! " + alertType + "\n");
  sim800.print("Date: "); sim800.print(date);
  sim800.print("\nTime: "); sim800.print(time);
  // vehicle data at the time of the event
  sim800.print("\nSpeed: "); sim800.print(currentSpeed); sim800.print("km/h");
  sim800.print("\nRPM: "); sim800.print(currentRPM);
  sim800.print("\nTemp: "); sim800.print(currentTemp); sim800.print("C");
  // gas level printed as a raw number and a percentage
  sim800.print("\nGas/Smoke: ");
  sim800.print(currentGasLevel);
  sim800.print(" (");
  sim800.print(map(currentGasLevel, 0, 4095, 0, 100)); // convert reading to a percentage
  sim800.print("%)");
  sim800.print("\nLoc: ");
  if (lat != "NA" && lng != "NA") {
    sim800.print("https://maps.google.com/?q=");
    sim800.print(lat);
    sim800.print(",");
    sim800.print(lng);
  } else {
    sim800.print("No GPS Signal Yet");
  }
  delay(200);
  sim800.write(26); // send command (Ctrl+Z)
  delay(3000);
  Serial.println(">> Alert SMS command sent.");
}

// ---------------------------------------------------------
// Extract date/time and location from GPS
// ---------------------------------------------------------
void getLocationAndTime(String &dateStr, String &timeStr, String &latStr, String &lngStr,
                         String &linkStr) {
  if (gps.location.isValid()) {
    latStr = String(gps.location.lat(), 6);
    lngStr = String(gps.location.lng(), 6);
    linkStr = "https://maps.google.com/?q=" + latStr + "," + lngStr;
  }
  if (gps.date.isValid() && gps.time.isValid()) {
    char dBuffer[16], tBuffer[16];
    int ksaHour = gps.time.hour() + 3; // convert to Saudi Arabia time (UTC+3)
    if (ksaHour >= 24) ksaHour = ksaHour - 24;
    sprintf(dBuffer, "%04d-%02d-%02d", gps.date.year(), gps.date.month(), gps.date.day());
    sprintf(tBuffer, "%02d:%02d:%02d", ksaHour, gps.time.minute(), gps.time.second());
    dateStr = String(dBuffer);
    timeStr = String(tBuffer);
  }
}

// ---------------------------------------------------------
// Handle an accident event (SD log + SMS)
// ---------------------------------------------------------
void handleAccident(float force) {
  String dateStr = "NA", timeStr = "NA", latStr = "NA", lngStr = "NA", linkStr = "NA";
  getLocationAndTime(dateStr, timeStr, latStr, lngStr, linkStr);

  // build the gas text for the log (number + percentage)
  String gasData = String(currentGasLevel) + " (" + String(map(currentGasLevel, 0, 4095, 0, 100)) + "%)";

  File file = SD.open("/accidents.csv", FILE_APPEND);
  if (file) {
    String logEntry = dateStr + "," + timeStr + ",Accident," + String(force, 2) + "," +
                       String(currentSpeed) + "," + String(currentRPM) + "," + String(currentTemp) + "," +
                       gasData + "," + linkStr + "," + latStr + "," + lngStr + "\r\n";
    file.print(logEntry);
    Serial.println(">> Accident Logged to SD");
    file.close();
  }
  sendAlertSMS("Accident Detected", dateStr, timeStr, latStr, lngStr);
}

// ---------------------------------------------------------
// Handle a gas/fire emergency (SD log + SMS)
// ---------------------------------------------------------
void handleFireEmergency() {
  String dateStr = "NA", timeStr = "NA", latStr = "NA", lngStr = "NA", linkStr = "NA";
  getLocationAndTime(dateStr, timeStr, latStr, lngStr, linkStr);

  // build the gas text for the log (number + percentage)
  String gasData = String(currentGasLevel) + " (" + String(map(currentGasLevel, 0, 4095, 0, 100)) + "%)";

  File file = SD.open("/accidents.csv", FILE_APPEND);
  if (file) {
    String logEntry = dateStr + "," + timeStr + ",FIRE/GAS,0.0," +
                       String(currentSpeed) + "," + String(currentRPM) + "," + String(currentTemp) + "," +
                       gasData + "," + linkStr + "," + latStr + "," + lngStr + "\r\n";
    file.print(logEntry);
    Serial.println(">> Fire Emergency Logged to SD");
    file.close();
  }
  sendAlertSMS("FIRE/SMOKE DETECTED!", dateStr, timeStr, latStr, lngStr);
}

// ---------------------------------------------------------
// OBD-II read functions, round-robin (speed / RPM / temp)
// ---------------------------------------------------------
void flushObd() {
  while (obdSerial.available()) obdSerial.read();
}

String readObdResponse() {
  String buf = "";
  long startTime = millis();
  while (millis() - startTime < 150) {
    if (obdSerial.available()) {
      char c = obdSerial.read();
      if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F')) buf += c;
    }
  }
  return buf;
}

void updateOBD() {
  if (millis() - lastObdUpdate < 300) return;
  flushObd();

  if (obdStep == 0) {
    obdSerial.print("010D\r");
    String r = readObdResponse();
    int idx = r.indexOf("410D");
    if (idx != -1 && r.length() >= idx + 6) {
      currentSpeed = strtol(r.substring(idx + 4, idx + 6).c_str(), NULL, 16);
    }
  }
  else if (obdStep == 1) {
    obdSerial.print("010C\r");
    String r = readObdResponse();
    int idx = r.indexOf("410C");
    if (idx != -1 && r.length() >= idx + 8) {
      long A = strtol(r.substring(idx + 4, idx + 6).c_str(), NULL, 16);
      long B = strtol(r.substring(idx + 6, idx + 8).c_str(), NULL, 16);
      currentRPM = ((A * 256) + B) / 4;
    }
  }
  else if (obdStep == 2) {
    obdSerial.print("0105\r");
    String r = readObdResponse();
    int idx = r.indexOf("4105");
    if (idx != -1 && r.length() >= idx + 6) {
      currentTemp = strtol(r.substring(idx + 4, idx + 6).c_str(), NULL, 16) - 40;
    }
  }

  obdStep++;
  if (obdStep > 2) obdStep = 0;
  lastObdUpdate = millis();
}

// ---------------------------------------------------------
// Setup
// ---------------------------------------------------------
void setup() {
  Serial.begin(115200);

  // 1. gas sensor setup
  pinMode(MQ135_PIN, INPUT);

  // 2. SD card setup
  if (!SD.begin(SD_CS_PIN)) {
    Serial.println("SD Card Failed!");
  } else {
    Serial.println("SD Card Ready.");
    if (!SD.exists("/accidents.csv")) {
      File file = SD.open("/accidents.csv", FILE_WRITE);
      if (file) {
        file.print("Date,Time,Event_Type,Force(G),Speed(kmh),RPM,Temp(C),Gas_Level,Google_Link,Latitude,Longitude\r\n");
        file.close();
      }
    }
  }

  // 3. MPU accelerometer setup
  Wire.begin(21, 22);
  if (!mpu.begin()) {
    Serial.println("MPU6050 not found!");
    while (1);
  }
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);

  // 4. communications setup
  gpsSerial.begin(9600, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  sim800.begin(9600, SERIAL_8N1, SIM800_RX_PIN, SIM800_TX_PIN);
  obdSerial.begin(9600);
  delay(1000);

  // 5. initialize the OBD-II adapter
  obdSerial.print("ATZ\r"); delay(500);
  obdSerial.print("ATE0\r"); delay(200);
  obdSerial.print("ATS0\r"); delay(200);
  obdSerial.print("ATSP0\r"); delay(500);

  // 6. initialize the SIM800L module
  sim800.println("AT"); delay(500);
  sim800.println("AT+CMGF=1"); delay(500);

  Serial.println("System Ready & Monitoring...");

  // 7. send the startup confirmation SMS immediately
  sendSimpleSMS("System Powered ON. Ready & Monitoring.");
}

// ---------------------------------------------------------
// Main loop
// ---------------------------------------------------------
void loop() {
  // 1. continuously read GPS to keep the location updated
  while (gpsSerial.available()) {
    gps.encode(gpsSerial.read());
  }

  // 2. update vehicle data (OBD)
  updateOBD();

  // 3. read the gas/smoke sensor (MQ-135)
  if (millis() - lastGasUpdate > 500) {
    currentGasLevel = analogRead(MQ135_PIN);
    if (currentGasLevel > GAS_THRESHOLD) {
      if (millis() - lastGasAlert > GAS_ALERT_DELAY || lastGasAlert == 0) {
        Serial.println("!!! FIRE/GAS DETECTED !!!");
        handleFireEmergency();
        lastGasAlert = millis();
      }
    }
    lastGasUpdate = millis();
  }

  // 4. read acceleration data (MPU6050)
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);
  float accel = sqrt(
    a.acceleration.x * a.acceleration.x +
    a.acceleration.y * a.acceleration.y +
    a.acceleration.z * a.acceleration.z
  );

  // 5. crash-detection logic
  if (accel > ACCIDENT_THRESHOLD && !accidentDetected) {
    accidentDetected = true;
    normalStartTime = 0;
    Serial.println("!!! ACCIDENT DETECTED !!!");
    handleAccident(accel);
  }

  // 6. reset accident state once things return to normal
  if (accidentDetected && accel < NORMAL_THRESHOLD) {
    if (normalStartTime == 0) normalStartTime = millis();
    if (millis() - normalStartTime >= NORMAL_TIME) {
      accidentDetected = false;
      normalStartTime = 0;
      Serial.println("System Reset. Ready for new detection.");
    }
  } else {
    if (accidentDetected) normalStartTime = 0;
  }

  // 7. print SIM800L responses
  if (sim800.available()) {
    Serial.write(sim800.read());
  }

  delay(10);
}
