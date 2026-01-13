import os
import socket
import struct
import traceback
import threading
import numpy as np
from datetime import datetime, timezone
import ADS1256
import RPi.GPIO as GPIO

# Environment variables, set with module in azure cloud
SAMPLE_RATE_HZ    = int(os.getenv("SAMPLE_RATE_HZ", "1000"))
FRAME_SAMPLES     = int(os.getenv("FRAME_SAMPLES", "1000"))
DIFF_CHANNEL      = int(os.getenv("DIFF_CHANNEL", "0"))
PGA_GAIN          = int(os.getenv("PGA_GAIN", "4"))

# Dual channel configuration
# Channel 0: AD620 instrumentation amp (CH0-CH1 on ADS1256)
# Channel 1: AD630 lock-in amp (CH2-CH3 on ADS1256)
AD620_DIFF_CHANNEL = 0  # AIN0-AIN1
AD630_DIFF_CHANNEL = 1  # AIN2-AIN3

# PWM configuration for AD630 lock-in reference signal
PWM_PIN = 25  # GPIO25 for lock-in reference output
PWM_FREQ_HZ = 22.0  # Default 22Hz for lock-in reference

# defaults in case the environment input variables fail
DEFAULT_GAIN_KEY  = "ADS1256_GAIN_4"
DEFAULT_DRATE_KEY = "ADS1256_1000SPS"

# Construct the strings the ADS1256 library is looking for
PGA_GAIN_SELECTION = f"ADS1256_GAIN_{PGA_GAIN}"           # e.g. "ADS1256_GAIN_16"
DRATE_SELECTION    = f"ADS1256_{SAMPLE_RATE_HZ}SPS"       # e.g. "ADS1256_1000SPS"

# Web UI UDP config
UDP_TARGET_HOST = os.getenv("WEBUI_HOST", "webui")
UDP_TARGET_PORT = int(os.getenv("WEBUI_PORT", "9000"))

# Updated header format for dual channel support (version 2)
# magic, version, codec, frame_index, start_us, end_us, sr, n, num_channels
UDP_HEADER_FMT_V2 = "<2sBBIQQfIB"
UDP_HEADER_SIZE_V2 = struct.calcsize(UDP_HEADER_FMT_V2)

udp_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
udp_target = (UDP_TARGET_HOST, UDP_TARGET_PORT)
udp_frame_counter = 0

# PWM control globals
pwm_instance = None
pwm_freq_lock = threading.Lock()
current_pwm_freq = PWM_FREQ_HZ


def utc_us_now() -> int:
    return int(datetime.now(timezone.utc).timestamp() * 1_000_000)


def init_pwm():
    """Initialize PWM on GPIO25 for AD630 lock-in reference signal."""
    global pwm_instance, current_pwm_freq

    try:
        GPIO.setmode(GPIO.BCM)
        GPIO.setwarnings(False)
        GPIO.setup(PWM_PIN, GPIO.OUT)

        # Create PWM instance at default frequency with 50% duty cycle
        pwm_instance = GPIO.PWM(PWM_PIN, current_pwm_freq)
        pwm_instance.start(50)  # 50% duty cycle for square wave
        print(f"[PWM] Started lock-in reference signal at {current_pwm_freq:.2f} Hz on GPIO{PWM_PIN}")
    except Exception as e:
        print(f"[PWM] Failed to initialize: {e}")
        pwm_instance = None


def set_pwm_frequency(freq_hz: float):
    """
    Set PWM frequency for lock-in reference.
    Valid range: 22Hz ± 0.01Hz (21.99 to 22.01 Hz)
    """
    global pwm_instance, current_pwm_freq

    # Clamp to valid range
    freq_hz = max(21.99, min(22.01, freq_hz))

    with pwm_freq_lock:
        if pwm_instance is not None:
            try:
                pwm_instance.ChangeFrequency(freq_hz)
                current_pwm_freq = freq_hz
                print(f"[PWM] Frequency changed to {freq_hz:.4f} Hz")
            except Exception as e:
                print(f"[PWM] Failed to change frequency: {e}")


def get_pwm_frequency() -> float:
    """Get current PWM frequency."""
    with pwm_freq_lock:
        return current_pwm_freq


# Passes data to webui module over UDP port
def send_udp_frame(buf_ad620: np.ndarray, buf_ad630: np.ndarray, start_us: int, end_us: int):
    """
    Fire-and-forget UDP frame for local visualization.
    Sends both AD620 and AD630 channels interleaved.
    """
    global udp_frame_counter

    frame_index = udp_frame_counter
    udp_frame_counter += 1

    sample_rate_hz = float(SAMPLE_RATE_HZ)
    frame_samples = int(len(buf_ad620))
    num_channels = 2

    header = struct.pack(
        UDP_HEADER_FMT_V2,
        b"PG",             # magic "Pigboy Geophone"
        2,                 # version 2 (dual channel)
        0,                 # codec 0 = int32 LE
        frame_index,
        int(start_us),
        int(end_us),
        sample_rate_hz,
        frame_samples,
        num_channels,      # 2 channels
    )

    # Interleave channels: [ad620[0], ad630[0], ad620[1], ad630[1], ...]
    interleaved = np.empty(frame_samples * 2, dtype=np.int32)
    interleaved[0::2] = buf_ad620
    interleaved[1::2] = buf_ad630

    payload = interleaved.astype("<i4", copy=False).tobytes()
    packet = header + payload

    try:
        udp_sock.sendto(packet, udp_target)
        if frame_index <= 3:
            print(f"[UDP] Sent dual-channel frame {frame_index} ({len(packet)} bytes) to {udp_target}")
    except OSError as e:
        if frame_index <= 3:
            print(f"[UDP] Error sending frame {frame_index}: {e}")
        # Visualization-only path; continue on failures


