from flask import Blueprint, jsonify, request, session, send_from_directory
from ..models.user import User
from ..face_recognition_util import get_face_encoding_from_image
import os
from werkzeug.utils import secure_filename
from datetime import datetime

bp = Blueprint('user', __name__, url_prefix='/user')

USER_FACES_BASE_PATH = "/srv/app/user_faces"
ALLOWED_EXTENSIONS = {'jpg', 'jpeg', 'png', 'gif'}

def allowed_file(filename):
    return '.' in filename and filename.rsplit('.', 1)[1].lower() in ALLOWED_EXTENSIONS


@bp.route('/')
def index():
    '''
    Retrieves and returns the database entry for the user in the current session
    '''
    user_id = session.get('user_id')
    if not user_id:
        return jsonify({"message": "No user logged in"}), 401

    current_user = User.query.get(user_id)
    if not current_user:
        return jsonify({"message": "User session exists but user not found in DB"}), 404

    return jsonify({
        "message": "User entry found",
        "user_data": {
            "id": current_user.id,
            "username": current_user.username,
            "has_face": current_user.get_face_encoding() is not None
        }
    }), 200


@bp.route('/add_user', methods=['POST'])
def add_user():
    '''
    creates a new account and post it to the database
    '''
    username = request.form.get("username")
    user = User(username=username)
    user.save()
    return index()


@bp.route('/upload_face', methods=['POST'])
def upload_face():
    '''
    Upload a profile photo for facial recognition.
    Expects: form data with 'username' and 'image' file
    '''
    # Check for required fields
    if 'username' not in request.form:
        return jsonify({"msg": "Missing username"}), 400
    
    if 'image' not in request.files:
        return jsonify({"msg": "No image file provided"}), 400
    
    username = request.form['username']
    image_file = request.files['image']
    
    # Validate filename
    if image_file.filename == '':
        return jsonify({"msg": "Empty filename"}), 400
    
    if not allowed_file(image_file.filename):
        return jsonify({"msg": "File type not allowed. Use JPG, PNG, or GIF"}), 400
    
    # Create user faces directory if needed
    if not os.path.exists(USER_FACES_BASE_PATH):
        os.makedirs(USER_FACES_BASE_PATH)
    
    # Check if user exists, create if not
    user = User.query.filter_by(username=username).first()
    if not user:
        user = User(username=username)
        user.save()
    
    # Save image temporarily
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    filename = secure_filename(f"{username}_{timestamp}.jpg")
    filepath = os.path.join(USER_FACES_BASE_PATH, filename)
    
    try:
        image_file.save(filepath)
        
        # Try to extract face encoding
        face_encoding = get_face_encoding_from_image(filepath)
        
        # Store encoding in database
        user.set_face_encoding(face_encoding)
        user.profile_image = filepath
        user.save()
        
        return jsonify({
            "msg": "Face uploaded and recognized",
            "user_id": user.id,
            "username": user.username
        }), 201
    
    except ValueError as e:
        # Face encoding error (no face, multiple faces, etc)
        if os.path.exists(filepath):
            os.remove(filepath)
        return jsonify({"msg": str(e), "error": "face_detection_failed"}), 400
    
    except Exception as e:
        if os.path.exists(filepath):
            os.remove(filepath)
        return jsonify({"msg": "Error processing face", "error": str(e)}), 500


@bp.route('/list', methods=['GET'])
def list_users():
    '''
    Get all registered users (without face encodings for privacy)
    '''
    users = User.all()
    output = []
    
    for user in users:
        has_face = user.get_face_encoding() is not None
        output.append({
            "id": user.id,
            "username": user.username,
            "has_face": has_face,
            "profile_image": user.profile_image
        })
    
    return jsonify(output), 200


@bp.route('/<int:user_id>', methods=['GET'])
def get_user(user_id):
    '''
    Get user details by ID
    '''
    user = User.query.get(user_id)
    
    if not user:
        return jsonify({"msg": "User not found"}), 404
    
    has_face = user.get_face_encoding() is not None
    
    return jsonify({
        "id": user.id,
        "username": user.username,
        "has_face": has_face,
        "profile_image": user.profile_image
    }), 200


@bp.route('/<int:user_id>', methods=['DELETE'])
def delete_user(user_id):
    '''
    Delete a user and their face data
    '''
    user = User.query.get(user_id)
    
    if not user:
        return jsonify({"msg": "User not found"}), 404
    
    # Delete profile image if exists
    if user.profile_image and os.path.exists(user.profile_image):
        try:
            os.remove(user.profile_image)
        except:
            pass
    
    # Delete user from database
    from ..db import db_session
    db_session.delete(user)
    db_session.commit()
    
    return jsonify({"msg": "User deleted"}), 200
