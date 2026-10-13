#pragma once

#include <GyverHX711.h>

// HX711 пины
#define HX711_DT 14
#define HX711_SCL 27
#define HX711_GAIN HX_GAIN128_A

// WiFi
#define WIFI_SSID      "mgts_143"
#define WIFI_PASSWORD  "89164922249"
#define WIFI_HOSTNAME  "vesy"

// Калибровка
extern float calibration_factor;
void loadCalibrationFactor();
void saveCalibrationFactor();

// Фильтрация
const float FILTER_COEF = 0.3;

// Логика веса
const float EMPTY_THRESHOLD = 10.0;
const unsigned long TARE_INTERVAL = 10000;

// Интервал вывода данных в USB COM-порт.
// 100 мс => 10 Гц (требование: не реже 5–10 раз/сек).
const unsigned long OUTPUT_INTERVAL = 100;

// --- Определение стабильности веса (НОВАЯ логика) ------------------------
// Поверх существующей фильтрации (ничего в фильтре не меняем).
// Берём окно из последних N отсчётов HX711 и смотрим разброс (max-min).
// Если разброс ниже порога — вес стабилен (флаг 'S'), иначе нестабилен ('U').
const int   STABILITY_WINDOW    = 8;     // размер окна, отсчётов (~0.8 c при 10 SPS)
const float STABILITY_THRESHOLD = 1.0;   // порог разброса, граммы