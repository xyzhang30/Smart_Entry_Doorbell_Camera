from ..db import db, db_session
import numpy as np
import json

class Person(db.Model):
    __tablename__ = 'persons'
    id = db.Column(db.Integer, primary_key=True, autoincrement=True)
    name = db.Column(db.String(128), unique=True, nullable=False)
    encoding = db.Column(db.Text, nullable=False)  # JSON-serialized float list

    def __repr__(self):
        return f"<Person name={self.name}>"

    def save(self):
        db_session.add(self)
        db_session.commit()

    def get_encoding(self):
        return np.array(json.loads(self.encoding))

    @classmethod
    def all(cls):
        return db_session.query(cls).all()

    @classmethod
    def get_by_name(cls, name):
        return db_session.query(cls).filter_by(name=name).first()