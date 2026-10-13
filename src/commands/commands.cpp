#include "commands.h"
#include "config.h"
#include "device/device.h"
#include "operations/operations.h"
#include "protocol/protocol.h"
#include <Arduino.h>
#include <esp_system.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {
constexpr size_t RX_BYTES = 96;
constexpr uint8_t CACHE_SIZE = 8;
constexpr uint32_t CACHE_MS = 30000, RX_IDLE_MS = 1000;
struct Reply { char lines[2][PROTOCOL_LINE_BYTES]; uint8_t count = 0; };
struct Cached { bool valid = false; uint16_t id = 0; uint32_t time = 0; char request[RX_BYTES]; Reply reply; };
Cached cache[CACHE_SIZE];
char input[RX_BYTES];
size_t inputLength = 0;
bool discarding = false, badCharacter = false, poisonReported = false, carriageReturn = false;
uint32_t lastByteTime = 0, bootId = 0;
ConfigResult startupConfig = ConfigResult::OK;

void format(Reply& reply, uint8_t part, uint16_t id, bool ok, const char* fmt, ...) {
    char* out = reply.lines[part];
    const int prefix = snprintf(out, PROTOCOL_LINE_BYTES, "#R %u %s ", id, ok ? "OK" : "ERR");
    va_list args;
    va_start(args, fmt);
    const int len = vsnprintf(out + prefix, PROTOCOL_LINE_BYTES - prefix, fmt, args);
    va_end(args);
    if (len < 0 || static_cast<size_t>(prefix + len + 1) >= PROTOCOL_LINE_BYTES) {
        snprintf(reply.lines[0], PROTOCOL_LINE_BYTES, "#R %u ERR INTERNAL_ERROR done=1\n", id);
        reply.count = 1;
        return;
    }
    const size_t end = prefix + len;
    out[end] = '\n'; out[end + 1] = 0;
    reply.count = part + 1;
}
void error(Reply& reply, uint16_t id, const char* code) { format(reply, 0, id, false, "%s done=1", code); }
void queue(const Reply& reply) {
    for (uint8_t i = 0; i < reply.count; ++i) protocolQueueLine(reply.lines[i]);
}
bool number(const char* value, float& out) {
    errno = 0;
    char* end = nullptr;
    out = strtof(value, &end);
    return end != value && *end == 0 && errno != ERANGE && isfinite(out);
}
uint16_t extractId(const char* line) {
    while (*line == ' ' || *line == '\t') ++line;
    if (*line++ != '@' || !isdigit(static_cast<unsigned char>(*line))) return 0;
    unsigned value = 0;
    while (isdigit(static_cast<unsigned char>(*line))) {
        value = value * 10 + (*line++ - '0');
        if (value > 65535) return 0;
    }
    return value && (*line == 0 || *line == ' ' || *line == '\t') ? value : 0;
}
bool configurationIdle() {
    const ScaleMode mode = getOperationStatus().mode;
    return mode == ScaleMode::WEIGHING || mode == ScaleMode::SENSOR_ERROR;
}
bool eq(const char* a, const char* b) { return strcmp(a, b) == 0; }

