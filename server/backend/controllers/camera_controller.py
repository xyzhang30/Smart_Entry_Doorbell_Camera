from flask import Blueprint, request, jsonify
from ..models.camera import Camera
import os
from datetime import datetime
from werkzeug.utils import secure_filename

bp = Blueprint('camera', __name__, url_prefix = '/camera')

IMAGE_STORE_BASE_PATH = "../captures"

@bp.route('/append_logentry', methods=['POST'])
def add_user():
    '''
    gets a new image and timestamp from the ESP32, store in database as an attempted entry log.
    '''
    image_data = request.form.get("image_data")
    timestamp = request.form.get("time")
    image_store_path = IMAGE_STORE_BASE_PATH + "_" + timestamp 
    logentry = Camera(image_store_path, timestamp)
    logentry.save()
    return index()