# BioSync Smart Pillow

## Project title

**IoT Smart Pillow for Sleep Posture Patterns and Local Sleep-Environment Analysis**

The ESP32 reads three pillow-zone FSRs and nearby temperature/humidity from a DHT22. It classifies pressure distribution, detects pillow-use sessions, and pulses the vibration motor after sustained center-dominant pressure. Sensor data is sent to ThingSpeak for cloud storage, live charts, and MATLAB analysis. This is an educational prototype, not a medical device or sleep-quality diagnostic.

## Hardware and pin map

| Component | ESP32 connection | Purpose |
|---|---:|---|
| Left FSR signal | GPIO 35 | Left-side pressure |
| Middle FSR signal | GPIO 32 | Center pressure |
| Right FSR signal | GPIO 34 | Right-side pressure |
| DHT22 data | GPIO 27 | Temperature and relative humidity |
| Vibration driver input | GPIO 25 | PWM feedback pulses |
| Touch input | GPIO 14 | User marker, alert snooze, or dismissal |

FSRs use their existing voltage-divider circuits. Keep ESP32 input signals within 3.3 V and connect sensor grounds to ESP32 GND. Drive the motor through its transistor/MOSFET driver or motor-driver module, with a supply suitable for the motor and a shared ground. Do not connect a bare motor directly to GPIO 25.

## Complete IoT pipeline

1. **Sense:** the FSRs, DHT22, and touch input collect pressure, local environment, and user interaction.
2. **Ingest:** the ESP32 samples sensors and uploads readings over Wi-Fi/HTTPS to a private ThingSpeak channel. USB serial remains available for debugging.
3. **Process:** firmware classifies pressure and sessions, and activates a pulsed motor alert after five seconds of sustained center-dominant pressure.
4. **Store:** ThingSpeak stores each successful channel update in the cloud.
5. **Analyze:** ThingSpeak's hosted MATLAB Analysis calculates posture-category share and changes, environmental averages, alert starts, and motor-on time. No Python analysis process runs on the PC in the cloud workflow.

## Cloud setup and use

Follow [cloud/ThingSpeak/README.md](cloud/ThingSpeak/README.md) to create the private channel, add Wi-Fi and ThingSpeak credentials locally, install Arduino libraries, upload the cloud sketch, open the field charts, and run the MATLAB Analysis code. The analysis file is [cloud/ThingSpeak/MATLAB_Analysis.m](cloud/ThingSpeak/MATLAB_Analysis.m).

Use `BioSync_Cloud/BioSync_Cloud.ino` for cloud operation. Copy `BioSync_Cloud/secrets.h.example` to `BioSync_Cloud/secrets.h` and fill in your values; `secrets.h` is ignored by Git. The cloud sketch keeps the existing hardware pins. Channel writes occur every 16 seconds to meet ThingSpeak's free-tier write interval. Confirm a response code `200` in Serial Monitor at 115200 baud.

### Cloud channel fields

| Field | Value |
|---:|---|
| 1–3 | Left, middle, and right FSR ADC values |
| 4–5 | Temperature (C) and relative humidity (%) |
| 6 | Posture code: 0 none, 1 left, 2 center, 3 right, 4 distributed |
| 7 | Alert code: 0 off, 1 active, 2 snoozed, 3 dismissed |
| 8 | Cumulative motor-on seconds for the current firmware session |

## Interpretation

- Raw FSR readings depend on resistor values, sensor placement, and pillow compression. Categories are relative prototype labels, not calibrated force measurements.
- Two-second device sampling can miss brief movement; the cloud dashboard receives a subset at the permitted channel upload rate.
- Temperature and humidity describe the DHT22's local surroundings. Motor-on time records firmware commands; direct observation confirms physical motor operation.
- Do not claim sleep-quality or health conclusions. Report actual cloud-collected values after running your own sessions.

## Project files

- `BioSync_Cloud/BioSync_Cloud.ino` — ESP32 cloud firmware
- `BioSync_Cloud/secrets.h.example` — local credential template
- `cloud/ThingSpeak/MATLAB_Analysis.m` — hosted cloud analysis script
- `cloud/ThingSpeak/README.md` — ThingSpeak setup and operation
- `tools/serial_logger.py` and `tools/analyze_sessions.py` — optional offline analysis of serial telemetry
- `tools/serial_logger.py` and `tools/analyze_sessions.py` — optional offline workflow
- `docs/project_report.md` — report and evaluation methodology

The Python tools remain available for offline evaluation only; the primary deliverable pipeline stores and analyzes data in ThingSpeak.
