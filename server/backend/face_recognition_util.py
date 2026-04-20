"""
Facial recognition utility for doorbell camera system
Uses face_recognition library to encode and compare faces
"""

import face_recognition
import numpy as np
from PIL import Image
import os

# Distance threshold for face matching (lower = stricter matching)
# 0.6 is standard, but we use 0.5 for tighter security on doorbell
FACE_MATCH_THRESHOLD = 0.5


def get_face_encoding_from_image(image_path):
    """
    Extract face encoding from an image file.
    Returns the encoding if exactly one face is found, None otherwise.
    """
    try:
        # Load image
        image = face_recognition.load_image_file(image_path)
        
        # Get face encodings
        face_encodings = face_recognition.face_encodings(image)
        
        # Return first encoding if exactly one face found
        if len(face_encodings) == 1:
            return face_encodings[0]
        elif len(face_encodings) == 0:
            raise ValueError("No faces found in image")
        else:
            raise ValueError(f"Multiple faces found ({len(face_encodings)}), expected exactly 1")
    
    except Exception as e:
        raise Exception(f"Error encoding face: {str(e)}")


def recognize_face(image_path, user_encodings_dict):
    """
    Compare a photo against a dictionary of known faces.
    
    Args:
        image_path: Path to the image to recognize
        user_encodings_dict: Dict of {user_id: encoding} or {username: encoding}
    
    Returns:
        tuple: (matched_user_id/username or None, confidence_score)
               confidence_score is 1 - distance (so higher = better match)
    """
    try:
        # Get encoding from the doorbell image
        doorbell_encoding = get_face_encoding_from_image(image_path)
        
        # Compare against all known faces
        best_match_id = None
        best_confidence = 0
        
        for user_id, known_encoding in user_encodings_dict.items():
            # Calculate face distance
            face_distance = face_recognition.face_distance([known_encoding], doorbell_encoding)[0]
            
            # Convert distance to confidence (0-1, higher = better)
            confidence = 1 - face_distance
            
            # Check if this is the best match so far
            if confidence > best_confidence:
                best_confidence = confidence
                best_match_id = user_id
        
        # Only return match if it exceeds threshold
        if best_confidence >= FACE_MATCH_THRESHOLD:
            return best_match_id, best_confidence
        else:
            return None, best_confidence
    
    except Exception as e:
        raise Exception(f"Error recognizing face: {str(e)}")


def get_all_known_faces(users_list):
    """
    Get a dictionary of all registered user face encodings.
    
    Args:
        users_list: List of User model objects
    
    Returns:
        dict: {user_id: face_encoding} for users with face encodings
    """
    encodings_dict = {}
    
    for user in users_list:
        encoding = user.get_face_encoding()
        if encoding is not None:
            encodings_dict[user.id] = encoding
    
    return encodings_dict
