import json
from ..db import db, db_session

class User(db.Model):
    __tablename__ = 'users'
    id = db.Column(db.Integer, primary_key=True, autoincrement=True)
    username = db.Column(db.String(80), unique=True, nullable=False)
    face_encoding = db.Column(db.Text, nullable=True)  # Store as JSON string
    profile_image = db.Column(db.String(256), nullable=True)  # Path to uploaded profile image
    
    def set_face_encoding(self, encoding):
        """Store face encoding as JSON"""
        self.face_encoding = json.dumps(encoding.tolist())
    
    def get_face_encoding(self):
        """Retrieve face encoding as numpy array"""
        if self.face_encoding:
            import numpy as np
            return np.array(json.loads(self.face_encoding))
        return None
    
    def save(self):
        db_session.add(self)
        db_session.commit()
    
    @classmethod
    def all(cls):
        '''returns all users'''
        return db_session.query(cls).all()