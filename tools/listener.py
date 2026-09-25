#!/usr/bin/env python3
"""
Consumidor de los eventos del M5Atom Lite: esta es la parte que "ejecuta cosas".

  pip install -r tools/requirements.txt
  python tools/listener.py                 # solo escucha e imprime (seguro)
  python tools/listener.py --exec          # ejecuta de verdad los comandos de ACTIONS
  python tools/listener.py --ping          # pide un pong al Atom y sale
  python tools/listener.py --led '#00ff88' # pinta su LED 2 s y sale

Por cada evento responde en <base>/<dev>/ack con {"seq":N,"ok":true|false},
que es lo que pone el LED del Atom en verde o en rojo.
"""

import argparse
import json
import re
import subprocess
import sys
import time
from pathlib import Path

import paho.mqtt.client as mqtt

CONFIG_NAMES = ("MQTT_HOST", "MQTT_PORT", "MQTT_BASE", "DEVICE_ID", "TRIGGER_TOPIC")


def read_config() -> tuple[dict, str]:
    """Saca los #define de include/config.h para no duplicar aqui broker y topics.

    Si no existe (recien clonado el repo), cae en config.example.h, que lleva
    placeholders a proposito: config.h esta en .gitignore.
    """
    root = Path(__file__).resolve().parent.parent
    for path in (root / "include" / "config.h", root / "include" / "config.example.h"):
        if not path.exists():
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        found = {}
        for name in CONFIG_NAMES:
            m = re.search(rf'^#define\s+{name}\s+(?:"([^"]*)"|(\S+))', text, re.M)
            if m:
                found[name] = m.group(1) if m.group(1) is not None else m.group(2)
        return found, path.name
    return {}, "(ninguno)"

# --- que hacer con cada gesto del boton -------------------------------------
# El valor es un comando de shell, o None para no ejecutar nada.
# Los comandos solo se lanzan si arrancas con --exec.
ACTIONS = {
    "click":  'echo "[click] disparado a las $(date +%H:%M:%S)"',
    "double": None,   # p.ej. 'curl -s -X POST http://nas.local/webhook/backup'
    "long":   None,   # p.ej. 'ssh pi@192.168.1.50 sudo systemctl restart grafana'
    "pong":   None,
}


def run_action(action: str, event: dict, allow_exec: bool) -> tuple[bool, str]:
    cmd = ACTIONS.get(action)
    if not cmd:
        return True, "sin accion asociada"
    if not allow_exec:
        return True, f"dry-run: {cmd}"
    try:
        p = subprocess.run(cmd, shell=True, capture_output=True, text=True, timeout=30)
        out = (p.stdout or p.stderr).strip().splitlines()
        tail = out[-1] if out else ""
        return p.returncode == 0, f"rc={p.returncode} {tail}"
    except subprocess.TimeoutExpired:
        return False, "timeout (30 s)"
    except Exception as exc:                       # noqa: BLE001
        return False, f"error: {exc}"


def main() -> int:
    cfg, cfg_name = read_config()

    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default=cfg.get("MQTT_HOST", "test.mosquitto.org"))
    ap.add_argument("--port", type=int, default=int(cfg.get("MQTT_PORT", 1883)))
    ap.add_argument("--user", default=None)
    ap.add_argument("--password", default=None)
    ap.add_argument("--base", default=cfg.get("MQTT_BASE", "tu-prefijo/CAMBIA-ESTO"),
                    help="por defecto, el MQTT_BASE de include/config.h")
    ap.add_argument("--dev", default=cfg.get("DEVICE_ID", "atom-01"),
                    help="por defecto, el DEVICE_ID de include/config.h")
    ap.add_argument("--exec", dest="allow_exec", action="store_true",
                    help="ejecutar de verdad los comandos de ACTIONS")
    ap.add_argument("--ping", action="store_true", help="pedir un pong y salir")
    ap.add_argument("--led", metavar="HEX", help="pintar el LED (p.ej. '#00ff88') y salir")
    args = ap.parse_args()

    print(f"== topics y broker leidos de include/{cfg_name}")
    if "CAMBIA-ESTO" in args.base:
        print("!! estas usando el prefijo de ejemplo: copia config.example.h a "
              "config.h y ponle un sufijo aleatorio tuyo")

    t_event = f"{args.base}/{args.dev}/event"
    # Los gestos van a TRIGGER_TOPIC si esta definido (p.ej. satori/capture).
    # No lo ejecutes a la vez que Satori: los dos confirmarian el mismo evento.
    t_trigger = cfg.get("TRIGGER_TOPIC") or t_event
    t_ack = f"{args.base}/{args.dev}/ack"
    t_cmd = f"{args.base}/{args.dev}/cmd"
    t_status = f"{args.base}/{args.dev}/status"

    cli = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2,
                      client_id=f"listener-{int(time.time()) % 100000}")
    if args.user:
        cli.username_pw_set(args.user, args.password)

    def on_connect(client, _ud, _flags, rc, _props=None):
        if rc != 0:
            print(f"!! conexion rechazada: {rc}")
            return
        print(f"== conectado a {args.host}:{args.port}")
        client.subscribe(list({(t_trigger, 1), (t_event, 1), (t_status, 1)}))
        print(f"   sub {t_trigger}")
        if t_trigger != t_event:
            print(f"   sub {t_event}")
        print(f"   sub {t_status}")
        print(f"   modo {'EJECUCION' if args.allow_exec else 'dry-run (usa --exec)'}\n")

    def on_message(client, _ud, msg):
        stamp = time.strftime("%H:%M:%S")
        try:
            data = json.loads(msg.payload.decode())
        except (UnicodeDecodeError, json.JSONDecodeError):
            print(f"{stamp} {msg.topic} <payload no-JSON> {msg.payload!r}")
            return

        if msg.topic == t_status:
            print(f"{stamp} [status] {data.get('state')} ip={data.get('ip', '-')}")
            return

        action = data.get("action", "?")
        seq = data.get("seq")
        print(f"{stamp} [{action}] seq={seq} rssi={data.get('rssi')} "
              f"uptime={data.get('uptime_s')}s")

        ok, detail = run_action(action, data, args.allow_exec)
        print(f"          -> {'OK ' if ok else 'ERR'} {detail}")

        if seq is not None:
            client.publish(data.get("reply_to") or t_ack,
                           json.dumps({"seq": seq, "ok": ok, "detail": detail[:80]}), qos=1)

    cli.on_connect = on_connect
    cli.on_message = on_message
    cli.connect(args.host, args.port, keepalive=30)

    # Modos "one-shot": mandan una orden al Atom y salen
    if args.ping or args.led:
        payload = {}
        if args.ping:
            payload["cmd"] = "ping"
        if args.led:
            payload["led"] = args.led
            payload["ms"] = 2000
        cli.loop_start()
        time.sleep(0.5)
        cli.publish(t_cmd, json.dumps(payload), qos=1).wait_for_publish(5)
        print(f"-> {t_cmd} {json.dumps(payload)}")
        time.sleep(1.5 if args.ping else 0.3)
        cli.loop_stop()
        return 0

    try:
        cli.loop_forever()
    except KeyboardInterrupt:
        print("\nbye")
    return 0


if __name__ == "__main__":
    sys.exit(main())
