import asyncio
import contextlib
import os
import socket
import struct

import numpy as np
from aiohttp import web

UDP_LISTEN_PORT = int(os.getenv("UDP_LISTEN_PORT", "9000"))
UDP_HEADER_FMT  = "<2sBBIQQfI"
UDP_HEADER_SIZE = struct.calcsize(UDP_HEADER_FMT)

# Simple broadcast list for connected websockets
ws_clients: set[web.WebSocketResponse] = set()
SAMPLE_RATE_HZ = 1000.0  # if you want this available to the client, you can also send it

async def udp_receiver():
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

        magic, version, codec, frame_index, start_us, end_us, sr, n = struct.unpack(
            UDP_HEADER_FMT, header
        )

        if magic != b"PG":
            continue
        if codec != 0:
            continue
        if len(payload) != n * 4:
            continue

        geo = np.frombuffer(payload, dtype="<i4").astype(np.float32)

        # If your sampler is exactly 1000 Hz and n ~ 1000, the client can
        # assume dt = 1/1000 and only needs t0 in seconds:
        t0_sec = start_us / 1_000_000.0

        frame_msg = {
            "t0": float(t0_sec),
            "geo": geo.tolist(),
        }

        # Broadcast frame to all active clients
        dead = []
        for ws in list(ws_clients):
            try:
                # fire-and-forget to avoid blocking UDP loop on slow clients
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
        async for _ in ws:
            # You don't expect messages from client; just ignore
            pass
    finally:
        ws_clients.discard(ws)
        with contextlib.suppress(Exception):
            await ws.close()

    return ws


async def index_handler(request):
    return web.FileResponse(path="static/index.html")


async def init_app():
    app = web.Application()
    app.router.add_get("/", index_handler)
    app.router.add_get("/ws", ws_handler)
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
