# MPCA BioSync Smart Pillow — Implementation Features

## 1. System Overview
- ESP32-based smart sleep monitor using FSR sensors, DHT temperature/humidity sensor, touch input, and vibration feedback.
- State-driven behavior with `HIBERNATE`, `MONITORING`, `VIBRATING`, and `SNOOZED` modes.
- Designed for low power and responsive user interaction.

## 2. Sensors and Inputs
- Three FSR sensors for pressure detection: left, middle, and right.
- Temperature and humidity sensing via DHT22.
- Touch button input for user commands.

## 3. Session and Pressure Management
- Detects head presence when any FSR reading exceeds `FSR_PRESSURE_THRESHOLD`.
- Starts monitoring automatically when pressure is detected.
- Uses a 3-second grace period (`NO_HEAD_TIMEOUT_MS`) before ending a session when pressure is lost.
- Logs session summary when the user leaves the pillow.

## 4. Posture Monitoring
- Evaluates posture by checking the middle FSR sensor against `FSR_POSTURE_THRESHOLD`.
- Interprets high middle-sensor pressure as bad posture (back/center sleeping).
- Side sleeping is treated as acceptable posture.
- Initiates vibration feedback if bad posture persists beyond `BAD_POSTURE_LIMIT_MS`.

## 5. Vibration and Alarm Handling
- Uses PWM control to drive the vibration motor.
- Vibrates continuously when a bad posture alert is active.
- Supports alarm scheduling via touch input with a configurable delay (`ALARM_DELAY_MS`).
- Stops vibration and returns to monitoring when posture improves.

## 6. Touch Controls
- Single tap: snooze vibration for `SNOOZE_DURATION_MS`.
- Double tap: dismiss current vibration alert and stay in monitoring mode.
- Triple tap: schedule an alarm to fire after the configured delay.

## 7. Environmental Logging
- Reads temperature and humidity every monitoring cycle.
- Prints live status updates every 2 seconds.
- Computes running averages for temperature and humidity during the current session.
- Includes average environmental data in the session summary.

## 8. Serial Debugging and Status Output
- Detailed serial messages for state changes, sensor readings, and touch actions.
- Status output includes FSR values, temperature, humidity, and current system state.
- Helps verify system behavior during testing and deployment.

## 9. Implementation Details
- Uses `millis()` timing for non-blocking delays.
- Manages state through an `enum SystemState` and a switch statement in `loop()`.
- Handles touch input with debounce-style tap counting and time window logic.
- Resets internal timers and flags cleanly on state transitions.
