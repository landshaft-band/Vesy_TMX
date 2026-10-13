#pragma once
#include <stddef.h>
#include <stdint.h>

constexpr size_t PROTOCOL_LINE_BYTES = 384;
constexpr uint8_t PROTOCOL_QUEUE_LINES = 12;
void protocolSetup();
bool protocolCanQueue(uint8_t lines = 1);
bool protocolQueueLine(const char* line);
void protocolPoll();
void protocolScheduleWeight(float weight, bool stable);
void protocolInvalidateWeight();
void protocolSetStream(bool enabled);
bool protocolStreamEnabled();
void protocolEnableDebug();
bool protocolDebugEnabled();
uint32_t protocolDroppedWeightLines();