void dispatch(uint16_t id, char* const* token, uint8_t n, Reply& reply) {
    const char* command = token[0];
    if (eq(command, "HELP") && n == 1) {
        format(reply, 0, id, true, "protocol=1 commands=HELP,STATUS,GET_FACTOR,GET_RAW,TARE,SET_FACTOR,SAVE,LOAD,AUTO_TARE,STREAM,CAL_START,CAL_ZERO,CAL_LOAD,CAL_STATUS,CAL_APPLY,CAL_CANCEL part=1 done=0");
        format(reply, 1, id, true, "rx_max=95 cache_size=8 cache_ms=30000 factor_min=%.9g factor_max=%.9g ref_min=%.9g ref_max=%.9g samples=%u spread_adc=%ld part=2 done=1",
               FACTOR_ABS_MIN, FACTOR_ABS_MAX, REFERENCE_MIN_GRAMS, REFERENCE_MAX_GRAMS,
               OPERATION_SAMPLES, static_cast<long>(SAMPLE_MAX_SPREAD_ADC));
    } else if (eq(command, "STATUS") && n == 1) {
        const auto& d = getDeviceSnapshot(); const auto& op = getOperationStatus();
        const bool valid = d.weightValid && deviceSensorHealthy() && operationsWeighing();
        format(reply, 0, id, true, "state=%s sensor=%s has_sample=%u valid=%u weight=%.2f raw_weight=%.2f empty=%u stable=%u age_ms=%lu seq=%lu job=%u result=%s part=1 done=0",
               scaleModeName(op.mode), sensorStateName(d.sensorState), d.hasSample, valid,
               d.outputWeight, d.unfilteredWeight, d.empty, valid && d.stable,
               static_cast<unsigned long>(deviceSampleAge()), static_cast<unsigned long>(d.sampleSequence), op.jobId, op.result);
        format(reply, 1, id, true, "factor=%.9g source=%s dirty=%u auto_tare=%u stream=%u dropped=%lu boot=%08lx protocol=1 boot_config=%s part=2 done=1",
               getCalibrationFactor(), factorSourceName(getFactorSource()), hasUnsavedCalibrationFactor(),
               deviceAutoTareEnabled(), protocolStreamEnabled(), static_cast<unsigned long>(protocolDroppedWeightLines()),
               static_cast<unsigned long>(bootId), configResultName(startupConfig));
    } else if (eq(command, "GET") && n == 2 && eq(token[1], "FACTOR")) {
        format(reply, 0, id, true, "factor=%.9g source=%s dirty=%u done=1", getCalibrationFactor(),
               factorSourceName(getFactorSource()), hasUnsavedCalibrationFactor());
    } else if (eq(command, "GET") && n == 2 && eq(token[1], "RAW")) {
        const auto& d = getDeviceSnapshot();
        if (!d.hasSample) format(reply, 0, id, true, "valid=0 has_sample=0 age_ms=%lu done=1", static_cast<unsigned long>(deviceSampleAge()));
        else format(reply, 0, id, true, "valid=%u has_sample=1 adc=%ld offset=%ld net=%ld age_ms=%lu seq=%lu done=1",
                    deviceSensorHealthy(), static_cast<long>(d.absolute), static_cast<long>(d.offset), static_cast<long>(d.net),
                    static_cast<unsigned long>(deviceSampleAge()), static_cast<unsigned long>(d.sampleSequence));
    } else if (eq(command, "STREAM") && n == 2 && (eq(token[1], "ON") || eq(token[1], "OFF"))) {
        protocolSetStream(eq(token[1], "ON"));
        format(reply, 0, id, true, "stream=%u done=1", protocolStreamEnabled());
    } else if (eq(command, "AUTO_TARE") && n == 2 && (eq(token[1], "ON") || eq(token[1], "OFF"))) {
        if (!configurationIdle()) { error(reply, id, "BUSY"); return; }
        deviceSetAutoTare(eq(token[1], "ON"));
        format(reply, 0, id, true, "auto_tare=%u done=1", deviceAutoTareEnabled());
    } else if (eq(command, "SET") && n == 3 && eq(token[1], "FACTOR")) {
        float value;
        if (!number(token[2], value)) { error(reply, id, "INVALID_ARGUMENT"); return; }
        if (!isCalibrationFactorValid(value)) { error(reply, id, "OUT_OF_RANGE"); return; }
        if (!configurationIdle()) { error(reply, id, "BUSY"); return; }
        applyCalibrationFactor(value, FactorSource::MANUAL);
        deviceResetMeasurement();
        format(reply, 0, id, true, "factor=%.9g dirty=%u done=1", getCalibrationFactor(), hasUnsavedCalibrationFactor());
    } else if ((eq(command, "SAVE") || eq(command, "LOAD")) && n == 1) {
        if (!configurationIdle()) { error(reply, id, "BUSY"); return; }
        const bool save = eq(command, "SAVE"), unchanged = !hasUnsavedCalibrationFactor();
        const ConfigResult result = save ? saveCalibrationFactor() : loadCalibrationFactor();
        if (result != ConfigResult::OK) { error(reply, id, configResultName(result)); return; }
        if (!save) deviceResetMeasurement();
        format(reply, 0, id, true, "factor=%.9g dirty=%u %s=1 unchanged=%u done=1", getCalibrationFactor(),
               hasUnsavedCalibrationFactor(), save ? "saved" : "loaded", save && unchanged);
    } else if (eq(command, "TARE") && n == 1) {
        const char* result = operationTare(id);
        if (result) error(reply, id, result);
        else format(reply, 0, id, true, "accepted=1 state=TARING job=%u done=1", id);
    } else if (eq(command, "CAL") && n >= 2) {
        const char* result = nullptr;
        if (eq(token[1], "STATUS") && n == 2) {
            const auto& op = getOperationStatus();
            format(reply, 0, id, true, "state=%s job=%u reference=%.9g samples=%u spread=%ld candidate=%.9g result=%s done=1",
                   scaleModeName(op.mode), op.jobId, op.reference, op.samples, static_cast<long>(op.spread), op.candidate, op.result);
            return;
        } else if (eq(token[1], "START") && n == 3) {
            float reference;
            if (!number(token[2], reference)) { error(reply, id, "INVALID_ARGUMENT"); return; }
            result = operationCalStart(reference, id);
        } else if (eq(token[1], "ZERO") && n == 2) result = operationCalZero();
        else if (eq(token[1], "LOAD") && n == 2) result = operationCalLoad();
        else if (eq(token[1], "APPLY") && n == 2) result = operationCalApply();
        else if (eq(token[1], "CANCEL") && n == 2) result = operationCalCancel();
        else { error(reply, id, "INVALID_ARGUMENT"); return; }
        if (result) error(reply, id, result);
        else format(reply, 0, id, true, "accepted=1 state=%s job=%u dirty=%u done=1",
                    scaleModeName(getOperationStatus().mode), getOperationStatus().jobId, hasUnsavedCalibrationFactor());
    } else {
        const bool known = eq(command, "GET") || eq(command, "SET") || eq(command, "HELP") || eq(command, "STATUS") ||
            eq(command, "SAVE") || eq(command, "LOAD") || eq(command, "STREAM") || eq(command, "AUTO_TARE") || eq(command, "TARE") || eq(command, "CAL");
        error(reply, id, known ? "INVALID_ARGUMENT" : "UNKNOWN_COMMAND");
    }
}

