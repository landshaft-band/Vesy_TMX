#include "protocol.h"
#include <Arduino.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace {
char queue[PROTOCOL_QUEUE_LINES][PROTOCOL_LINE_BYTES];
uint8_t head = 0, count = 0;
char current[PROTOCOL_LINE_BYTES];
size_t currentLength = 0, position = 0;
char weightLine[48];
bool weightPending = false, stream = true, debug = false;
uint32_t dropped = 0;
}
void protocolSetup() {
    head = count = 0;
    currentLength = position = 0;
    weightPending = debug = false;
    stream = true;
    dropped = 0;
}
bool protocolCanQueue(uint8_t lines) { return lines <= PROTOCOL_QUEUE_LINES - count; }
bool protocolQueueLine(const char* line) {
    const size_t len = strlen(line);
    if (!len || len >= PROTOCOL_LINE_BYTES || line[len - 1] != '\n' || !protocolCanQueue()) return false;
    memcpy(queue[(head + count) % PROTOCOL_QUEUE_LINES], line, len + 1);
    ++count;
    return true;
}
void protocolPoll() {
    if (position == currentLength) {
        currentLength = position = 0;
        if (count) {
            strcpy(current, queue[head]);
            head = (head + 1) % PROTOCOL_QUEUE_LINES;
            --count;
        } else if (weightPending && stream) {
            strcpy(current, weightLine);
            weightPending = false;
        } else return;
        currentLength = strlen(current);
    }
    const int available = Serial.availableForWrite();
    if (available <= 0) return;
    size_t bytes = currentLength - position;
    if (bytes > static_cast<size_t>(available)) bytes = available;
    if (bytes > 64) bytes = 64;
    position += Serial.write(reinterpret_cast<const uint8_t*>(current + position), bytes);
}
void protocolScheduleWeight(float weight, bool stable) {
    if (!stream || !isfinite(weight)) return;
    const int len = snprintf(weightLine, sizeof(weightLine), "%.2f %c\n", weight, stable ? 'S' : 'U');
    if (len <= 0 || static_cast<size_t>(len) >= sizeof(weightLine)) { ++dropped; return; }
    if (weightPending) ++dropped;
    weightPending = true;
}
void protocolSetStream(bool enabled) { stream = enabled; if (!enabled) weightPending = false; }
void protocolInvalidateWeight() { if (weightPending) { weightPending = false; ++dropped; } }
bool protocolStreamEnabled() { return stream; }
void protocolEnableDebug() { debug = true; }
bool protocolDebugEnabled() { return debug; }
uint32_t protocolDroppedWeightLines() { return dropped; }
