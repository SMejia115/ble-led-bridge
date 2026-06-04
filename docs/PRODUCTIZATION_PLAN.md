# BLE LED Bridge Productization Plan

Este documento describe el camino para convertir el prototipo actual en un producto robusto, fácil de configurar y preparado para una integración comercial con Alexa.

## Objetivo

El usuario final debe poder comprar el dispositivo, conectarlo a corriente, configurarlo desde el celular y usarlo con Alexa sin conocer direcciones IP, editar firmware ni hacer cambios manuales en el router.

## Estado Actual

- El ESP32 se conecta a una red Wi-Fi definida en `src/wifi_settings.h`.
- El dashboard se sirve en `http://<IP>:81`.
- Alexa funciona mediante `fauxmoESP`, emulando una luz compatible tipo Hue.
- El estado de color y encendido se guarda en `Preferences`.
- Si el router cambia la IP asignada, el dashboard y algunos flujos de Alexa/rutinas pueden quedar apuntando a la IP anterior.

## Principios De Producto

- El firmware no debe incluir credenciales de Wi-Fi hardcodeadas.
- El cliente no debe configurar IPs, DHCP reservations ni parámetros del router.
- El dispositivo debe recuperarse solo ante cortes de luz, reinicios del router y desconexiones temporales.
- El setup debe tener una ruta clara de recuperacion: boton fisico o flujo de factory reset.
- La integracion Alexa final debe usar una ruta oficial si el producto se va a vender como compatible.

## Fase 1: Base Robusta De Firmware

### 1. Configuracion Wi-Fi Sin Código

Implementar un modo de configuracion inicial:

- Si no hay credenciales guardadas, el ESP32 crea un access point de setup.
- El usuario se conecta a una red como `LED-Bridge-Setup`.
- Una pagina local permite elegir SSID, ingresar password y guardar.
- El ESP32 guarda la configuracion en `Preferences` y reinicia en modo cliente Wi-Fi.

### 2. Reconexión Automática

El firmware debe:

- Intentar reconectarse indefinidamente cuando se pierde Wi-Fi.
- No bloquear el loop principal durante reconexion.
- Mantener BLE y control local funcionando cuando sea posible.
- Reanunciar servicios al reconectar.

### 3. Hostname y mDNS

Agregar hostname y mDNS:

- Hostname base: `led-bridge`.
- URL local sugerida: `http://led-bridge.local:81`.
- Servicio anunciado: HTTP en puerto `81`.

Esto no reemplaza una integracion Alexa oficial, pero mejora soporte, pruebas y uso local.

### 4. Factory Reset

Agregar un boton fisico o combinacion definida:

- Mantener presionado 5-10 segundos borra credenciales Wi-Fi.
- El dispositivo vuelve al modo setup.
- El usuario puede reconfigurarlo despues de cambiar router, red o propietario.

### 5. Indicadores De Estado

Definir patrones de LED:

- Setup pendiente: parpadeo lento.
- Conectando Wi-Fi: parpadeo rapido.
- Wi-Fi conectado y BLE conectado: encendido breve o pulso de confirmacion.
- Error BLE: doble parpadeo.
- Factory reset: parpadeo largo antes de reiniciar.

### 6. OTA Updates

Agregar capacidad de actualizacion:

- OTA local durante desarrollo y soporte tecnico.
- Evaluar OTA segura para produccion.
- Documentar versionado del firmware y proceso de rollback si aplica.

## Fase 2: Preparacion De Arquitectura Alexa

### 1. Aislar La Integracion Actual

Separar `fauxmoESP` en un adaptador propio, por ejemplo:

- `AlexaDemoAdapter`
- `LightControlService`
- `DeviceStateStore`

El objetivo es que el control de luz no dependa directamente de fauxmo. Luego se puede reemplazar por Matter, ACK o una skill sin reescribir toda la logica.

### 2. Mantener Fauxmo Como Demo

`fauxmoESP` debe quedar como modo de desarrollo/demo:

- Util para pruebas rapidas.
- No posicionarlo como integracion comercial final.
- Documentar sus limitaciones: emulacion no oficial, dependencia de descubrimiento local y posibles problemas al cambiar IP.

## Fase 3: Ruta Comercial Alexa

Hay tres rutas viables:

### Opcion A: Matter

Recomendada si se busca compatibilidad amplia con Alexa, Apple Home, Google Home y otros ecosistemas.

Ventajas:

- Estandar moderno de smart home.
- Control local.
- Mejor expectativa de interoperabilidad.

Consideraciones:

- Requiere soporte Matter real en firmware/hardware.
- Requiere proceso de certificacion Matter si se vende formalmente como compatible.

### Opcion B: Alexa Connect Kit

Recomendada si el objetivo principal es vender un producto muy orientado a Alexa con menor carga de cloud propio.

Ventajas:

- Ruta gestionada por Amazon.
- Reduce necesidad de crear skill, cloud propio y manejo complejo de seguridad.
- Pensada para fabricantes y certificacion.

Consideraciones:

- Requiere evaluar modulos/SDKs aprobados.
- Puede implicar costos por dispositivo y relacion comercial con Amazon/proveedores.

### Opcion C: Alexa Smart Home Skill

Recomendada solo si se necesita una nube propia y control remoto avanzado.

Ventajas:

- Maxima flexibilidad.
- Integracion formal con Alexa Smart Home APIs.

Consideraciones:

- Requiere backend, cuentas de usuario, account linking, seguridad, monitoreo y operacion cloud.
- Mayor costo de mantenimiento.

## Ruta Recomendada

1. Robustecer el firmware actual con setup Wi-Fi, reconexion, mDNS, reset fisico e indicadores.
2. Encapsular la integracion `fauxmoESP` como adaptador demo.
3. Preparar una decision tecnica entre Matter y Alexa Connect Kit.
4. Si el producto apunta solo a Alexa y velocidad comercial, evaluar ACK primero.
5. Si el producto apunta a ecosistema amplio, evaluar Matter primero.
6. Preparar certificacion cuando la ruta final este validada.

## Definicion De Listo Para MVP Comercial

- Un usuario puede configurar Wi-Fi sin cable USB.
- El dispositivo se recupera despues de cortes de luz.
- El dispositivo no depende de IP fija.
- Hay factory reset claro.
- Hay versionado de firmware.
- El dashboard local tiene URL estable por hostname/mDNS donde el sistema operativo lo soporte.
- La integracion Alexa demo esta documentada como temporal.
- La ruta oficial Alexa/Matter/ACK esta decidida antes de producir unidades.

