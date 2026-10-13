#include <Arduino.h>
#include <GyverHX711.h>
#include "config.h"
#include "device/device.h"
#include "operations/operations.h"
#include "protocol/protocol.h"
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

uint32_t fakeMillis = 0;
FakeSerial Serial;
std::deque<int32_t> fakeAdc;
unsigned fakeReads = 0, fakeWrites = 0;
bool fakeNamespace = false, fakeHasKey = false, fakeOpenFailure = false, fakeWriteFailure = false;
float fakeStored = 0;
void setup();
void loop();
#define CHECK(expr) do { if (!(expr)) throw std::runtime_error(std::string(#expr) + " at line " + std::to_string(__LINE__)); } while (0)

void advance(uint32_t ms, int32_t adc = 1000, bool feed = true, uint32_t period = 100) {
    for (uint32_t i = 0; i < ms; ++i) {
        ++fakeMillis;
        if (feed && i % period == 0) fakeAdc.push_back(adc);
        loop();
    }
}
void reset(bool boot = true, uint32_t start = 0) {
    fakeMillis = start; Serial = FakeSerial{}; fakeAdc.clear(); fakeReads = fakeWrites = 0;
    fakeNamespace = fakeHasKey = fakeOpenFailure = fakeWriteFailure = false;
    applyCalibrationFactor(DEFAULT_CALIBRATION_FACTOR, FactorSource::FIRMWARE_DEFAULT);
    setup();
    if (boot) { advance(4000); CHECK(operationsWeighing()); }
    Serial.output.clear();
}
std::string send(const std::string& request) {
    Serial.output.clear(); Serial.feed(request);
    advance(60, 1000, false);
    return Serial.output;
}
bool contains(const std::string& text, const std::string& expected) { return text.find(expected) != std::string::npos; }
void calZero(unsigned id = 20, float grams = 275) {
    CHECK(contains(send("@" + std::to_string(id) + " CAL START " + std::to_string(grams) + "\n"), "OK"));
    CHECK(contains(send("@" + std::to_string(id + 1) + " CAL ZERO\n"), "OK"));
    advance(2300, 1000);
    CHECK(getOperationStatus().mode == ScaleMode::CAL_WAIT_LOAD);
}

