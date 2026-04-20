from flask_sqlalchemy import SQLAlchemy
from sqlalchemy.orm import DeclarativeBase
import time

# Define a base class for your models
class Base(DeclarativeBase):
    pass

db = SQLAlchemy(model_class=Base)
db_session = db.session
# def init_db(app):
#     with app.app_context():
#         # Import models here so SQLAlchemy "sees" them before create_all
#         from .models.user import User 
#         db.create_all()
def init_db(app):
    with app.app_context():
        # CRITICAL: You MUST import the model inside this function 
        # BEFORE calling create_all so SQLAlchemy "sees" it.
        from .models.user import User 
        from .models.camera import Camera
        from .models.person import Person
        
        # Simple retry loop to handle the DB boot-up lag
        for i in range(10):
            try:
                db.create_all()
                print("--- Tables created successfully! ---")
                return
            except Exception as e:
                print(f"--- Waiting for DB... attempt {i+1} ---")
                time.sleep(2)