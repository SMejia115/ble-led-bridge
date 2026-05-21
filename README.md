# BLE LED Bridge

Proyecto que convierte un ESP32 en puente entre una tira RGB BLE y redes Wi-Fi/Alexa. El ESP32 se conecta al controlador BLE (dirección detectada mediante escaneo local), expone un dashboard web responsivo y emula un dispositivo compatible con Alexa mediante `fauxmoESP`, así puedes encender, apagar y restaurar el último color sin abrir más apps.

## Características actuales
- Conexión BLE con controladores genéricos (servicio `FFF0`, característica `FFF3`).
- Servidor HTTP (`http://<IP>:81`) con selector de color, brillo y feedback en tiempo real.
- HTTP API (`/api/color`, `/api/status`) para scripts o automatizaciones.
- LED azul integrado indica que Wi-Fi + BLE están conectados.
- Emulación de dispositivo Alexa (`Tira LED`) para encender/apagar y recuperar la escena previa.
- Registro periódido cada 5 s con la IP y estado Wi-Fi/BLE.

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

## Alexa
1. Asegúrate de que el ESP y el Echo estén en la misma red Wi-Fi.
2. En la app Alexa, agrega un dispositivo → “Otro” → “Wi-Fi”.
3. Durante el descubrimiento el ESP anuncia `Tira LED`; así que simplemente selecciónalo.
4. Luego puedes decir “Alexa, enciende Tira LED” (restaura el último color) o “Alexa, apaga Tira LED” (envía 0,0,0).
5. Para cambiar colores específicos con Alexa puedes crear una rutina que llame al endpoint `/api/color` usando un servicio intermedio (IFTTT, webhook, etc.).

## Estado actual y próximos pasos
- BLE → Wi-Fi → Alexa: completado. El LED azul monitorea el stack.
- Pendiente: crear presets en la web y exponerlos como opciones rápidas (podemos guardar escenas en `Preferences`).
- Pendiente: documentar los paquetes BLE exactos para otros controladores.

## Logs de sesión
Consulta `SESSION_LOG.md` para ver todo lo trabajado y continuar en una futura sesión sin perder el contexto.
