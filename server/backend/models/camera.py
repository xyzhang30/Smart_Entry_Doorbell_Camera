from ..db import db, db_session
import json

class Camera(db.Model):
    __tablename__ = 'cameralog'
    id = db.Column(db.Integer, primary_key=True, autoincrement=True)
    image = db.Column(db.String(256), nullable=False)
    timestamp = db.Column(db.DateTime, unique=True, nullable=False)
    recognized_names = db.Column(db.Text, nullable=True)

    def __repr__(self):
        return f"<Camera imgpath={self.image} timestamp={self.timestamp}>"

    def save(self):
        db_session.add(self)
        db_session.commit()

    def get_recognized_names(self):
        """Return recognized names as a Python list, or empty list if none."""
        if self.recognized_names is None:
            return []
        return json.loads(self.recognized_names)

    def set_recognized_names(self, names):
        """Store a list of name strings as JSON."""
        self.recognized_names = json.dumps(names)

    @classmethod
    def all(cls):
        '''
        returns all images logged in the db
        '''
        return db_session.query(cls).all()