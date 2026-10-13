#pragma once
#include <stdint.h>

constexpr uint8_t HX711_DT = 14;
constexpr uint8_t HX711_SCL = 27;
constexpr float DEFAULT_CALIBRATION_FACTOR = -118.217292f;
// Technical input limits; load capacity must be specified for the actual scale.
constexpr float FACTOR_ABS_MIN = 0.01f;
constexpr float FACTOR_ABS_MAX = 1000000.0f;
constexpr float REFERENCE_MIN_GRAMS = 1.0f;
constexpr float REFERENCE_MAX_GRAMS = 100000.0f;
constexpr float FILTER_COEF = 0.3f;
constexpr float EMPTY_THRESHOLD = 10.0f;
constexpr uint32_t OUTPUT_INTERVAL = 100;
constexpr uint8_t STABILITY_WINDOW = 8;
constexpr float STABILITY_THRESHOLD = 1.0f;
constexpr uint32_t SENSOR_TIMEOUT_MS = 1000;
constexpr uint32_t BOOT_SETTLE_MS = 500;
constexpr uint32_t SAMPLE_SETTLE_MS = 400;
constexpr uint32_t SAMPLE_TIMEOUT_MS = 5000;
constexpr uint32_t USER_TIMEOUT_MS = 60000;
constexpr uint8_t OPERATION_SAMPLES = 16;
constexpr int32_t SAMPLE_MAX_SPREAD_ADC = 240;
constexpr int32_t CAL_MIN_SIGNAL_ADC = 64;
constexpr uint32_t AUTO_TARE_DWELL_MS = 10000;
constexpr float AUTO_TARE_MAX_GRAMS = 0.5f;

enum class ConfigResult { OK, NO_SAVED_FACTOR, INVALID_SAVED_FACTOR,
                          NVS_OPEN_FAILED, NVS_WRITE_FAILED, OUT_OF_RANGE };
enum class FactorSource { FIRMWARE_DEFAULT, NVS, MANUAL, CALIBRATION };
bool isCalibrationFactorValid(float value);
ConfigResult loadCalibrationFactor();
ConfigResult saveCalibrationFactor();
ConfigResult applyCalibrationFactor(float value, FactorSource source);
float getCalibrationFactor();
FactorSource getFactorSource();
bool hasUnsavedCalibrationFactor();
const char* configResultName(ConfigResult result);
const char* factorSourceName(FactorSource source);
