#include "device.h"
#include "config.h"

GyverHX711 sensor(HX711_DT, HX711_SCL, HX711_GAIN);
float filteredWeight = 0;
float rawWeight = 0;
bool emptyState = true;
unsigned long lastTareTime = 0;

// --- НОВАЯ логика стабильности (существующую фильтрацию не трогает) ------
// Кольцевой буфер последних отсчётов веса для оценки разброса.
static float stabilityBuf[STABILITY_WINDOW];
static int   stabilityIdx = 0;
static int   stabilityFill = 0;   // сколько отсчётов уже накоплено (< WINDOW)

void deviceSetup() {
    sensor.tare();
    lastTareTime = millis();
    // Сброс окна стабильности при старте
    stabilityIdx = 0;
    stabilityFill = 0;
}

void checkScaleState() {
    bool nowEmpty = (abs(filteredWeight) < EMPTY_THRESHOLD);
    if(nowEmpty != emptyState) {
        emptyState = nowEmpty;
        if(emptyState) {
            sensor.tare();
            lastTareTime = millis();
        }
    }
}

void deviceLoop() {
    if(sensor.available()) {
        long rawData = sensor.read();
        rawWeight = rawData / calibration_factor;
        filteredWeight = filteredWeight * (1 - FILTER_COEF) + rawWeight * FILTER_COEF;
        checkScaleState();
        if(emptyState && (millis() - lastTareTime >= TARE_INTERVAL)) {
            sensor.tare();
            lastTareTime = millis();
        }

        // Добавляем очередной отфильтрованный отсчёт в окно стабильности.
        stabilityBuf[stabilityIdx] = filteredWeight;
        stabilityIdx = (stabilityIdx + 1) % STABILITY_WINDOW;
        if(stabilityFill < STABILITY_WINDOW) stabilityFill++;
    }
}

float getFilteredWeight() {
    return emptyState ? 0.0 : filteredWeight;
}

bool isEmpty() {
    return emptyState;
}

// НОВАЯ функция: стабильность по разбросу отсчётов в окне.
// Не меняет фильтрацию — только читает накопленные отсчёты.
bool isStable() {
    if(stabilityFill < STABILITY_WINDOW) return false; // окно ещё не заполнено
    float mn = stabilityBuf[0];
    float mx = stabilityBuf[0];
    for(int i = 1; i < STABILITY_WINDOW; i++) {
        if(stabilityBuf[i] < mn) mn = stabilityBuf[i];
        if(stabilityBuf[i] > mx) mx = stabilityBuf[i];
    }
    return (mx - mn) < STABILITY_THRESHOLD;
}