# Smart Black Box

**Advanced Vehicle Accident Detection & Data Logging System**

Senior graduation project — Computer Engineering, College of Computers and Information Technology, Taif University (Fall 2026). Supervised by Prof. Dr. Hatim Zaini.

An OBD-II plug-and-play telematics unit that continuously monitors a vehicle, detects accidents and gas/fire emergencies in real time, logs every event to an SD card, and sends an emergency SMS with GPS location the moment something happens.

## Why

Standard vehicles don't keep an objective, tamper-resistant record of what happened in a crash — no reliable data on speed, impact force, location, or cabin conditions. That makes crash reconstruction and liability determination slow and disputed. Saudi Arabia's SASO regulation also requires every new vehicle sold in the Kingdom to include an automatic SOS system by January 2027. Smart Black Box is a low-cost, locally engineered system built to meet that deadline and close the data gap, in line with Vision 2030's road-safety goals.

## What it does

- **Crash detection** — reads the MPU6050 accelerometer continuously; a impact above a configurable G-force threshold triggers accident handling.
- **Gas / fire detection** — monitors an MQ-135 sensor for smoke and dangerous gas levels, with its own alert path and cooldown.
- **Emergency SMS alerts** — on either event, sends an SMS via SIM800L (GSM) with the event type, timestamp, vehicle speed/RPM/temperature, gas reading, and a Google Maps link to the GPS location.
- **Vehicle telemetry over OBD-II** — polls the car's ECU (via a SparkFun OBD-II UART interface) for speed, RPM, and coolant temperature.
- **Local data logging** — every accident or emergency event is appended to `/accidents.csv` on a microSD card, so nothing is lost even without a cellular connection.
- **GPS positioning** — NEO-6M module provides location, date, and time (converted to Saudi local time) for every logged event.

## Hardware

| Component | Role |
|---|---|
| ESP32 | Main controller — runs all sensing, detection, and logging logic |
| MPU6050 | Accelerometer/gyroscope — impact and rollover detection |
| MQ-135 | Gas/smoke sensor — fire and air-quality hazard detection |
| NEO-6M GPS | Location, date, and time |
| SparkFun OBD-II UART | Reads vehicle speed, RPM, and engine temperature from the ECU |
| SIM800L | GSM module — sends emergency SMS alerts |
| MicroSD card module | Local event logging (`/accidents.csv`) |
| 2x 18650 Li-ion + LM2596 | Power supply, regulated to 5V/3.3V |

**Wiring summary** (see `Final.ino` for exact pins):
- MPU6050 — I2C (SDA=GPIO21, SCL=GPIO22)
- GPS — UART2 (RX=GPIO16, TX=GPIO17), 9600 baud
- SIM800L — UART1 (RX=GPIO26, TX=GPIO27), 9600 baud
- OBD-II — SoftwareSerial (RX=GPIO32, TX=GPIO33), 9600 baud
- MQ-135 — analog, GPIO34
- SD card — SPI, CS=GPIO5

## How it was built

Built over a 9-week schedule: procurement and environment setup, GPS integration, crash detection tuning, gas sensing + SD logging, emergency SMS, OBD-II integration, full assembly into an ABS enclosure, end-to-end testing, then the final report and demonstration. The firmware (`Final.ino`, ~355 lines) integrates every subsystem into a single non-blocking `loop()` built around `millis()` timing.

## Repository contents

- `Final.ino` — the complete firmware, as built and tested on the prototype.

The full graduation report (system design, validation results, cost/impact analysis, and the accompanying cloud device-management platform design) lives outside this repo as the official university submission.

## Team

Project lead: Mohammad Jamal Baker. Built with a 14-member Computer Engineering team at Taif University:
Amjad Alsauwat, Majed Alnefaie, Mukbel Al-subaie, Mohammed Alshmrani, Abdulrahman Fattah, Khaled Alghraybi, Abed Almalki, Feras Alobide, Abdulmajeed Almalki, Abdullah Alhomaidi, Mutaz Althomali, Nawaf Al zaidi, Abdulaziz Alqarni.

Supervised by Prof. Dr. Hatim Zaini, Department of Computer Engineering, Taif University.
