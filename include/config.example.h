#pragma once
// ---------------------------------------------------------------------------
// Copia este archivo a include/config.h y rellena tus datos.
// config.h esta en .gitignore para que no subas credenciales.
// ---------------------------------------------------------------------------

// ---------------- WiFi (solo 2.4 GHz, el ESP32 no ve las de 5 GHz) ---------
#define WIFI_SSID   "TU_WIFI"
#define WIFI_PASS   "TU_PASSWORD"

// ---------------- Broker MQTT ----------------------------------------------
#define MQTT_HOST     "test.mosquitto.org"   // broker publico de pruebas
#define MQTT_PORT     1883
#define MQTT_USER     ""                      // "" = sin autenticacion
#define MQTT_PASS     ""
#define MQTT_USE_TLS  0                       // 1 = TLS (puerto 8883 tipico)

// ---------------- Identidad y topics ---------------------------------------
// MQTT_BASE lleva un sufijo aleatorio a proposito: en un broker publico
// cualquiera puede leer y escribir en tus topics. Cambialo por otro random.
#define DEVICE_ID   "atom-01"
#define MQTT_BASE   "tu-prefijo/CAMBIA-ESTO"
//   <base>/<dev>/event   (pub)  el boton disparo algo
//   <base>/<dev>/ack     (sub)  el consumidor confirma la ejecucion
//   <base>/<dev>/cmd     (sub)  ordenes hacia el Atom (color de LED, ping...)
//   <base>/<dev>/status  (pub)  online / offline (retained + LWT)

// ---------------- Comportamiento del boton ---------------------------------
#define LONG_PRESS_MS        800   // a partir de aqui es pulsacion larga
#define DOUBLE_CLICK_GAP_MS  350   // ventana para detectar doble click
#define DEBOUNCE_MS           30

// Espera confirmacion en .../ack antes de dar el LED en verde.
// 0 = "fire and forget" (verde en cuanto se publica).
#define WAIT_FOR_ACK      1
#define ACK_TIMEOUT_MS    3000

// Permitir {"cmd":"reboot"} desde MQTT. Dejalo en 0 en brokers publicos.
#define ALLOW_REMOTE_REBOOT  0

// ---------------- Hardware Atom Lite ---------------------------------------
#define ATOM_LED_PIN         27   // WS2812 interno (Atom Lite / Matrix)
#define LED_IDLE_BRIGHTNESS   6   // verde tenue en reposo (0-255)
