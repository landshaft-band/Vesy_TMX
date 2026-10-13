#pragma once

void calibrationSetup(float referenceWeight);
void calibrationLoop();
bool isCalibrationDone();
float getCalibratedFactor();
const char* getCalibrationStatusMessage();