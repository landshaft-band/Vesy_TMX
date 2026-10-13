#include "operations.h"
#include "config.h"
#include <Arduino.h>
#include <math.h>

namespace {
OperationStatus status;
OperationEvent event;
bool initialized = false, savedAutoTare = false;
uint32_t stageStarted = 0, settleMs = 0;
int32_t samples[OPERATION_SAMPLES];
uint8_t index = 0, fill = 0;
int64_t sum = 0;
double zeroMean = 0;

void sampling(ScaleMode mode, uint32_t settle = SAMPLE_SETTLE_MS) {
    status.mode = mode;
    stageStarted = millis();
    settleMs = settle;
    index = fill = 0;
    sum = 0;
    status.samples = 0;
    status.spread = 0;
    deviceResetMeasurement();
}
void emit(const char* name, const char* error = "NONE") {
    event = {status.jobId, name, error, status.samples, status.spread,
             getDeviceSnapshot().offset, getCalibrationFactor(), deviceAutoTareEnabled()};
    if (status.mode == ScaleMode::CAL_REVIEW) event.factor = status.candidate;
}
void finish(const char* name, const char* error = "NONE", bool success = false) {
    status.result = success ? "OK" : error;
    if (!success) status.candidate = 0;
    deviceSetAutoTare(success ? false : savedAutoTare);
    status.mode = deviceSensorHealthy() && initialized ? ScaleMode::WEIGHING : ScaleMode::SENSOR_ERROR;
    deviceResetMeasurement();
    emit(name, error);
}
const char* canBegin() {
    if (event.name || status.mode != ScaleMode::WEIGHING) return "BUSY";
    if (!deviceSensorHealthy()) return sensorStateName(getDeviceSnapshot().sensorState);
    return nullptr;
}
const char* calState(ScaleMode expected) {
    if (event.name) return "BUSY";
    if (status.mode != expected) return "INVALID_STATE";
    return nullptr;
}
bool addSample(int32_t value) {
    if (fill == OPERATION_SAMPLES) sum -= samples[index];
    else ++fill;
    samples[index] = value;
    index = (index + 1) % OPERATION_SAMPLES;
    sum += value;
    int32_t mn = samples[0], mx = samples[0];
    for (uint8_t i = 1; i < fill; ++i) {
        if (samples[i] < mn) mn = samples[i];
        if (samples[i] > mx) mx = samples[i];
    }
    status.samples = fill;
    status.spread = mx - mn;
    return fill == OPERATION_SAMPLES && status.spread <= SAMPLE_MAX_SPREAD_ADC;
}
}

void operationsSetup() {
    status = OperationStatus{};
    event = OperationEvent{};
    initialized = savedAutoTare = false;
    sampling(ScaleMode::BOOT_WAIT_SENSOR, BOOT_SETTLE_MS);
}
const OperationStatus& getOperationStatus() { return status; }
bool operationsWeighing() { return status.mode == ScaleMode::WEIGHING; }
bool operationsCalibrationActive() {
    return status.mode >= ScaleMode::CAL_WAIT_ZERO && status.mode <= ScaleMode::CAL_REVIEW;
}
bool operationsIdle() { return operationsWeighing() && !event.name; }
const OperationEvent* operationsPeekEvent() { return event.name ? &event : nullptr; }
void operationsPopEvent() { event = OperationEvent{}; }

const char* operationTare(uint16_t id) {
    if (const char* error = canBegin()) return error;
    status.jobId = id;
    status.result = "RUNNING";
    status.reference = status.candidate = 0;
    savedAutoTare = deviceAutoTareEnabled();
    deviceSetAutoTare(false);
    sampling(ScaleMode::TARING);
    return nullptr;
}
const char* operationCalStart(float reference, uint16_t id) {
    if (!isfinite(reference) || reference < REFERENCE_MIN_GRAMS || reference > REFERENCE_MAX_GRAMS) return "OUT_OF_RANGE";
    if (const char* error = canBegin()) return error;
    status.jobId = id;
    status.reference = reference;
    status.candidate = 0;
    status.samples = 0;
    status.spread = 0;
    status.result = "RUNNING";
    status.mode = ScaleMode::CAL_WAIT_ZERO;
    stageStarted = millis();
    savedAutoTare = deviceAutoTareEnabled();
    deviceSetAutoTare(false);
    deviceResetMeasurement();
    return nullptr;
}
const char* operationCalZero() {
    if (const char* error = calState(ScaleMode::CAL_WAIT_ZERO)) return error;
    sampling(ScaleMode::CAL_ZERO_SAMPLING);
    return nullptr;
}
const char* operationCalLoad() {
    if (const char* error = calState(ScaleMode::CAL_WAIT_LOAD)) return error;
    sampling(ScaleMode::CAL_LOAD_SAMPLING);
    return nullptr;
}
const char* operationCalApply() {
    if (const char* error = calState(ScaleMode::CAL_REVIEW)) return error;
    if (!deviceSensorHealthy()) return "SENSOR_TIMEOUT";
    if (applyCalibrationFactor(status.candidate, FactorSource::CALIBRATION) != ConfigResult::OK) return "OUT_OF_RANGE";
    deviceApplyZero(static_cast<int32_t>(llround(zeroMean)));
    finish("CAL_DONE", "NONE", true);
    return nullptr;
}
const char* operationCalCancel() {
    if (event.name) return "BUSY";
    if (!operationsCalibrationActive()) return "INVALID_STATE";
    finish("CAL_CANCELLED", "CANCELLED");
    status.candidate = 0;
    return nullptr;
}

