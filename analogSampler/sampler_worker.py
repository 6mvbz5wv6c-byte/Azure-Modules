import os
import socket
import struct
import traceback
import numpy as np
from datetime import datetime, timezone
import ADS1256

# Environment variables, set with module in azure cloud
SAMPLE_RATE_HZ    = int(os.getenv("SAMPLE_RATE_HZ", "1000"))
FRAME_SAMPLES     = int(os.getenv("FRAME_SAMPLES", "1000"))
DIFF_CHANNEL      = int(os.getenv("DIFF_CHANNEL", "0"))
PGA_GAIN          = int(os.getenv("PGA_GAIN", "4"))

# defaults in case the environment input variables fail
DEFAULT_GAIN_KEY  = "ADS1256_GAIN_4"
DEFAULT_DRATE_KEY = "ADS1256_1000SPS"

# Construct the strings the ADS1256 library is looking for
PGA_GAIN_SELECTION = f"ADS1256_GAIN_{PGA_GAIN}"           # e.g. "ADS1256_GAIN_16"
DRATE_SELECTION    = f"ADS1256_{SAMPLE_RATE_HZ}SPS"       # e.g. "ADS1256_1000SPS"

# Web UI UDP config
UDP_TARGET_HOST = os.getenv("WEBUI_HOST", "webui")
UDP_TARGET_PORT = int(os.getenv("WEBUI_PORT", "9000"))

UDP_HEADER_FMT = "<2sBBIQQfI"  # magic, version, codec, frame_index, start_us, end_us, sr, n
UDP_HEADER_SIZE = struct.calcsize(UDP_HEADER_FMT)

udp_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
udp_target = (UDP_TARGET_HOST, UDP_TARGET_PORT)
udp_frame_counter = 0


def utc_us_now() -> int:
    return int(datetime.now(timezone.utc).timestamp() * 1_000_000)


# Passes data to webui module over UDP port
def send_udp_frame(buf: np.ndarray, start_us: int, end_us: int):
    """
    Fire-and-forget UDP frame for local visualization.
    """
    global udp_frame_counter

    frame_index = udp_frame_counter
    udp_frame_counter += 1

    sample_rate_hz = float(SAMPLE_RATE_HZ)
    frame_samples = int(len(buf))

    header = struct.pack(
        UDP_HEADER_FMT,
        b"PG",             # magic "Pigboy Geophone"
        1,                 # version
        0,                 # codec 0 = int32 LE
        frame_index,
        int(start_us),
        int(end_us),
        sample_rate_hz,
        frame_samples,
    )

    payload = buf.astype("<i4", copy=False).tobytes()
    packet = header + payload

    try:
        udp_sock.sendto(packet, udp_target)
        if frame_index <= 3:
            print(f"[UDP] Sent frame {frame_index} ({len(packet)} bytes) to {udp_target}")
    except OSError as e:
        if frame_index <= 3:
            print(f"[UDP] Error sending frame {frame_index}: {e}")
        # Visualization-only path; continue on failures


