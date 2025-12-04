#!/usr/bin/env python3
import asyncio
import contextlib
import json
import os
import socket
import struct
from typing import Dict, Any, Set

import numpy as np
from aiohttp import web

# -----------------------------
# UDP frame format
# -----------------------------
UDP_LISTEN_PORT = int(os.getenv("UDP_LISTEN_PORT", "9000"))
UDP_HEADER_FMT = "<2sBBIQQfI"  # magic, version, codec, frame_index, start_us, end_us, sr, n
UDP_HEADER_SIZE = struct.calcsize(UDP_HEADER_FMT)

# -----------------------------
# Config / "twin" style flags
# -----------------------------
TWIN_FLAG_KEY = "analog_sampling_enabled"
CONFIG_FILE = "config.json"

DEFAULT_SAMPLE_RATE_HZ = float(os.getenv("SAMPLE_RATE_HZ", "1000"))
DEFAULT_PGA_GAIN = int(os.getenv("PGA_GAIN", "16"))

# Allowable PGA gains for ADS1256
ALLOWED_PGA_GAINS = [1, 2, 4, 8, 16, 32, 64]


def _env_bool(name: str, default: bool) -> bool:
    val = os.getenv(name)
    if val is None:
        return default
    val = val.strip().lower()
    return val not in ("0", "false", "no", "off", "")


def load_config() -> Dict[str, Any]:
    # Start from environment defaults
    cfg: Dict[str, Any] = {
        TWIN_FLAG_KEY: _env_bool("ANALOG_SAMPLING_ENABLED", True),
        "SAMPLE_RATE_HZ": DEFAULT_SAMPLE_RATE_HZ,
        "PGA_GAIN": DEFAULT_PGA_GAIN,
    }

    # Override from config.json if present
    if os.path.exists(CONFIG_FILE):
        try:
            with open(CONFIG_FILE, "r", encoding="utf-8") as f:
                data = json.load(f)
            if isinstance(data, dict):
                cfg.update(data)
        except Exception:
            # Ignore corrupt config file and continue with defaults
            pass

    # Clamp / validate
    cfg[TWIN_FLAG_KEY] = bool(cfg.get(TWIN_FLAG_KEY, True))

    try:
        sr = float(cfg.get("SAMPLE_RATE_HZ", DEFAULT_SAMPLE_RATE_HZ))
        if sr <= 0:
            sr = DEFAULT_SAMPLE_RATE_HZ
    except Exception:
        sr = DEFAULT_SAMPLE_RATE_HZ
    cfg["SAMPLE_RATE_HZ"] = sr

    try:
        gain = int(cfg.get("PGA_GAIN", DEFAULT_PGA_GAIN))
    except Exception:
        gain = DEFAULT_PGA_GAIN
    if gain not in ALLOWED_PGA_GAINS:
        gain = DEFAULT_PGA_GAIN
    cfg["PGA_GAIN"] = gain

    return cfg


def save_config(cfg: Dict[str, Any]) -> None:
    tmp_path = CONFIG_FILE + ".tmp"
    with open(tmp_path, "w", encoding="utf-8") as f:
        json.dump(cfg, f, indent=2)
    os.replace(tmp_path, CONFIG_FILE)


CONFIG: Dict[str, Any] = load_config()

# -----------------------------
# WebSocket client tracking
# -----------------------------
ws_clients: Set[web.WebSocketResponse] = set()

BUFFER_SECONDS = 45.0
Y_MIN = -8_000_000
Y_MAX = 8_000_000


async def udp_receiver():
    """
    Receives UDP packets from sampler module and streams frames
    directly to connected WebSocket clients.

    Each UDP frame is broadcast as:
        {
          "t0": <start time seconds>,
          "sr": <sample_rate_hz>,
          "geo": [int32 counts...]
        }

    No rebiasing, decimation, or reprocessing is performed.
    """
    loop = asyncio.get_running_loop()
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("0.0.0.0", UDP_LISTEN_PORT))
    sock.setblocking(True)

    while True:
        data, addr = await loop.run_in_executor(None, sock.recvfrom, 65535)
        if len(data) < UDP_HEADER_SIZE:
            continue

        header = data[:UDP_HEADER_SIZE]
        payload = data[UDP_HEADER_SIZE:]

        try:
            magic, version, codec, frame_index, start_us, end_us, sr, n = struct.unpack(
                UDP_HEADER_FMT, header
            )
        except struct.error:
            continue

        if magic != b"PG":
            continue
        if codec != 0:
            continue
        if len(payload) != n * 4:
            continue

        geo = np.frombuffer(payload, dtype="<i4").astype(np.float32)

        t0_sec = start_us / 1_000_000.0

        frame_msg = {
            "t0": float(t0_sec),
            "sr": float(sr),
            "geo": geo.tolist(),
            "yMin": Y_MIN,
            "yMax": Y_MAX,
        }

        # If analog sampling is disabled via config, drop frames at the UI layer
        if not CONFIG.get(TWIN_FLAG_KEY, True):
            continue

        dead = []
        for ws in list(ws_clients):
            try:
                # Fire-and-forget per client to avoid blocking UDP loop
                asyncio.create_task(ws.send_json(frame_msg))
            except Exception:
                dead.append(ws)

        for ws in dead:
            ws_clients.discard(ws)


