from flask import Blueprint, request, jsonify, send_from_directory
from ..models.camera import Camera
from ..models.user import User
from ..face_recognition_util import recognize_face, get_all_known_faces
import os
from datetime import datetime
from werkzeug.utils import secure_filename

bp = Blueprint('camera', __name__, url_prefix = '/camera')

IMAGE_STORE_BASE_PATH = "/srv/app/captures"


@bp.route('/append_logentry', methods=['POST'])
def add_user():
    '''
    gets a new image (in raw binary) and timestamp from the ESP32
    expects 'X-Timestamp' header for metadata
    performs facial recognition against registered users
    stores image on disk and path in database.
    '''
    image_bytes = request.data
    timestamp = request.headers.get("X-Timestamp")

    if not image_bytes:
        return jsonify({"msg": "No image data received"}), 400

    if not timestamp:
        timestamp = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
        timestamp_unix = datetime.now().timestamp()
    else:
        timestamp_unix = float(timestamp)
    
    dt_object = datetime.fromtimestamp(timestamp_unix)

    if not os.path.exists(IMAGE_STORE_BASE_PATH):
        os.mkdir(IMAGE_STORE_BASE_PATH)

    fname = f"event_{timestamp_unix}.jpg"
    fpath = os.path.join(IMAGE_STORE_BASE_PATH, fname)

    try:
        with open(fpath, "wb") as f:
            f.write(image_bytes)
    except Exception as e:
        return jsonify({"msg": "File system error", "error": str(e)}), 500

    # Try facial recognition
    recognized_user_id = None
    confidence = None
    
    try:
        # Get all known user face encodings
        all_users = User.all()
        known_faces = get_all_known_faces(all_users)
        
        if known_faces:
            # Perform recognition
            recognized_user_id, confidence = recognize_face(fpath, known_faces)
    except Exception as e:
        # Log error but don't fail — still save the image
        print(f"Facial recognition error: {str(e)}")

    try:
        new_log = Camera(
            image=fpath,
            timestamp=dt_object,
            recognized_user_id=recognized_user_id,
            confidence=confidence
        )
        new_log.save()
        
        response = {
            "msg": "Raw log entry saved",
            "path": fpath,
            "timestamp": dt_object.isoformat()
        }
        
        # Add recognition info if available
        if recognized_user_id:
            user = User.query.get(recognized_user_id)
            if user:
                response["recognized"] = {
                    "user_id": recognized_user_id,
                    "username": user.username,
                    "confidence": float(confidence)
                }
        else:
            response["recognized"] = None
        
        return jsonify(response), 201
    except Exception as e:
        return jsonify({"msg": "Database error", "error": str(e)}), 500



@bp.route('/get_logs', methods=['GET'])
def get_logs():
    '''
    fetch all log entries from the database for display on frontend
    '''
    logs = Camera.all()
    output = []
    for log in logs:
        entry = {
            "id": log.id,
            "image": log.image,
            "timestamp": log.timestamp.isoformat(),
            "recognized_user_id": log.recognized_user_id,
            "confidence": log.confidence
        }
        
        # Add username if recognized
        if log.recognized_user_id:
            user = User.query.get(log.recognized_user_id)
            if user:
                entry["username"] = user.username
        
        output.append(entry)
    return jsonify(output)


@bp.route('/images/<filename>')
def get_image(filename):
    '''
    serve image files from the captures directory
    '''
    return send_from_directory(IMAGE_STORE_BASE_PATH, filename)