int main() {
    const std::vector<std::pair<const char*, std::function<void()>>> tests = {
        {"status/help multipart and unchanged weight format", [] {
            reset(); auto status = send("@1 STATUS\r\n");
            CHECK(contains(status, "part=1 done=0")); CHECK(contains(status, "part=2 done=1"));
            CHECK(contains(status, "protocol=1")); CHECK(contains(status, "auto_tare=0"));
            CHECK(contains(send("@2 HELP\n"), "cache_size=8"));
            advance(200); CHECK(contains(Serial.output, "0.00 S\n"));
        }},
        {"negative factor, bounds, invalid numbers", [] {
            reset(); CHECK(contains(send("@1 SET FACTOR -120.5\n"), "OK"));
            CHECK(getCalibrationFactor() == -120.5f);
            unsigned id = 2;
            for (const auto& value : {"0", "0.001", "1000001"}) CHECK(contains(send("@" + std::to_string(id++) + " SET FACTOR " + value + "\n"), "OUT_OF_RANGE"));
            for (const auto& value : {"nan", "inf", "1e999", "1e-999", "118junk", "118,2"})
                CHECK(contains(send("@" + std::to_string(id++) + " SET FACTOR " + value + "\n"), "INVALID_ARGUMENT"));
            CHECK(getCalibrationFactor() == -120.5f);
        }},
        {"NVS save/load, dirty, write/open failure", [] {
            reset(); CHECK(contains(send("@1 LOAD\n"), "NO_SAVED_FACTOR"));
            send("@2 SET FACTOR -120\n"); CHECK(hasUnsavedCalibrationFactor());
            fakeWriteFailure = true; CHECK(contains(send("@3 SAVE\n"), "NVS_WRITE_FAILED")); CHECK(hasUnsavedCalibrationFactor());
            fakeWriteFailure = false; CHECK(contains(send("@4 SAVE\n"), "saved=1")); CHECK(!hasUnsavedCalibrationFactor());
            unsigned writes = fakeWrites; CHECK(contains(send("@5 SAVE\n"), "unchanged=1")); CHECK(fakeWrites == writes);
            send("@6 SET FACTOR -130\n"); send("@7 LOAD\n"); CHECK(getCalibrationFactor() == -120); CHECK(!hasUnsavedCalibrationFactor());
            fakeStored = std::numeric_limits<float>::quiet_NaN();
            CHECK(contains(send("@8 LOAD\n"), "INVALID_SAVED_FACTOR")); CHECK(getCalibrationFactor() == -120);
            CHECK(hasUnsavedCalibrationFactor()); CHECK(contains(send("@10 SAVE\n"), "saved=1")); CHECK(fakeStored == -120);
            fakeOpenFailure = true; CHECK(contains(send("@9 LOAD\n"), "NVS_OPEN_FAILED"));
        }},
        {"GET RAW never reads HX711 again", [] {
            reset(); const auto before = fakeReads;
            auto raw = send("@1 GET RAW\n"); CHECK(contains(raw, "adc=1000 offset=-1000 net=0")); CHECK(fakeReads == before);
        }},
        {"manual tare fresh samples and persistent container zero", [] {
            reset(); advance(1000, -10822);
            const int32_t old = getDeviceSnapshot().offset;
            CHECK(contains(send("@1 TARE\n"), "accepted=1")); CHECK(getDeviceSnapshot().offset == old);
            advance(2300, -22644); CHECK(contains(Serial.output, "#E 1 TARE_DONE"));
            CHECK(getDeviceSnapshot().offset == 22644); CHECK(!deviceAutoTareEnabled()); CHECK(fakeWrites == 0);
            advance(20000, 1000); CHECK(getDeviceSnapshot().offset == 22644);
        }},
        {"calibration candidate, apply and explicit save", [] {
            reset(); calZero(); CHECK(getDeviceSnapshot().offset == -1000);
            CHECK(contains(send("@22 CAL LOAD\n"), "OK")); advance(2300, -32000);
            CHECK(getOperationStatus().mode == ScaleMode::CAL_REVIEW); CHECK(getOperationStatus().candidate == -120);
            CHECK(getCalibrationFactor() == DEFAULT_CALIBRATION_FACTOR); CHECK(fakeWrites == 0);
            CHECK(contains(send("@23 CAL APPLY\n"), "OK")); CHECK(getCalibrationFactor() == -120);
            CHECK(contains(Serial.output, "#E 20 CAL_DONE")); CHECK(fakeWrites == 0);
            send("@24 SAVE\n"); CHECK(fakeStored == -120); CHECK(fakeWrites == 1);
        }},
        {"calibration absent reference rejected", [] {
            reset(); calZero(); send("@22 CAL LOAD\n"); advance(2300, 1000);
            CHECK(contains(Serial.output, "SIGNAL_TOO_SMALL")); CHECK(getCalibrationFactor() == DEFAULT_CALIBRATION_FACTOR);
            CHECK(getDeviceSnapshot().offset == -1000); CHECK(fakeWrites == 0);
        }},
        {"calibration cancel at every stage", [] {
            for (int stage = 0; stage < 5; ++stage) {
                reset(); send("@20 CAL START 275\n");
                if (stage >= 1) send("@21 CAL ZERO\n");
                if (stage >= 2) advance(2300, 1000);
                if (stage >= 3) send("@22 CAL LOAD\n");
                if (stage >= 4) advance(2300, -32000);
                CHECK(contains(send("@30 CAL CANCEL\n"), "OK"));
                CHECK(contains(Serial.output, "CAL_CANCELLED")); CHECK(operationsWeighing());
                CHECK(getCalibrationFactor() == DEFAULT_CALIBRATION_FACTOR); CHECK(getDeviceSnapshot().offset == -1000);
            }
        }},
        {"busy mutations, diagnostic commands and stream during CAL", [] {
            reset(); send("@1 CAL START 275\n");
            CHECK(contains(send("@2 SET FACTOR -120\n"), "BUSY"));
            CHECK(contains(send("@3 TARE\n"), "BUSY")); CHECK(contains(send("@4 SAVE\n"), "BUSY"));
            CHECK(contains(send("@5 LOAD\n"), "BUSY")); CHECK(contains(send("@6 AUTO_TARE ON\n"), "BUSY"));
            CHECK(contains(send("@7 CAL STATUS\n"), "CAL_WAIT_ZERO")); CHECK(contains(send("@8 STREAM OFF\n"), "OK"));
        }},
        {"user timeout preserves configuration", [] {
            reset(); send("@1 CAL START 275\n"); advance(61000, 1000);
            CHECK(contains(Serial.output, "USER_TIMEOUT")); CHECK(operationsWeighing()); CHECK(getDeviceSnapshot().offset == -1000);
        }},
        {"noisy tare fails, old zero survives", [] {
            reset(); send("@1 TARE\n");
            for (int i = 0; i < 52; ++i) advance(100, i % 2 ? 2000 : 1000);
            CHECK(contains(Serial.output, "UNSTABLE")); CHECK(getDeviceSnapshot().offset == -1000);
        }},
        {"sensor timeout, stale S suppression, recovery without re-tare", [] {
            reset(); advance(1000, -10822); const int32_t offset = getDeviceSnapshot().offset;
            advance(1100, 0, false); CHECK(!getDeviceSnapshot().weightValid); CHECK(!getDeviceSnapshot().stable);
            CHECK(std::string(getOperationStatus().result) == "OK");
            Serial.output.clear(); advance(300, 0, false); CHECK(Serial.output.empty());
            advance(1300, -10822); CHECK(operationsWeighing()); CHECK(getDeviceSnapshot().offset == offset);
        }},
        {"sensor failure during calibration and ADC rail rejection", [] {
            reset(); calZero(); send("@22 CAL LOAD\n"); advance(1200, 0, false);
            CHECK(contains(Serial.output, "CAL_FAILED error=SENSOR_TIMEOUT")); CHECK(getCalibrationFactor() == DEFAULT_CALIBRATION_FACTOR);
            advance(500, 1000); send("@30 TARE\n"); advance(100, 8388607);
            CHECK(contains(Serial.output, "TARE_FAILED error=ADC_RANGE")); CHECK(getDeviceSnapshot().offset == -1000);
        }},
        {"boot without sensor remains command-responsive", [] {
            reset(false); advance(1500, 0, false);
            CHECK(contains(send("@1 STATUS\n"), "sensor=SENSOR_TIMEOUT")); CHECK(!getDeviceSnapshot().weightValid);
            advance(3500, 1000); CHECK(operationsWeighing()); CHECK(getDeviceSnapshot().offset == -1000);
        }},
        {"auto tare cannot erase 20g near timer", [] {
            reset(); send("@1 AUTO_TARE ON\n"); advance(9900, 1000);
            const int32_t offset = getDeviceSnapshot().offset;
            advance(1200, -1364); CHECK(getDeviceSnapshot().offset == offset);
            CHECK(getDeviceSnapshot().outputWeight > 19);
        }},
        {"duplicate TARE ACK replay, one operation", [] {
            reset(); send("@10 TARE\n"); advance(1100, 2000);
            CHECK(contains(send("@10 TARE\n"), "OK")); advance(1200, 2000);
            CHECK(contains(Serial.output, "#E 10 TARE_DONE")); CHECK(operationsWeighing());
            CHECK(contains(send("@10 CAL START 275\n"), "ID_CONFLICT"));
        }},
        {"active job ID survives cache turnover and 30 seconds", [] {
            reset(); send("@10 CAL START 275\n"); advance(31000, 1000);
            for (unsigned id = 20; id < 32; ++id) send("@" + std::to_string(id) + " STATUS\n");
            CHECK(contains(send("@10 CAL START 275\n"), "OK accepted=1"));
            CHECK(getOperationStatus().mode == ScaleMode::CAL_WAIT_ZERO);
        }},
        {"frame bounds, poison remainder and idle timeout", [] {
            reset(); const float factor = getCalibrationFactor();
            CHECK(contains(send("@1 " + std::string(150, 'X') + "@2 SET FACTOR -120\n@3 GET FACTOR\n"), "LINE_TOO_LONG"));
            CHECK(getCalibrationFactor() == factor);
            send("@4 " + std::string(150, 'X')); advance(1100, 1000);
            send("@5 SET FACTOR -120\n@6 STATUS\n"); CHECK(getCalibrationFactor() == factor);
            CHECK(contains(Serial.output, "#R 6 OK"));
            send("@7 SET FACTOR"); advance(1100, 1000); CHECK(contains(Serial.output, "BAD_FRAME"));
            CHECK(contains(send("@8 GET FACTOR\n"), "OK"));
        }},
        {"invalid ID/extra args/control chars never mutate", [] {
            reset(); const float factor = getCalibrationFactor();
            for (const auto& line : {"@0 TARE\n", "@65536 TARE\n", "@1 SET FACTOR -120 junk\n", "@2 TARE junk\n", "@3 CAL START 0\n", "@4 CAL START NAN\n"})
                CHECK(contains(send(line), "ERR"));
            std::string control = "@5 SET FACTOR -120"; control.push_back('\0'); control += "@6 TARE\n";
            CHECK(contains(send(control), "BAD_FRAME")); CHECK(getCalibrationFactor() == factor); CHECK(operationsWeighing());
        }},
        {"fragmented and back-to-back requests", [] {
            reset(); send("@1 SET FAC"); CHECK(getCalibrationFactor() == DEFAULT_CALIBRATION_FACTOR);
            CHECK(contains(send("TOR -120\r\n@2 GET FACTOR\n"), "factor=-120")); CHECK(getCalibrationFactor() == -120);
        }},
        {"95-byte LF/CRLF frames and embedded CR", [] {
            reset(); std::string request = "@1 GET FACTOR"; request.resize(95, ' ');
            CHECK(contains(send(request + "\r\n"), "#R 1 OK"));
            request = "@2 GET FACTOR"; request.resize(95, ' ');
            CHECK(contains(send(request + "\n"), "#R 2 OK"));
            CHECK(contains(send("@3 TARE\rX\n"), "BAD_FRAME")); CHECK(operationsWeighing());
        }},
        {"large finite weights remain within TX frame bounds", [] {
            reset(); send("@1 SET FACTOR 0.01\n"); advance(1200, 7000000);
            auto reply = send("@2 STATUS\n"); CHECK(contains(reply, "#R 2 OK")); CHECK(!contains(reply, "INTERNAL_ERROR"));
            CHECK(std::isfinite(getDeviceSnapshot().outputWeight));
        }},
        {"TX backpressure and byte-wise output preserve every ACK", [] {
            reset(); Serial.capacity = 0;
            for (unsigned id = 1; id <= 24; ++id) Serial.feed("@" + std::to_string(id) + " SET FACTOR -" + std::to_string(120 + id) + "\n");
            advance(80, 1000); CHECK(!Serial.input.empty()); CHECK(getCalibrationFactor() != -144);
            Serial.capacity = 1; advance(20000, 1000);
            for (unsigned id = 1; id <= 24; ++id) CHECK(contains(Serial.output, "#R " + std::to_string(id) + " OK factor="));
            CHECK(Serial.input.empty()); CHECK(getCalibrationFactor() == -144);
        }},
        {"64-bit calibration sums support wide ADC difference", [] {
            reset(); send("@1 CAL START 100\n"); send("@2 CAL ZERO\n"); advance(2300, -7000000);
            send("@3 CAL LOAD\n"); advance(2300, 7000000);
            CHECK(getOperationStatus().mode == ScaleMode::CAL_REVIEW); CHECK(getOperationStatus().candidate == 140000);
        }},
        {"millis rollover", [] {
            reset(true, 0xfffff000u); send("@1 TARE\n"); advance(2300, 1234);
            CHECK(operationsWeighing()); CHECK(getDeviceSnapshot().offset == -1234);
        }},
    };
    unsigned passed = 0;
    for (const auto& test : tests) {
        try { test.second(); ++passed; std::cout << "PASS " << test.first << '\n'; }
        catch (const std::exception& error) { std::cerr << "FAIL " << test.first << ": " << error.what() << '\n'; return 1; }
    }
    std::cout << passed << " firmware tests passed\n";
}
