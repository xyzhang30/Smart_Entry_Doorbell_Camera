from flask import Blueprint, request, jsonify
from ..models.camera import Camera
import os
from datetime import datetime
from werkzeug.utils import secure_filename

bp = Blueprint('camera', __name__, url_prefix = '/camera')

IMAGE_STORE_BASE_PATH = "/srv/app/captures"

@bp.route('/append_logentry', methods=['POST'])
def add_user():
    '''
    gets a new image (in raw binary) and timestamp from the ESP32
    expects 'X-Timestamp' header for metatdata
    stores image on disk and path in database.
    '''
    image_bytes = request.data
    timestamp = request.headers.get("X-Timestamp")
    
    if not image_bytes:
        return jsonify({"msg": "No image data received"}), 400

    if not timestamp:
        timestamp = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    dt_object = datetime.fromtimestamp(float(timestamp))

    if not os.path.exists(IMAGE_STORE_BASE_PATH):
        os.mkdir(IMAGE_STORE_BASE_PATH)
    
    fname = f"event_{timestamp}.jpg"
    fpath = os.path.join(IMAGE_STORE_BASE_PATH, fname)

    try:
        with open(fpath, "wb") as f:
            f.write(image_bytes)
    except Exception as e:
        return jsonify({"msg": "File system error", "error": str(e)}), 500
    
    try:
        new_log = Camera(image=fpath, timestamp=dt_object)
        new_log.save()
        return jsonify({"msg": "Raw log entry saved", "path": fpath}), 201
    except Exception as e:
        return jsonify({"msg": "Database error", "error": str(e)}), 500