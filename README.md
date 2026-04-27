# Smart_Entry_Doorbell_Camera

## Overview

Modern access systems rely on phones or ID cards, which creates a common issue: users can get locked out if they forget both inside.

This project solves that problem by building a smart doorbell system that:

Detects a knock using a piezoelectric sensor
Captures an image of the visitor
Sends it to a cloud server for processing
Uses facial recognition to determine identity
Automatically unlocks the door if the user is authorized


## System Architecture
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

## Installation & Setup
1. Clone the repository:
    ```
    git clone https://github.com/xyzhang30/Smart_Entry_Doorbell_Camera.git
    ```

2. Install and setup Docker and Docker Compose: 
    ```
    sudo apt install docker.io
    sudo apt install docker-compose-plugin -y
    sudo apt install docker-compose -y
    sudo usermod -aG docker $USER
    newgrp docker
    mkdir -p ~/.docker/cli-plugins/
    curl -SL https://github.com/docker/compose/releases/download/v2.24.5/docker-compose-linux-x86_64 -o ~/.docker/cli-plugins/docker-compose
    chmod +x ~/.docker/cli-plugins/docker-compose
    ```
    
3. Configuring Nginx as the reverse proxy

    cd into nginx configuration: 
    ```
    sudo nano /etc/nginx/sites-available/flaskapp
    ```
    copy the following into ```/etc/nginx/sites-available/flaskapp```:
    ```
    server {
        listen 80;
        server_name 67.159.65.184;

        # backend
        location /api/ {
            proxy_pass http://127.0.0.1:8080/;
            proxy_set_header Host $host;
            proxy_set_header X-Real-IP $remote_addr;
            proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
            proxy_set_header X-Forwarded-Proto $scheme;
        }

        # frontend
        location / {
            proxy_pass http://127.0.0.1:3000/;
            proxy_set_header Host $host;
            proxy_set_header X-Real-IP $remote_addr;
            proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
            proxy_set_header X-Forwarded-Proto $scheme;
        }
    }
    ```
    run ```sudo systemctl reload nginx``` to apply configuration update.

4. Run the webapp
    ```
    cd server/
    docker compose build
    docker compose up
    ```

5. Flash ESP32 Devices  

    Upload firmware to:
    ESP32-S3 (sensor + motor control) Use the code in the Piezo Folder
    ESP32-CAM (image capture): Use the code in the camera folder

6. Configure Environment 

    Set server IP in ESP32 code, make sure IP address of ESP32-CAM and ESP32-3 are correct.
    Ensure devices are on same network
