# M5Atom Lite → trigger MQTT

El botón del Atom Lite publica un evento MQTT; lo que escuche ese topic ejecuta
la acción y confirma con un `ack`, que el Atom te muestra en su LED RGB.

```
[botón] --event--> [broker MQTT] --> [listener.py / Node-RED / HA]
   LED <--ack------------------------------┘
```

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
   2.4 GHz) y el broker. `config.h` está en `.gitignore`.

2. Compilar y flashear (tu Atom aparece como `COM6`):

```bash
pio run -t upload && pio device monitor
```

3. En el PC, la parte que ejecuta cosas:

```bash
pip install -r tools/requirements.txt
python tools/listener.py
```

   Arranca en *dry-run*: imprime el evento y el comando que lanzaría, pero no lo
   ejecuta. Cuando el mapeo te guste, añade `--exec`.

## Gestos y topics

| Gesto | Payload en `<base>/<dev>/event` |
|---|---|
| Click | `{"dev":"atom-01","action":"click","seq":7,"held_ms":95,"uptime_s":42,"rssi":-58}` |
| Doble click | `"action":"double"` |
| Larga (≥800 ms) | `"action":"long"` — dispara al cruzar el umbral, sin esperar a que sueltes |

| Topic | Dirección | Para qué |
|---|---|---|
| `<base>/<dev>/event` | Atom → | el botón disparó algo |
| `<base>/<dev>/ack` | → Atom | `{"seq":7,"ok":true}` confirma la ejecución |
| `<base>/<dev>/cmd` | → Atom | `{"led":"#00ff88","ms":2000}`, `{"cmd":"ping"}` |
| `<base>/<dev>/status` | Atom → | `online` / `offline` (retained + Last Will) |

## El LED

| Color | Significado |
|---|---|
| Rojo respirando | sin WiFi |
| Ámbar respirando | WiFi ok, sin broker |
| Verde muy tenue | conectado, en reposo |
| Azul pulsando | evento publicado, esperando `ack` |
| Verde 0,5 s | el consumidor confirmó la ejecución |
| Rojo parpadeando | fallo al publicar, `ack` negativo o timeout |

## Por qué hay `ack`

`PubSubClient` solo publica con **QoS 0**: si el mensaje se pierde, el Atom no se
enteraría y tú te quedarías mirando un LED verde sin que nada haya pasado. El
`ack` de vuelta confirma *end-to-end* que la acción se ejecutó de verdad, que es
más fuerte que QoS 1 (eso solo garantiza entrega al broker). Si no lo quieres,
pon `WAIT_FOR_ACK 0` en `config.h`.

## Probar sin el Atom

```bash
mosquitto_sub -h test.mosquitto.org -t 'tu-prefijo/CAMBIA-ESTO/atom-01/#' -v
```

```bash
mosquitto_pub -h test.mosquitto.org -t 'tu-prefijo/CAMBIA-ESTO/atom-01/cmd' -m '{"led":"#ff00ff","ms":3000}'
```

## Seguridad

`test.mosquitto.org` es **público**: cualquiera puede leer tus eventos y publicar
en tus topics. Vale para la primera prueba, pero:

- cambia el sufijo aleatorio de `MQTT_BASE` por otro tuyo;
- `ALLOW_REMOTE_REBOOT` se queda en `0` mientras uses un broker público;
- no mapees nada destructivo en `ACTIONS` hasta tener broker propio.

Para algo permanente: Mosquitto en tu LAN con usuario/contraseña, o un broker
gestionado con TLS (pon `MQTT_USE_TLS 1`, puerto 8883, y sustituye
`setInsecure()` por `setCACert()` con el CA de tu broker).

## Siguientes pasos

- Añadir un sensor por Grove (`GPIO32`/`26`) y publicar telemetría periódica.
- `esp_sleep` + wake por `GPIO39` si lo quieres a pilas.
- Mapear `event` a automatizaciones de Home Assistant vía MQTT Discovery.
