#include "config.h"
#include <Preferences.h>

float calibration_factor = -118.217292;

static Preferences prefs;
static const char* PREF_NAMESPACE = "vesy";
static const char* PREF_KEY_CAL = "cal_factor";

void loadCalibrationFactor() {
    if (!prefs.begin(PREF_NAMESPACE, false)) return;
    if (prefs.isKey(PREF_KEY_CAL)) {
        calibration_factor = prefs.getFloat(PREF_KEY_CAL, calibration_factor);
    }
    prefs.end();
}

void saveCalibrationFactor() {
    if (!prefs.begin(PREF_NAMESPACE, false)) return;
    prefs.putFloat(PREF_KEY_CAL, calibration_factor);
    prefs.end();
}