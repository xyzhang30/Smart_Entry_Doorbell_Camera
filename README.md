# Smart_Entry_Doorbell_Camera

Overview

Modern access systems rely on phones or ID cards, which creates a common issue: users can get locked out if they forget both inside.

This project solves that problem by building a smart doorbell system that:

Detects a knock using a piezoelectric sensor
Captures an image of the visitor
Sends it to a cloud server for processing
Uses facial recognition to determine identity
Automatically unlocks the door if the user is authorized


System Architecture
1. Edge Hardware Layer
ESP32-S3: Main controller (sensor + logic)
Piezoelectric sensor: Detects knocks
ESP32-CAM: Captures and uploads images
DC Motor + Driver (Cytron MD20A): Turns door handle

3. Cloud Infrastructure
Flask backend:  Handles API + CV processing
React frontend: Displays logs and user interface
PostgreSQL: Stores entry logs + face encodings
Docker: Containerized deployment
Nginx: Reverse proxy for stable routing

4. Web Application
-View entry logs (timestamp + image + identity)
-Upload user face images for recognition
-Trigger door unlock for recognized users

Installation & Setup
1. Clone the repository
git clone https://github.com/xyzhang30/Smart_Entry_Doorbell_Camera.git
cd Smart_Entry_Doorbell_Camera

2. Run backend (Docker): Look in server folder
docker-compose up --build

4. Flash ESP32 Devices  
Upload firmware to:
ESP32-S3 (sensor + motor control) Use the code in the Piezo Folder
ESP32-CAM (image capture): Use the code in the camera folder

6. Configure Environment
Set server IP in ESP32 code
Ensure devices are on same network
