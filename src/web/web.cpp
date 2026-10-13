#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>

#include "config.h"
#include "device/device.h"
#include "calibration/calibration.h"

extern bool calibrationMode;
extern float calibration_factor;

static WebServer server(80);
static unsigned long lastWifiAttempt = 0;
static bool servicesStarted = false;
static String logBuffer;
static const size_t LOG_MAX_LOG_LENGTH = 4000;

static const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <title>Меню отладки весов</title>
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <style>
    body { font-family: system-ui, sans-serif; margin: 0; padding: 1rem; background: #111; color: #eee; }
    .card { background: #1e1e1e; border-radius: 8px; padding: 1rem 1.25rem; margin-bottom: 1rem; box-shadow: 0 2px 6px rgba(0,0,0,0.4); }
    h1 { font-size: 1.4rem; margin: 0 0 0.5rem; }
    h2 { font-size: 1.1rem; margin: 0 0 0.5rem; }
    .value { font-size: 2.4rem; font-weight: 600; }
    .label { opacity: 0.7; font-size: 0.9rem; }
    .row { display: flex; gap: 1rem; flex-wrap: wrap; }
    .row > div { flex: 1 1 160px; }
    button { padding: 0.4rem 0.9rem; border-radius: 999px; border: none; background: #2563eb; color: #fff; cursor: pointer; font-size: 0.9rem; }
    button:disabled { opacity: 0.5; cursor: default; }
    input { padding: 0.35rem 0.6rem; border-radius: 999px; border: 1px solid #333; background: #111; color: #eee; width: 5rem; }
    .status-dot { width: 10px; height: 10px; border-radius: 50%; display: inline-block; margin-right: 0.4rem; }
    .status-ok { background: #22c55e; }
    .status-warn { background: #facc15; }
    .status-bad { background: #ef4444; }
    code { background: #111827; padding: 2px 5px; border-radius: 4px; }
    .api { font-size: 0.85rem; opacity: 0.85; }
  </style>
</head>
<body>
  <div class="card">
    <h1>Весы Vesy — отладочный интерфейс</h1>
    <div class="row">
      <div>
        <div class="label">Текущий вес</div>
        <div class="value" id="weight">--.-</div>
        <div class="label">Единицы: граммы</div>
      </div>
      <div>
        <div class="label">Калибровочный коэффициент</div>
        <div class="value" id="calFactor">--</div>
        <div class="label" id="emptyState"></div>
      </div>
      <div>
        <div class="label">Статус</div>
        <div id="statusLine">
          <span class="status-dot status-warn" id="statusDot"></span>
          <span id="statusText">Connecting...</span>
        </div>
      </div>
    </div>
  </div>

  <div class="card">
    <h2>Калибровка</h2>
    <div style="display:flex;align-items:center;gap:0.5rem;flex-wrap:wrap;">
      <span class="label">Эталонный груз (г):</span>
      <input id="refWeight" type="number" step="1" value="275">
      <button id="calBtn" onclick="startCalibration()">Начать калибровку</button>
    </div>
    <div class="label" id="calInfo" style="margin-top:0.4rem;"></div>
  </div>

  <div class="card api">
    <h2>HTTP API</h2>
    <div><code>GET /api/status</code> – JSON с полями <code>weight</code>, <code>empty</code>, <code>calibration_factor</code>, <code>calibrating</code>, <code>calibration_message</code></div>
    <div><code>POST /api/calibrate?ref=&lt;grams&gt;</code> – запуск калибровки с указанным эталонным весом</div>
  </div>

  <div class="card">
    <h2>WebSerial (логи)</h2>
    <pre id="log" style="max-height:200px;overflow:auto;font-family:monospace;font-size:0.8rem;background:#000;padding:0.5rem;border-radius:4px;"></pre>
  </div>

  <script>
    async function fetchStatus() {
      try {
        const res = await fetch('/api/status');
        if (!res.ok) throw new Error('HTTP ' + res.status);
        const data = await res.json();
        document.getElementById('weight').textContent = data.weight.toFixed(2);
        document.getElementById('calFactor').textContent = data.calibration_factor.toFixed(6);
        document.getElementById('emptyState').textContent = data.empty ? 'Весы пустые' : 'Есть нагрузка';

        const dot = document.getElementById('statusDot');
        const text = document.getElementById('statusText');
        const calBtn = document.getElementById('calBtn');
        const calInfo = document.getElementById('calInfo');

        const msg = data.calibration_message || '';
        calInfo.textContent = msg;

        if (data.calibrating) {
          dot.className = 'status-dot status-warn';
          text.textContent = 'Идет калибровка';
          calBtn.disabled = true;
        } else {
          dot.className = 'status-dot status-ok';
          text.textContent = 'Онлайн';
          calBtn.disabled = false;
        }
      } catch (e) {
        const dot = document.getElementById('statusDot');
        const text = document.getElementById('statusText');
        dot.className = 'status-dot status-bad';
        text.textContent = 'Ошибка получения статуса';
      }
    }

    async function startCalibration() {
      const refInput = document.getElementById('refWeight');
      const ref = parseFloat(refInput.value) || 275;
      const calBtn = document.getElementById('calBtn');
      const calInfo = document.getElementById('calInfo');
      try {
        calBtn.disabled = true;
        calInfo.textContent = 'Запуск калибровки...';
        const res = await fetch('/api/calibrate?ref=' + encodeURIComponent(ref), { method: 'POST' });
        if (!res.ok) {
          const txt = await res.text();
          throw new Error(txt || ('HTTP ' + res.status));
        }
        // Further messages will come from /api/status -> calibration_message
      } catch (e) {
        calInfo.textContent = 'Не удалось запустить калибровку: ' + e.message;
      } finally {
        // button re-enabled via status polling
      }
    }

    async function fetchLog() {
      try {
        const res = await fetch('/api/log');
        if (!res.ok) throw new Error('HTTP ' + res.status);
        const data = await res.json();
        const logEl = document.getElementById('log');
        logEl.textContent = data.log || '';
        logEl.scrollTop = logEl.scrollHeight;
      } catch (e) {
        // ignore log errors, status card already shows connection problems
      }
    }

    function poll() {
      fetchStatus();
      fetchLog();
    }

    poll();
    setInterval(poll, 1000);
  </script>
</body>
</html>
)rawliteral";

static void appendLog(const String& line) {
    logBuffer += line + "\n";
    if (logBuffer.length() > LOG_MAX_LOG_LENGTH) {
        logBuffer.remove(0, logBuffer.length() - LOG_MAX_LOG_LENGTH);
    }
}

void webLog(const String& line) {
    Serial.println(line);
    appendLog(line);
}

static void startNetworkServices() {
    webLog(String("WiFi connected, IP: ") + WiFi.localIP().toString());

    if (!servicesStarted) {
        if (MDNS.begin(WIFI_HOSTNAME)) {
            webLog(String("mDNS started: http://") + WIFI_HOSTNAME + ".local/");
        }

        ArduinoOTA.setHostname(WIFI_HOSTNAME);
        ArduinoOTA.onStart([]() {
            webLog("OTA start");
        });
        ArduinoOTA.onEnd([]() {
            webLog("OTA end, rebooting");
        });
        ArduinoOTA.onError([](ota_error_t error) {
            webLog(String("OTA error: ") + String((int)error));
        });
        ArduinoOTA.begin();
        webLog("OTA ready");

        servicesStarted = true;
    }
}

static void handleRoot() {
    server.send_P(200, "text/html", INDEX_HTML);
}

static void handleStatus() {
    const float weight = getFilteredWeight();
    const bool empty = isEmpty();
    const bool calibrating = calibrationMode;
    const char* calMsg = getCalibrationStatusMessage();

    String json = "{";
    json += "\"weight\":" + String(weight, 2) + ",";
    json += "\"empty\":" + String(empty ? "true" : "false") + ",";
    json += "\"calibration_factor\":" + String(calibration_factor, 6) + ",";
    json += "\"calibrating\":" + String(calibrating ? "true" : "false") + ",";

    String msg = calMsg ? String(calMsg) : String("");
    msg.replace("\\", "\\\\");
    msg.replace("\"", "\\\"");
    json += "\"calibration_message\":\"" + msg + "\"";
    json += "}";

    server.send(200, "application/json", json);
}

static void handleLog() {
    String esc = logBuffer;
    esc.replace("\\", "\\\\");
    esc.replace("\"", "\\\"");
    esc.replace("\n", "\\n");

    String json = "{\"log\":\"" + esc + "\"}";
    server.send(200, "application/json", json);
}

static void handleCalibrate() {
    if (calibrationMode) {
        server.send(409, "application/json", "{\"error\":\"calibration already running\"}");
        return;
    }

    float ref = 275.0f;
    if (server.hasArg("ref")) {
        ref = server.arg("ref").toFloat();
        if (ref <= 0.0f) {
            ref = 275.0f;
        }
    }

    calibrationMode = true;
    calibrationSetup(ref);
    server.send(202, "application/json", "{\"message\":\"calibration started\"}");
}

void webSetup() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    webLog(String("Connecting to WiFi: ") + WIFI_SSID);

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
        delay(500);
    }
    webLog("WiFi connect attempt finished");

    if (WiFi.status() == WL_CONNECTED) {
        startNetworkServices();
    } else {
        webLog("WiFi connect timeout, will retry in background");
    }

    server.on("/", handleRoot);
    server.on("/api/status", HTTP_GET, handleStatus);
    server.on("/api/log", HTTP_GET, handleLog);
    server.on("/api/calibrate", HTTP_POST, handleCalibrate);
    server.begin();
    webLog("HTTP server started");
}

void webLoop() {
    server.handleClient();

    if (WiFi.status() == WL_CONNECTED) {
        if (!servicesStarted) {
            startNetworkServices();
        }
        ArduinoOTA.handle();
    } else {
        if (millis() - lastWifiAttempt >= 5000) {
            lastWifiAttempt = millis();
            webLog("WiFi not connected, retrying...");
            WiFi.disconnect();
            WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        }
    }
}

