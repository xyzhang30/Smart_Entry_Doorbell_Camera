from ..db import db, db_session

class Camera(db.Model):
    __tablename__ = 'cameralog'
    id = db.Column(db.Integer, primary_key=True, autoincrement=True)
    image = db.Column(db.String(256), nullable=False)
    timestamp = db.Column(db.DateTime, unique=True, nullable=False)
    
    def save(self):
        db_session.add(self)
        db_session.commit()