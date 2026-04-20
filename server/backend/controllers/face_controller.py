from flask import Blueprint, request, jsonify
from ..models.person import Person
from ..models.camera import Camera
import face_recognition
import numpy as np
import json
import os
from datetime import datetime

bp = Blueprint('face', __name__, url_prefix='/face')

IMAGE_STORE_BASE_PATH = "/srv/app/captures"
REFERENCE_STORE_BASE_PATH = "/srv/app/references"


def _load_known_faces():
    """Load all person encodings from the database."""
    persons = Person.all()
    known_encodings = []
    known_names = []
    for p in persons:
        known_encodings.append(p.get_encoding())
        known_names.append(p.name)
    return known_encodings, known_names


@bp.route('/add_person', methods=['POST'])
def add_person():
    """
    Upload a reference image for a named person.
    Expects multipart form with 'name' (str) and 'image' (file).
    Computes and stores the face encoding in the database.
    """
    name = request.form.get('name', '').strip()
    image_file = request.files.get('image')

    if not name:
        return jsonify({"msg": "Name is required"}), 400
    if not image_file:
        return jsonify({"msg": "Image file is required"}), 400

    os.makedirs(REFERENCE_STORE_BASE_PATH, exist_ok=True)
    fname = f"ref_{name}_{datetime.now().strftime('%Y%m%d%H%M%S')}.jpg"
    fpath = os.path.join(REFERENCE_STORE_BASE_PATH, fname)
    image_file.save(fpath)

    image = face_recognition.load_image_file(fpath)
    encodings = face_recognition.face_encodings(image)

    if not encodings:
        os.remove(fpath)
        return jsonify({"msg": "No face detected in the uploaded image"}), 422

    encoding_json = json.dumps(encodings[0].tolist())

    existing = Person.get_by_name(name)
    if existing:
        existing.encoding = encoding_json
        existing.save()
        return jsonify({"msg": f"Updated encoding for '{name}'"}), 200

    person = Person(name=name, encoding=encoding_json)
    person.save()
    return jsonify({"msg": f"Person '{name}' added successfully"}), 201


@bp.route('/persons', methods=['GET'])
def list_persons():
    """Return all registered persons (names only)."""
    persons = Person.all()
    return jsonify([{"id": p.id, "name": p.name} for p in persons])


@bp.route('/persons/<int:person_id>', methods=['DELETE'])
def delete_person(person_id):
    """Remove a person and their encoding from the database."""
    from ..db import db_session
    person = db_session.query(Person).get(person_id)
    if not person:
        return jsonify({"msg": "Person not found"}), 404
    db_session.delete(person)
    db_session.commit()
    return jsonify({"msg": f"Person '{person.name}' deleted"}), 200


@bp.route('/recognize/<int:log_id>', methods=['GET'])
def recognize_log(log_id):
    """
    Run face recognition on a specific camera log entry.
    Returns list of recognized names found in the image.
    """
    from ..db import db_session
    log = db_session.query(Camera).get(log_id)
    if not log:
        return jsonify({"msg": "Log entry not found"}), 404

    known_encodings, known_names = _load_known_faces()
    if not known_encodings:
        return jsonify({"names": [], "msg": "No reference faces registered"}), 200

    if not os.path.exists(log.image):
        return jsonify({"msg": "Image file not found on disk"}), 404

    image = face_recognition.load_image_file(log.image)
    face_locations = face_recognition.face_locations(image, model="hog")
    face_encodings = face_recognition.face_encodings(image, face_locations)

    found_names = []
    for encoding in face_encodings:
        distances = face_recognition.face_distance(known_encodings, encoding)
        best_idx = int(np.argmin(distances))
        if distances[best_idx] < 0.55:  # Tolerance threshold
            found_names.append(known_names[best_idx])
        else:
            found_names.append("Unknown")

    return jsonify({"log_id": log_id, "names": found_names}), 200


@bp.route('/recognize_all', methods=['POST'])
def recognize_all():
    """
    Run face recognition across all camera log entries.
    Returns a mapping of log_id -> list of recognized names.
    """
    known_encodings, known_names = _load_known_faces()
    if not known_encodings:
        return jsonify({"msg": "No reference faces registered", "results": {}}), 200

    logs = Camera.all()
    results = {}

    for log in logs:
        if not os.path.exists(log.image):
            results[log.id] = []
            continue
        try:
            image = face_recognition.load_image_file(log.image)
            face_locations = face_recognition.face_locations(image, model="hog")
            face_encodings = face_recognition.face_encodings(image, face_locations)

            found_names = []
            for encoding in face_encodings:
                distances = face_recognition.face_distance(known_encodings, encoding)
                best_idx = int(np.argmin(distances))
                if distances[best_idx] < 0.55:
                    found_names.append(known_names[best_idx])
                else:
                    found_names.append("Unknown")

            results[log.id] = found_names
        except Exception:
            results[log.id] = []

    return jsonify({"results": results}), 200