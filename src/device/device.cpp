#include "device.h"
#include "config.h"
#include <Arduino.h>
#include <GyverHX711.h>
#include <math.h>

namespace {
GyverHX711 sensor(HX711_DT, HX711_SCL, HX_GAIN128_A);
DeviceSnapshot data;
uint32_t started = 0;
float stability[STABILITY_WINDOW];
uint8_t stabilityFill = 0, stabilityIndex = 0;
bool measurementEnabled = false, filterInitialized = false, autoTare = false;
bool nearZero = false;
uint32_t nearZeroSince = 0;
int64_t zeroSum = 0;
uint32_t zeroCount = 0;
}
void deviceSetup() {
    data = DeviceSnapshot{};
    sensor.setOffset(0);
    started = millis();
    autoTare = measurementEnabled = false;
    deviceResetMeasurement();
}
void deviceResetMeasurement() {
    stabilityFill = stabilityIndex = 0;
    filterInitialized = false;
    data.weightValid = data.stable = false;
    data.empty = true;
    data.unfilteredWeight = data.filteredWeight = data.outputWeight = 0;
    nearZero = false;
    zeroSum = zeroCount = 0;
}
uint32_t deviceSampleAge() { return millis() - (data.hasSample ? data.lastSampleTime : started); }
bool deviceSensorHealthy() {
    return data.hasSample && data.sensorState == SensorState::OK && deviceSampleAge() < SENSOR_TIMEOUT_MS;
}
bool devicePoll(AdcSample& sample) {
    bool received = false;
    if (sensor.available()) {
        const int32_t net = static_cast<int32_t>(sensor.read()); // Only HX711 read in the application.
        const int64_t absolute = static_cast<int64_t>(net) - sensor.getOffset();
        const bool valid = absolute > -8388592 && absolute < 8388591;
        if (data.sensorState != SensorState::OK) deviceResetMeasurement();
        data.hasSample = true;
        data.lastSampleTime = millis();
        ++data.sampleSequence;
        data.absolute = static_cast<int32_t>(absolute);
        data.offset = static_cast<int32_t>(sensor.getOffset());
        data.net = net;
        data.sensorState = valid ? SensorState::OK : SensorState::ADC_RANGE;
        sample = {data.absolute, data.sampleSequence, data.lastSampleTime, valid};
        received = true;
    }
    if (deviceSampleAge() >= SENSOR_TIMEOUT_MS) {
        if (data.sensorState != SensorState::SENSOR_TIMEOUT) deviceResetMeasurement();
        data.sensorState = SensorState::SENSOR_TIMEOUT;
    }
    if (!deviceSensorHealthy()) data.weightValid = data.stable = false;
    return received;
}
void deviceSetMeasurementEnabled(bool enabled) {
    measurementEnabled = enabled;
    if (!enabled) data.weightValid = data.stable = false;
}
void deviceApplyZero(int32_t zeroAdc) {
    sensor.setOffset(-zeroAdc);
    data.offset = -zeroAdc;
    data.net = data.absolute + data.offset;
    deviceResetMeasurement();
}
void deviceProcessWeight() {
    if (!measurementEnabled || !deviceSensorHealthy()) return;
    const float weight = data.net / getCalibrationFactor();
    if (!isfinite(weight)) { deviceResetMeasurement(); return; }
    data.unfilteredWeight = weight;
    data.filteredWeight = filterInitialized
        ? data.filteredWeight * (1.0f - FILTER_COEF) + weight * FILTER_COEF : weight;
    filterInitialized = true;
    data.empty = fabsf(data.filteredWeight) < EMPTY_THRESHOLD;
    data.outputWeight = data.empty ? 0.0f : data.filteredWeight;
    stability[stabilityIndex] = data.filteredWeight;
    stabilityIndex = (stabilityIndex + 1) % STABILITY_WINDOW;
    if (stabilityFill < STABILITY_WINDOW) ++stabilityFill;
    float mn = stability[0], mx = stability[0];
    for (uint8_t i = 1; i < stabilityFill; ++i) {
        if (stability[i] < mn) mn = stability[i];
        if (stability[i] > mx) mx = stability[i];
    }
    data.stable = stabilityFill == STABILITY_WINDOW && mx - mn < STABILITY_THRESHOLD;
    data.weightValid = true;
    // Zero tracking uses the instantaneous reading, not the delayed EMA/empty flag.
    if (!autoTare || !data.stable || fabsf(weight) > AUTO_TARE_MAX_GRAMS) {
        nearZero = false; zeroSum = zeroCount = 0;
        return;
    }
    if (!nearZero) { nearZero = true; nearZeroSince = millis(); }
    zeroSum += data.absolute;
    ++zeroCount;
    if (millis() - nearZeroSince >= AUTO_TARE_DWELL_MS && zeroCount >= OPERATION_SAMPLES) {
        deviceApplyZero(static_cast<int32_t>(llround(static_cast<double>(zeroSum) / zeroCount)));
    }
}
const DeviceSnapshot& getDeviceSnapshot() { return data; }
void deviceSetAutoTare(bool enabled) { autoTare = enabled; nearZero = false; zeroSum = zeroCount = 0; }
bool deviceAutoTareEnabled() { return autoTare; }
const char* sensorStateName(SensorState state) {
    switch (state) {
        case SensorState::OK: return "OK";
        case SensorState::SENSOR_TIMEOUT: return "SENSOR_TIMEOUT";
        case SensorState::ADC_RANGE: return "ADC_RANGE";
        default: return "STARTING";
    }
}
