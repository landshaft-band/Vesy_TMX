#include <Arduino.h>
#include "config.h"
#include "device/device.h"
#include "calibration/calibration.h"

// === ИЗМЕНЕНО: режим "склад / USB COM-порт" ============================
// Веб-модуль (WiFi/WebServer/OTA) НЕ используется (на складе нет WiFi),
// файл web/web.* остаётся в проекте, но исключён из сборки через
// platformio.ini (build_src_filter). Чтение команд из порта убрано:
// устройство только передаёт данные наружу.
// ======================================================================

unsigned long previousMillis = 0;
bool calibrationMode = false;

// Буфер под формируемую строку вывода ("123.45 S\n" + запас).
static char outBuf[32];

// Вывести очередную строку веса в USB COM-порт.
// Формат: "<вес> <S|U>\n", где S — стабильно, U — нестабильно.
// Защита от переполнения: проверяем свободное место в TX-буфере
// (availableForWrite). Если место есть — пишем; если нет — просто
// пропускаем этот отсчёт. Никаких блокирующих ожиданий, зависаний нет.
static void outputWeightLine() {
    float w = getFilteredWeight();
    char flag = isStable() ? 'S' : 'U';
    int len = snprintf(outBuf, sizeof(outBuf), "%.2f %c\n", w, flag);

    // availableForWrite() возвращает число байт, которые можно записать
    // без блокировки. Если вся строка влезает — пишем целиком.
    if (len > 0 && (size_t)len <= Serial.availableForWrite()) {
        Serial.write(outBuf, len);
    }
    // Если не влезает — тихо пропускаем. Буфер USB-UART не переполнится,
    // прошивка не "встанет" даже если компьютер не читает порт.
}

void setup() {
    Serial.begin(115200);
    delay(500);

    loadCalibrationFactor();
    deviceSetup();

    // Режим калибровки при старте по-прежнему можно включить через
    // отдельный билд-флаг (см. примечание в README). По умолчанию — выкл.
#ifdef RUN_CALIBRATION_AT_BOOT
    calibrationMode = true;
    calibrationSetup(275.0); // эталонный вес
#endif
}

void loop() {
    // Если калибровка активна — крутим её до завершения и не выводим вес.
    if (calibrationMode) {
        calibrationLoop();
        if (isCalibrationDone()) {
            calibrationMode = false;
        }
        return;
    }

    // Основной цикл: опрос датчика (фильтрация/тара/стабильность).
    deviceLoop();

    // Периодический вывод в USB COM-порт.
    unsigned long currentMillis = millis();
    if (currentMillis - previousMillis >= OUTPUT_INTERVAL) {
        previousMillis = currentMillis;
        outputWeightLine();
    }
}
