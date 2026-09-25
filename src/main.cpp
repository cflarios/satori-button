/*
 * M5Stack Atom Lite - el boton como trigger remoto via MQTT
 * ---------------------------------------------------------------------------
 *  Click             -> publica {"action":"click"}   en <base>/<dev>/event
 *  Doble click       -> publica {"action":"double"}
 *  Pulsacion larga   -> publica {"action":"long"}    (dispara al llegar al umbral)
 *
 *  El LED RGB es toda la interfaz que tiene este cacharro:
 *    rojo respirando    -> sin WiFi
 *    ambar respirando   -> WiFi ok, sin broker
 *    verde muy tenue    -> conectado y en reposo
 *    azul pulsando      -> evento publicado, esperando ack
 *    verde 0.5 s        -> el consumidor confirmo la ejecucion
 *    rojo parpadeando   -> fallo al publicar, ack negativo o timeout
 *
 *  Todo el loop es no bloqueante: ni delay() largo ni while(!connected).
 */
#include <M5Unified.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Adafruit_NeoPixel.h>
#include <ArduinoJson.h>

#include "config.h"

#if MQTT_USE_TLS
#include <WiFiClientSecure.h>
static WiFiClientSecure netClient;
#else
static WiFiClient netClient;
#endif

static PubSubClient     mqtt(netClient);
static Adafruit_NeoPixel led(1, ATOM_LED_PIN, NEO_GRB + NEO_KHZ800);

// ---------------------------------------------------------------------------
// Topics e identidad (se construyen en setup a partir de config.h + MAC)
// ---------------------------------------------------------------------------
static char topicEvent[128];
static char topicAck[128];
static char topicCmd[128];
static char topicStatus[128];
static char clientId[48];
static char willPayload[96];

// ---------------------------------------------------------------------------
// Estado del LED
// ---------------------------------------------------------------------------
enum class Ui : uint8_t { WifiDown, MqttDown, Idle, Pending, Ok, Fail, Custom };
static Ui       ui        = Ui::WifiDown;
static uint32_t uiSince   = 0;
static uint32_t customRgb = 0;
static uint32_t customMs  = 0;

static void setUi(Ui s) {
  ui      = s;
  uiSince = millis();
}

static uint8_t breathe(uint16_t periodMs, uint8_t lo, uint8_t hi) {
  float ph = (millis() % periodMs) / (float)periodMs;
  float k  = 0.5f - 0.5f * cosf(ph * 2.0f * PI);
  return (uint8_t)(lo + (hi - lo) * k);
}

static void renderLed() {
  static uint32_t lastShow = 0;
  if (millis() - lastShow < 33) return;   // ~30 fps, no hace falta mas
  lastShow = millis();

  uint32_t t = millis() - uiSince;
  uint8_t  r = 0, g = 0, b = 0;

  switch (ui) {
    case Ui::WifiDown:
      r = breathe(2000, 10, 90);
      break;
    case Ui::MqttDown:
      r = breathe(1200, 10, 90);
      g = (uint8_t)(r * 0.40f);
      break;
    case Ui::Idle:
      g = LED_IDLE_BRIGHTNESS;
      break;
    case Ui::Pending:
      b = breathe(500, 15, 140);
      break;
    case Ui::Ok:
      g = 140;
      if (t > 500) setUi(Ui::Idle);
      break;
    case Ui::Fail:
      r = ((t / 120) % 2) ? 160 : 0;
      if (t > 960) setUi(mqtt.connected() ? Ui::Idle : Ui::MqttDown);
      break;
    case Ui::Custom:
      r = (customRgb >> 16) & 0xFF;
      g = (customRgb >> 8) & 0xFF;
      b = customRgb & 0xFF;
      if (t > customMs) setUi(mqtt.connected() ? Ui::Idle : Ui::MqttDown);
      break;
  }

  led.setPixelColor(0, led.Color(r, g, b));
  led.show();
}

// ---------------------------------------------------------------------------
// Publicacion de eventos + seguimiento del ack
// ---------------------------------------------------------------------------
static uint32_t seq          = 0;   // contador de eventos
static uint32_t pendingSeq   = 0;   // evento esperando confirmacion (0 = ninguno)
static uint32_t pendingSince = 0;

static void publishJson(const char* topic, JsonDocument& doc, bool retain) {
  char buf[224];
  size_t n = serializeJson(doc, buf, sizeof(buf));
  mqtt.publish(topic, (const uint8_t*)buf, n, retain);
}

