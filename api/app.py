
import os
import secrets
import json

from datetime import datetime, timezone
from typing import Literal

from fastapi import FastAPI, HTTPException, Header, Query, Response
from fastapi.responses import JSONResponse
from pydantic import BaseModel, Field
from pymongo import MongoClient
from pymongo.errors import DuplicateKeyError
from dotenv import load_dotenv
from paho.mqtt import publish

load_dotenv()

app = FastAPI()

# MongoDB connection
client = MongoClient(os.environ["MONGODB_URI"])
db = client["test2"]

fans = db["fans"]
readings = db["readings"]
settings = db["settings"]

fans.create_index("mac", unique=True)
settings.create_index("mac", unique=True)

# MQTT configuration
MQTT_BROKER = "135.232.203.144"
MQTT_NAME = os.environ["MQTT_NAME"]


# Models
class Fan(BaseModel):
    mac: str
    room: str


class Reading(BaseModel):
    mac: str
    humidity: float = Field(ge=0, le=100)


class FanSettings(BaseModel):
    mode: Literal["auto", "on", "off"]
    max_humidity: float = Field(ge=0, le=100)


# A1: Register a fan
@app.post("/fans", status_code=201)
def create_fan(fan: Fan):

    new_fan = fan.model_dump()

    new_fan["api_key"] = secrets.token_urlsafe(16)

    try:
        fans.insert_one(new_fan.copy())

    except DuplicateKeyError:
        raise HTTPException(
            status_code=409,
            detail="Fan already exists"
        )

    return new_fan


# A1: Get all fans
@app.get("/fans")
def get_fans():

    return list(
        fans.find({}, {"_id": 0, "api_key": 0})
    )


# A1: Get one fan
@app.get("/fans/{mac}")
def get_fan(mac: str):

    fan = fans.find_one(
        {"mac": mac},
        {"_id": 0, "api_key": 0}
    )

    if fan is None:
        raise HTTPException(
            status_code=404,
            detail="Fan not found"
        )

    return fan


# A2: Receive authenticated humidity readings
@app.post("/readings", status_code=201)
def create_reading(
    reading: Reading,
    x_api_key: str | None = Header(default=None)
):

    if x_api_key is None:
        raise HTTPException(
            status_code=401,
            detail="API key required"
        )

    fan = fans.find_one({"api_key": x_api_key})

    if fan is None:
        raise HTTPException(
            status_code=401,
            detail="Invalid API key"
        )

    if fan["mac"] != reading.mac:
        raise HTTPException(
            status_code=403,
            detail="Key belongs to another fan"
        )

    new_reading = reading.model_dump()

    new_reading["time"] = datetime.now(timezone.utc)

    readings.insert_one(new_reading.copy())

    return new_reading


# A3: Get readings
@app.get("/readings")
def get_readings(
    mac: str | None = None,
    limit: int = Query(default=10, ge=1, le=100)
):

    query = {}

    if mac is not None:
        query["mac"] = mac

    return list(
        readings.find(query, {"_id": 0})
        .sort("time", -1)
        .limit(limit)
    )


# A4: Create or update fan settings
@app.put("/fans/{mac}/settings")
def set_fan_settings(
    mac: str,
    fan_settings: FanSettings
):

    if fans.find_one({"mac": mac}) is None:
        raise HTTPException(
            status_code=404,
            detail="Fan not found"
        )

    payload = fan_settings.model_dump()

    new_settings = {
        "mac": mac,
        **payload
    }

    result = settings.replace_one(
        {"mac": mac},
        new_settings.copy(),
        upsert=True
    )

    topic = f"ecse3038/{MQTT_NAME}/fan/settings"

    publish.single(
        topic,
        payload=json.dumps(payload),
        hostname=MQTT_BROKER,
        port=1883,
        retain=True
    )

    if result.upserted_id is not None:
        return JSONResponse(
            status_code=201,
            content=new_settings
        )

    return new_settings


# A5: Delete a fan
@app.delete("/fans/{mac}", status_code=204)
def delete_fan(
    mac: str,
    x_api_key: str | None = Header(default=None)
):

    target = fans.find_one({"mac": mac})

    if target is None:
        raise HTTPException(
            status_code=404,
            detail="Fan not found"
        )

    if x_api_key is None:
        raise HTTPException(
            status_code=401,
            detail="API key required"
        )

    owner = fans.find_one({"api_key": x_api_key})

    if owner is None:
        raise HTTPException(
            status_code=401,
            detail="Invalid API key"
        )

    if owner["mac"] != mac:
        raise HTTPException(
            status_code=403,
            detail="Key belongs to another fan"
        )

    fans.delete_one({"mac": mac})

    return Response(status_code=204)
