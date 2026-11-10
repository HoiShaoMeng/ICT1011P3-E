
"""
ICT1011 - StudyWhere
Telegram QR Reporting Handler

Features:
- Handle QR deep links
- Identify TinyScreen+ Node 01
- Display confirmation buttons
- Forward confirmed reports to team
- Prevent repeated button submissions
- Basic report cooldown

[RASPI] Run this on Raspberry Pi later.
"""

import json
import os
import time
import urllib.parse
import urllib.request

from datetime import datetime, timedelta, timezone
from html import escape
from urllib.error import URLError, HTTPError

# =====================================
# 1. CONFIGURATION
# =====================================

BOT_TOKEN = os.getenv("TELEGRAM_BOT_TOKEN")
TEAM_CHAT_ID = os.getenv("TELEGRAM_CHAT_ID")

SGT = timezone(timedelta(hours=8))

# [RASPI] Map QR codes to device IDs.
DEVICE_MAP = {
    "n1": "tinyscreen_01"
}

# Prevent rapid repeat reports
REPORT_COOLDOWN = 60

# Pending confirmations:
# (user_id, message_id) -> device_id
pending_reports = {}

# Last successful reports:
# (user_id, device_id) -> monotonic time
last_reports = {}


# =====================================
# 2. TELEGRAM API
# =====================================

def telegram_api(method, payload=None):

    url = (
        f"https://api.telegram.org/"
        f"bot{BOT_TOKEN}/{method}"
    )

    data = urllib.parse.urlencode(
        payload or {}
    ).encode("utf-8")

    request = urllib.request.Request(
        url,
        data=data,
        method="POST"
    )

    with urllib.request.urlopen(
        request, timeout=35
    ) as response:
        result = json.load(response)

    if not result.get("ok"):
        raise RuntimeError("Telegram request failed")

    return result["result"]


def send_message(chat_id, text, buttons=None):

    payload = {
        "chat_id": chat_id,
        "text": text,
        "parse_mode": "HTML"
    }

    if buttons:
        payload["reply_markup"] = json.dumps({
            "inline_keyboard": buttons
        })

    return telegram_api("sendMessage", payload)


# =====================================
# 3. QR REPORT CONFIRMATION
# =====================================

def handle_start(message):

    chat = message["chat"]

    # Only allow reports through private chats.
    if chat.get("type") != "private":
        return

    chat_id = chat["id"]
    user_id = message["from"]["id"]

    parts = message.get("text", "").split()

    if len(parts) < 2:
        send_message(
            chat_id,
            "👋 <b>Welcome to StudyWhere!</b>\n\n"
            "Scan a StudyWhere device QR code "
            "to report a connectivity problem."
        )
        return

    device_code = parts[1]

    if device_code not in DEVICE_MAP:
        send_message(
            chat_id,
            "⚠️ This device QR code is not recognised."
        )
        return

    device_id = DEVICE_MAP[device_code]

    buttons = [
        [
            {
                "text": "✅ Confirm Report",
                "callback_data": "confirm"
            }
        ],
        [
            {
                "text": "❌ Cancel",
                "callback_data": "cancel"
            }
        ]
    ]

    response = send_message(
        chat_id,
        "📡 <b>StudyWhere | Connectivity Report</b>\n\n"
        f"Device: <code>{escape(device_id)}</code>\n\n"
        "Are you experiencing a connectivity "
        "problem with this device?\n\n"
        "Please confirm before submitting.",
        buttons
    )

    # Associate confirmation with this user
    # and this specific Telegram message.
    pending_reports[
        (user_id, response["message_id"])
    ] = device_id


# =====================================
# 4. HANDLE BUTTON CLICKS
# =====================================

