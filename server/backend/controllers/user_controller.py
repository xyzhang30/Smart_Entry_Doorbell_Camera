from flask import Blueprint, jsonify
from ..models.user import User

bp = Blueprint('user', __name__, url_prefix = '/user')

@bp.route('/')
def index():
    return jsonify({"message": "Backend is running!"})