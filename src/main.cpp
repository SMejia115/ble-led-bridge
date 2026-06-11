#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <Preferences.h>
#include <WebServer.h>
#include <fauxmoESP.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <cctype>
#include <vector>
#include <map>
#include <memory>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "controller_settings.h"
#include "ble_control.h"

AsyncWebServer appServer(81);
WebServer setupServer(80);
fauxmoESP fauxmo;
Preferences prefs;

static constexpr char WIFI_PREF_NS[] = "wifi";
static constexpr char HOSTNAME[] = "led-bridge";
static constexpr char AP_SSID[] = "LED-Bridge-Setup";
static constexpr int STATUS_LED_PIN = 2;
static constexpr int AUX_LED_PIN = 5;
static constexpr unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000;
static constexpr unsigned long WIFI_RETRY_INTERVAL_MS = 10000;
static constexpr unsigned long STATUS_INTERVAL_MS = 5000;
static constexpr unsigned long INDICATOR_DURATION = 10000;
static constexpr char STRIP_CONFIG_NS[] = "stripcfg";
static constexpr char STRIP_STATE_NS[] = "stripstate";
static constexpr uint8_t MAX_STRIP_ENTRIES = 8;
static constexpr int BLE_SCAN_SECONDS = 4;
static constexpr unsigned long BLE_SCAN_TTL_MS = 120000;

struct StripDefinition {
    String name;
    String mac;
};

struct BleScanEntry {
    String name;
    String mac;
    int rssi;
};

struct LedState {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t brightness;
};

enum class EffectType { NONE, MUSIC, POLICE, STROBE };

struct EffectState {
    EffectType type = EffectType::NONE;
    unsigned long nextChange = 0;
    unsigned long interval = 200;
    unsigned long endTime = 0;
    int step = 0;
    bool toggle = false;
};

struct AlexaCommand {
    bool pending = false;
    bool state = true;
    unsigned char brightness = 0xFF;
    bool hasRgb = false;
    byte rgb[3] = {0, 0, 0};
};

struct StripInstance {
    StripDefinition definition;
    LedState lastState{255, 0, 0, 0xFF};
    bool power = true;
    bool bleConnected = false;
    EffectState effect;
    AlexaCommand alexaAction;
    unsigned char fauxmoDeviceId = 0;
    uint8_t index = 0;
    BLEControl control;
};

static std::vector<std::unique_ptr<StripInstance>> strips;
static std::map<unsigned char, StripInstance*> alexaDeviceMap;
static std::vector<BleScanEntry> bleScanResults;
static unsigned long lastBleScanAt = 0;
static bool bleScanInProgress = false;
static TaskHandle_t bleScanTaskHandle = nullptr;

static unsigned long lastStatusLog = 0;
static unsigned long indicatorStart = 0;
static bool indicatorActive = false;
static bool indicatorShown = false;
static bool indicatorDone = false;
static bool wifiSetupMode = false;
static bool wifiConnected = false;
static unsigned long wifiRetryAt = 0;
static bool restartPending = false;
static unsigned long restartAt = 0;
static std::vector<StripDefinition> stripDefinitions;

struct WifiCredentials {
    String ssid;
    String password;
    bool valid() const { return ssid.length() > 0; }
};

