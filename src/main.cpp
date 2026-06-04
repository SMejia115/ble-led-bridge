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

#include "controller_settings.h"
#include "ble_control.h"

BLEControl ledController;
AsyncWebServer appServer(81);
WebServer setupServer(80);
fauxmoESP fauxmo;
Preferences prefs;

static constexpr char WIFI_PREF_NS[] = "wifi";
static constexpr char STATE_PREF_NS[] = "led";
static constexpr char HOSTNAME[] = "led-bridge";
static constexpr char AP_SSID[] = "LED-Bridge-Setup";
static constexpr int STATUS_LED_PIN = 2;
static constexpr int AUX_LED_PIN = 5;
static constexpr unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000;
static constexpr unsigned long WIFI_RETRY_INTERVAL_MS = 10000;
static constexpr unsigned long STATUS_INTERVAL_MS = 5000;
static constexpr unsigned long INDICATOR_DURATION = 10000;

struct LedState {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t brightness;
};
LedState lastState{255, 0, 0, 0xFF};
bool alexaPower = true;
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

struct WifiCredentials {
    String ssid;
    String password;
    bool valid() const { return ssid.length() > 0; }
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
EffectState effect;

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
        .card{width:100%; max-width:420px; background:rgba(8,10,25,.9); border:1px solid rgba(255,255,255,.08); border-radius:24px; padding:2rem; box-shadow:0 30px 40px rgba(0,0,0,.65); backdrop-filter:blur(20px);}
        h1{margin:0 0 .25rem; font-size:1.9rem; letter-spacing:.05em;}
        p.note{margin:0 0 1.2rem; color:#9aa0bd; font-size:.9rem;}
        label{display:block; font-size:.8rem; letter-spacing:.2em; text-transform:uppercase; color:#5a6c8c; margin-top:1.1rem;}
        input[type=color]{width:100%; height:72px; border:none; border-radius:18px; cursor:pointer;}
        .row{display:flex; align-items:center; gap:1rem; margin-top:.5rem;}
        input[type=range]{width:100%;}
        button{width:100%; margin-top:1.3rem; border:none; background:linear-gradient(135deg,#1e64ff,#00d4ff); color:#fff; padding:.85rem 1rem; border-radius:999px; font-size:1rem; font-weight:600; letter-spacing:.05em; cursor:pointer; transition:transform .2s ease, box-shadow .2s ease; box-shadow:0 12px 30px -12px rgba(0,212,255,.9);} 
        button:active{transform:translateY(2px); box-shadow:0 10px 20px -10px rgba(0,212,255,.8);}
        .status{margin-top:1rem; font-size:.85rem; color:#bedeff;}
        .presets{display:grid; grid-template-columns:repeat(2,minmax(0,1fr)); gap:.75rem; margin-top:1.5rem;}
        .preset{border:none; border-radius:14px; padding:.9rem; font-weight:600; letter-spacing:.05em; text-transform:uppercase; font-size:.75rem; cursor:pointer; transition:transform .2s ease;}
        .preset:active{transform:translateY(2px);}
        .preset[data-color="0,102,255"]{background:#0f5fff; color:#fff;}
        .preset[data-color="102,255,255"]{background:#02c8ff; color:#04132b;}
        .preset[data-color="0,255,102"]{background:#00ff9d; color:#02140c;}
        .preset[data-color="255,255,255"]{background:#fff; color:#010101;}
        .alexa-note{margin-top:1.5rem; font-size:.78rem; color:#7b86a9; text-align:center;}
    </style>
</head>
<body>
    <div class="card">
        <h1>LED Bridge</h1>
        <p class="note">Controla tu tira RGB desde el navegador, Alexa o presets rápidos.</p>
        <label for="color">Elige color</label>
        <input type="color" id="color" value="#ff6a00" />
        <label for="brightness">Brillo (<span id="brightness-value">100</span>%)</label>
        <div class="row"><input type="range" id="brightness" min="10" max="100" value="100" /></div>
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
        <p class="alexa-note">Alexa entiende comandos de color como “pon la luz led azul”, y puedes añadir rutinas HTTP para efectos más complejos.</p>
    </div>

    <script>
        const colorPicker = document.getElementById('color');
        const brightnessSlider = document.getElementById('brightness');
        const brightnessValue = document.getElementById('brightness-value');
        const status = document.getElementById('status');

        brightnessSlider.addEventListener('input', () => {
            brightnessValue.textContent = brightnessSlider.value;
        });

        async function sendColorCommand(red, green, blue, brightness) {
            const response = await fetch(`/api/color?red=${red}&green=${green}&blue=${blue}&brightness=${brightness}`);
            const data = await response.json();
            status.textContent = data.message + (data.success ? ' • Estado OK' : ' • Revisar LED');
        }

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
                colorPicker.value = `#${((1 << 24) + (r << 16) + (g << 8) + b).toString(16).slice(1)}`;
                sendColorCommand(r, g, b, 0xff);
            });
        });

        document.querySelectorAll('.effect').forEach(btn => {
            btn.addEventListener('click', () => {
                const effect = btn.dataset.effect;
                fetch(`/api/effect?name=${effect}`)
                    .then(res => res.json())
                    .then(data => {
                        status.textContent = data.message;
                    });
            });
        });

        async function refreshStatus() {
            const response = await fetch('/api/status');
            const data = await response.json();
            status.textContent = data.connected ? 'Conectado al controlador BLE' : 'Sin conexión BLE';
        }

        setInterval(refreshStatus, 5000);
        refreshStatus();
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
}

static void clearWifiCredentials() {
    prefs.begin(WIFI_PREF_NS, false);
    prefs.clear();
    prefs.end();
}

static String setupPage() {
    return R"rawliteral(
<!DOCTYPE html>
<html lang="es">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>LED Bridge Setup</title>
  <style>
    body{font-family:system-ui,-apple-system,sans-serif;background:#0c1020;color:#f5f7ff;margin:0;min-height:100vh;display:grid;place-items:center;padding:24px}
    .card{width:100%;max-width:420px;background:#121831;border:1px solid rgba(255,255,255,.08);border-radius:18px;padding:24px}
    h1{margin:0 0 8px}
    label{display:block;margin-top:14px;font-size:12px;text-transform:uppercase;letter-spacing:.12em;color:#93a4d9}
    input{width:100%;box-sizing:border-box;padding:12px 14px;border-radius:12px;border:1px solid rgba(255,255,255,.1);background:#0a0f1f;color:#fff;margin-top:8px}
    button,a{display:inline-block;width:100%;margin-top:16px;padding:12px 14px;border:0;border-radius:999px;background:#4b7bff;color:#fff;text-align:center;text-decoration:none;font-weight:700}
    .small{font-size:13px;color:#9aa8d5;line-height:1.5}
  </style>
</head>
<body>
  <form class="card" method="POST" action="/setup/save">
    <h1>LED Bridge</h1>
    <p class="small">Conecta el dispositivo a tu Wi-Fi. Si ya cambiaste de red, borra credenciales con el boton fisico y vuelve a configurar.</p>
    <label for="ssid">SSID</label>
    <input id="ssid" name="ssid" autocomplete="off" required />
    <label for="password">Password</label>
    <input id="password" name="password" type="password" autocomplete="off" />
    <button type="submit">Guardar y conectar</button>
  </form>
</body>
</html>
)rawliteral";
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
}

static bool startStationFromSavedCredentials() {
    WifiCredentials creds = loadWifiCredentials();
    if (!creds.valid()) {
        return false;
    }

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

void saveState() {
    prefs.begin(STATE_PREF_NS, false);
    prefs.putUChar("red", lastState.red);
    prefs.putUChar("green", lastState.green);
    prefs.putUChar("blue", lastState.blue);
    prefs.putUChar("brightness", lastState.brightness);
    prefs.putBool("power", alexaPower);
    prefs.end();
}

void loadState() {
    prefs.begin(STATE_PREF_NS, false);
    lastState.red = prefs.getUChar("red", 255);
    lastState.green = prefs.getUChar("green", 0);
    lastState.blue = prefs.getUChar("blue", 0);
    lastState.brightness = prefs.getUChar("brightness", 0xFF);
    alexaPower = prefs.getBool("power", true);
    prefs.end();
}

void stopEffect() {
    effect.type = EffectType::NONE;
}

void hsvToRgb(uint16_t h, uint8_t s, uint8_t v, uint8_t& out_r, uint8_t& out_g, uint8_t& out_b) {
    float hh = h / 60.0f;
    int i = floor(hh);
    float f = hh - i;
    float p = v * (1 - s / 255.0f);
    float q = v * (1 - f * s / 255.0f);
    float t = v * (1 - (1 - f) * s / 255.0f);
    float r, g, b;
    auto clampVal = [](float v) -> uint8_t {
        return static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, v)));
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

void startEffect(EffectType type, unsigned long duration, unsigned long interval) {
    effect.type = type;
    effect.interval = interval ? interval : 200;
    effect.nextChange = millis();
    effect.endTime = duration ? millis() + duration : 0;
    effect.step = 0;
    effect.toggle = false;
}

bool applyColor(uint8_t red, uint8_t green, uint8_t blue, uint8_t brightness, bool updateState = true) {
    const bool success = ledController.sendColor(red, green, blue, brightness);
    if (success && updateState) {
        stopEffect();
        lastState.red = red;
        lastState.green = green;
        lastState.blue = blue;
        lastState.brightness = brightness;
        alexaPower = true;
        saveState();
    }
    return success;
}

void updateEffect() {
    if (effect.type == EffectType::NONE) {
        return;
    }
    const unsigned long now = millis();
    if (effect.endTime && now >= effect.endTime) {
        stopEffect();
        return;
    }
    if (now < effect.nextChange) {
        return;
    }
    effect.nextChange = now + effect.interval;
    uint8_t r = 0, g = 0, b = 0;
    switch (effect.type) {
        case EffectType::MUSIC:
            effect.step = (effect.step + 15) % 360;
            hsvToRgb(effect.step, 255, 255, r, g, b);
            applyColor(r, g, b, 0xFF, false);
            break;
        case EffectType::POLICE:
            effect.toggle = !effect.toggle;
            if (effect.toggle) {
                applyColor(255, 0, 0, 0xFF, false);
            } else {
                applyColor(0, 0, 255, 0xFF, false);
            }
            break;
        case EffectType::STROBE:
            effect.toggle = !effect.toggle;
            if (effect.toggle) {
                applyColor(255, 255, 255, 0xFF, false);
            } else {
                applyColor(0, 0, 0, 0x00, false);
            }
            break;
        default:
            break;
    }
}

void handleAlexaCommand(bool state, unsigned char brightness, byte* rgb) {
    alexaPower = state;
    prefs.putBool("power", state);
    if (!state) {
        ledController.sendColor(0, 0, 0, 0x10);
        return;
    }
    uint8_t effectiveBrightness = brightness ? brightness : lastState.brightness;
    if (rgb) {
        applyColor(rgb[0], rgb[1], rgb[2], effectiveBrightness);
    } else {
        applyColor(lastState.red, lastState.green, lastState.blue, effectiveBrightness);
    }
}

void setup() {
    Serial.begin(115200);
    delay(100);

    pinMode(STATUS_LED_PIN, OUTPUT);
    pinMode(AUX_LED_PIN, OUTPUT);
    digitalWrite(STATUS_LED_PIN, LOW);
    digitalWrite(AUX_LED_PIN, LOW);

    loadState();

    ledController.begin();
    ledController.configure(BLE_CONTROLLER_ADDRESS, BLE_SERVICE_UUID, BLE_CHARACTERISTIC_UUID);

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
        saveWifiCredentials(ssid, password);
        setupServer.send(200, "text/plain", "Guardado. Reiniciando...");
        restartPending = true;
        restartAt = millis() + 1000;
    });

    appServer.on("/", AsyncWebRequestMethod::HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send_P(200, "text/html", index_html);
    });

    appServer.on("/api/color", AsyncWebRequestMethod::HTTP_GET, [](AsyncWebServerRequest* request) {
        const int red = clampColorParam(request, "red");
        const int green = clampColorParam(request, "green");
        const int blue = clampColorParam(request, "blue");
        int brightness = 0xFF;
        if (request->hasParam("brightness")) {
            brightness = constrain(request->getParam("brightness")->value().toInt(), 0, 255);
        }
        const bool success = applyColor(red, green, blue, brightness);
        request->send(200, "application/json", buildJson(success, success ? "Color enviado" : "Error BLE"));
    });

    appServer.on("/api/status", AsyncWebRequestMethod::HTTP_GET, [](AsyncWebServerRequest* request) {
        const bool connected = ledController.isConnected();
        const String body = buildJsonStatus(connected, wifiSetupMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString(), wifiSetupMode ? "setup" : (wifiConnected ? "station" : "reconnecting"));
        request->send(200, "application/json", body);
    });

    appServer.on("/api/wifi", AsyncWebRequestMethod::HTTP_POST, [](AsyncWebServerRequest* request) {
        clearWifiCredentials();
        request->send(200, "text/plain", "Credenciales borradas. Reiniciando...");
        restartPending = true;
        restartAt = millis() + 1000;
    });

    appServer.on("/api/effect", AsyncWebRequestMethod::HTTP_GET, [](AsyncWebServerRequest* request) {
        const char* nameParam = request->hasParam("name") ? request->getParam("name")->value().c_str() : "";
        const std::string name(nameParam);
        std::string lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c){ return std::tolower(c); });
        unsigned long duration = request->hasParam("duration") ? request->getParam("duration")->value().toInt() : 15000;
        unsigned long speed = request->hasParam("speed") ? request->getParam("speed")->value().toInt() : 200;
        std::string message;
        if (lower == "stop" || lower.empty()) {
            stopEffect();
            message = "Efecto detenido";
        } else {
            EffectType type = EffectType::NONE;
            if (lower == "music") type = EffectType::MUSIC;
            else if (lower == "police") type = EffectType::POLICE;
            else if (lower == "strobe") type = EffectType::STROBE;
            if (type == EffectType::NONE) {
                message = "Efecto desconocido";
            } else {
                startEffect(type, duration, speed);
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
        fauxmo.addDevice("Tira LED");
        fauxmo.onSetState([](unsigned char device_id, const char* device_name, bool state, unsigned char value) {
            handleAlexaCommand(state, value, nullptr);
        });
        fauxmo.onSetState([](unsigned char device_id, const char* device_name, bool state, unsigned char value, byte* rgb) {
            handleAlexaCommand(state, value, rgb);
        });
    }

    if (alexaPower) {
        applyColor(lastState.red, lastState.green, lastState.blue, lastState.brightness);
    } else {
        ledController.sendColor(0, 0, 0, 0x10);
    }

    setupServer.begin();
    appServer.begin();
    Serial.println("Servidores HTTP iniciados");
}

void loop() {
    if (!ledController.isConnected()) {
        ledController.sendColor(0, 0, 0, 0x10);
    }

    handleWifiReconnect();

    const bool wifiReady = (WiFi.status() == WL_CONNECTED);
    const bool bleReady = ledController.isConnected();
    const bool connected = wifiReady && bleReady;
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

    updateEffect();
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
