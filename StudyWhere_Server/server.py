
"""
ICT1011 - StudyWhere
Flask Sensor Monitoring + Telegram Alerts

Features:
- Receive TinyScreen+ readings every 10 seconds
- Detect offline devices after 35 seconds
- Notify Telegram when a device goes offline
- Notify Telegram when a device reconnects
- Avoid duplicate alerts
- Provide a device status API

[RASPI] Can be migrated to Raspberry Pi.
"""

import os
import json
import math
import urllib.parse
import urllib.request

from datetime import datetime, timezone, timedelta
from html import escape
from threading import Thread, Lock
from time import monotonic, sleep
from queue import Queue, Empty

from flask import Flask, request, jsonify

app = Flask(__name__)

# =====================================
# 1. CONFIGURATION
# =====================================

OFFLINE_TIMEOUT = 35
CHECK_INTERVAL = 5
SERVER_PORT = 5000

# [RASPI] Set these environment variables
# on the Raspberry Pi when deploying.
TELEGRAM_TOKEN = os.getenv("TELEGRAM_BOT_TOKEN")
TELEGRAM_CHAT_ID = os.getenv("TELEGRAM_CHAT_ID")

SGT = timezone(timedelta(hours=8))

def format_sgt(dt):
    return dt.astimezone(SGT).strftime("%d %b %Y, %I:%M:%S %p SGT")

def format_duration(seconds):
    seconds = max(0, int(seconds))
    minutes, secs = divmod(seconds, 60)
    hours, minutes = divmod(minutes, 60)

    if hours:
        return f"{hours}h {minutes}m {secs}s"
    if minutes:
        return f"{minutes}m {secs}s"
    return f"{secs}s"

# =====================================
# 2. SHARED STATE
# =====================================

devices = {}
devices_lock = Lock()

# Telegram requests run separately so
# they do not block sensor reception.
alert_queue = Queue()


# =====================================
# 3. TELEGRAM NOTIFICATIONS
# =====================================

def send_telegram(message):

    if not TELEGRAM_TOKEN or not TELEGRAM_CHAT_ID:
        print("[TELEGRAM] Configuration missing", flush=True)
        return False

    url = (
        f"https://api.telegram.org/"
        f"bot{TELEGRAM_TOKEN}/sendMessage"
    )

    payload = urllib.parse.urlencode({
        "chat_id": TELEGRAM_CHAT_ID,
        "text": message,
        "parse_mode": "HTML"
    }).encode("utf-8")

    try:
        req = urllib.request.Request(
            url,
            data=payload,
            method="POST"
        )

        with urllib.request.urlopen(req, timeout=8) as response:
            result = json.load(response)

        if result.get("ok"):
            print("[TELEGRAM] Alert sent", flush=True)
            return True

    except Exception as error:
        print(
            f"[TELEGRAM] Delivery failed: {error}",
            flush=True
        )

    return False


def telegram_worker():

    while True:
        try:
            message = alert_queue.get(timeout=1)
        except Empty:
            continue

        try:
            send_telegram(message)
        finally:
            alert_queue.task_done()


# =====================================
# 4. HOME ENDPOINT
# =====================================

@app.route("/", methods=["GET"])
def home():
    return "StudyWhere Server is Running!"


# =====================================
# 5. RECEIVE SENSOR READINGS
# =====================================


def offline_alert(device_id, last_seen, detected_at):
    device = escape(device_id)

    return (
        "🚨 <b>STUDYWHERE | DEVICE OFFLINE</b>\n"
        "━━━━━━━━━━━━━━━━━━\n\n"
        "📟 <b>Device Information</b>\n"
        f"Device ID: <code>{device}</code>\n"
        "Status: 🔴 OFFLINE\n"
        f"Last Reading: {format_sgt(last_seen)}\n"
        f"Detected At: {format_sgt(detected_at)}\n\n"
        "⚠️ <b>Issue Detected</b>\n"
        "No sensor readings have been received "
        "for at least 35 seconds.\n\n"
        "🔧 <b>Recommended Action</b>\n"
        "• Check the device power supply.\n"
        "• Verify Wi-Fi connectivity.\n"
        "• Check HTTP/backend availability.\n\n"
        "ℹ️ <i>Automatic monitoring alert</i>"
    )