def handle_callback(query):

    query_id = query["id"]
    user_id = query["from"]["id"]

    message = query.get("message", {})
    chat = message.get("chat", {})

    if chat.get("type") != "private":
        telegram_api(
            "answerCallbackQuery",
            {"callback_query_id": query_id}
        )
        return

    chat_id = chat["id"]
    message_id = message.get("message_id")
    action = query.get("data", "")

    key = (user_id, message_id)

    device_id = pending_reports.get(key)

    if not device_id:
        telegram_api(
            "answerCallbackQuery",
            {
                "callback_query_id": query_id,
                "text": "This request has expired or was processed."
            }
        )
        return

    if action == "cancel":

        pending_reports.pop(key, None)

        telegram_api(
            "answerCallbackQuery",
            {
                "callback_query_id": query_id,
                "text": "Report cancelled"
            }
        )

        send_message(
            chat_id,
            "❎ Your connectivity report was cancelled."
        )
        return

    if action != "confirm":
        telegram_api(
            "answerCallbackQuery",
            {"callback_query_id": query_id}
        )
        return

    cooldown_key = (user_id, device_id)
    now = time.monotonic()

    last = last_reports.get(cooldown_key)

    if last is not None and (
        now - last < REPORT_COOLDOWN
    ):

        pending_reports.pop(key, None)

        telegram_api(
            "answerCallbackQuery",
            {
                "callback_query_id": query_id,
                "text": "You recently reported this device."
            }
        )
        return

    # =================================
    # FORWARD REPORT TO TEAM
    # =================================

    timestamp = datetime.now(SGT).strftime(
        "%d %b %Y, %I:%M:%S %p SGT"
    )

    report = (
        "🚨 <b>STUDYWHERE | MANUAL REPORT</b>\n"
        "━━━━━━━━━━━━━━━━━━\n\n"
        "📟 <b>Device Information</b>\n"
        f"Device: <code>{escape(device_id)}</code>\n"
        "Reported Issue: Connectivity problem\n"
        f"Reported At: {timestamp}\n\n"
        "📝 <b>Report Source</b>\n"
        "Student QR-code submission\n\n"
        "🔧 <b>Recommended Action</b>\n"
        "Verify the device power, Wi-Fi connection "
        "and HTTP communication.\n\n"
        "ℹ️ <i>Manual report — not automatically "
        "verified by the monitoring server.</i>"
    )

    try:
        send_message(TEAM_CHAT_ID, report)
    except (HTTPError, URLError, RuntimeError, TimeoutError):
        telegram_api(
            "answerCallbackQuery",
            {
                "callback_query_id": query_id,
                "text": "Unable to submit. Please try again."
            }
        )
        return

    # Mark as submitted only after delivery succeeds.
    last_reports[cooldown_key] = now
    pending_reports.pop(key, None)

    telegram_api(
        "answerCallbackQuery",
        {
            "callback_query_id": query_id,
            "text": "Report submitted successfully!"
        }
    )

    send_message(
        chat_id,
        "✅ <b>Report Submitted</b>\n\n"
        f"Device: <code>{escape(device_id)}</code>\n\n"
        "Thank you! The StudyWhere team has "
        "been notified."
    )

    print(f"[QR REPORT] {device_id} submitted")


# =====================================
# 5. TELEGRAM LONG POLLING
# =====================================

def main():

    if not BOT_TOKEN or not TEAM_CHAT_ID:
        raise RuntimeError(
            "Missing Telegram environment variables"
        )

    print("[BOT] StudyWhere QR handler started")

    offset = None

    while True:

        try:
            payload = {
                "timeout": 25,
                "allowed_updates": json.dumps([
                    "message",
                    "callback_query"
                ])
            }

            if offset is not None:
                payload["offset"] = offset

            updates = telegram_api(
                "getUpdates", payload
            )

            for update in updates:

                offset = update["update_id"] + 1

                if "message" in update:

                    message = update["message"]
                    text = message.get("text", "")

                    if (
                        text.split(" ", 1)[0]
                        in ("/start", "/start@StudyWhere_SIT_bot")
                    ):
                        handle_start(message)

                elif "callback_query" in update:
                    handle_callback(
                        update["callback_query"]
                    )

        except (
            HTTPError,
            URLError,
            RuntimeError,
            TimeoutError,
            KeyError,
            ValueError
        ) as error:

            # Do not print exception URLs;
            # they may contain the bot token.
            print(
                f"[BOT] Request failed: "
                f"{type(error).__name__}"
            )

            time.sleep(5)


# =====================================
# 6. START HANDLER
# =====================================

if __name__ == "__main__":
    main()
