#!/usr/bin/env bash
set -euo pipefail

# This script intentionally creates issues without GitHub labels.
# Some GitHub tokens can create issues but cannot create or read labels, and on
# some local setups those label calls fail in opaque ways. The suggested labels
# are included in the issue body instead.

create_issue() {
  local title="$1"
  local labels="$2"
  local body="$3"

  echo "Creating issue: $title"
  gh issue create \
    --title "$title" \
    --body "Suggested labels: $labels

$body"
}

create_issue \
  "Reemplazar credenciales hardcodeadas por configuracion persistente" \
  "firmware, wifi, productization, priority-high" \
  "Eliminar la dependencia de src/wifi_settings.h para credenciales de red y mover la configuracion Wi-Fi a almacenamiento persistente usando Preferences.

Alcance:
- Crear un modulo de configuracion Wi-Fi.
- Guardar ssid y password en Preferences.
- Detectar si existen credenciales validas al iniciar.
- Mantener compatibilidad temporal con valores de desarrollo si hace falta.

Criterios de aceptacion:
- El firmware puede compilar sin credenciales reales hardcodeadas.
- El ESP32 lee credenciales desde Preferences.
- Si no hay credenciales, entra en modo setup.
- README o documentacion indica que wifi_settings.h ya no es obligatorio para usuarios finales."

create_issue \
  "Implementar portal de configuracion Wi-Fi" \
  "firmware, wifi, ux, priority-high" \
  "Cuando no existan credenciales guardadas, el ESP32 debe crear un access point de configuracion y servir una pagina para que el usuario seleccione la red Wi-Fi e ingrese la clave.

Alcance:
- Crear AP LED-Bridge-Setup.
- Servir una pagina de setup.
- Escanear redes disponibles.
- Permitir guardar SSID/password.
- Reiniciar o cambiar a modo cliente despues de guardar.

Criterios de aceptacion:
- Un usuario puede configurar Wi-Fi sin editar codigo.
- El setup funciona desde celular.
- Las credenciales quedan guardadas despues de reiniciar.
- Si el password es incorrecto, el dispositivo vuelve a setup o muestra estado de error recuperable."

create_issue \
  "Agregar factory reset por boton fisico" \
  "firmware, hardware, ux, priority-high" \
  "Agregar un mecanismo fisico para borrar credenciales y volver al modo configuracion.

Alcance:
- Definir pin de boton.
- Detectar pulsacion larga de 5-10 segundos.
- Borrar credenciales Wi-Fi guardadas.
- Reiniciar en modo setup.
- Evitar borrados accidentales.

Criterios de aceptacion:
- Mantener el boton presionado borra configuracion.
- Pulsaciones cortas no borran credenciales.
- El dispositivo confirma el reset mediante patron de LED.
- El flujo esta documentado."

create_issue \
  "Implementar reconexion Wi-Fi no bloqueante" \
  "firmware, wifi, resilience, priority-high" \
  "El firmware debe recuperarse automaticamente ante cortes de luz, reinicios del router y desconexiones temporales sin bloquear el control BLE ni el loop principal.

Alcance:
- Reemplazar esperas bloqueantes prolongadas por maquina de estados o timers.
- Reintentar conexion de forma indefinida.
- Registrar eventos de conexion y desconexion.
- Reanudar servicios HTTP, mDNS y Alexa demo cuando Wi-Fi vuelva.

Criterios de aceptacion:
- Si el router se apaga y vuelve, el ESP32 recupera Wi-Fi sin reflashear.
- El loop principal no queda detenido esperando Wi-Fi.
- El monitor serial muestra transiciones claras.
- Alexa demo y dashboard vuelven a estar disponibles despues de reconectar."

create_issue \
  "Agregar hostname y mDNS" \
  "firmware, networking, support, priority-medium" \
  "Permitir acceso local por hostname para evitar depender de IP visible al usuario.

Alcance:
- Configurar hostname led-bridge.
- Iniciar mDNS con led-bridge.local.
- Anunciar servicio HTTP en puerto 81.
- Reanunciar mDNS despues de reconexion Wi-Fi.

Criterios de aceptacion:
- El dashboard es accesible como http://led-bridge.local:81 en redes compatibles con mDNS.
- El monitor serial imprime hostname e IP.
- Si mDNS falla, el firmware sigue funcionando por IP."

create_issue \
  "Definir patrones de LED para estados del sistema" \
  "firmware, ux, diagnostics, priority-medium" \
  "Crear patrones de LED que permitan entender el estado del dispositivo sin monitor serial.