def recovery_alert(device_id, recovered_at, outage_seconds):
    device = escape(device_id)

    return (
        "✅ <b>STUDYWHERE | DEVICE RECOVERED</b>\n"
        "━━━━━━━━━━━━━━━━━━\n\n"
        "📟 <b>Device Information</b>\n"
        f"Device ID: <code>{device}</code>\n"
        "Status: 🟢 ONLINE\n"
        f"Recovered At: {format_sgt(recovered_at)}\n"
        f"Outage Duration: {format_duration(outage_seconds)}\n\n"
        "📡 <b>System Update</b>\n"
        "Sensor readings have resumed, and the device "
        "is communicating with the monitoring server.\n\n"
        "✅ <b>Action Required:</b> None\n\n"
        "ℹ️ <i>Automatic recovery notification</i>"
    )


@app.route("/api/sensor", methods=["POST"])
def receive_sensor():

    data = request.get_json(silent=True)

    if not isinstance(data, dict):
        return jsonify({"error": "Invalid JSON"}), 400

    required = (
        "device_id",
        "temperature",
        "noise_amplitude"
    )

    if any(field not in data for field in required):
        return jsonify({"error": "Missing fields"}), 400

    device_id = data["device_id"]

    if not isinstance(device_id, str) or not device_id.strip():
        return jsonify({"error": "Invalid device ID"}), 400

    for field in ("temperature", "noise_amplitude"):
        value = data[field]

        if (
            isinstance(value, bool)
            or not isinstance(value, (int, float))
            or not math.isfinite(value)
        ):
            return jsonify({"error": f"Invalid {field}"}), 400

    now = monotonic()
    timestamp = datetime.now(SGT)

    with devices_lock:

        previous = devices.get(device_id)

        recovered = (
            previous is not None
            and previous["status"] == "OFFLINE"
        )

        outage_seconds = (
            now - previous["last_seen"]
            if recovered else 0
        )

        devices[device_id] = {
            "last_seen": now,
            "timestamp": timestamp.isoformat(),
            "temperature": data["temperature"],
            "noise": data["noise_amplitude"],
            "status": "ONLINE"
        }

        if recovered:
            alert_queue.put(
                recovery_alert(
                    device_id,
                    timestamp,
                    outage_seconds
                )
            )

    print(
        f"[DATA] {device_id} | "
        f"Temp: {data['temperature']} C | "
        f"Noise: {data['noise_amplitude']}",
        flush=True
    )

    return jsonify({
        "status": "success",
        "message": "Sensor data received"
    }), 200



# =====================================
# 6. OFFLINE MONITORING
# =====================================

def monitor_devices():
    """Monitor sensor connections and send offline alerts."""

    while True:
        sleep(CHECK_INTERVAL)
        now = monotonic()
        offline_events = []

        # Check all registered devices
        with devices_lock:
            for device_id, info in devices.items():

                elapsed = now - info["last_seen"]

                # Trigger only once per disconnection
                if (
                    info["status"] == "ONLINE"
                    and elapsed >= OFFLINE_TIMEOUT
                ):
                    info["status"] = "OFFLINE"

                    offline_events.append({
                        "device_id": device_id,
                        "last_seen": info["timestamp"],
                        "elapsed": elapsed
                    })

        # Process alerts outside the lock
        for event in offline_events:

            device_id = event["device_id"]

            last_seen = datetime.fromisoformat(
                event["last_seen"]
            )

            detected_at = datetime.now(SGT)

            print(
                f"[OFFLINE] {device_id} | "
                f"No readings for {event['elapsed']:.0f}s",
                flush=True
            )

            # Queue professional Telegram alert
            alert_queue.put(
                offline_alert(
                    device_id=device_id,
                    last_seen=last_seen,
                    detected_at=detected_at
                )
            )


# =====================================
# 7. DEVICE STATUS API
# =====================================

@app.route("/api/status", methods=["GET"])
def device_status():

    with devices_lock:

        result = {
            device_id: {
                "status": info["status"],
                "temperature": info["temperature"],
                "noise": info["noise"],
                "last_seen": info["timestamp"]
            }
            for device_id, info in devices.items()
        }

    return jsonify(result), 200


# =====================================
# 8. SERVER STARTUP
# =====================================

if __name__ == "__main__":

    if not TELEGRAM_TOKEN or not TELEGRAM_CHAT_ID:
        print("[WARNING] Telegram credentials missing")

    Thread(
        target=telegram_worker,
        daemon=True
    ).start()

    Thread(
        target=monitor_devices,
        daemon=True
    ).start()

    # [RASPI] Use a managed service for deployment.
    # This is a development server with in-memory state.
    app.run(
        host="0.0.0.0",
        port=SERVER_PORT,
        debug=False,
        use_reloader=False
    )
