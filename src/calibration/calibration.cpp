#include "calibration.h"
#include "device/device.h"   // чтобы использовать sensor
#include "config.h"
#include <Arduino.h>

static bool done = false;
static float measuredFactor = 0;
static float referenceWeightGlobal = 100.0;
static long sum = 0;
static int measurementsCount = 0;
static unsigned long startTime = 0;
static String statusMessage;

void calibrationSetup(float referenceWeight) {
    referenceWeightGlobal = referenceWeight;
    done = false;
    sum = 0;
    measurementsCount = 0;
    startTime = millis();

    Serial.println("Калибровка весов");
    Serial.println("Установите нулевой вес и подождите...");
    statusMessage = "Калибровка весов. Установите нулевой вес и подождите...";
    delay(1000);
    sensor.tare();

    Serial.print("Теперь установите эталонный груз "); 
    Serial.print(referenceWeightGlobal); 
    Serial.println(" г");
    Serial.println("Ждите 10 секунд...");
    statusMessage = "Установите эталонный груз " + String(referenceWeightGlobal, 1) + " г и ждите 10 секунд...";
    delay(10000);

    startTime = millis(); // начинаем сбор данных
    statusMessage = "Идет сбор данных для калибровки...";
}

void calibrationLoop() {
    if(done) return;

    // Собираем данные в течение 3 секунд
    if(millis() - startTime < 3000) {
        statusMessage = "Идет сбор данных для калибровки...";
        if(sensor.available()) {
            sum += sensor.read();
            measurementsCount++;
        }
    } else {
        if(measurementsCount > 0) {
            float avgRaw = (float)sum / measurementsCount;
            measuredFactor = avgRaw / referenceWeightGlobal;

            calibration_factor = measuredFactor;
            saveCalibrationFactor();

            Serial.println("Калибровка завершена!");
            Serial.print("Количество измерений: "); Serial.println(measurementsCount);
            Serial.print("Среднее сырое значение: "); Serial.println(avgRaw);
            Serial.print("Калибровочный коэффициент: "); Serial.println(calibration_factor, 6);
            statusMessage = "Калибровка завершена. Коэффициент: " + String(calibration_factor, 6);
        } else {
            Serial.println("Ошибка: не получено ни одного измерения!");
            statusMessage = "Ошибка калибровки: не получено ни одного измерения!";
        }
        done = true;
    }
}

bool isCalibrationDone() {
    return done;
}

float getCalibratedFactor() {
    return measuredFactor;
}

const char* getCalibrationStatusMessage() {
    return statusMessage.c_str();
}