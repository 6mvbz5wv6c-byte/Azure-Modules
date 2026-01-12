#!/usr/bin/env python3
"""
Local test script - generates synthetic geophone data for testing the webUI
without requiring Azure IoT or ADS1256 hardware.

Usage:
    python3 local_test.py [--host localhost] [--port 9000]

This generates a test signal with:
  - 22 Hz primary signal (the target frequency)
  - 60 Hz powerline interference
  - Random noise

Perfect for testing the filtering controls in the webUI.
"""

import argparse
import socket
import struct
import time
import numpy as np
from datetime import datetime, timezone

# Default config
SAMPLE_RATE_HZ = 1000
FRAME_SAMPLES = 1000
UDP_HEADER_FMT = "<2sBBIQQfI"


def utc_us_now() -> int:
    return int(datetime.now(timezone.utc).timestamp() * 1_000_000)


def generate_test_signal(n_samples: int, t_start: float, sample_rate: float) -> np.ndarray:
    """
    Generate synthetic geophone signal with:
    - 22 Hz target signal (adjustable amplitude)
    - 60 Hz powerline noise
    - Broadband noise
    """
    t = np.arange(n_samples) / sample_rate + t_start

    # Primary 22 Hz signal (what we want to isolate)
    signal_22hz = 2_000_000 * np.sin(2 * np.pi * 22 * t)

    # 60 Hz powerline interference
    noise_60hz = 500_000 * np.sin(2 * np.pi * 60 * t)

    # Some harmonics
    noise_120hz = 100_000 * np.sin(2 * np.pi * 120 * t)

    # Low frequency drift
    drift = 300_000 * np.sin(2 * np.pi * 0.5 * t)

    # Random noise
    noise = np.random.normal(0, 100_000, n_samples)

    # Combine all components
    combined = signal_22hz + noise_60hz + noise_120hz + drift + noise

    return combined.astype(np.int32)


def main():
    parser = argparse.ArgumentParser(description="Local test data generator for webUI")
    parser.add_argument("--host", default="localhost", help="WebUI host (default: localhost)")
    parser.add_argument("--port", type=int, default=9000, help="WebUI UDP port (default: 9000)")
    parser.add_argument("--freq", type=float, default=22.0, help="Primary signal frequency (default: 22 Hz)")
    args = parser.parse_args()

    udp_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    udp_target = (args.host, args.port)

    print(f"[local_test] Sending synthetic data to {args.host}:{args.port}")
    print(f"[local_test] Signal: {args.freq} Hz + 60 Hz noise + random noise")
    print(f"[local_test] Sample rate: {SAMPLE_RATE_HZ} Hz, Frame size: {FRAME_SAMPLES}")
    print(f"[local_test] Press Ctrl+C to stop")
    print()

    frame_index = 0
    Ts_us = int(1_000_000 / SAMPLE_RATE_HZ)
    base_start_us = utc_us_now()
    t_elapsed = 0.0

    try:
        while True:
            start_us = base_start_us + frame_index * FRAME_SAMPLES * Ts_us
            end_us = start_us + (FRAME_SAMPLES - 1) * Ts_us

            # Generate test signal
            buf = generate_test_signal(FRAME_SAMPLES, t_elapsed, SAMPLE_RATE_HZ)
            t_elapsed += FRAME_SAMPLES / SAMPLE_RATE_HZ

            # Build UDP packet
            header = struct.pack(
                UDP_HEADER_FMT,
                b"PG",
                1,
                0,
                frame_index,
                int(start_us),
                int(end_us),
                float(SAMPLE_RATE_HZ),
                FRAME_SAMPLES,
            )
            payload = buf.tobytes()
            packet = header + payload

            try:
                udp_sock.sendto(packet, udp_target)
            except OSError as e:
                print(f"[local_test] UDP send error: {e}")

            frame_index += 1
            if frame_index % 10 == 0:
                print(f"[local_test] Sent frame {frame_index} (t={t_elapsed:.1f}s)")

            # Sleep ~1 second (real-time pacing)
            time.sleep(FRAME_SAMPLES / SAMPLE_RATE_HZ)

    except KeyboardInterrupt:
        print("\n[local_test] Stopped")


if __name__ == "__main__":
    main()