def sampler_process_main(frame_queue):
    """
    Runs in its own process.
    Continuously samples ADS1256 in RDATAC continuous mode and pushes frames to frame_queue.

    Each item: (start_us: int, end_us: int, samples_bytes: bytes)
    """
    import time  # ensure time is available

    print(f"[sampler_proc] === STARTING SAMPLER ===")
    print(f"[sampler_proc] Config: SAMPLE_RATE_HZ={SAMPLE_RATE_HZ}, FRAME_SAMPLES={FRAME_SAMPLES}")
    print(f"[sampler_proc] Config: DIFF_CHANNEL={DIFF_CHANNEL}, PGA_GAIN={PGA_GAIN}")
    print(f"[sampler_proc] UDP target: {UDP_TARGET_HOST}:{UDP_TARGET_PORT}")

    try:
        print("[sampler_proc] Creating ADS1256 instance...")
        adc = ADS1256.ADS1256()
        print(f"[sampler_proc] ADS1256 instance created, pins: RST={adc.rst_pin}, CS={adc.cs_pin}, DRDY={adc.drdy_pin}")

        attempt = 1
        while True:
            print(f"[sampler_proc] ADS1256_init attempt {attempt}...")
            rc = adc.ADS1256_init()
            print(f"[sampler_proc] ADS1256_init attempt {attempt} rc={rc}")
            if rc == 0:
                print("[sampler_proc] ADS1256 init succeeded")
                break
            print("[sampler_proc] ADS1256 init failed, retrying in 30 seconds...")
            attempt += 1
            time.sleep(30)


        # Differential mode
        adc.ADS1256_SetMode(0)


        # Set data sampling rate and PGA gain values. Use environment variables first
        GAIN_KEY  = PGA_GAIN_SELECTION # set by environment module
        SAMPLE_RATE_KEY = DRATE_SELECTION # set by environment module

        if GAIN_KEY not in ADS1256.ADS1256_GAIN_E:
            print(f"[WARN] Invalid gain key {GAIN_KEY}, falling back to {DEFAULT_GAIN_KEY}")
            GAIN_KEY = DEFAULT_GAIN_KEY

        if SAMPLE_RATE_KEY not in ADS1256.ADS1256_DRATE_E:
            print(f"[WARN] Invalid drate key {SAMPLE_RATE_KEY}, falling back to {DEFAULT_DRATE_KEY}")
            SAMPLE_RATE_KEY = DEFAULT_DRATE_KEY

        gain_value  = ADS1256.ADS1256_GAIN_E[GAIN_KEY]
        drate_value = ADS1256.ADS1256_DRATE_E[SAMPLE_RATE_KEY]

        # Configure and enter continuous conversion on the chosen diff channel
        print(f"[sampler_proc] Starting continuous diff mode on channel {DIFF_CHANNEL}...")
        print(f"[sampler_proc] Gain: {GAIN_KEY}={gain_value}, Rate: {SAMPLE_RATE_KEY}={drate_value}")
        adc.ADS1256_StartContinuousDiff(
            diff_channel=DIFF_CHANNEL,
            gain=gain_value,
            drate=drate_value,
        )
        print("[sampler_proc] Continuous mode started successfully")

    except Exception as e:
        print(f"[sampler_proc] FATAL ERROR during init: {e}")
        traceback.print_exc()
        return

    # Nominal 1 kHz → 1 ms between samples
    Ts_us = int(1_000_000 / SAMPLE_RATE_HZ)
    buf = np.zeros(FRAME_SAMPLES, dtype=np.int32)

    from multiprocessing import queues
    Full = queues.Full

    # Establish a fixed, ideal time grid for the frames
    base_start_us = utc_us_now()
    frame_index = 0

    print(f"[sampler_proc] === ENTERING SAMPLE LOOP ===")
    print(f"[sampler_proc] Reading first sample to verify ADC communication...")

    # Test read a single sample
    try:
        test_sample = adc.ADS1256_ReadContinuousSample()
        print(f"[sampler_proc] First sample read OK: {test_sample}")
    except Exception as e:
        print(f"[sampler_proc] ERROR reading first sample: {e}")
        traceback.print_exc()
        return

    while True:
        # Idealized timestamps from the fixed time grid
        start_us = base_start_us + frame_index * FRAME_SAMPLES * Ts_us

        # Read exactly FRAME_SAMPLES conversions; each call waits on DRDY
        for i in range(FRAME_SAMPLES):
            buf[i] = adc.ADS1256_ReadContinuousSample()

        end_us = start_us + (FRAME_SAMPLES - 1) * Ts_us

        # UDP tap 
        send_udp_frame(buf, start_us, end_us)

        # Convert to bytes for IoT Edge publisher
        samples_bytes = buf.astype("<i4", copy=False).tobytes()

        # Non-blocking enqueue with drop-oldest policy
        try:
            frame_queue.put_nowait((start_us, end_us, samples_bytes))
        except Full:
            try:
                _ = frame_queue.get_nowait()  # drop oldest
            except Exception:
                pass
            try:
                frame_queue.put_nowait((start_us, end_us, samples_bytes))
            except Full:
                # Still full: drop this frame
                pass

        frame_index += 1
        if frame_index == 1:
            # Extra debug for first frame
            print(f"[sampler_proc] First frame sent! min={buf.min()}, max={buf.max()}, mean={buf.mean():.0f}")
        if frame_index % 10 == 0:
            try:
                qs = frame_queue.qsize()
            except NotImplementedError:
                qs = -1
            print(f"[sampler_proc] frame {frame_index}, qsize={qs}, last_sample={buf[-1]}")
