# M5Atom Lite → trigger MQTT

El botón del Atom Lite publica un evento MQTT; lo que escuche ese topic ejecuta
la acción y confirma con un `ack`, que el Atom te muestra en su LED RGB.

Por defecto es el disparador remoto de [Satori](https://github.com/cflarios/Satori):
un click publica en `satori/capture`, Satori captura de la cámara, Claude
resuelve y el `ack` llega cuando ya hay respuesta.

```
[botón] --click--> [broker MQTT] --> [Satori: cámara -> Claude]
   LED <--ack (cuando termina)-----------------┘
```

También sirve como botón genérico (`listener.py`, Node-RED, Home Assistant):
deja `TRIGGER_TOPIC ""` y los gestos van a `<base>/<dev>/event`.

## Hardware

| Qué | Dónde | Nota |
|---|---|---|
| Botón frontal | `GPIO39` | es `M5.BtnA` en M5Unified |
| LED RGB (SK6812) | `GPIO27` | 1 píxel, orden GRB |
| Grove | `GPIO32` / `GPIO26` | libre para sensores |
| IR LED | `GPIO12` | sin usar aquí |

El Atom Lite no tiene pantalla: **el LED es toda la interfaz**.

## Puesta en marcha

1. Credenciales:

```bash
cp include/config.example.h include/config.h
```

   Edita `include/config.h`: `WIFI_SSID`, `WIFI_PASS` (el ESP32 solo ve redes de
   2.4 GHz) y el broker (`MQTT_HOST`, el mismo que `MQTT_BROKER` en el `.env` de
   Satori). `config.h` está en `.gitignore`.

2. Compilar y flashear (con PlatformIO en VS Code: *Upload*, o por consola):

```bash
pio run -t upload && pio device monitor
```

   El Atom Lite lleva un conversor USB-serie **FTDI** (VID `0403`, PID `6001`).
   Si Windows lo muestra como "M5stack" con error y sin puerto COM, falta el
   driver: descarga el *VCP driver* de ftdichip.com y, si viene como `.zip` con
   `.inf` (sin `.exe`), instálalo desde una terminal de administrador:

```powershell
pnputil /add-driver "<carpeta del driver>\*.inf" /install
```

   Si "no aparece nada" al conectarlo, prueba otro cable: muchos USB-C solo cargan.

3. Con Satori no hace falta nada más: arranca Satori con `MQTT_BROKER` apuntando al
   mismo broker y una cámara de red configurada (`CAMERA_URL`).

   Para usarlo como botón genérico (`TRIGGER_TOPIC ""`), la parte que ejecuta
   cosas en el PC es `listener.py` (no lo ejecutes a la vez que Satori sobre el
   mismo topic: los dos confirmarían el mismo evento):

```bash
pip install -r tools/requirements.txt
python tools/listener.py
```

   Arranca en *dry-run*: imprime el evento y el comando que lanzaría, pero no lo
   ejecuta. Cuando el mapeo te guste, añade `--exec`.

## Gestos y topics

| Gesto | Payload en `TRIGGER_TOPIC` |
|---|---|
| Click | `{"dev":"atom-01","action":"click","seq":7,"held_ms":95,"uptime_s":42,"rssi":-58,"reply_to":"satori/atom-01/ack"}` |
| Doble click | `"action":"double"` |
| Larga (≥800 ms) | `"action":"long"` — dispara al cruzar el umbral, sin esperar a que sueltes |

Satori solo captura con el **click**; el doble click y la pulsación larga los
confirma con `ok:false` ("not mapped"), así que el LED parpadea en rojo.

| Topic | Dirección | Para qué |
|---|---|---|
| `TRIGGER_TOPIC` (`satori/capture`) | Atom → | el botón disparó algo; `reply_to` dice dónde confirmar |
| `<base>/<dev>/event` | Atom → | `pong`, y los gestos si `TRIGGER_TOPIC` es `""` |
| `<base>/<dev>/ack` | → Atom | `{"seq":7,"ok":true}` confirma la ejecución |
| `<base>/<dev>/cmd` | → Atom | `{"led":"#00ff88","ms":2000}`, `{"cmd":"ping"}` |
| `<base>/<dev>/status` | Atom → | `online` / `offline` (retained + Last Will) |

## El LED

| Color | Significado |
|---|---|
| Rojo respirando | sin WiFi |
| Ámbar respirando | WiFi ok, sin broker |
| Verde muy tenue | conectado, en reposo |
| Azul pulsando | evento publicado, esperando `ack` (con Satori: Claude analizando) |
| Verde 0,5 s | el consumidor confirmó la ejecución |
| Rojo parpadeando | fallo al publicar, `ack` negativo o timeout |

## Por qué hay `ack`

`PubSubClient` solo publica con **QoS 0**: si el mensaje se pierde, el Atom no se
enteraría y tú te quedarías mirando un LED verde sin que nada haya pasado. El
`ack` de vuelta confirma *end-to-end* que la acción se ejecutó de verdad, que es
más fuerte que QoS 1 (eso solo garantiza entrega al broker). Si no lo quieres,
pon `WAIT_FOR_ACK 0` en `config.h`.

Satori confirma cuando Claude ya resolvió (~5-15 s), por eso `ACK_TIMEOUT_MS` es
de 30 s: el LED queda azul mientras analiza y pasa a verde o rojo según el
resultado real.

## Probar sin el Atom

```bash
mosquitto_sub -h 192.168.1.21 -t 'satori/#' -v
```

```bash
mosquitto_pub -h 192.168.1.21 -t 'satori/atom-01/cmd' -m '{"led":"#ff00ff","ms":3000}'
```

## Seguridad

Con el broker de la LAN no hay nada expuesto a internet. Si usas uno público
(p. ej. `test.mosquitto.org`), cualquiera puede leer tus eventos y publicar en tus
topics — y un mensaje en `satori/capture` gasta una llamada a Claude. En ese caso:

- usa un `MQTT_BASE` y un `TRIGGER_TOPIC` con sufijo aleatorio tuyo (y el mismo
  `MQTT_CAPTURE_TOPIC` en Satori);
- `ALLOW_REMOTE_REBOOT` se queda en `0` mientras uses un broker público;
- no mapees nada destructivo en `ACTIONS` hasta tener broker propio.

Para algo permanente: Mosquitto en tu LAN con usuario/contraseña, o un broker
gestionado con TLS (pon `MQTT_USE_TLS 1`, puerto 8883, y sustituye
`setInsecure()` por `setCACert()` con el CA de tu broker).

## Siguientes pasos

- Añadir un sensor por Grove (`GPIO32`/`26`) y publicar telemetría periódica.
- `esp_sleep` + wake por `GPIO39` si lo quieres a pilas.
- Mapear `event` a automatizaciones de Home Assistant vía MQTT Discovery.