def sampler_process_main(frame_queue):
    """
    Runs in its own process.
    Samples ADS1256 from two differential channels:
    - CH0-CH1 (AD620 instrumentation amp)
    - CH2-CH3 (AD630 lock-in amp)

    Each item: (start_us: int, end_us: int, samples_bytes: bytes)
    """
    import time  # ensure time is available

    print(f"[sampler_proc] === STARTING DUAL-CHANNEL SAMPLER ===")
    print(f"[sampler_proc] Config: SAMPLE_RATE_HZ={SAMPLE_RATE_HZ}, FRAME_SAMPLES={FRAME_SAMPLES}")
    print(f"[sampler_proc] Config: AD620_CH={AD620_DIFF_CHANNEL}, AD630_CH={AD630_DIFF_CHANNEL}, PGA_GAIN={PGA_GAIN}")
    print(f"[sampler_proc] UDP target: {UDP_TARGET_HOST}:{UDP_TARGET_PORT}")

    # Initialize PWM for AD630 lock-in reference signal
    print(f"[sampler_proc] Initializing PWM on GPIO{PWM_PIN} for lock-in reference...")
    init_pwm()

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

        # Differential mode (ScanMode = 1)
        adc.ADS1256_SetMode(1)

        # Set data sampling rate and PGA gain values
        GAIN_KEY = PGA_GAIN_SELECTION
        SAMPLE_RATE_KEY = DRATE_SELECTION

        if GAIN_KEY not in ADS1256.ADS1256_GAIN_E:
            print(f"[WARN] Invalid gain key {GAIN_KEY}, falling back to {DEFAULT_GAIN_KEY}")
            GAIN_KEY = DEFAULT_GAIN_KEY

        if SAMPLE_RATE_KEY not in ADS1256.ADS1256_DRATE_E:
            print(f"[WARN] Invalid drate key {SAMPLE_RATE_KEY}, falling back to {DEFAULT_DRATE_KEY}")
            SAMPLE_RATE_KEY = DEFAULT_DRATE_KEY

        gain_value = ADS1256.ADS1256_GAIN_E[GAIN_KEY]
        drate_value = ADS1256.ADS1256_DRATE_E[SAMPLE_RATE_KEY]

        # Configure ADC with gain and sample rate
        print(f"[sampler_proc] Configuring ADC: Gain={GAIN_KEY}, Rate={SAMPLE_RATE_KEY}")
        adc.ADS1256_ConfigADC(gain_value, drate_value)
        print("[sampler_proc] ADC configured for dual-channel sampling")

    except Exception as e:
        print(f"[sampler_proc] FATAL ERROR during init: {e}")
        traceback.print_exc()
        return

    # Nominal 1 kHz → 1 ms between samples (per channel)
    Ts_us = int(1_000_000 / SAMPLE_RATE_HZ)
    buf_ad620 = np.zeros(FRAME_SAMPLES, dtype=np.int32)
    buf_ad630 = np.zeros(FRAME_SAMPLES, dtype=np.int32)

    from multiprocessing import queues
    Full = queues.Full

    # Establish a fixed, ideal time grid for the frames
    base_start_us = utc_us_now()
    frame_index = 0

    print(f"[sampler_proc] === ENTERING DUAL-CHANNEL SAMPLE LOOP ===")
    print(f"[sampler_proc] Reading test samples from both channels...")

    # Test read from both channels
    try:
        test_ad620 = adc.ADS1256_GetChannalValue(AD620_DIFF_CHANNEL)
        test_ad630 = adc.ADS1256_GetChannalValue(AD630_DIFF_CHANNEL)
        print(f"[sampler_proc] Test samples - AD620: {test_ad620}, AD630: {test_ad630}")
    except Exception as e:
        print(f"[sampler_proc] ERROR reading test samples: {e}")
        traceback.print_exc()
        return

    while True:
        # Idealized timestamps from the fixed time grid
        start_us = base_start_us + frame_index * FRAME_SAMPLES * Ts_us

        # Read FRAME_SAMPLES from both channels (alternating)
        for i in range(FRAME_SAMPLES):
            buf_ad620[i] = adc.ADS1256_GetChannalValue(AD620_DIFF_CHANNEL)
            buf_ad630[i] = adc.ADS1256_GetChannalValue(AD630_DIFF_CHANNEL)

        end_us = start_us + (FRAME_SAMPLES - 1) * Ts_us

        # UDP tap (dual channel)
        send_udp_frame(buf_ad620, buf_ad630, start_us, end_us)

        # Convert both channels to bytes for IoT Edge publisher (interleaved)
        interleaved = np.empty(FRAME_SAMPLES * 2, dtype=np.int32)
        interleaved[0::2] = buf_ad620
        interleaved[1::2] = buf_ad630
        samples_bytes = interleaved.astype("<i4", copy=False).tobytes()

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
            print(f"[sampler_proc] First dual-channel frame sent!")
            print(f"[sampler_proc] AD620: min={buf_ad620.min()}, max={buf_ad620.max()}, mean={buf_ad620.mean():.0f}")
            print(f"[sampler_proc] AD630: min={buf_ad630.min()}, max={buf_ad630.max()}, mean={buf_ad630.mean():.0f}")
        if frame_index % 10 == 0:
            try:
                qs = frame_queue.qsize()
            except NotImplementedError:
                qs = -1
            print(f"[sampler_proc] frame {frame_index}, qsize={qs}, AD620={buf_ad620[-1]}, AD630={buf_ad630[-1]}")
