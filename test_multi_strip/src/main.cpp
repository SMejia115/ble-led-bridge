#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <BLEDevice.h>
#include "../../src/ble_control.h"
#include "../../src/controller_settings.h"

static constexpr char WIFI_SSID[] = "FLIAMEJIA";
static constexpr char WIFI_PASSWORD[] = "42132023";
static const char TEST_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>BLE Dual Test</title>
  <style>
    body{font-family:Arial,sans-serif;background:#0a0f1f;color:#f2f5ff;display:flex;flex-direction:column;align-items:center;justify-content:center;height:100vh;margin:0;padding:1rem;}
    .panel{background:rgba(5,7,15,.9);border:1px solid rgba(255,255,255,.1);border-radius:18px;padding:2rem;width:320px;box-shadow:0 20px 40px rgba(0,0,0,.4);}
    h1{margin-top:0;margin-bottom:1rem;font-size:1.4rem;text-align:center;}
    .strip{margin-bottom:1.5rem;}
    .strip:last-child{margin-bottom:0;}
    label{display:block;font-size:.8rem;text-transform:uppercase;letter-spacing:.2em;color:#8892c9;margin-bottom:.5rem;}
    input[type=color]{width:100%;height:54px;border:none;border-radius:12px;margin-bottom:.8rem;}
    button{width:100%;background:linear-gradient(135deg,#1e64ff,#00d4ff);border:none;border-radius:999px;padding:.75rem;font-weight:700;color:#fff;cursor:pointer;font-size:.95rem;}
    .status{margin-top:1rem;font-size:.85rem;color:#9fd8ff;text-align:center;}
  </style>
</head>
<body>
  <div class="panel">
    <h1>Test Multi BLE</h1>
    <div class="strip">
      <label>Tira 1</label>
      <input id="color0" type="color" value="#ff0000" />
      <button data-strip="0">Aplicar Tira 1</button>
    </div>
    <div class="strip">
      <label>Tira 2</label>
      <input id="color1" type="color" value="#00ff00" />
      <button data-strip="1">Aplicar Tira 2</button>
    </div>
    <p class="status" id="status">Esperando comandos...</p>
  </div>
  <script>
    document.querySelectorAll('button[data-strip]').forEach(btn => btn.addEventListener('click', async () => {
      const strip = btn.getAttribute('data-strip');
      const picker = document.getElementById(`color${strip}`);
      const hex = picker.value;
      const red = parseInt(hex.substring(1,3),16);
      const green = parseInt(hex.substring(3,5),16);
      const blue = parseInt(hex.substring(5,7),16);
      try {
        const response = await fetch(`/api/color?strip=${strip}&red=${red}&green=${green}&blue=${blue}&brightness=255`);
        const data = await response.json();
        document.getElementById('status').textContent = data.success ? `Strip ${parseInt(strip)+1}: ${data.message}` : `Error: ${data.message}`;
      } catch (err) {
        document.getElementById('status').textContent = 'Error de conexión';
      }
    }));
  </script>
</body>
</html>
)rawliteral";

static WebServer server(81);
static BLEControl stripControl[2];
static const char* stripMacs[2] = {"BE:58:BC:00:16:DB", "BE:37:F1:00:0F:13"};

static String buildSuccess(bool ok) {
  return String("{\"success\":") + (ok ? "true" : "false") + ",\"message\":\"" + (ok ? "Color aplicado" : "BLE fail") + "\"}";
}

void setup() {
  Serial.begin(115200);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(200);
  }
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html", TEST_PAGE);
  });
  server.on("/api/color", HTTP_GET, []() {
    int strip = server.hasArg("strip") ? server.arg("strip").toInt() : -1;
    if (strip < 0 || strip >= 2) {
      server.send(400, "application/json", buildSuccess(false));
      return;
    }
    int red = server.hasArg("red") ? server.arg("red").toInt() : 0;
    int green = server.hasArg("green") ? server.arg("green").toInt() : 0;
    int blue = server.hasArg("blue") ? server.arg("blue").toInt() : 0;
    red = constrain(red, 0, 255);
    green = constrain(green, 0, 255);
    blue = constrain(blue, 0, 255);
    stripControl[strip].begin();
    stripControl[strip].configure(stripMacs[strip], BLE_SERVICE_UUID, BLE_CHARACTERISTIC_UUID);
    const bool ok = stripControl[strip].sendColor(red, green, blue, 0xFF);
    server.send(200, "application/json", buildSuccess(ok));
  });
  server.begin();
}

void loop() {
}
