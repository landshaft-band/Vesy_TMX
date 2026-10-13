#include <Arduino.h>
#include "config.h"
#include "device/device.h"
#include "operations/operations.h"
#include "commands/commands.h"
#include "protocol/protocol.h"

namespace { uint32_t previousOutput = 0; }

void setup() {
    Serial.begin(115200);
    protocolSetup();
    const ConfigResult config = loadCalibrationFactor();
    deviceSetup();
    operationsSetup();
    commandsSetup(config);
    previousOutput = millis();
}

void loop() {
    commandsPoll();
    AdcSample sample{};
    const bool received = devicePoll(sample);
    operationsPoll(received ? &sample : nullptr);
    deviceSetMeasurementEnabled(operationsWeighing());
    if (received && operationsWeighing()) deviceProcessWeight();

    const auto& data = getDeviceSnapshot();
    const bool valid = data.weightValid && deviceSensorHealthy() && operationsWeighing();
    if (!valid) protocolInvalidateWeight();
    if (millis() - previousOutput >= OUTPUT_INTERVAL) {
        previousOutput = millis();
        if (valid) protocolScheduleWeight(data.outputWeight, data.stable);
    }
    protocolPoll();
}
