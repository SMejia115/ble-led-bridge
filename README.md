# BLE LED Bridge

Proyecto que convierte un ESP32 en puente entre una tira RGB BLE y redes Wi-Fi/Alexa. El ESP32 se conecta al controlador BLE (dirección detectada mediante escaneo local), expone un dashboard web responsivo y emula un dispositivo compatible con Alexa mediante `fauxmoESP`, así puedes encender, apagar y restaurar el último color sin abrir más apps.

## Características actuales
- Conexión BLE con controladores genéricos (servicio `FFF0`, característica `FFF3`).
- Dashboard `http://<IP>:81` con selector de color, brillo y preset de escenarios rápidos.
- HTTP API (`/api/color`, `/api/status`) para scripts automáticos.
- Alexa emulada (`Tira LED`) que responde a on/off y a colores (puedes decir “pon la luz led azul”).
- LED azul integrado indica que Wi-Fi + BLE están conectados y el log serie reporta estado cada 5 s.

## Primeros pasos
1. Asegúrate de tener PlatformIO instalado y el ESP32 conectado por USB.
2. Cambia al directorio del proyecto:
   ```bash
   cd /Users/smejia/Documents/repos/ble-led-bridge
   ```
3. Descarga dependencias y compila:
   ```bash
   pio run -e esp32dev
   ```
4. Flashea el firmware:
   ```bash
   pio run -e esp32dev -t upload
   ```
5. Abre el monitor serie para obtener la IP del ESP32 y revisar el estado:
   ```bash
   pio device monitor -e esp32dev
   ```
   El log imprime `IP: 192.168.x.x | WiFi: OK | BLE: OK` cada 5 s.

## Panel web y API
- Dashboard: `http://<IP>:81/` (usa el selector de color, slider y botón “Aplicar color”).
- Cambiar color por HTTP (ejemplo rojo):
  ```bash
  curl "http://<IP>:81/api/color?red=255&green=0&blue=0&brightness=255"
  ```
- Estado de conexión: `http://<IP>:81/api/status` (`{"connected":true/false}`).

## Control de color con Alexa
1. Asegúrate de que el ESP está en `FLIAMEJIA` y el LED azul del ESP se mantenga encendido.
2. En la app Alexa, agrega un dispositivo → “Otro” → “Wi-Fi”.
3. El ESP anuncia el dispositivo `Tira LED` y lo descubres como una luz estándar.
4. Di frases como “Alexa, pon la luz led azul” o cualquier color que Alexa entienda: el color se traduce a RGB y se envía directamente por BLE.
5. “Alexa, enciende Tira LED” restituye el último color, mientras que “Alexa, apaga Tira LED” manda `0,0,0`.
6. Si quieres escenas adicionales (atardecer, cine), crea una rutina que llame a `/api/color` con los valores preferidos.

## Estado actual y próximos pasos
- BLE → Wi-Fi → Alexa: completado. El LED azul monitorea el stack.
- Pendiente: crear presets en la web y exponerlos como opciones rápidas (podemos guardar escenas en `Preferences`).
- Pendiente: documentar los paquetes BLE exactos para otros controladores.

## Logs de sesión
Consulta `SESSION_LOG.md` para ver todo lo trabajado y continuar en una futura sesión sin perder el contexto.
