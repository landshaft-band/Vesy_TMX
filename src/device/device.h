#pragma once
#include <stdint.h>

enum class SensorState { STARTING, OK, SENSOR_TIMEOUT, ADC_RANGE };
struct AdcSample { int32_t absolute; uint32_t sequence; uint32_t time; bool valid; };
struct DeviceSnapshot {
    bool hasSample = false, weightValid = false, empty = true, stable = false;
    uint32_t sampleSequence = 0, lastSampleTime = 0;
    int32_t absolute = 0, offset = 0, net = 0;
    float unfilteredWeight = 0, filteredWeight = 0, outputWeight = 0;
    SensorState sensorState = SensorState::STARTING;
};
void deviceSetup();
bool devicePoll(AdcSample& sample);
void deviceProcessWeight();
void deviceSetMeasurementEnabled(bool enabled);
void deviceResetMeasurement();
void deviceApplyZero(int32_t zeroAdc);
const DeviceSnapshot& getDeviceSnapshot();
uint32_t deviceSampleAge();
bool deviceSensorHealthy();
const char* sensorStateName(SensorState state);
void deviceSetAutoTare(bool enabled);
bool deviceAutoTareEnabled();
