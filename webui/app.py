import asyncio
import contextlib
import os
import socket
import struct
import json

import numpy as np
from aiohttp import web

UDP_LISTEN_PORT = int(os.getenv("UDP_LISTEN_PORT", "9000"))

# Header formats for different protocol versions
UDP_HEADER_FMT_V1 = "<2sBBIQQfI"  # version 1: single channel
UDP_HEADER_FMT_V2 = "<2sBBIQQfIB"  # version 2: dual channel (adds num_channels byte)
UDP_HEADER_SIZE_V1 = struct.calcsize(UDP_HEADER_FMT_V1)
UDP_HEADER_SIZE_V2 = struct.calcsize(UDP_HEADER_FMT_V2)

# Simple broadcast list for connected websockets
ws_clients: set[web.WebSocketResponse] = set()
SAMPLE_RATE_HZ = 1000.0

# PWM frequency state (synchronized with sampler via HTTP)
current_pwm_freq = 22.0


async def udp_receiver():
    loop = asyncio.get_running_loop()
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("0.0.0.0", UDP_LISTEN_PORT))
    sock.setblocking(True)

    while True:
        data, addr = await loop.run_in_executor(None, sock.recvfrom, 65535)

        # Try to parse version from first few bytes
        if len(data) < UDP_HEADER_SIZE_V1:
            continue

        # Check magic and version first
        magic = data[0:2]
        if magic != b"PG":
            continue

        version = data[2]

        if version == 2:
            # Dual channel format
            if len(data) < UDP_HEADER_SIZE_V2:
                continue

            header = data[:UDP_HEADER_SIZE_V2]
            payload = data[UDP_HEADER_SIZE_V2:]

            _, _, codec, frame_index, start_us, end_us, sr, n, num_channels = struct.unpack(
                UDP_HEADER_FMT_V2, header
            )

            if codec != 0:
                continue

            expected_payload_size = n * num_channels * 4
            if len(payload) != expected_payload_size:
                continue

            # De-interleave channels
            interleaved = np.frombuffer(payload, dtype="<i4").astype(np.float32)
            ad620 = interleaved[0::2].tolist()  # Even indices: AD620
            ad630 = interleaved[1::2].tolist()  # Odd indices: AD630

            t0_sec = start_us / 1_000_000.0

            frame_msg = {
                "t0": float(t0_sec),
                "ad620": ad620,
                "ad630": ad630,
                "version": 2,
            }

        else:
            # Version 1: single channel (backward compatible)
            header = data[:UDP_HEADER_SIZE_V1]
            payload = data[UDP_HEADER_SIZE_V1:]

            _, _, codec, frame_index, start_us, end_us, sr, n = struct.unpack(
                UDP_HEADER_FMT_V1, header
            )

            if codec != 0:
                continue
            if len(payload) != n * 4:
                continue

            geo = np.frombuffer(payload, dtype="<i4").astype(np.float32)
            t0_sec = start_us / 1_000_000.0

            frame_msg = {
                "t0": float(t0_sec),
                "geo": geo.tolist(),
                "version": 1,
            }

        # Broadcast frame to all active clients
        dead = []
        for ws in list(ws_clients):
            try:
                asyncio.create_task(ws.send_json(frame_msg))
            except Exception:
                dead.append(ws)

        for ws in dead:
            ws_clients.discard(ws)


async def ws_handler(request):
    ws = web.WebSocketResponse()
    await ws.prepare(request)

    ws_clients.add(ws)
    try:
        async for msg in ws:
            # Handle PWM frequency control messages from client
            if msg.type == web.WSMsgType.TEXT:
                try:
                    data = json.loads(msg.data)
                    if data.get("type") == "set_pwm_freq":
                        freq = float(data.get("freq", 22.0))
                        # Clamp to valid range: 22Hz ± 0.01Hz
                        freq = max(21.99, min(22.01, freq))
                        global current_pwm_freq
                        current_pwm_freq = freq
                        # Broadcast PWM frequency update to all clients
                        pwm_msg = {"type": "pwm_freq_update", "freq": freq}
                        for client in list(ws_clients):
                            try:
                                asyncio.create_task(client.send_json(pwm_msg))
                            except Exception:
                                pass
                except json.JSONDecodeError:
                    pass
    finally:
        ws_clients.discard(ws)
        with contextlib.suppress(Exception):
            await ws.close()

    return ws


async def index_handler(request):
    return web.FileResponse(path="static/index.html")


async def pwm_handler(request):
    """REST endpoint to get/set PWM frequency."""
    global current_pwm_freq

    if request.method == "GET":
        return web.json_response({"freq": current_pwm_freq})
    elif request.method == "POST":
        try:
            data = await request.json()
            freq = float(data.get("freq", 22.0))
            # Clamp to valid range: 22Hz ± 0.01Hz
            freq = max(21.99, min(22.01, freq))
            current_pwm_freq = freq
            # Broadcast to all WebSocket clients
            pwm_msg = {"type": "pwm_freq_update", "freq": freq}
            for ws in list(ws_clients):
                try:
                    asyncio.create_task(ws.send_json(pwm_msg))
                except Exception:
                    pass
            return web.json_response({"freq": current_pwm_freq, "status": "ok"})
        except Exception as e:
            return web.json_response({"error": str(e)}, status=400)


async def init_app():
    app = web.Application()
    app.router.add_get("/", index_handler)
    app.router.add_get("/ws", ws_handler)
    app.router.add_get("/api/pwm", pwm_handler)
    app.router.add_post("/api/pwm", pwm_handler)
    return app


async def main():
    app = await init_app()
    runner = web.AppRunner(app)
    await runner.setup()

    site = web.TCPSite(runner, "0.0.0.0", 8080)
    await site.start()

    print(f"[webui] Server started on http://0.0.0.0:8080")
    print(f"[webui] UDP listener on port {UDP_LISTEN_PORT}")

    udp_task = asyncio.create_task(udp_receiver())
    try:
        await asyncio.Future()
    finally:
        udp_task.cancel()
        with contextlib.suppress(asyncio.CancelledError):
            await udp_task


if __name__ == "__main__":
    asyncio.run(main())
