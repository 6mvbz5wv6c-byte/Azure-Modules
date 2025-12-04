# WebUI for PigTracks Geophone demo
FROM python:3.12-slim

ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1

# Optional: non-root user
RUN useradd -m pigtracks
WORKDIR /app

# System deps (you can omit curl if you don't care)
RUN apt-get update && apt-get install -y --no-install-recommends \
      curl \
    && rm -rf /var/lib/apt/lists/*

# Python deps – aiohttp for web server, numpy for sample handling
RUN pip install --no-cache-dir \
      aiohttp \
      numpy

# Copy application
COPY app.py ./app.py
COPY static ./static

# UDP listener port (from Geophone) and HTTP/WebSocket port
EXPOSE 8080
ENV UDP_LISTEN_PORT=9000

USER pigtracks

CMD ["python", "app.py"]
