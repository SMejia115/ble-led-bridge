#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <Preferences.h>
#include <fauxmoESP.h>

#include "wifi_settings.h"
#include "controller_settings.h"
#include "ble_control.h"

BLEControl ledController;
AsyncWebServer server(81);
fauxmoESP fauxmo;

struct LedState {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t brightness;
};
LedState lastState{255, 0, 0, 0xff};

bool alexaPower = false;

static constexpr int STATUS_LED_PIN = 2; // LED azul incorporado en muchos ESP32
static unsigned long lastStatusLog = 0;
static const unsigned long STATUS_INTERVAL_MS = 5000;

static const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8" />
    <meta name="viewport" content="width=device-width, initial-scale=1" />
    <title>LED Bridge</title>
    <style>
        body{font-family:system-ui, -apple-system, sans-serif; background:linear-gradient(135deg,#101010,#1f1f2d); color:#f0f0f5; display:flex; flex-direction:column; align-items:center; min-height:100vh; margin:0; padding:2rem 1rem;}
        h1{margin-bottom:.4rem;}
        .card{background:rgba(255,255,255,.05); border:1px solid rgba(255,255,255,.08); border-radius:16px; padding:1.5rem; width:100%; max-width:420px; box-shadow:0 25px 40px rgba(0,0,0,.35);}
        label{display:block; font-size:.85rem; letter-spacing:.05em; text-transform:uppercase; color:#9aa0bd; margin-top:1rem;}
        input[type=color]{width:100%; height:68px; border:none; background:transparent; cursor:pointer;}
        button{width:100%; margin-top:1.2rem; border:none; background:#ff6a00; color:#fff; padding:.9rem; border-radius:999px; font-size:1rem; font-weight:600; cursor:pointer; transition:background .2s ease;}
        button:active{background:#e25c00;}
        .status{margin-top:1rem; font-size:.9rem; color:#c1d1ff;}
        .row{display:flex; align-items:center; gap:1rem; margin-top:.8rem;}
        .row input[type=range]{flex:1;}
        .note{margin-top:.4rem; font-size:.75rem; color:#7b869f;}
    </style>
</head>
<body>
    <div class="card">
        <h1>LED Bridge</h1>
        <p class="note">Controla tu tira RGB desde el navegador o Alexa.</p>
        <label for="color">Elige color</label>
        <input type="color" id="color" value="#ff6a00" />
        <label for="brightness">Brillo (<span id="brightness-value">100</span>%)</label>
        <div class="row">
            <input type="range" id="brightness" min="10" max="100" value="100" />
        </div>
        <button id="apply">Aplicar color</button>
        <div class="status" id="status">Conectando al LED...</div>
    </div>

    <script>
        const colorPicker = document.getElementById('color');
        const brightnessSlider = document.getElementById('brightness');
        const brightnessValue = document.getElementById('brightness-value');
        const status = document.getElementById('status');

        brightnessSlider.addEventListener('input', () => {
            brightnessValue.textContent = brightnessSlider.value;
        });

        document.getElementById('apply').addEventListener('click', async () => {
            const hex = colorPicker.value;
            const red = parseInt(hex.substring(1, 3), 16);
            const green = parseInt(hex.substring(3, 5), 16);
            const blue = parseInt(hex.substring(5, 7), 16);
            const brightness = Math.round((parseInt(brightnessSlider.value) / 100) * 0xff);

            const response = await fetch(`/api/color?red=${red}&green=${green}&blue=${blue}&brightness=${brightness}`);
            const text = await response.json();
            status.textContent = text.message + (text.success ? ' • Estado OK' : ' • Revisar LED');
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

static int clampColorParam(AsyncWebServerRequest* request, const char* name) {
    if (!request->hasParam(name)) {
        return 0;
    }
    int value = request->getParam(name)->value().toInt();
    return constrain(value, 0, 255);
}

bool applyColor(uint8_t red, uint8_t green, uint8_t blue, uint8_t brightness, bool updateState = true) {
    if (ledController.sendColor(red, green, blue, brightness)) {
        if (updateState) {
            lastState.red = red;
            lastState.green = green;
            lastState.blue = blue;
            lastState.brightness = brightness;
        }
        return true;
    }
    return false;
}

void handleAlexaDevice(bool state) {
    alexaPower = state;
    if (state) {
        applyColor(lastState.red, lastState.green, lastState.blue, lastState.brightness, false);
    } else {
        ledController.sendColor(0, 0, 0, 0x10);
    }
}

void setup() {
    Serial.begin(115200);
    delay(100);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("Iniciando WiFi");

    pinMode(STATUS_LED_PIN, OUTPUT);
    digitalWrite(STATUS_LED_PIN, LOW);

    uint8_t attempt = 0;
    while (WiFi.status() != WL_CONNECTED && attempt++ < 20) {
        Serial.print('.');
        delay(500);
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWiFi conectado");
        Serial.print("IP: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("\nNo fue posible establecer WiFi");
    }

    ledController.begin();
    ledController.configure(BLE_CONTROLLER_ADDRESS, BLE_SERVICE_UUID, BLE_CHARACTERISTIC_UUID);

    server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send_P(200, "text/html", index_html);
    });

    server.on("/api/color", HTTP_GET, [](AsyncWebServerRequest* request) {
        const int red = clampColorParam(request, "red");
        const int green = clampColorParam(request, "green");
        const int blue = clampColorParam(request, "blue");
        int brightness = 0xff;

        if (request->hasParam("brightness")) {
            brightness = constrain(request->getParam("brightness")->value().toInt(), 0, 255);
        }

        const bool success = applyColor(red, green, blue, brightness);
        const String message = success ? "Color enviado" : "Error BLE";
        if (success && alexaPower) {
            // keep Alexa state consistent
            alexaPower = true;
        }
        request->send(200, "application/json", buildJson(success, message));
    });

    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* request) {
        const bool connected = ledController.isConnected();
        const String body = String("{\"connected\":") + (connected ? "true" : "false") + "}";
        request->send(200, "application/json", body);
    });

    server.onNotFound([](AsyncWebServerRequest* request) {
        request->send(404, "text/plain", "Not found");
    });

    server.begin();

    fauxmo.createServer(true);
    fauxmo.setPort(80);
    fauxmo.enable(true);
    fauxmo.addDevice("Tira LED");
    fauxmo.onSetState([](unsigned char device_id, const char* device_name, bool state, unsigned char value) {
        handleAlexaDevice(state);
    });
}

void loop() {
    if (!ledController.isConnected()) {
        ledController.sendColor(0, 0, 0, 0x10);
    }

    const bool wifiReady = (WiFi.status() == WL_CONNECTED);
    const bool bleReady = ledController.isConnected();
    digitalWrite(STATUS_LED_PIN, (wifiReady && bleReady) ? HIGH : LOW);

    const unsigned long now = millis();
    if (now - lastStatusLog >= STATUS_INTERVAL_MS) {
        lastStatusLog = now;
        Serial.printf("IP: %s | WiFi: %s | BLE: %s\n",
                      wifiReady ? WiFi.localIP().toString().c_str() : "sin conexion",
                      wifiReady ? "OK" : "NO",
                      bleReady ? "OK" : "NO");
    }

    fauxmo.handle();
    delay(1000);
}
