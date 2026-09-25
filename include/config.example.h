#pragma once
// ---------------------------------------------------------------------------
// Copia este archivo a include/config.h y rellena tus datos.
// config.h esta en .gitignore para que no subas credenciales.
// ---------------------------------------------------------------------------

// ---------------- WiFi (solo 2.4 GHz, el ESP32 no ve las de 5 GHz) ---------
#define WIFI_SSID   "TU_WIFI"
#define WIFI_PASS   "TU_PASSWORD"

// ---------------- Broker MQTT ----------------------------------------------
// El mismo broker que MQTT_BROKER en el .env de Satori (el ESP32 broker de la LAN).
#define MQTT_HOST     "192.168.1.21"
#define MQTT_PORT     1883
#define MQTT_USER     ""                      // "" = sin autenticacion
#define MQTT_PASS     ""
#define MQTT_USE_TLS  0                       // 1 = TLS (puerto 8883 tipico)

// ---------------- Identidad y topics ---------------------------------------
// Si usas un broker publico, pon en MQTT_BASE un sufijo aleatorio: ahi
// cualquiera puede leer y escribir en tus topics.
#define DEVICE_ID   "atom-01"
#define MQTT_BASE   "satori"
//   TRIGGER_TOPIC        (pub)  el boton disparo algo (click / double / long)
//   <base>/<dev>/event   (pub)  otros eventos del Atom (pong)
//   <base>/<dev>/ack     (sub)  el consumidor confirma la ejecucion
//   <base>/<dev>/cmd     (sub)  ordenes hacia el Atom (color de LED, ping...)
//   <base>/<dev>/status  (pub)  online / offline (retained + LWT)

// Donde se publican los gestos. "satori/capture" es el topic que escucha
// Satori: un click captura y resuelve. Cada evento lleva "reply_to" (el topic
// ack de arriba) para que Satori sepa donde confirmar.
// "" = usar <base>/<dev>/event, como el listener generico de tools/.
#define TRIGGER_TOPIC  "satori/capture"

// ---------------- Comportamiento del boton ---------------------------------
#define LONG_PRESS_MS        800   // a partir de aqui es pulsacion larga
#define DOUBLE_CLICK_GAP_MS  350   // ventana para detectar doble click
#define DEBOUNCE_MS           30

// Espera confirmacion en .../ack antes de dar el LED en verde.
// 0 = "fire and forget" (verde en cuanto se publica).
// Satori confirma cuando Claude ya resolvio la captura (~5-15 s), asi que el
// LED queda azul mientras analiza: el timeout tiene que cubrir ese tiempo.
#define WAIT_FOR_ACK      1
#define ACK_TIMEOUT_MS    30000

// Permitir {"cmd":"reboot"} desde MQTT. Dejalo en 0 en brokers publicos.
#define ALLOW_REMOTE_REBOOT  0

// ---------------- Hardware Atom Lite ---------------------------------------
#define ATOM_LED_PIN         27   // WS2812 interno (Atom Lite / Matrix)
#define LED_IDLE_BRIGHTNESS   6   // verde tenue en reposo (0-255)
