#pragma once
#include "device/device.h"
#include <stdint.h>

enum class ScaleMode { BOOT_WAIT_SENSOR, WEIGHING, TARING, CAL_WAIT_ZERO,
                       CAL_ZERO_SAMPLING, CAL_WAIT_LOAD, CAL_LOAD_SAMPLING,
                       CAL_REVIEW, SENSOR_ERROR };
struct OperationStatus {
    ScaleMode mode = ScaleMode::BOOT_WAIT_SENSOR;
    uint16_t jobId = 0;
    float reference = 0, candidate = 0;
    uint8_t samples = 0;
    int32_t spread = 0;
    const char* result = "NONE";
};
struct OperationEvent {
    uint16_t jobId = 0;
    const char* name = nullptr;
    const char* error = "NONE";
    uint8_t samples = 0;
    int32_t spread = 0, offset = 0;
    float factor = 0;
    bool autoTare = false;
};
void operationsSetup();
void operationsPoll(const AdcSample* sample);
const OperationStatus& getOperationStatus();
const char* scaleModeName(ScaleMode mode);
bool operationsWeighing();
bool operationsCalibrationActive();
bool operationsIdle();
const char* operationTare(uint16_t id);
const char* operationCalStart(float reference, uint16_t id);
const char* operationCalZero();
const char* operationCalLoad();
const char* operationCalApply();
const char* operationCalCancel();
const OperationEvent* operationsPeekEvent();
void operationsPopEvent();
