# Vesy_v1

ESP32-based scale using HX711 load cell amplifier and GyverHX711 library. Supports optional calibration and filtered weight output over Serial.

## Overview

- **Board**: DOIT ESP32 DEVKIT V1
- **Sensor**: HX711 (load cell amplifier)
- **Library**: GyverHX711
- **Output**: Filtered weight (grams) printed to Serial at 115200 baud
- **Calibration**: Runtime trigger via Serial `c`; value stored in NVS and restored on boot

## Hardware Connections

| HX711 Pin | ESP32 Pin | Config constant |
|-----------|-----------|-----------------|
| DT (Data) | GPIO 14   | `HX711_DT`      |
| SCK (Clock) | GPIO 27 | `HX711_SCL`     |
| VCC       | 3.3V      | —               |
| GND       | GND       | —               |

Calibration requires a known reference weight (default: 275 g). Ensure the load cell and HX711 are powered correctly (3.3V recommended).

## Firmware Structure

```
main.cpp
    ├── config.h / config.cpp (calibration_factor, pins, constants)
    ├── device/device.h + device.cpp (HX711, filtering, tare, empty detection)
    └── calibration/calibration.h + calibration.cpp (reference-weight calibration)
```

**Flow**: Startup → optional calibration (if enabled) → main loop reads sensor → filtered weight output.

| Module | Purpose |
|--------|---------|
| `main.cpp` | Entry point, calibration vs normal mode, periodic Serial output |
| `config.h` / `config.cpp` | Pins, filter coefficient, intervals, thresholds, `calibration_factor`, NVS load/save |
| `device/` | HX711 handling, tare, exponential filter, empty detection |
| `calibration/` | Interactive calibration with reference weight |

## Build & Flash

1. Open the project folder in PlatformIO (VS Code / Cursor).
2. Build:
   ```bash
   platformio run -e esp32doit-devkit-v1
   ```
3. Upload (USB):
   ```bash
   platformio run -e esp32doit-devkit-v1 --target upload
   ```
4. Monitor Serial (115200 baud):
   ```bash
   platformio device monitor -b 115200
   ```

## Calibration Procedure

1. Open Serial monitor at 115200 baud.
2. Type `c` and press Enter to start calibration.
3. Remove all weight from the scale. Wait for "Установите нулевой вес и подождите...".
4. Place the reference weight (275 g by default) on the scale.
5. Wait 10 seconds as instructed.
6. The firmware collects data for ~3 seconds and computes `calibration_factor`.
7. When "Калибровка завершена!" appears, calibration is done and saved to NVS.
8. The calibration value is restored automatically on every reboot.

## Extending the Project

Planned areas for future development:

- **WiFi / Web UI**: Use existing `lib_deps` (WiFi, WebServer) to expose weight over HTTP.
- **Unit selection**: Grams, kilograms, ounces.
- **State machine**: BOOT, CALIBRATING, IDLE, MEASURING, ERROR for clearer logic.

## Roadmap (Prioritized)

| Priority | Task | Notes |
|----------|------|-------|
| 1 | ~~Persistent calibration (Preferences/NVS)~~ | Done: saved on calibration, loaded at boot |
| 2 | ~~Runtime calibration trigger~~ | Done: Serial `c` command |
| 3 | WiFi + simple web page | Expose current weight and status |
| 4 | Inline documentation | Document each module and function |
| 5 | Logging helper | Centralize Serial output |
| 6 | State machine | Simplify mode handling as features grow |
| 7 | Unit selection | Grams, kg, oz |
| 8 | Error handling | Sensor disconnect, unstable readings, overrange |
