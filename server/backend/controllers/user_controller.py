from flask import Blueprint, jsonify, request, session
from ..models.user import User

bp = Blueprint('user', __name__, url_prefix = '/user')

@bp.route('/')
def index():
    '''
    Retrieves and returns the database entry for the user in the current session
    '''
    user_id = session.get('user_id')
    if not user_id:
        return jsonify({"message": "No user logged in"}), 401

    current_user = User.get_user_by_id(user_id)
    if not current_user:
        return jsonify({"message": "User session exists but user not found in DB"}), 404

    return jsonify({
        "message": "User entry found",
        "user_data": current_user.to_dict()
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