# -----------------------------
# REST API: config control
# -----------------------------
async def get_config(request: web.Request) -> web.Response:
    """
    GET /api/config
    Returns current configuration:
    {
      "analog_sampling_enabled": true/false,
      "SAMPLE_RATE_HZ": float,
      "PGA_GAIN": int
    }
    """
    return web.json_response(CONFIG)


async def update_config(request: web.Request) -> web.Response:
    """
    POST /api/config
    Accepts partial or full config JSON, validates it, persists to disk,
    and updates the in-memory CONFIG used by the rest of the app.
    """
    try:
        data = await request.json()
        if not isinstance(data, dict):
            raise ValueError("Config body must be a JSON object")
    except Exception as e:
        return web.json_response(
            {"error": f"Invalid JSON: {e}"}, status=400
        )

    updated = dict(CONFIG)

    # analog_sampling_enabled (TWIN_FLAG_KEY)
    if TWIN_FLAG_KEY in data:
        updated[TWIN_FLAG_KEY] = bool(data[TWIN_FLAG_KEY])

    # SAMPLE_RATE_HZ
    if "SAMPLE_RATE_HZ" in data:
        try:
            sr_val = float(data["SAMPLE_RATE_HZ"])
            if sr_val <= 0 or sr_val > 100_000:
                raise ValueError("SAMPLE_RATE_HZ out of range")
            updated["SAMPLE_RATE_HZ"] = sr_val
        except Exception as e:
            return web.json_response(
                {"error": f"Invalid SAMPLE_RATE_HZ: {e}"}, status=400
            )

    # PGA_GAIN
    if "PGA_GAIN" in data:
        try:
            gain_val = int(data["PGA_GAIN"])
        except Exception as e:
            return web.json_response(
                {"error": f"Invalid PGA_GAIN: {e}"}, status=400
            )
        if gain_val not in ALLOWED_PGA_GAINS:
            return web.json_response(
                {"error": f"PGA_GAIN must be one of {ALLOWED_PGA_GAINS}"}, status=400
            )
        updated["PGA_GAIN"] = gain_val

    # Persist
    global CONFIG
    CONFIG = updated
    save_config(CONFIG)

    return web.json_response(CONFIG)


# -----------------------------
# Web UI handlers
# -----------------------------
async def ws_handler(request: web.Request) -> web.StreamResponse:
    """
    WebSocket endpoint for streaming raw frames to the oscilloscope UI.
    """
    ws = web.WebSocketResponse()
    await ws.prepare(request)

    ws_clients.add(ws)

    try:
        async for _ in ws:
            # Currently we don't expect client messages;
            # could be extended later for commands.
            pass
    finally:
        ws_clients.discard(ws)
        with contextlib.suppress(Exception):
            await ws.close()

    return ws


async def index_handler(request: web.Request) -> web.Response:
    # Serve the main UI
    return web.FileResponse(path="static/index.html")


async def init_app() -> web.Application:
    app = web.Application()
    app.router.add_get("/", index_handler)
    app.router.add_get("/ws", ws_handler)
    app.router.add_get("/api/config", get_config)
    app.router.add_post("/api/config", update_config)
    app.router.add_static("/static", path="static", name="static")
    return app


async def main():
    app = await init_app()
    runner = web.AppRunner(app)
    await runner.setup()

    site = web.TCPSite(runner, "0.0.0.0", 8080)
    await site.start()

    udp_task = asyncio.create_task(udp_receiver())

    try:
        await asyncio.Future()
    finally:
        udp_task.cancel()
        with contextlib.suppress(asyncio.CancelledError):
            await udp_task


if __name__ == "__main__":
    asyncio.run(main())
