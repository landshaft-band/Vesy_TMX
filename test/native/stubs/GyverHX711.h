#pragma once
#include <cstdint>
#include <deque>
#define HX_GAIN128_A 0
extern std::deque<int32_t> fakeAdc;
extern unsigned fakeReads;
class GyverHX711 {
public:
    GyverHX711(uint8_t, uint8_t, uint8_t) {}
    bool available() { return !fakeAdc.empty(); }
    long read() {
        if (available()) { last = fakeAdc.front(); fakeAdc.pop_front(); ++fakeReads; }
        return last + offset;
    }
    void setOffset(long value) { offset = value; }
    long getOffset() { return offset; }
private:
    int32_t last = 0;
    long offset = 0;
};
