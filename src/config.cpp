#include "config.h"
#include <Preferences.h>
#include <math.h>

namespace {
float factor = DEFAULT_CALIBRATION_FACTOR;
float savedFactor = DEFAULT_CALIBRATION_FACTOR;
bool hasSaved = false;
FactorSource source = FactorSource::FIRMWARE_DEFAULT;
constexpr const char* PREF_NAMESPACE = "vesy";
constexpr const char* PREF_KEY = "cal_factor";
}

bool isCalibrationFactorValid(float value) {
    return isfinite(value) && fabsf(value) >= FACTOR_ABS_MIN && fabsf(value) <= FACTOR_ABS_MAX;
}
float getCalibrationFactor() { return factor; }
FactorSource getFactorSource() { return source; }
bool hasUnsavedCalibrationFactor() { return !hasSaved || factor != savedFactor; }

ConfigResult applyCalibrationFactor(float value, FactorSource origin) {
    if (!isCalibrationFactorValid(value)) return ConfigResult::OUT_OF_RANGE;
    factor = value;
    source = origin;
    return ConfigResult::OK;
}

ConfigResult loadCalibrationFactor() {
    Preferences prefs;
    // Read-only begin also fails when the namespace does not exist. Probe/create
    // the namespace without writing a value, to distinguish absence from bad NVS.
    if (!prefs.begin(PREF_NAMESPACE, true)) {
        if (!prefs.begin(PREF_NAMESPACE, false)) return ConfigResult::NVS_OPEN_FAILED;
    }
    if (!prefs.isKey(PREF_KEY)) {
        prefs.end();
        hasSaved = false;
        return ConfigResult::NO_SAVED_FACTOR;
    }
    const float candidate = prefs.getFloat(PREF_KEY, NAN);
    prefs.end();
    if (!isCalibrationFactorValid(candidate)) { hasSaved = false; return ConfigResult::INVALID_SAVED_FACTOR; }
    factor = savedFactor = candidate;
    hasSaved = true;
    source = FactorSource::NVS;
    return ConfigResult::OK;
}

ConfigResult saveCalibrationFactor() {
    if (!isCalibrationFactorValid(factor)) return ConfigResult::OUT_OF_RANGE;
    if (!hasUnsavedCalibrationFactor()) return ConfigResult::OK;
    Preferences prefs;
    if (!prefs.begin(PREF_NAMESPACE, false)) return ConfigResult::NVS_OPEN_FAILED;
    const size_t written = prefs.putFloat(PREF_KEY, factor);
    prefs.end();
    if (written != sizeof(float)) return ConfigResult::NVS_WRITE_FAILED;
    savedFactor = factor;
    hasSaved = true;
    return ConfigResult::OK;
}

const char* configResultName(ConfigResult result) {
    switch (result) {
        case ConfigResult::OK: return "OK";
        case ConfigResult::NO_SAVED_FACTOR: return "NO_SAVED_FACTOR";
        case ConfigResult::INVALID_SAVED_FACTOR: return "INVALID_SAVED_FACTOR";
        case ConfigResult::NVS_OPEN_FAILED: return "NVS_OPEN_FAILED";
        case ConfigResult::NVS_WRITE_FAILED: return "NVS_WRITE_FAILED";
        default: return "OUT_OF_RANGE";
    }
}
const char* factorSourceName(FactorSource origin) {
    switch (origin) {
        case FactorSource::NVS: return "NVS";
        case FactorSource::MANUAL: return "MANUAL";
        case FactorSource::CALIBRATION: return "CALIBRATION";
        default: return "DEFAULT";
    }
}
