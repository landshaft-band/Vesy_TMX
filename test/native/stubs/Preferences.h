#pragma once
#include <cstddef>
extern bool fakeNamespace, fakeHasKey, fakeOpenFailure, fakeWriteFailure;
extern float fakeStored;
extern unsigned fakeWrites;
class Preferences {
public:
    bool begin(const char*, bool readOnly = false) {
        if (fakeOpenFailure || (readOnly && !fakeNamespace)) return false;
        fakeNamespace = true; return true;
    }
    bool isKey(const char*) { return fakeHasKey; }
    float getFloat(const char*, float fallback) { return fakeHasKey ? fakeStored : fallback; }
    size_t putFloat(const char*, float value) {
        ++fakeWrites;
        if (fakeWriteFailure) return 0;
        fakeHasKey = true; fakeStored = value; return sizeof(float);
    }
    void end() {}
};
