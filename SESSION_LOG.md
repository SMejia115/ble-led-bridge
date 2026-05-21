# Sesión actual

1. **Configuración inicial**: diseñé el puente ESP32-WiFi-BLE, instalé dependencias (`ESPAsyncWebServer`, `AsyncTCP`, `Preferences`, `BLE`) y dejé los archivos base (`platformio.ini`, `src/`).
2. **Conectividad Wi-Fi/BLE**: añadí las credenciales `FLIAMEJIA`/`42132023`, apunté al controlador `BE:58:BC:00:16:DB` (servicio `FFF0`, característica `FFF3`) y construí `BLEControl` para enviar los comandos RGB.
3. **Panel web + API**: creé el dashboard que corre en `http://<IP>:81`, el endpoint `/api/color` y un log cada 5 s (con LED azul) que reporta IP/estado. También probé con cURL para poner la tira en rojo y azul.
4. **Integración Alexa**: añadí `fauxmoESP` para emular el dispositivo “Tira LED”, con control de encendido/apagado y restauración del último color.
5. **Refactor y limpieza**: renombré la carpeta a `ble-led-bridge`, eliminé duplicados, añadí README con instrucciones y generé este log para documentar el avance.
6. **Alexa + color**: actualicé `main.cpp` para guardar el estado en `Preferences`, añadí presets web, permití la frase “Alexa, pon la luz led azul” y convertí los datos de color enviados por Alexa a comandos RGB BLE.
7. **Efectos dinámicos**: añadí botones/endpoint `/api/effect` para activar efectos de música, policía o estroboscópico y actualicé el loop para ejecutarlos sin bloquear.

## Qué sigue
- Crear presets de color en el dashboard y/o rutinas de Alexa que disparen `/api/color`.
- Guardar/restaurar escenas en `Preferences` para mantener el color al reiniciar.
- Documentar el protocolo BLE detectado para facilitar otras tiras.
