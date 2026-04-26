from flask import Blueprint, request, jsonify, send_from_directory
from ..models.camera import Camera
from ..models.person import Person
import face_recognition
import numpy as np
import json
import os
import requests
from datetime import datetime, timezone, timedelta

try:
    from zoneinfo import ZoneInfo
except ImportError:
    ZoneInfo = None

bp = Blueprint('camera', __name__, url_prefix='/camera')

IMAGE_STORE_BASE_PATH = "/srv/app/captures"

# ESP32 motor trigger endpoint
ESP32_MOTOR_URI = "http://172.28.149.127/trigger_motor"

# Discord webhook URL — set this in your environment or replace directly
DISCORD_WEBHOOK_URL = os.environ.get("DISCORD_WEBHOOK_URL", "")


def _run_recognition(image_path):
    """
    Run face recognition on an image file.
    Returns a list of name strings (known names or 'Unknown').
    Returns an empty list if no faces are detected or on error.
    """
    persons = Person.all()
    if not persons:
        return []

    known_encodings = [p.get_encoding() for p in persons]
    known_names = [p.name for p in persons]

    try:
        image = face_recognition.load_image_file(image_path)
        face_locations = face_recognition.face_locations(image, model="hog")
        face_encodings = face_recognition.face_encodings(image, face_locations)
    except Exception:
        return []

    found_names = []
    for encoding in face_encodings:
        distances = face_recognition.face_distance(known_encodings, encoding)
        best_idx = int(np.argmin(distances))
        if distances[best_idx] < 0.55:
            found_names.append(known_names[best_idx])
        else:
            found_names.append("Unknown")

    return found_names


def _trigger_motor(known_names=None):
    """
    POST to the ESP32 motor endpoint with recognized person names.
    """
    if not known_names:
        return

    payload = {"recognized_names": known_names}
    try:
        resp = requests.post(ESP32_MOTOR_URI, json=payload, timeout=5)
        print(f"[camera] Motor trigger response: {resp.status_code}, payload={payload}")
    except Exception as e:
        print(f"[camera] Motor trigger failed: {e}")


def _notify_discord(known_names, all_names, timestamp, image_path):
    """
    Send a Discord webhook notification when an entry is detected.
    """
    if not DISCORD_WEBHOOK_URL:
        print("[camera] DISCORD_WEBHOOK_URL not set, skipping notification")
        return

    unknown_count = all_names.count("Unknown")
    names_display = ", ".join(known_names) if known_names else "Nobody known"

    # Build a description line summarising what was seen
    parts = []
    if known_names:
        parts.append(f"**{names_display}**")
    if unknown_count:
        parts.append(f"{unknown_count} unknown face{'s' if unknown_count > 1 else ''}")
    description = " + ".join(parts) + " detected at the door."

    ts_formatted = timestamp.strftime("%Y-%m-%d %H:%M:%S %Z")
    image_filename = os.path.basename(image_path)

    embed = {
        "title": "Entry detected",
        "description": description,
        "color": 0x2ECC71 if known_names else 0xE74C3C,  # green for known, red for unknown-only
        "fields": [
            {
                "name": "Recognised",
                "value": names_display,
                "inline": True,
            },
            {
                "name": "Time",
                "value": ts_formatted,
                "inline": True,
            },
            {
                "name": "Image file",
                "value": f"`{image_filename}`",
                "inline": False,
            },
        ],
        "footer": {"text": "Door camera system"},
        "timestamp": timestamp.isoformat(),
    }

    payload = {"embeds": [embed]}

    try:
        resp = requests.post(
            DISCORD_WEBHOOK_URL,
            json=payload,
            timeout=5,
            headers={"Content-Type": "application/json"},
        )
        print(f"[camera] Discord notification sent: {resp.status_code}")
    except Exception as e:
        print(f"[camera] Discord notification failed: {e}")


@bp.route('/append_logentry', methods=['POST', 'PATCH'])
def add_user():
    '''
    Gets a new image (in raw binary) and timestamp from the ESP32.
    Expects 'X-Timestamp' header for metadata.
    Stores image on disk, runs face recognition immediately,
    saves results to DB, triggers the motor, and sends a Discord
    notification if any face (known or unknown) is detected.
    '''
    image_bytes = request.data
    timestamp = request.headers.get("X-Timestamp")

    if not image_bytes:
        return jsonify({"msg": "No image data received"}), 400

    def _est_timezone():
        if ZoneInfo is not None:
            return ZoneInfo('America/New_York')
        return timezone(timedelta(hours=-5))

    est_tz = _est_timezone()

    def _parse_timestamp(value):
        if value.isdigit():
            return datetime.fromtimestamp(float(value), tz=timezone.utc).astimezone(est_tz)
        try:
            dt_naive = datetime.strptime(value, "%Y-%m-%d_%H-%M-%S")
            return dt_naive.replace(tzinfo=est_tz)
        except ValueError:
            return datetime.now(tz=est_tz)

    if not timestamp:
        dt_object = datetime.now(tz=est_tz)
        timestamp = dt_object.strftime("%Y-%m-%d_%H-%M-%S")
    else:
        dt_object = _parse_timestamp(timestamp)

    if not os.path.exists(IMAGE_STORE_BASE_PATH):
        os.mkdir(IMAGE_STORE_BASE_PATH)

    fname = f"event_{timestamp}.jpg"
    fpath = os.path.join(IMAGE_STORE_BASE_PATH, fname)

    try:
        with open(fpath, "wb") as f:
            f.write(image_bytes)
    except Exception as e:
        return jsonify({"msg": "File system error", "error": str(e)}), 500

    # Run recognition immediately
    found_names = _run_recognition(fpath)
    known_found = [n for n in found_names if n != "Unknown"]

    # Trigger motor if at least one known face was recognised
    if known_found:
        _trigger_motor(known_found)

    # Notify Discord whenever any face is detected
    # Change `if found_names` to `if known_found` to only notify for known people
    if found_names:
        _notify_discord(known_found, found_names, dt_object, fpath)

    try:
        new_log = Camera(image=fpath, timestamp=dt_object)
        new_log.set_recognized_names(found_names)
        new_log.save()
        return jsonify({
            "msg": "Raw log entry saved",
            "path": fpath,
            "recognized": found_names
        }), 201
    except Exception as e:
        return jsonify({"msg": "Database error", "error": str(e)}), 500


@bp.route('/get_logs', methods=['GET'])
def get_logs():
    '''
    Fetch all log entries from the database for display on frontend.
    '''
    logs = Camera.all()
    output = []
    for log in logs:
        output.append({
            "id": log.id,
            "image": log.image,
            "timestamp": log.timestamp.isoformat(),
            "recognized_names": log.get_recognized_names(),
        })
    return jsonify(output)


@bp.route('/images/<filename>')
def get_image(filename):
    '''
    Serve image files from the captures directory.
    '''
    return send_from_directory(IMAGE_STORE_BASE_PATH, filename)