bool pinned(uint16_t id) {
    return getOperationStatus().jobId == id &&
        (operationsCalibrationActive() || getOperationStatus().mode == ScaleMode::TARING);
}
void frame() {
    Reply reply{};
    input[inputLength] = 0;
    const uint16_t id = extractId(input);
    if (discarding || badCharacter) {
        error(reply, id, badCharacter ? "BAD_FRAME" : "LINE_TOO_LONG"); queue(reply); return;
    }
    char* cursor = input;
    while (*cursor == ' ' || *cursor == '\t') ++cursor;
    if (!*cursor) return;
    if (!id) { error(reply, 0, "BAD_FRAME"); queue(reply); return; }
    char* state = nullptr;
    strtok_r(cursor, " \t", &state); // @id
    char* token[6]; uint8_t count = 0;
    for (char* item = strtok_r(nullptr, " \t", &state); item; item = strtok_r(nullptr, " \t", &state)) {
        if (count == 6) { error(reply, id, "INVALID_ARGUMENT"); queue(reply); return; }
        for (char* p = item; *p; ++p) *p = toupper(static_cast<unsigned char>(*p));
        token[count++] = item;
    }
    if (!count) { error(reply, id, "INVALID_ARGUMENT"); queue(reply); return; }
    protocolEnableDebug();
    char canonical[RX_BYTES] = {};
    size_t length = 0;
    for (uint8_t i = 0; i < count; ++i) {
        const size_t bytes = strlen(token[i]);
        if (i) canonical[length++] = ' ';
        memcpy(canonical + length, token[i], bytes); length += bytes;
    }
    for (auto& item : cache) {
        if (!item.valid || (millis() - item.time >= CACHE_MS && !pinned(item.id))) { item.valid = false; continue; }
        if (item.id == id) {
            if (eq(item.request, canonical)) queue(item.reply);
            else { error(reply, id, "ID_CONFLICT"); queue(reply); }
            return;
        }
    }
    dispatch(id, token, count, reply);
    queue(reply);
    Cached* slot = nullptr;
    for (auto& item : cache) {
        if (pinned(item.id) && item.valid) continue;
        if (!slot || !item.valid || (slot->valid && millis() - item.time > millis() - slot->time)) slot = &item;
        if (!item.valid) break;
    }
    if (slot) {
        slot->valid = true; slot->id = id; slot->time = millis();
        strcpy(slot->request, canonical); slot->reply = reply;
    }
}
void events() {
    const auto* event = operationsPeekEvent();
    if (!event) return;
    if (!protocolDebugEnabled() && event->jobId == 0) { operationsPopEvent(); return; }
    if (!protocolCanQueue()) return;
    char line[PROTOCOL_LINE_BYTES];
    const int len = snprintf(line, sizeof(line), "#E %u %s error=%s samples=%u spread=%ld offset=%ld factor=%.9g auto_tare=%u\n",
                             event->jobId, event->name, event->error, event->samples, static_cast<long>(event->spread),
                             static_cast<long>(event->offset), event->factor, event->autoTare);
    if (len > 0 && static_cast<size_t>(len) < sizeof(line) && protocolQueueLine(line)) operationsPopEvent();
}
}

void commandsSetup(ConfigResult bootConfig) {
    startupConfig = bootConfig; bootId = esp_random();
    inputLength = 0; discarding = badCharacter = poisonReported = carriageReturn = false;
    lastByteTime = millis();
    for (auto& item : cache) item.valid = false;
}
void commandsPoll() {
    events();
    if (!poisonReported && (inputLength || discarding || badCharacter || carriageReturn) && millis() - lastByteTime >= RX_IDLE_MS && protocolCanQueue(2)) {
        Reply reply{}; input[inputLength] = 0;
        error(reply, extractId(input), discarding ? "LINE_TOO_LONG" : "BAD_FRAME"); queue(reply);
        if (discarding || badCharacter) poisonReported = true; // Discard remainder until LF, even after timeout.
        else { inputLength = 0; carriageReturn = false; }
    }
    uint8_t bytes = 0, frames = 0;
    while (Serial.available() && bytes < 64 && frames < 2 && protocolCanQueue(2)) {
        const int value = Serial.read(); ++bytes; lastByteTime = millis();
        if (value == '\n') {
            if (!poisonReported) frame();
            inputLength = 0; discarding = badCharacter = poisonReported = carriageReturn = false; ++frames;
        } else if (!discarding && !badCharacter) {
            if (carriageReturn) { badCharacter = true; continue; }
            if (value == '\r') { carriageReturn = true; continue; }
            if (value != '\t' && (value < 32 || value > 126)) { badCharacter = true; continue; }
            if (inputLength == RX_BYTES - 1) discarding = true;
            else input[inputLength++] = static_cast<char>(value);
        }
    }
}
