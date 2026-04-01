import os
from flask import Flask
from flask_cors import CORS
from .db import db, init_db

def create_app():
    app = Flask(__name__)
    CORS(app)

    # Database Configuration using your Docker Compose envs
    user = os.getenv('POSTGRES_USER')
    pw = os.getenv('POSTGRES_PASSWORD')
    host = os.getenv('POSTGRES_HOST')
    db_name = os.getenv('POSTGRES_DB')
    
    app.config['SQLALCHEMY_DATABASE_URI'] = f'postgresql://{user}:{pw}@{host}:5432/{db_name}'
    app.config['SQLALCHEMY_TRACK_MODIFICATIONS'] = False

    # Initialize the extension
    db.init_app(app)

    # Register Blueprints
    from .controllers import user_controller
    app.register_blueprint(user_controller.bp)

    # Create tables
    init_db(app)

    @app.route('/')
    def hello():
        return 'backend server running'

    return app

app = create_app()