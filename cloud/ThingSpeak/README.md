# ThingSpeak cloud dashboard and analysis

This is the primary cloud workflow for BioSync. The ESP32 sends up to eight fields over HTTPS to a private ThingSpeak channel. ThingSpeak stores the readings, charts them, and runs the MATLAB Analysis code in its hosted service. The laptop is only needed to upload firmware and view/configure the channel; it does not run the analysis.

## 1. Create the channel

Create a ThingSpeak account and a **private** channel. Enable and name these fields in order:

1. `FSR Left (ADC)`
2. `FSR Middle (ADC)`
3. `FSR Right (ADC)`
4. `Temperature (C)`
5. `Humidity (%)`
6. `Posture Code` (0 none, 1 left, 2 center, 3 right, 4 distributed)
7. `Alert Code` (0 off, 1 active, 2 snoozed, 3 dismissed)
8. `Motor On Seconds` (cumulative for the current firmware session)

Keep the channel private. The channel status text also carries session number, state, posture, alert and latest event. Use the channel’s built-in field charts as the dashboard; each enabled field appears as a chart on the channel view. ThingSpeak supports up to eight fields per channel.

## 2. Add credentials locally

In the Arduino sketch folder, copy `BioSync_Cloud/secrets.h.example` to `BioSync_Cloud/secrets.h`. Fill in your 2.4 GHz Wi-Fi name/password, channel ID, and channel **Write API Key**. Keep `secrets.h` private; it is excluded by `.gitignore`. Do not paste credentials into a public report or repository.

## 3. Upload the cloud firmware

Install the **ThingSpeak** library by MathWorks and the **DHT sensor library** by Adafruit from Arduino IDE Library Manager. Open `BioSync_Cloud/BioSync_Cloud.ino`, choose your ESP32 board and port, then compile and upload. Open Serial Monitor at 115200 baud. A ThingSpeak response code of `200` confirms a successful channel update. The firmware sends one update every 16 seconds to comply with the free channel interval of 15 seconds.

This cloud sketch preserves the existing sensor pins and motor logic. Use the cloud sketch when you want cloud storage and analysis; use the original sketch only for USB serial operation.

## 4. View and analyze in ThingSpeak

Open the private channel’s **Private View** to see its field charts. For cloud calculations, open **Apps > MATLAB Analysis**, create an analysis, paste `MATLAB_Analysis.m`, replace the channel ID and read key with your own, then **Save and Run**. The output reports pressure-category shares and transitions for the selected recent samples, mean temperature/humidity, alert starts, and the latest motor-on counter. ThingSpeak hosts this MATLAB analysis; it does not require a Python process on your PC.

For recurring cloud analysis, add a TimeControl or a condition-based React action in ThingSpeak. On a free license, scheduled MATLAB Analysis runs no more often than every five minutes. The channel’s own field charts update as readings arrive; the MATLAB Analysis app displays text output and does not itself draw figures.

## Data and interpretation

The analysis describes readings in its selected recent-data window; it does not claim sleep quality or diagnose health. FSR values are uncalibrated ADC readings, posture codes are relative categories, and DHT22 readings describe the local sensor environment. Motor-on time is the firmware’s commanded time, so physical motor operation still needs direct observation.

If Wi-Fi is unavailable, the ESP32 retains serial telemetry, but the cloud-only workflow will resume uploading after connection returns. The channel only stores cloud samples that were successfully uploaded; it does not backfill missed readings.