void operationsPoll(const AdcSample* sample) {
    // Never overwrite an undelivered transition. RX/TX continue in main.
    if (event.name) return;
    const SensorState sensor = getDeviceSnapshot().sensorState;
    if (status.mode == ScaleMode::SENSOR_ERROR) {
        if (deviceSensorHealthy()) {
            if (!initialized) sampling(ScaleMode::BOOT_WAIT_SENSOR, BOOT_SETTLE_MS);
            else {
                status.mode = ScaleMode::WEIGHING; deviceResetMeasurement();
                const uint16_t previousJob = status.jobId;
                status.jobId = 0; emit("SENSOR_RECOVERED"); status.jobId = previousJob;
            }
        }
        return;
    }
    if (sensor == SensorState::SENSOR_TIMEOUT || sensor == SensorState::ADC_RANGE) {
        const char* error = sensorStateName(sensor);
        if (operationsCalibrationActive()) finish("CAL_FAILED", error);
        else if (status.mode == ScaleMode::TARING) finish("TARE_FAILED", error);
        else {
            status.mode = ScaleMode::SENSOR_ERROR;
            if (!initialized) status.result = error; // A system fault must not rewrite a completed job's result.
            const uint16_t previousJob = status.jobId;
            status.jobId = 0;
            emit("SENSOR_ERROR", error);
            status.jobId = previousJob;
        }
        return;
    }
    if (operationsWeighing()) return;
    const bool collecting = status.mode == ScaleMode::BOOT_WAIT_SENSOR || status.mode == ScaleMode::TARING ||
        status.mode == ScaleMode::CAL_ZERO_SAMPLING || status.mode == ScaleMode::CAL_LOAD_SAMPLING;
    const uint32_t elapsed = millis() - stageStarted;
    if (!collecting) {
        if (elapsed >= USER_TIMEOUT_MS) finish("CAL_FAILED", "USER_TIMEOUT");
        return;
    }
    if (elapsed >= SAMPLE_TIMEOUT_MS) {
        const char* error = fill == OPERATION_SAMPLES ? "UNSTABLE" : "SENSOR_TIMEOUT";
        if (status.mode == ScaleMode::BOOT_WAIT_SENSOR) {
            status.mode = ScaleMode::SENSOR_ERROR;
            status.result = error;
            emit("BOOT_FAILED", error);
        } else finish(status.mode == ScaleMode::TARING ? "TARE_FAILED" : "CAL_FAILED", error);
        return;
    }
    if (!sample || !sample->valid || elapsed < settleMs || !addSample(sample->absolute)) return;
    const double mean = static_cast<double>(sum) / fill;
    if (status.mode == ScaleMode::BOOT_WAIT_SENSOR) {
        initialized = true;
        deviceApplyZero(static_cast<int32_t>(llround(mean)));
        status.mode = ScaleMode::WEIGHING;
        status.result = "OK";
        emit("BOOT_DONE");
    } else if (status.mode == ScaleMode::TARING) {
        deviceApplyZero(static_cast<int32_t>(llround(mean)));
        finish("TARE_DONE", "NONE", true);
    } else if (status.mode == ScaleMode::CAL_ZERO_SAMPLING) {
        zeroMean = mean;
        status.mode = ScaleMode::CAL_WAIT_LOAD;
        stageStarted = millis();
        emit("CAL_WAIT_LOAD");
    } else {
        const double delta = mean - zeroMean;
        if (fabs(delta) < CAL_MIN_SIGNAL_ADC || fabs(delta) < 4.0 * status.spread) {
            finish("CAL_FAILED", "SIGNAL_TOO_SMALL");
            return;
        }
        const float candidate = static_cast<float>(delta / status.reference);
        if (!isCalibrationFactorValid(candidate)) { finish("CAL_FAILED", "OUT_OF_RANGE"); return; }
        status.candidate = candidate;
        status.mode = ScaleMode::CAL_REVIEW;
        stageStarted = millis();
        emit("CAL_REVIEW");
    }
}
const char* scaleModeName(ScaleMode mode) {
    switch (mode) {
        case ScaleMode::WEIGHING: return "WEIGHING";
        case ScaleMode::TARING: return "TARING";
        case ScaleMode::CAL_WAIT_ZERO: return "CAL_WAIT_ZERO";
        case ScaleMode::CAL_ZERO_SAMPLING: return "CAL_ZERO_SAMPLING";
        case ScaleMode::CAL_WAIT_LOAD: return "CAL_WAIT_LOAD";
        case ScaleMode::CAL_LOAD_SAMPLING: return "CAL_LOAD_SAMPLING";
        case ScaleMode::CAL_REVIEW: return "CAL_REVIEW";
        case ScaleMode::SENSOR_ERROR: return "SENSOR_ERROR";
        default: return "BOOT_WAIT_SENSOR";
    }
}