Alcance:
- Definir estados: setup, conectando Wi-Fi, conectado, error BLE, factory reset.
- Implementar patrones no bloqueantes.
- Documentar los patrones.

Criterios de aceptacion:
- Cada estado critico tiene un patron visual distinto.
- Los patrones no bloquean Wi-Fi, BLE ni servidor HTTP.
- El README incluye una tabla de estados."

create_issue \
  "Agregar OTA local para firmware" \
  "firmware, ota, support, priority-medium" \
  "Permitir actualizar firmware sin conectar el ESP32 por USB durante soporte o pruebas de campo.

Alcance:
- Evaluar ArduinoOTA o endpoint HTTP OTA.
- Proteger el update con password/token local.
- Mostrar version actual de firmware.
- Documentar proceso de actualizacion.

Criterios de aceptacion:
- Se puede actualizar firmware desde la red local.
- El firmware reporta version.
- Un fallo de autenticacion no permite iniciar OTA.
- El flujo esta documentado para desarrollo/soporte."

create_issue \
  "Agregar pagina de diagnostico y estado" \
  "firmware, dashboard, diagnostics, priority-medium" \
  "Agregar una vista de diagnostico para soporte tecnico y pruebas.

Alcance:
- Mostrar Wi-Fi status, IP, hostname, RSSI, BLE status, firmware version y uptime.
- Exponer la misma informacion por /api/status.
- Mantener la pagina simple para usuarios finales.

Criterios de aceptacion:
- /api/status devuelve datos utiles para soporte.
- El dashboard muestra estado sin requerir monitor serial.
- No se exponen passwords ni datos sensibles."

create_issue \
  "Separar logica de control de luz de transportes externos" \
  "architecture, firmware, priority-high" \
  "Crear una capa de servicio para que HTTP, Alexa demo, Matter o ACK puedan controlar la luz sin duplicar logica.

Alcance:
- Extraer operaciones de color, brillo, encendido y efectos a un servicio.
- Mantener persistencia de estado en una capa dedicada.
- Hacer que endpoints HTTP y fauxmo usen el mismo servicio.

Criterios de aceptacion:
- No hay logica de color duplicada entre HTTP y Alexa demo.
- El servicio central aplica y guarda estado consistentemente.
- La refactorizacion no cambia el comportamiento visible actual."

create_issue \
  "Encapsular fauxmoESP como adaptador de demo" \
  "architecture, alexa, prototype, priority-medium" \
  "Encapsular la integracion actual de fauxmoESP para dejar claro que es una capa reemplazable y no la integracion comercial final.

Alcance:
- Crear un adaptador AlexaDemoAdapter o equivalente.
- Inicializar fauxmo desde ese modulo.
- Redirigir comandos al servicio central de luz.
- Documentar limitaciones de fauxmo.

Criterios de aceptacion:
- main.cpp no contiene directamente toda la configuracion de fauxmo.
- La integracion demo se puede desactivar con una bandera de compilacion.
- La documentacion aclara que fauxmo es solo prototipo/demo."

create_issue \
  "Evaluar Matter vs Alexa Connect Kit vs Smart Home Skill" \
  "research, alexa, product, priority-high" \
  "Tomar una decision tecnica y comercial sobre la ruta oficial de integracion Alexa para producto vendible.

Alcance:
- Comparar Matter, Alexa Connect Kit y Alexa Smart Home Skill.
- Revisar requisitos de hardware, firmware, certificacion, costos y tiempos.
- Definir recomendacion para una tira LED RGB.

Criterios de aceptacion:
- Existe un documento de decision tecnica.
- La recomendacion incluye pros, contras, riesgos y costos conocidos.
- Se define si fauxmoESP queda solo como demo o se elimina del build comercial."

create_issue \
  "Preparar plan de certificacion comercial" \
  "certification, alexa, product, priority-medium" \
  "Documentar el proceso requerido para vender el producto como compatible con Alexa o con ecosistemas smart home.

Alcance:
- Identificar certificaciones necesarias segun la ruta elegida.
- Documentar pruebas de calidad esperadas.
- Preparar checklist de manufactura, provisionamiento y soporte.

Criterios de aceptacion:
- Existe checklist de certificacion.
- Se identifican riesgos legales/comerciales del uso de claims como Works with Alexa.
- Se define el minimo requerido antes de fabricar unidades."