static const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="es">
<head>
    <meta charset="UTF-8" />
    <meta name="viewport" content="width=device-width, initial-scale=1" />
    <title>LED Bridge</title>
    <style>
        :root {
            color-scheme: dark;
        }
        body {font-family:'Space Grotesk', system-ui, -apple-system, sans-serif; background: radial-gradient(circle at top, rgba(0,104,201,.35), transparent 40%), #05030b; color:#f8fbff; display:flex; justify-content:center; align-items:center; min-height:100vh; margin:0; padding:1.5rem;}
        .card{width:100%; max-width:440px; background:rgba(8,10,25,.92); border:1px solid rgba(255,255,255,.08); border-radius:28px; padding:2.2rem; box-shadow:0 35px 55px rgba(0,0,0,.65); backdrop-filter:blur(26px);}
        h1{margin:0 0 .25rem; font-size:2rem; letter-spacing:.05em;}
        p.note{margin:0 0 1rem; color:#9aa0bd; font-size:.9rem;}
        label{display:block; font-size:.78rem; letter-spacing:.3em; text-transform:uppercase; color:#5a6c8c; margin-top:1.2rem;}
        input[type=color]{width:100%; height:72px; border:none; border-radius:18px; cursor:pointer;}
        input[type=range]{width:100%;}
        button{width:100%; margin-top:1.3rem; border:none; background:linear-gradient(135deg,#1e64ff,#00d4ff); color:#fff; padding:.9rem 1rem; border-radius:999px; font-size:1rem; font-weight:600; letter-spacing:.05em; cursor:pointer; transition:transform .2s ease, box-shadow .2s ease; box-shadow:0 12px 28px -14px rgba(0,212,255,.9);} 
        button:active{transform:translateY(2px); box-shadow:0 9px 20px -10px rgba(0,212,255,.8);}
        .status{margin-top:1rem; font-size:.85rem; color:#bedeff; min-height:1.2rem;}
        .strip-row{display:flex; gap:.8rem; align-items:center; margin-top:.2rem; flex-wrap:wrap;}
        select{flex:1; background:rgba(255,255,255,.04); border:1px solid rgba(255,255,255,.08); border-radius:12px; padding:.75rem; color:#fff;}
        .strip-status{font-size:.78rem; padding:.35rem .75rem; border-radius:999px; border:1px solid rgba(255,255,255,.15); background:rgba(255,255,255,.04);}
        .presets{display:grid; grid-template-columns:repeat(2,minmax(0,1fr)); gap:.75rem; margin-top:1.5rem;}
        .preset{border:none; border-radius:14px; padding:.9rem; font-weight:600; letter-spacing:.05em; text-transform:uppercase; font-size:.75rem; cursor:pointer; background:rgba(255,255,255,.05); color:#fff; transition:transform .2s ease, box-shadow .2s ease;}
        .preset:active{transform:translateY(2px);}
        .preset[data-color="0,102,255"]{background:#0f5fff; color:#fff; box-shadow:0 12px 25px -14px rgba(15,95,255,.9);}
        .preset[data-color="102,255,255"]{background:#02c8ff; color:#04132b;}
        .preset[data-color="0,255,102"]{background:#00ff9d; color:#02140c;}
        .preset[data-color="255,255,255"]{background:#fff; color:#010101;}
        .effects{display:grid; grid-template-columns:repeat(2,minmax(0,1fr)); gap:.65rem; margin-top:1.1rem;}
        .effect{border:none; border-radius:12px; padding:.75rem; background:rgba(255,255,255,.08); color:#fff; font-weight:600; letter-spacing:.04em; cursor:pointer; text-transform:uppercase; font-size:.7rem; transition:transform .2s ease;}
        .effect:active{transform:translateY(2px);}
        .alexa-note{margin-top:1.5rem; font-size:.78rem; color:#7b86a9; text-align:center;}
    </style>
</head>
<body>
    <div class="card">
        <h1>LED Bridge</h1>
        <p class="note">Controla tus tiras BLE desde el navegador, Alexa o presets rápidos.</p>
        <label for="strip-select">Selecciona una tira</label>
        <div class="strip-row">
            <select id="strip-select" disabled></select>
            <span class="strip-status" id="strip-status">Sin tiras configuradas</span>
        </div>
        <label for="color">Elige color</label>
        <input type="color" id="color" value="#ff6a00" />
        <label for="brightness">Brillo (<span id="brightness-value">100</span>%)</label>
        <input type="range" id="brightness" min="10" max="100" value="100" />
        <button id="apply">Aplicar color</button>
        <div class="status" id="status">Conectando al controlador...</div>
        <div class="presets">
            <button class="preset" data-color="0,102,255">Azul Neón</button>
            <button class="preset" data-color="102,255,255">Ciclo Marino</button>
            <button class="preset" data-color="0,255,102">Verde Futuro</button>
            <button class="preset" data-color="255,255,255">Blanco Puro</button>
        </div>
        <div class="effects">
            <button class="effect" data-effect="music">Al ritmo</button>
            <button class="effect" data-effect="police">Luces de policía</button>
            <button class="effect" data-effect="strobe">Estroboscópico</button>
            <button class="effect" data-effect="stop">Detener</button>
        </div>
        <p class="alexa-note">Alexa entiende frases como “pon la luz led azul”, y puedes combinar rutinas HTTP para escenas avanzadas.</p>
    </div>

    <script>
        const colorPicker = document.getElementById('color');
        const brightnessSlider = document.getElementById('brightness');
        const brightnessValue = document.getElementById('brightness-value');
        const status = document.getElementById('status');
        const stripSelect = document.getElementById('strip-select');
        const stripStatus = document.getElementById('strip-status');

        let strips = [];

        const toHex = (r, g, b) => `#${((1 << 24) + (r << 16) + (g << 8) + b).toString(16).slice(1)}`;

        const setSelectedStrip = (stripId) => {
            const selected = strips.find(strip => strip.id === stripId);
            if (!selected) {
                stripStatus.textContent = 'Sin tiras configuradas';
                return;
            }
            colorPicker.value = toHex(selected.red, selected.green, selected.blue);
            const percent = Math.round((selected.brightness / 255) * 100);
            brightnessSlider.value = Math.max(10, percent);
            brightnessValue.textContent = brightnessSlider.value;
            stripStatus.textContent = `${selected.name} • ${selected.connected ? 'BLE conectado' : 'Sin conexión BLE'} • ${selected.power ? 'Encendida' : 'Apagada'}`;
        };

        const refreshStripOptions = (list) => {
            stripSelect.innerHTML = '';
            list.forEach(strip => {
                const option = document.createElement('option');
                option.value = strip.id;
                option.textContent = `${strip.name} (${strip.mac})`;
                stripSelect.appendChild(option);
            });
            stripSelect.disabled = list.length === 0;
            if (list.length) {
                setSelectedStrip(parseInt(stripSelect.value, 10));
            }
        };

        const fetchStrips = async () => {
            try {
                const response = await fetch('/api/strips');
                const data = await response.json();
                strips = data;
                if (!strips.length) {
                    stripStatus.textContent = 'Registra al menos una tira desde el setup';
                    stripSelect.disabled = true;
                    return;
                }
                const currentId = parseInt(stripSelect.value, 10);
                refreshStripOptions(strips);
                if (strips.some(strip => strip.id === currentId)) {
                    stripSelect.value = currentId;
                    setSelectedStrip(currentId);
                }
            } catch (error) {
                stripStatus.textContent = 'No se pudieron cargar las tiras';
            }
        };

        const getSelectedStrip = () => {
            const stripId = parseInt(stripSelect.value, 10);
            return strips.find(strip => strip.id === stripId);
        };

        const updateStatusMessage = (message, ok = true) => {
            status.textContent = message;
            status.style.color = ok ? '#bedeff' : '#ff8a8a';
        };

        const sendColorCommand = async (red, green, blue, brightness) => {
            const strip = getSelectedStrip();
            if (!strip) {
                updateStatusMessage('Selecciona una tira', false);
                return;
            }
            const response = await fetch(`/api/color?strip=${strip.id}&red=${red}&green=${green}&blue=${blue}&brightness=${brightness}`);
            const data = await response.json();
            updateStatusMessage(data.message + (data.success ? ' • Estado OK' : ' • Revisar LED'), data.success);
            stripStatus.textContent = `${strip.name} • ${strip.connected ? 'BLE conectado' : 'Sin conexión BLE'} • ${strip.power ? 'Encendida' : 'Apagada'}`;
        };

        brightnessSlider.addEventListener('input', () => {
            brightnessValue.textContent = brightnessSlider.value;
        });

        document.getElementById('apply').addEventListener('click', () => {
            const hex = colorPicker.value;
            const red = parseInt(hex.substring(1, 3), 16);
            const green = parseInt(hex.substring(3, 5), 16);
            const blue = parseInt(hex.substring(5, 7), 16);
            const brightness = Math.round((parseInt(brightnessSlider.value) / 100) * 0xff);
            sendColorCommand(red, green, blue, brightness);
        });

        document.querySelectorAll('.preset').forEach(btn => {
            btn.addEventListener('click', () => {
                const [r, g, b] = btn.dataset.color.split(',').map(Number);
                brightnessSlider.value = 100;
                brightnessValue.textContent = 100;
                colorPicker.value = toHex(r, g, b);
                sendColorCommand(r, g, b, 0xff);
            });
        });

        document.querySelectorAll('.effect').forEach(btn => {
            btn.addEventListener('click', () => {
                const effect = btn.dataset.effect;
                const strip = getSelectedStrip();
                if (!strip) {
                    updateStatusMessage('Selecciona una tira', false);
                    return;
                }
                fetch(`/api/effect?strip=${strip.id}&name=${effect}`)
                    .then(res => res.json())
                    .then(data => updateStatusMessage(data.message));
            });
        });

        stripSelect.addEventListener('change', () => {
            const selectedId = parseInt(stripSelect.value, 10);
            setSelectedStrip(selectedId);
        });

        async function refreshStatus() {
            const response = await fetch('/api/status');
            const data = await response.json();
            status.textContent = data.connected ? 'Wi-Fi y BLE OK' : 'Verifica conexión Wi-Fi/BLE';
        }

        fetchStrips();
        refreshStatus();
        setInterval(refreshStatus, 5000);
        setInterval(fetchStrips, 10000);
    </script>
</body>
</html>
)rawliteral";

static String buildJson(bool success, const String& message) {
    return String("{\"success\":") + (success ? "true" : "false") + ",\"message\":\"" + message + "\"}";
}

static String buildJsonStatus(bool connected, const String& ip, const String& mode) {
    return String("{\"connected\":") + (connected ? "true" : "false") +
           ",\"ip\":\"" + ip + "\",\"mode\":\"" + mode + "\"}";
}

static int clampColorParam(AsyncWebServerRequest* request, const char* name) {
    if (!request->hasParam(name)) {
        return 0;
    }
    int value = request->getParam(name)->value().toInt();
    return constrain(value, 0, 255);
}

static WifiCredentials loadWifiCredentials() {
    WifiCredentials creds;
    prefs.begin(WIFI_PREF_NS, false);
    creds.ssid = prefs.getString("ssid", "");
    creds.password = prefs.getString("password", "");
    prefs.end();
    return creds;
}

static void saveWifiCredentials(const String& ssid, const String& password) {
    prefs.begin(WIFI_PREF_NS, false);
    prefs.putString("ssid", ssid);
    prefs.putString("password", password);
    prefs.end();
    Serial.printf("Guardadas credenciales WiFi: %s\n", ssid.c_str());
}

static void clearWifiCredentials() {
    prefs.begin(WIFI_PREF_NS, false);
    prefs.clear();
    prefs.end();
}

static void clearStripDefinitionsPrefs() {
    prefs.begin(STRIP_CONFIG_NS, false);
    prefs.clear();
    prefs.end();
    prefs.begin(STRIP_STATE_NS, false);
    prefs.clear();
    prefs.end();
    stripDefinitions.clear();
    strips.clear();
}

static String escapeForJson(const String& value) {
    String escaped;
    for (size_t i = 0; i < value.length(); ++i) {
        char c = value[i];
        if (c == '\\' || c == '"') {
            escaped += '\\';
        }
        escaped += c;
    }
    return escaped;
}

static String escapeForSingleQuotedJS(const String& value) {
    String escaped;
    for (size_t i = 0; i < value.length(); ++i) {
        char c = value[i];
        if (c == '\\') {
            escaped += "\\\\";
        } else if (c == '\'') {
            escaped += "\\'";
        } else if (c == '\n') {
            escaped += "\\n";
        } else if (c == '\r') {
            escaped += "\\r";
        } else {
            escaped += c;
        }
    }
    return escaped;
}

static String buildStripListJson() {
    String json = "[";
    for (size_t i = 0; i < stripDefinitions.size(); ++i) {
        if (i > 0) {
            json += ",";
        }
        const StripDefinition& strip = stripDefinitions[i];
        json += "{\"name\":\"" + escapeForJson(strip.name) + "\",\"mac\":\"" + escapeForJson(strip.mac) + "\"}";
    }
    json += "]";
    return escapeForSingleQuotedJS(json);
}

static String buildStripListApiJson() {
    String json = "[";
    for (size_t i = 0; i < strips.size(); ++i) {
        if (i > 0) {
            json += ",";
        }
        const StripInstance* strip = strips[i].get();
        json += "{\"id\":" + String(strip->index) +
                ",\"name\":\"" + escapeForJson(strip->definition.name) +
                "\",\"mac\":\"" + escapeForJson(strip->definition.mac) +
                "\",\"connected\":" + (strip->bleConnected ? "true" : "false") +
                ",\"power\":" + (strip->power ? "true" : "false") +
                ",\"red\":" + String(strip->lastState.red) +
                ",\"green\":" + String(strip->lastState.green) +
                ",\"blue\":" + String(strip->lastState.blue) +
                ",\"brightness\":" + String(strip->lastState.brightness) +
                "}";
    }
    json += "]";
    return json;
}

static void loadStripDefinitions() {
    stripDefinitions.clear();
    prefs.begin(STRIP_CONFIG_NS, false);
    uint8_t count = prefs.getUChar("count", 0);
    for (uint8_t i = 0; i < count; ++i) {
        String prefix = "strip_" + String(i) + "_";
        String name = prefs.getString((prefix + "name").c_str(), "");
        String mac = prefs.getString((prefix + "mac").c_str(), "");
        name.trim();
        mac.trim();
        if (mac.length() == 0) {
            continue;
        }
        stripDefinitions.push_back({ name.length() ? name : String("Tira LED"), mac });
    }
    prefs.end();
    if (stripDefinitions.empty()) {
        stripDefinitions.push_back({ String("Tira LED"), String(BLE_CONTROLLER_ADDRESS) });
    }
}

static void saveStripDefinitions() {
    prefs.begin(STRIP_CONFIG_NS, false);
    prefs.clear();
    const uint8_t count = stripDefinitions.size();
    prefs.putUChar("count", count);
    for (uint8_t i = 0; i < count; ++i) {
        const StripDefinition& strip = stripDefinitions[i];
        String prefix = "strip_" + String(i) + "_";
        prefs.putString((prefix + "name").c_str(), strip.name);
        prefs.putString((prefix + "mac").c_str(), strip.mac);
    }
    prefs.end();
}

static StripInstance* getStripByIndex(int index) {
    if (index < 0 || index >= static_cast<int>(strips.size())) {
        return nullptr;
    }
    return strips[index].get();
}

static void loadStripState(uint8_t index, StripInstance& strip) {
    prefs.begin(STRIP_STATE_NS, false);
    String prefix = "strip_" + String(index) + "_";
    strip.lastState.red = prefs.getUChar((prefix + "red").c_str(), strip.lastState.red);
    strip.lastState.green = prefs.getUChar((prefix + "green").c_str(), strip.lastState.green);
    strip.lastState.blue = prefs.getUChar((prefix + "blue").c_str(), strip.lastState.blue);
    strip.lastState.brightness = prefs.getUChar((prefix + "brightness").c_str(), strip.lastState.brightness);
    strip.power = prefs.getBool((prefix + "power").c_str(), strip.power);
    prefs.end();
    strip.index = index;
}

static void saveStripState(uint8_t index, const StripInstance& strip) {
    prefs.begin(STRIP_STATE_NS, false);
    String prefix = "strip_" + String(index) + "_";
    prefs.putUChar((prefix + "red").c_str(), strip.lastState.red);
    prefs.putUChar((prefix + "green").c_str(), strip.lastState.green);
    prefs.putUChar((prefix + "blue").c_str(), strip.lastState.blue);
    prefs.putUChar((prefix + "brightness").c_str(), strip.lastState.brightness);
    prefs.putBool((prefix + "power").c_str(), strip.power);
    prefs.end();
}

static void rebuildStripInstances() {
    strips.clear();
    alexaDeviceMap.clear();
    for (uint8_t i = 0; i < stripDefinitions.size(); ++i) {
        auto instance = std::unique_ptr<StripInstance>(new StripInstance());
        instance->definition = stripDefinitions[i];
        loadStripState(i, *instance);
        instance->control.configure(stripDefinitions[i].mac.c_str(), BLE_SERVICE_UUID, BLE_CHARACTERISTIC_UUID);
        instance->index = i;
        strips.push_back(std::move(instance));
    }
    if (strips.empty()) {
        auto fallback = std::unique_ptr<StripInstance>(new StripInstance());
        fallback->definition = { String("Tira LED"), String(BLE_CONTROLLER_ADDRESS) };
        loadStripState(0, *fallback);
        fallback->control.configure(BLE_CONTROLLER_ADDRESS, BLE_SERVICE_UUID, BLE_CHARACTERISTIC_UUID);
        fallback->index = 0;
        strips.push_back(std::move(fallback));
    }
}

static void performBleScan() {
    BLEScan* scanner = BLEDevice::getScan();
    if (!scanner) {
        bleScanResults.clear();
        lastBleScanAt = millis();
        return;
    }
    scanner->setActiveScan(true);
    scanner->setInterval(100);
    scanner->setWindow(99);
    BLEScanResults results = scanner->start(BLE_SCAN_SECONDS, false);
    bleScanResults.clear();
    for (int i = 0; i < results.getCount(); ++i) {
        BLEAdvertisedDevice device = results.getDevice(i);
        String mac = device.getAddress().toString().c_str();
        String name = device.getName().c_str();
        if (mac.length() == 0) {
            continue;
        }
        bool duplicate = false;
        for (const auto& existing : bleScanResults) {
            if (existing.mac == mac) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            continue;
        }
        bleScanResults.push_back({ name.length() ? name : String("Desconocido"), mac, device.getRSSI() });
    }
    lastBleScanAt = millis();
}

static void bleScanTask(void* params) {
    performBleScan();
    bleScanInProgress = false;
    bleScanTaskHandle = nullptr;
    vTaskDelete(nullptr);
}

static void scheduleBleScan(bool force = false) {
    if (bleScanInProgress) {
        return;
    }
    if (!force && !wifiSetupMode && !wifiConnected) {
        return;
    }
    bleScanInProgress = true;
    xTaskCreatePinnedToCore(bleScanTask, "BleScan", 4096, nullptr, 1, &bleScanTaskHandle, 1);
}

static String buildBleScanJson() {
    String json = "[";
    for (size_t i = 0; i < bleScanResults.size(); ++i) {
        if (i > 0) {
            json += ",";
        }
        const BleScanEntry& entry = bleScanResults[i];
        json += "{\"name\":\"" + escapeForJson(entry.name) + "\",\"mac\":\"" + escapeForJson(entry.mac) + "\",\"rssi\":" + String(entry.rssi) + "}";
    }
    json += "]";
    return json;
}

static String setupPage() {
    String page = R"rawliteral(
<!DOCTYPE html>
<html lang="es">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>LED Bridge Setup</title>
  <style>
    body{font-family:system-ui,-apple-system,sans-serif;background:#0c1020;color:#f5f7ff;margin:0;min-height:100vh;display:grid;place-items:center;padding:24px}
    .card{width:100%;max-width:480px;background:#121831;border:1px solid rgba(255,255,255,.08);border-radius:18px;padding:24px}
    h1{margin:0 0 8px}
    label{display:block;margin-top:14px;font-size:12px;text-transform:uppercase;letter-spacing:.12em;color:#93a4d9}
    input{width:100%;box-sizing:border-box;padding:12px 14px;border-radius:12px;border:1px solid rgba(255,255,255,.1);background:#0a0f1f;color:#fff;margin-top:8px}
    button,a{display:inline-block;width:100%;margin-top:16px;padding:12px 14px;border:0;border-radius:999px;background:#4b7bff;color:#fff;text-align:center;text-decoration:none;font-weight:700}
    .small{font-size:13px;color:#9aa8d5;line-height:1.5}
        .strip-section{margin-top:24px}
        .strip-section h2{margin:0;font-size:1rem;letter-spacing:.1em;text-transform:uppercase;color:#9aa8d5}
        .strip-entry{border:1px solid rgba(255,255,255,.08);border-radius:16px;padding:16px;margin-top:14px;background:rgba(255,255,255,.02)}
        .strip-entry label{font-size:10px;text-transform:uppercase;color:#7f8cb3;margin-top:10px}
        .strip-entry input{margin-top:4px}
        .strip-entry .remove-strip{background:#ff5f5f;}
        .strip-section button#add-strip{background:#0da0ff;}
        .ble-scan-section{margin-top:24px;border:1px solid rgba(255,255,255,.08);border-radius:18px;padding:16px;background:rgba(255,255,255,.02)}
        .ble-scan-header{display:flex;justify-content:space-between;align-items:center;gap:.5rem;}
        .ble-scan-header h2{margin:0;font-size:.9rem;letter-spacing:.1em;text-transform:uppercase;color:#9aa8d5}
        #ble-results{margin-top:12px;display:flex;flex-direction:column;gap:.75rem;}
        .ble-device{padding:12px;border-radius:14px;border:1px solid rgba(255,255,255,.05);display:flex;justify-content:space-between;align-items:center;gap:1rem;background:rgba(255,255,255,.02);}
        .ble-device span{font-size:.85rem;}
        .ble-device button{border:none;border-radius:12px;padding:.45rem .9rem;background:#0da0ff;color:#fff;font-size:.8rem;cursor:pointer;}
  </style>
</head>
<body>
  <form class="card" method="POST" action="/setup/save">
    <h1>LED Bridge</h1>
    <p class="small">Conecta el dispositivo a tu Wi-Fi y registra cada tira BLE para que Alexa las descubra fácilmente.</p>
    <label for="ssid">SSID</label>
    <input id="ssid" name="ssid" autocomplete="off" required />
    <label for="password">Password</label>
    <input id="password" name="password" type="password" autocomplete="off" />
    <div class="strip-section">
      <h2>Tiras conectadas</h2>
      <div id="strip-list"></div>
      <button type="button" id="add-strip">Agregar otra tira</button>
      <input type="hidden" name="strip_count" id="strip-count" value="0" />
    </div>
    <div class="ble-scan-section">
      <div class="ble-scan-header">
        <h2>Detectar tiras cercanas</h2>
        <button type="button" id="ble-scan">Buscar ahora</button>
      </div>
      <p class="small" id="ble-scan-status">Presiona buscar para descubrir qué tiras están cerca.</p>
      <div id="ble-results"></div>
    </div>
    <button type="submit">Guardar y conectar</button>
  </form>
  <script>
    (function() {
      const stripList = document.getElementById('strip-list');
      const stripCount = document.getElementById('strip-count');
      const addButton = document.getElementById('add-strip');
      const existingStrips = JSON.parse('{stripData}');

      function refreshStripIndices() {
        const entries = stripList.querySelectorAll('.strip-entry');
        entries.forEach((entry, index) => {
          entry.dataset.index = index;
          entry.querySelector('[data-name-field]').name = `strip_name_${index}`;
          entry.querySelector('[data-mac-field]').name = `strip_mac_${index}`;
        });
        stripCount.value = entries.length;
      }

      function addStripEntry(name = '', mac = '') {
        const entry = document.createElement('div');
        entry.className = 'strip-entry';
        const nameLabel = document.createElement('label');
        nameLabel.textContent = 'Nombre (alias Alexa)';
        entry.appendChild(nameLabel);
        const nameInput = document.createElement('input');
        nameInput.type = 'text';
        nameInput.dataset.nameField = 'true';
        nameInput.placeholder = 'Sala, Cocina...';
        nameInput.value = name;
        entry.appendChild(nameInput);
        const macLabel = document.createElement('label');
        macLabel.textContent = 'Dirección MAC BLE';
        entry.appendChild(macLabel);
        const macInput = document.createElement('input');
        macInput.type = 'text';
        macInput.dataset.macField = 'true';
        macInput.placeholder = 'AA:BB:CC:DD:EE:FF';
        macInput.value = mac;
        entry.appendChild(macInput);
        const removeBtn = document.createElement('button');
        removeBtn.type = 'button';
        removeBtn.className = 'remove-strip';
        removeBtn.textContent = 'Eliminar';
        removeBtn.addEventListener('click', () => {
          entry.remove();
          refreshStripIndices();
        });
        entry.appendChild(removeBtn);
        stripList.appendChild(entry);
        refreshStripIndices();
      }

      addButton.addEventListener('click', () => addStripEntry());
      if (existingStrips.length) {
        existingStrips.forEach(strip => addStripEntry(strip.name, strip.mac));
      } else {
        addStripEntry('Tira LED', '');
      }

      const bleResults = document.getElementById('ble-results');
      const bleScanStatus = document.getElementById('ble-scan-status');
      const bleScanButton = document.getElementById('ble-scan');
      let bleRefreshTimer = null;

      const createBleRow = (device) => {
        const wrapper = document.createElement('div');
        wrapper.className = 'ble-device';
        const info = document.createElement('span');
        info.textContent = `${device.name} • ${device.mac} • RSSI ${device.rssi}`;
        const btn = document.createElement('button');
        btn.type = 'button';
        btn.textContent = 'Agregar';
        btn.addEventListener('click', () => {
          addStripEntry(device.name, device.mac);
        });
        wrapper.appendChild(info);
        wrapper.appendChild(btn);
        return wrapper;
      };

      const renderBleDevices = (devices) => {
        bleResults.innerHTML = '';
        if (!devices.length) {
          bleResults.innerHTML = '<p class="small">No se detectaron tiras BLE aún. Intenta volver a escanear.</p>';
          return;
        }
        devices.forEach(device => bleResults.appendChild(createBleRow(device)));
      };

      const scheduleBleRefresh = (delay, refresh) => {
        if (bleRefreshTimer) {
          clearTimeout(bleRefreshTimer);
        }
        bleRefreshTimer = setTimeout(() => fetchBleDevices(refresh), delay);
      };

      const fetchBleDevices = async (refresh = false) => {
        try {
          bleScanStatus.textContent = refresh ? 'Escaneando... espera unos segundos' : 'Actualizando lista...';
          const url = `/api/ble-discovery${refresh ? '?refresh=1' : ''}`;
          const response = await fetch(url);
          const payload = await response.json();
          renderBleDevices(payload.results || []);
          if (payload.status === 'scanning') {
            bleScanStatus.textContent = 'Escaneando (20s)...';
            scheduleBleRefresh(3000, false);
          } else {
            bleScanStatus.textContent = `Último escaneo: ${payload.timestamp ? new Date(payload.timestamp).toLocaleTimeString() : 'justo ahora'}`;
            scheduleBleRefresh(15000, false);
          }
        } catch (error) {
          bleResults.innerHTML = '<p class="small">No se pudo consultar los dispositivos BLE.</p>';
          bleScanStatus.textContent = 'Error al escanear. Reintenta.';
          scheduleBleRefresh(5000, false);
        }
      };

      bleScanButton.addEventListener('click', () => fetchBleDevices(true));
      fetchBleDevices(true);
    })();
  </script>
</body>
</html>
)rawliteral";
    page.replace("{stripData}", buildStripListJson());
    return page;
}

static void updateIndicatorsForWifi(bool connected) {
    digitalWrite(STATUS_LED_PIN, connected ? HIGH : LOW);
    digitalWrite(AUX_LED_PIN, connected ? HIGH : LOW);
}

static void startApSetup() {
    wifiSetupMode = true;
    wifiConnected = false;
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_AP);
    IPAddress apIp(192, 168, 4, 1);
    IPAddress apGateway(192, 168, 4, 1);
    IPAddress apSubnet(255, 255, 255, 0);
    WiFi.softAPConfig(apIp, apGateway, apSubnet);
    WiFi.softAP(AP_SSID);
    Serial.println("Modo setup WiFi activo");
    Serial.print("AP IP: ");
    Serial.println(WiFi.softAPIP());
    Serial.print("Clientes AP: ");
    Serial.println(WiFi.softAPgetStationNum());
    scheduleBleScan(true);
}

static bool startStationFromSavedCredentials() {
    WifiCredentials creds = loadWifiCredentials();
    if (!creds.valid()) {
        return false;
    }

    Serial.printf("Intentando conectar a WiFi: %s\n", creds.ssid.c_str());
    wifiSetupMode = false;
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(HOSTNAME);
    WiFi.begin(creds.ssid.c_str(), creds.password.c_str());

    Serial.print("Conectando a WiFi guardado");
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start) < WIFI_CONNECT_TIMEOUT_MS) {
        delay(250);
        Serial.print('.');
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        wifiConnected = true;
        WiFi.setAutoReconnect(true);
        WiFi.persistent(true);
        if (MDNS.begin(HOSTNAME)) {
            MDNS.addService("http", "tcp", 81);
        }
        Serial.print("WiFi conectado. IP: ");
        Serial.println(WiFi.localIP());
        scheduleBleScan();
        return true;
    }

    Serial.println("No fue posible establecer WiFi; pasando a modo setup");
    WiFi.disconnect(true);
    startApSetup();
    return false;
}

static void handleWifiReconnect() {
    if (wifiSetupMode) {
        return;
    }

    if (WiFi.status() == WL_CONNECTED) {
        if (!wifiConnected) {
            wifiConnected = true;
            if (MDNS.begin(HOSTNAME)) {
                MDNS.addService("http", "tcp", 81);
            }
            Serial.print("WiFi reconectado. IP: ");
            Serial.println(WiFi.localIP());
            scheduleBleScan();
        }
        return;
    }

    wifiConnected = false;
    if (millis() < wifiRetryAt) {
        return;
    }

    wifiRetryAt = millis() + WIFI_RETRY_INTERVAL_MS;
    WifiCredentials creds = loadWifiCredentials();
    if (!creds.valid()) {
        startApSetup();
        return;
    }

    Serial.println("Reintentando WiFi");
    WiFi.disconnect();
    WiFi.begin(creds.ssid.c_str(), creds.password.c_str());
}

static StripInstance* resolveStripFromRequest(AsyncWebServerRequest* request) {
    int stripId = request->hasParam("strip") ? request->getParam("strip")->value().toInt() : 0;
    return getStripByIndex(stripId);
}

static bool anyStripConnected() {
    for (const auto& instance : strips) {
        if (instance->control.isConnected()) {
            return true;
        }
    }
    return false;
}

static void stopEffect(StripInstance& strip) {
    strip.effect.type = EffectType::NONE;
}

static void hsvToRgb(uint16_t h, uint8_t s, uint8_t v, uint8_t& out_r, uint8_t& out_g, uint8_t& out_b) {
    float hh = h / 60.0f;
    int i = floor(hh);
    float f = hh - i;
    float p = v * (1 - s / 255.0f);
    float q = v * (1 - f * s / 255.0f);
    float t = v * (1 - (1 - f) * s / 255.0f);
    float r, g, b;
    auto clampVal = [](float value) -> uint8_t {
        return static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, value)));
    };
    switch (i % 6) {
        case 0: r = v; g = t; b = p; break;
        case 1: r = q; g = v; b = p; break;
        case 2: r = p; g = v; b = t; break;
        case 3: r = p; g = q; b = v; break;
        case 4: r = t; g = p; b = v; break;
        case 5: default: r = v; g = p; b = q; break;
    }
    out_r = clampVal(r);
    out_g = clampVal(g);
    out_b = clampVal(b);
}

static void startEffect(StripInstance& strip, EffectType type, unsigned long duration, unsigned long interval) {
    strip.effect.type = type;
    strip.effect.interval = interval ? interval : 200;
    strip.effect.nextChange = millis();
    strip.effect.endTime = duration ? millis() + duration : 0;
    strip.effect.step = 0;
    strip.effect.toggle = false;
}

static bool applyColor(StripInstance& strip, uint8_t red, uint8_t green, uint8_t blue, uint8_t brightness, bool updateState = true) {
    const bool success = strip.control.sendColor(red, green, blue, brightness);
    strip.bleConnected = success && strip.control.isConnected();
    if (success && updateState) {
        stopEffect(strip);
        strip.lastState.red = red;
        strip.lastState.green = green;
        strip.lastState.blue = blue;
        strip.lastState.brightness = brightness;
        strip.power = true;
        saveStripState(strip.index, strip);
    }
    if (!success) {
        strip.bleConnected = false;
    }
    return success;
}

static void updateEffect(StripInstance& strip) {
    if (strip.effect.type == EffectType::NONE) {
        return;
    }
    const unsigned long now = millis();
    if (strip.effect.endTime && now >= strip.effect.endTime) {
        stopEffect(strip);
        return;
    }
    if (now < strip.effect.nextChange) {
        return;
    }
    strip.effect.nextChange = now + strip.effect.interval;
    uint8_t r = 0, g = 0, b = 0;
    switch (strip.effect.type) {
        case EffectType::MUSIC:
            strip.effect.step = (strip.effect.step + 15) % 360;
            hsvToRgb(strip.effect.step, 255, 255, r, g, b);
            applyColor(strip, r, g, b, 0xFF, false);
            break;
        case EffectType::POLICE:
            strip.effect.toggle = !strip.effect.toggle;
            if (strip.effect.toggle) {
                applyColor(strip, 255, 0, 0, 0xFF, false);
            } else {
                applyColor(strip, 0, 0, 255, 0xFF, false);
            }
            break;
        case EffectType::STROBE:
            strip.effect.toggle = !strip.effect.toggle;
            if (strip.effect.toggle) {
                applyColor(strip, 255, 255, 255, 0xFF, false);
            } else {
                applyColor(strip, 0, 0, 0, 0x00, false);
            }
            break;
        default:
            break;
    }
}

static void handleAlexaCommand(StripInstance& strip, bool state, unsigned char brightness, byte* rgb) {
    strip.alexaAction.pending = true;
    strip.alexaAction.state = state;
    strip.alexaAction.brightness = brightness;
    strip.alexaAction.hasRgb = rgb != nullptr;
    if (rgb) {
        strip.alexaAction.rgb[0] = rgb[0];
        strip.alexaAction.rgb[1] = rgb[1];
        strip.alexaAction.rgb[2] = rgb[2];
    }
}

static void processAlexaCommands() {
    for (auto& instance : strips) {
        StripInstance& strip = *instance;
        if (!strip.alexaAction.pending) {
            continue;
        }
        strip.alexaAction.pending = false;
        strip.power = strip.alexaAction.state;
        if (!strip.power) {
            applyColor(strip, 0, 0, 0, 0x10, false);
            saveStripState(strip.index, strip);
            fauxmo.setState(strip.fauxmoDeviceId, false, 0);
            continue;
        }
        uint8_t effectiveBrightness = strip.alexaAction.brightness ? constrain(strip.alexaAction.brightness, 1, 254) : strip.lastState.brightness;
        if (strip.alexaAction.hasRgb) {
            if (applyColor(strip, strip.alexaAction.rgb[0], strip.alexaAction.rgb[1], strip.alexaAction.rgb[2], effectiveBrightness)) {
                fauxmo.setState(strip.fauxmoDeviceId, true, effectiveBrightness, strip.alexaAction.rgb);
            }
        } else {
            byte rgb[3] = { strip.lastState.red, strip.lastState.green, strip.lastState.blue };
            if (applyColor(strip, strip.lastState.red, strip.lastState.green, strip.lastState.blue, effectiveBrightness)) {
                fauxmo.setState(strip.fauxmoDeviceId, true, effectiveBrightness, rgb);
            }
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(100);

    pinMode(STATUS_LED_PIN, OUTPUT);
    pinMode(AUX_LED_PIN, OUTPUT);
    digitalWrite(STATUS_LED_PIN, LOW);
    digitalWrite(AUX_LED_PIN, LOW);

    loadStripDefinitions();
    rebuildStripInstances();
    BLEDevice::init("ESP32-LED-Bridge");
    scheduleBleScan();

    setupServer.on("/", []() {
        setupServer.send(200, "text/html", setupPage());
    });

    setupServer.on("/setup", []() {
        setupServer.send(200, "text/html", setupPage());
    });

    setupServer.on("/setup/save", []() {
        String ssid = setupServer.arg("ssid");
        String password = setupServer.arg("password");
        if (ssid.isEmpty()) {
            setupServer.send(400, "text/plain", "SSID requerido");
            return;
        }
        int stripCount = setupServer.hasArg("strip_count") ? setupServer.arg("strip_count").toInt() : 0;
        stripCount = constrain(stripCount, 0, MAX_STRIP_ENTRIES);
        stripDefinitions.clear();
        for (int i = 0; i < stripCount; ++i) {
            String name = setupServer.arg("strip_name_" + String(i));
            String mac = setupServer.arg("strip_mac_" + String(i));
            name.trim();
            mac.trim();
            if (mac.length() == 0) {
                continue;
            }
            stripDefinitions.push_back({ name.length() ? name : String("Tira LED"), mac });
        }
        if (stripDefinitions.empty()) {
            stripDefinitions.push_back({ String("Tira LED"), String(BLE_CONTROLLER_ADDRESS) });
        }
        saveStripDefinitions();
        saveWifiCredentials(ssid, password);
        setupServer.send(200, "text/plain", "Guardado. Reiniciando...");
        restartPending = true;
        restartAt = millis() + 1000;
    });

    setupServer.on("/api/ble-discovery", []() {
        scheduleBleScan();
        String status = bleScanInProgress ? String("scanning") : String("ready");
        String body = String("{\"status\":\"") + status + "\",\"timestamp\":" + String(lastBleScanAt) + ",\"results\":" + buildBleScanJson() + "}";
        setupServer.send(200, "application/json", body);
    });

    appServer.on("/", AsyncWebRequestMethod::HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send_P(200, "text/html", index_html);
    });

    appServer.on("/api/color", AsyncWebRequestMethod::HTTP_GET, [](AsyncWebServerRequest* request) {
        StripInstance* strip = resolveStripFromRequest(request);
        if (!strip) {
            request->send(400, "application/json", buildJson(false, "Strip inválida"));
            return;
        }
        const int red = clampColorParam(request, "red");
        const int green = clampColorParam(request, "green");
        const int blue = clampColorParam(request, "blue");
        int brightness = 0xFF;
        if (request->hasParam("brightness")) {
            brightness = constrain(request->getParam("brightness")->value().toInt(), 0, 255);
        }
        const bool success = applyColor(*strip, red, green, blue, brightness);
        request->send(200, "application/json", buildJson(success, success ? "Color enviado" : "Error BLE"));
    });

    appServer.on("/api/status", AsyncWebRequestMethod::HTTP_GET, [](AsyncWebServerRequest* request) {
        const bool wifiReady = (WiFi.status() == WL_CONNECTED);
        const bool bleReady = anyStripConnected();
        const String body = buildJsonStatus(bleReady && wifiReady, wifiSetupMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString(), wifiSetupMode ? "setup" : (wifiConnected ? "station" : "reconnecting"));
        request->send(200, "application/json", body);
    });

    appServer.on("/api/ble-discovery", AsyncWebRequestMethod::HTTP_GET, [](AsyncWebServerRequest* request) {
        const bool refresh = request->hasParam("refresh") && request->getParam("refresh")->value() == "1";
        if (refresh) {
            scheduleBleScan();
        }
        if (bleScanResults.empty() && !bleScanInProgress) {
            scheduleBleScan();
        }
        String status = bleScanInProgress ? String("scanning") : String("ready");
        String body = String("{\"status\":\"") + status + "\",\"timestamp\":" + String(lastBleScanAt) + ",\"results\":" + buildBleScanJson() + "}";
        request->send(200, "application/json", body);
    });

    appServer.on("/api/strips", AsyncWebRequestMethod::HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "application/json", buildStripListApiJson());
    });

    appServer.on("/api/wifi", AsyncWebRequestMethod::HTTP_POST, [](AsyncWebServerRequest* request) {
        clearWifiCredentials();
        clearStripDefinitionsPrefs();
        bleScanResults.clear();
        lastBleScanAt = 0;
        request->send(200, "text/plain", "Credenciales borradas. Reiniciando...");
        restartPending = true;
        restartAt = millis() + 1000;
    });

    appServer.on("/api/effect", AsyncWebRequestMethod::HTTP_GET, [](AsyncWebServerRequest* request) {
        StripInstance* strip = resolveStripFromRequest(request);
        if (!strip) {
            request->send(400, "application/json", buildJson(false, "Strip inválida"));
            return;
        }
        const char* nameParam = request->hasParam("name") ? request->getParam("name")->value().c_str() : "";
        const std::string name(nameParam);
        std::string lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c){ return std::tolower(c); });
        unsigned long duration = request->hasParam("duration") ? request->getParam("duration")->value().toInt() : 15000;
        unsigned long speed = request->hasParam("speed") ? request->getParam("speed")->value().toInt() : 200;
        std::string message;
        if (lower == "stop" || lower.empty()) {
            stopEffect(*strip);
            message = "Efecto detenido";
        } else {
            EffectType type = EffectType::NONE;
            if (lower == "music") type = EffectType::MUSIC;
            else if (lower == "police") type = EffectType::POLICE;
            else if (lower == "strobe") type = EffectType::STROBE;
            if (type == EffectType::NONE) {
                message = "Efecto desconocido";
            } else {
                startEffect(*strip, type, duration, speed);
                message = "Efecto " + lower + " activado";
            }
        }
        request->send(200, "application/json", buildJson(true, String(message.c_str())));
    });

    setupServer.onNotFound([]() {
        setupServer.send(404, "text/plain", "Not found");
    });

    appServer.onNotFound([](AsyncWebServerRequest* request) {
        request->send(404, "text/plain", "Not found");
    });

    if (!startStationFromSavedCredentials()) {
        startApSetup();
    }

    if (!wifiSetupMode) {
        fauxmo.createServer(true);
        fauxmo.setPort(80);
        fauxmo.enable(true);
        for (auto& instance : strips) {
            StripInstance& strip = *instance;
            unsigned char deviceId = fauxmo.addDevice(strip.definition.name.c_str());
            strip.fauxmoDeviceId = deviceId;
            alexaDeviceMap[deviceId] = &strip;
        }
        fauxmo.onSetState([](unsigned char device_id, const char* device_name, bool state, unsigned char value) {
            auto it = alexaDeviceMap.find(device_id);
            if (it == alexaDeviceMap.end()) {
                return;
            }
            handleAlexaCommand(*it->second, state, value, nullptr);
        });
        fauxmo.onSetState([](unsigned char device_id, const char* device_name, bool state, unsigned char value, byte* rgb) {
            auto it = alexaDeviceMap.find(device_id);
            if (it == alexaDeviceMap.end()) {
                return;
            }
            handleAlexaCommand(*it->second, state, value, rgb);
        });
    }

    for (auto& instance : strips) {
        StripInstance& strip = *instance;
        if (strip.power) {
            applyColor(strip, strip.lastState.red, strip.lastState.green, strip.lastState.blue, strip.lastState.brightness, false);
        } else {
            strip.control.sendColor(0, 0, 0, 0x10);
        }
    }

    setupServer.begin();
    appServer.begin();
    Serial.println("Servidores HTTP iniciados");
}

void loop() {
    handleWifiReconnect();

    const bool wifiReady = (WiFi.status() == WL_CONNECTED);
    const bool bleReady = anyStripConnected();
    if (!wifiReady) {
        updateIndicatorsForWifi(false);
        indicatorActive = false;
        indicatorShown = false;
        indicatorDone = false;
    } else if (!indicatorShown && !indicatorDone) {
        updateIndicatorsForWifi(true);
        indicatorActive = true;
        indicatorShown = true;
        indicatorStart = millis();
    }
    if (indicatorActive && (millis() - indicatorStart) >= INDICATOR_DURATION) {
        updateIndicatorsForWifi(false);
        indicatorActive = false;
        indicatorDone = true;
    }

    const unsigned long now = millis();
    if (now - lastStatusLog >= STATUS_INTERVAL_MS) {
        lastStatusLog = now;
        String ipText = "sin conexion";
        if (wifiSetupMode) {
            ipText = WiFi.softAPIP().toString();
        } else if (wifiReady) {
            ipText = WiFi.localIP().toString();
        }
        Serial.printf("IP: %s | WiFi: %s | BLE: %s\n",
                      ipText.c_str(),
                      wifiReady ? "OK" : "NO",
                      bleReady ? "OK" : "NO");
    }

    for (auto& instance : strips) {
        updateEffect(*instance);
    }
    processAlexaCommands();
    if (!wifiSetupMode) {
        fauxmo.handle();
    } else {
        setupServer.handleClient();
    }

    if (restartPending && millis() >= restartAt) {
        ESP.restart();
    }

    delay(50);
}
