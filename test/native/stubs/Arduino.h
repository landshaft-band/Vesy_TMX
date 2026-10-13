#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#define DEFAULT 1
extern uint32_t fakeMillis;
inline unsigned long millis() { return fakeMillis; }
struct FakeSerial {
    std::deque<unsigned char> input;
    std::string output;
    int capacity = 128;
    void begin(unsigned long) {}
    int available() { return static_cast<int>(input.size()); }
    int read() { if (input.empty()) return -1; int c = input.front(); input.pop_front(); return c; }
    int availableForWrite() { return capacity; }
    size_t write(const uint8_t* data, size_t size) {
        size = std::min(size, static_cast<size_t>(std::max(0, capacity)));
        output.append(reinterpret_cast<const char*>(data), size); return size;
    }
    void feed(const std::string& text) { for (unsigned char c : text) input.push_back(c); }
};
extern FakeSerial Serial;