static void fireEvent(const char* action, uint32_t heldMs) {
  seq++;

  JsonDocument doc;
  doc["dev"]      = DEVICE_ID;
  doc["action"]   = action;
  doc["seq"]      = seq;
  doc["held_ms"]  = heldMs;
  doc["uptime_s"] = millis() / 1000;
  doc["rssi"]     = WiFi.RSSI();

  char buf[224];
  size_t n = serializeJson(doc, buf, sizeof(buf));

  bool ok = mqtt.connected() &&
            mqtt.publish(topicEvent, (const uint8_t*)buf, n, false);

  Serial.printf("[EVT] %-6s seq=%lu held=%lums -> %s\n",
                action, (unsigned long)seq, (unsigned long)heldMs,
                ok ? "publicado" : "FALLO (sin broker)");

  if (!ok) {
    setUi(Ui::Fail);
    return;
  }

#if WAIT_FOR_ACK
  pendingSeq   = seq;
  pendingSince = millis();
  setUi(Ui::Pending);
#else
  setUi(Ui::Ok);
#endif
}

// ---------------------------------------------------------------------------
// Boton: click / doble click / pulsacion larga (maquina de estados propia)
// ---------------------------------------------------------------------------
static bool     btnDown     = false;
static uint32_t btnDownAt   = 0;
static bool     longFired   = false;
static uint8_t  clickCount  = 0;
static uint32_t lastClickAt = 0;
static uint32_t lastHeldMs  = 0;

static void pollButton() {
  const uint32_t t   = millis();
  const bool     now = M5.BtnA.isPressed();

  if (now && !btnDown) {                       // flanco de bajada
    btnDown   = true;
    btnDownAt = t;
    longFired = false;
  }

  // La larga se dispara al cruzar el umbral, sin esperar a soltar: asi ves
  // la confirmacion en el LED con el dedo todavia encima del boton.
  if (now && btnDown && !longFired && (t - btnDownAt) >= LONG_PRESS_MS) {
    longFired  = true;
    clickCount = 0;
    fireEvent("long", t - btnDownAt);
  }

  if (!now && btnDown) {                       // flanco de subida
    btnDown = false;
    const uint32_t held = t - btnDownAt;
    if (!longFired && held >= DEBOUNCE_MS) {
      if (clickCount < 2) clickCount++;
      lastClickAt = t;
      lastHeldMs  = held;
    }
  }

  // Se cerro la ventana del doble click: ya podemos decidir que fue
  if (clickCount && (t - lastClickAt) >= DOUBLE_CLICK_GAP_MS) {
    fireEvent(clickCount >= 2 ? "double" : "click", lastHeldMs);
    clickCount = 0;
  }
}

// ---------------------------------------------------------------------------
// Entrada MQTT: acks y ordenes
// ---------------------------------------------------------------------------
static void onMqttMessage(char* topic, byte* payload, unsigned int len) {
  JsonDocument doc;
  if (deserializeJson(doc, payload, len) != DeserializationError::Ok) {
    Serial.printf("[MQTT] payload no-JSON en %s\n", topic);
    return;
  }

  // ---- confirmacion de ejecucion ----
  if (strcmp(topic, topicAck) == 0) {
    const uint32_t s  = doc["seq"] | 0UL;
    const bool     ok = doc["ok"] | true;
    if (pendingSeq && s == pendingSeq) {
      Serial.printf("[ACK] seq=%lu ok=%d (%lu ms)\n",
                    (unsigned long)s, (int)ok,
                    (unsigned long)(millis() - pendingSince));
      pendingSeq = 0;
      setUi(ok ? Ui::Ok : Ui::Fail);
    }
    return;
  }

  // ---- ordenes hacia el Atom ----
  if (strcmp(topic, topicCmd) == 0) {
    const char* hex = doc["led"];                    // {"led":"#00ff88","ms":2000}
    if (hex) {
      if (*hex == '#') hex++;
      customRgb = strtoul(hex, nullptr, 16);
      customMs  = doc["ms"] | 1500UL;
      setUi(Ui::Custom);
      Serial.printf("[CMD] led=%06lX %lums\n",
                    (unsigned long)customRgb, (unsigned long)customMs);
    }

    const char* cmd = doc["cmd"] | "";
    if (strcmp(cmd, "ping") == 0) {
      JsonDocument out;
      out["dev"]      = DEVICE_ID;
      out["action"]   = "pong";
      out["uptime_s"] = millis() / 1000;
      out["rssi"]     = WiFi.RSSI();
      publishJson(topicEvent, out, false);
      Serial.println("[CMD] ping -> pong");
    } else if (strcmp(cmd, "reboot") == 0) {
#if ALLOW_REMOTE_REBOOT
      Serial.println("[CMD] reboot");
      delay(100);
      ESP.restart();
#else
      Serial.println("[CMD] reboot ignorado (ALLOW_REMOTE_REBOOT=0)");
#endif
    }
  }
}

