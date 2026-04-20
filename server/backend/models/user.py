from ..db import db, db_session

class User(db.Model):
    __tablename__ = 'users'
    id = db.Column(db.Integer, primary_key=True, autoincrement=True)
    username = db.Column(db.String(80), unique=True, nullable=False)
    
    def save(self):
        db_session.add(self)
        db_session.commit()