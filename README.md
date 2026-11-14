# ECSE3038 Test 2 — Bathroom Fan Controller

## Project Overview

This project is an IoT bathroom extractor fan controller that uses an ESP32 and a DHT22 sensor to monitor humidity. An LED represents the extractor fan.

The ESP32 sends humidity readings to a FastAPI backend hosted on an Azure virtual machine. Readings and fan settings are stored in MongoDB Atlas. MQTT is used to communicate fan settings and connection status between the API and the ESP32.

## Repository Structure

- `api/` — FastAPI backend and Python dependencies.
- `board/` — ESP32 firmware, PlatformIO configuration and Wokwi simulation.

## How to Run the API

1. Clone the repository.
2. Navigate to the `api` directory.
3. Create a Python virtual environment:

   `python3 -m venv venv`

4. Activate the virtual environment:

   `source venv/bin/activate`

5. Install the required dependencies:

   `python -m pip install -r requirements.txt`

6. Create a `.env` file inside the `api` directory containing:

   `MONGODB_URI=your_mongodb_connection_string`

   `MQTT_NAME=calisia`

7. Start the API:

   `uvicorn app:app --host 127.0.0.1 --port 8000`

The API runs on an Azure virtual machine, with nginx forwarding public HTTP requests to port 8000.

Swagger documentation:

http://iot-calisia.northcentralus.cloudapp.azure.com/docs

## API Endpoints

| Method | Endpoint | Description |
|---|---|---|
| POST | /fans | Register a fan and generate an API key |
| GET | /fans | Retrieve all fans without API keys |
| GET | /fans/{mac} | Retrieve a specific fan |
| POST | /readings | Submit authenticated humidity readings |
| GET | /readings | Retrieve readings with optional MAC and limit filters |
| PUT | /fans/{mac}/settings | Create or update fan settings and publish them through MQTT |
| DELETE | /fans/{mac} | Delete a fan using its own API key |

## How to Run the Board

1. Open the `board` folder in VS Code.
2. Ensure the PlatformIO and Wokwi extensions are installed.
3. Create `include/secrets.h` containing:

   `#define DEVICE_KEY ""`

4. Build the PlatformIO project.
5. Start the Wokwi simulator.
6. Copy the ESP32 MAC address displayed in the Serial Monitor.
7. Register the ESP32 using `POST /fans` with its MAC address and room name.
8. Copy the generated API key into `include/secrets.h`.
9. Rebuild the firmware and restart the simulation.

The DHT22 is connected to GPIO 15, and the fan LED is connected to GPIO 2.

The ESP32 sends humidity readings to the API every 10 seconds using the X-API-Key header.

## Fan Operating Modes

- **auto:** The fan turns on when the humidity exceeds the configured maximum humidity and turns off otherwise.
- **on:** The fan remains on regardless of humidity.
- **off:** The fan remains off regardless of humidity.

The fan remains off until valid settings are received. Changes to fan settings are applied immediately.

## MQTT Communication

Broker: `135.232.203.144:1883`

| Topic | Description |
|---|---|
| ecse3038/calisia/fan/settings | Receives fan mode and maximum humidity as JSON |
| ecse3038/calisia/fan/state | Publishes on or off when the fan changes state |
| ecse3038/calisia/status | Publishes online or offline connection status |

All three topics use retained messages.

The ESP32 automatically reconnects to the MQTT broker if the connection drops. A Last Will message publishes offline when the board disconnects unexpectedly.

Invalid JSON, missing fields and invalid fan modes are rejected without changing the previous valid settings.

## Security

API keys are randomly generated and used to authenticate humidity readings and fan deletion requests.

The `.env` and `secrets.h` files are excluded from GitHub using `.gitignore`. Virtual environments and PlatformIO build output are also excluded.