// ---------------------------------------------------------------------------
// Conectividad
// ---------------------------------------------------------------------------
static void ensureWifi() {
  static uint32_t lastTry = 0;

  if (WiFi.status() == WL_CONNECTED) return;

  if (ui != Ui::WifiDown) {
    Serial.println("[WiFi] desconectado");
    setUi(Ui::WifiDown);
  }

  if (lastTry && millis() - lastTry < 10000) return;
  lastTry = millis();

  Serial.printf("[WiFi] conectando a \"%s\"...\n", WIFI_SSID);
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASS);
}

static void ensureMqtt() {
  static uint32_t lastTry = 0;

  if (WiFi.status() != WL_CONNECTED) return;
  if (mqtt.connected()) return;

  if (ui == Ui::WifiDown) setUi(Ui::MqttDown);
  if (lastTry && millis() - lastTry < 3000) return;
  lastTry = millis();

  Serial.printf("[MQTT] conectando a %s:%d como %s ... ",
                MQTT_HOST, (int)MQTT_PORT, clientId);

  const char* user = strlen(MQTT_USER) ? MQTT_USER : nullptr;
  const char* pass = strlen(MQTT_PASS) ? MQTT_PASS : nullptr;

  // Last Will: si el Atom se cae, el broker avisa por .../status
  bool ok = mqtt.connect(clientId, user, pass,
                         topicStatus, 0, true, willPayload, true);

  if (!ok) {
    Serial.printf("fallo (rc=%d)\n", mqtt.state());
    setUi(Ui::MqttDown);
    return;
  }

  Serial.println("ok");

  JsonDocument st;
  st["dev"]   = DEVICE_ID;
  st["state"] = "online";
  st["ip"]    = WiFi.localIP().toString();
  publishJson(topicStatus, st, true);

  mqtt.subscribe(topicAck, 1);
  mqtt.subscribe(topicCmd, 1);
  Serial.printf("[MQTT] sub %s\n", topicAck);
  Serial.printf("[MQTT] sub %s\n", topicCmd);

  setUi(Ui::Idle);
}

// ---------------------------------------------------------------------------
void setup() {
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;
  cfg.clear_display   = false;
  cfg.internal_imu    = false;   // el Atom Lite no lleva IMU
  cfg.internal_rtc    = false;
  M5.begin(cfg);

  led.begin();
  led.setBrightness(255);        // el brillo real lo pone cada color
  led.clear();
  led.show();

  uint8_t mac[6];
  WiFi.macAddress(mac);
  snprintf(clientId, sizeof(clientId), "%s-%02X%02X%02X",
           DEVICE_ID, mac[3], mac[4], mac[5]);

  snprintf(topicEvent,  sizeof(topicEvent),  "%s/%s/event",  MQTT_BASE, DEVICE_ID);
  snprintf(topicAck,    sizeof(topicAck),    "%s/%s/ack",    MQTT_BASE, DEVICE_ID);
  snprintf(topicCmd,    sizeof(topicCmd),    "%s/%s/cmd",    MQTT_BASE, DEVICE_ID);
  snprintf(topicStatus, sizeof(topicStatus), "%s/%s/status", MQTT_BASE, DEVICE_ID);
  snprintf(willPayload, sizeof(willPayload),
           "{\"dev\":\"%s\",\"state\":\"offline\"}", DEVICE_ID);

  Serial.println();
  Serial.println("=== M5Atom Lite :: trigger MQTT ===");
  Serial.printf("client  : %s\n", clientId);
  Serial.printf("event   : %s\n", topicEvent);
  Serial.printf("ack     : %s\n", topicAck);
  Serial.printf("cmd     : %s\n", topicCmd);
  Serial.printf("status  : %s\n", topicStatus);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);          // menos latencia a cambio de mas consumo
  WiFi.setAutoReconnect(true);

#if MQTT_USE_TLS
  // Para produccion: netClient.setCACert(ROOT_CA) con el CA de tu broker.
  netClient.setInsecure();
#endif

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onMqttMessage);
  mqtt.setBufferSize(512);
  mqtt.setKeepAlive(30);
  mqtt.setSocketTimeout(4);      // que un broker caido no congele el loop

  setUi(Ui::WifiDown);
}

void loop() {
  M5.update();

  ensureWifi();
  ensureMqtt();
  mqtt.loop();

  pollButton();

#if WAIT_FOR_ACK
  if (pendingSeq && millis() - pendingSince > ACK_TIMEOUT_MS) {
    Serial.printf("[ACK] timeout seq=%lu\n", (unsigned long)pendingSeq);
    pendingSeq = 0;
    setUi(Ui::Fail);
  }
#endif

  renderLed();
  delay(2);                      // deja respirar al scheduler de WiFi
}
