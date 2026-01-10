#!/usr/bin/env python3
import os, json, time, asyncio, signal, traceback, base64, random
from multiprocessing import Process, Queue

from azure.iot.device.aio import IoTHubModuleClient
from azure.iot.device import Message
from azure.iot.device import exceptions as iot_exceptions

from sampler_worker import sampler_process_main  # worker process

OUTPUT_NAME            = "telemetry"
DEVICE_ID              = os.getenv("IOTEDGE_DEVICEID", "UNKNOWN_DEVICE")
GPS_LAT_PLACEHOLDER    = 36.13706
GPS_LONG_PLACEHOLDER   = -94.14972
BATTERY_PLACEHOLDER    = 7.2
TWIN_FLAG_KEY          = "analog_sampling_enabled"  # desired property key


async def twin_control(client: IoTHubModuleClient, streaming_enabled: asyncio.Event):
    """
    Control streaming based on module twin.

    Desired property: { "analog_sampling_enabled": true/false }

    When false: publisher will drop frames (no cloud messages).
    When true: publisher will send frames as normal.
    """
    try:
        twin = await client.get_twin()
        desired = twin.get("desired", twin.get("properties", {}).get("desired", {}))
    except Exception:
        traceback.print_exc()
        streaming_enabled.set()
        return

    enabled = bool(desired.get(TWIN_FLAG_KEY, True))
    if enabled:
        streaming_enabled.set()
    else:
        streaming_enabled.clear()

    try:
        await client.patch_twin_reported_properties({TWIN_FLAG_KEY: enabled})
    except Exception:
        traceback.print_exc()

    while True:
        try:
            patch = await client.receive_twin_desired_properties_patch()
        except Exception:
            traceback.print_exc()
            await asyncio.sleep(1)
            continue

        if TWIN_FLAG_KEY in patch:
            enabled = bool(patch[TWIN_FLAG_KEY])
            if enabled:
                streaming_enabled.set()
                print("[twin] streaming_enabled = True")
            else:
                streaming_enabled.clear()
                print("[twin] streaming_enabled = False")

            try:
                await client.patch_twin_reported_properties({TWIN_FLAG_KEY: enabled})
            except Exception:
                traceback.print_exc()


async def publisher_from_mpqueue(
    client: IoTHubModuleClient,
    mp_queue: Queue,
    streaming_enabled: asyncio.Event,
):
    """
    Async publisher that consumes frames from multiprocessing.Queue and sends to IoT Edge.

    Twin flag 'streaming_enabled' controls whether we actually send or just drop frames.
    """
    while True:
        # Block in a thread so the asyncio event loop isn't blocked on mp_queue.get()
        start_us, end_us, samples_bytes = await asyncio.to_thread(mp_queue.get)

        if not streaming_enabled.is_set():
            # Drop frame when streaming disabled
            continue

        samples_b64 = base64.b64encode(samples_bytes).decode("ascii")

        payload = {
            "deviceId": DEVICE_ID,
            "startUs": int(start_us),
            "endUs": int(end_us),
            "samplesB64": samples_b64,
        }

        body = json.dumps(payload)

        msg = Message(body)
        msg.content_type = "application/json"
        msg.content_encoding = "utf-8"

        msg.custom_properties["Enc"] = "int32-b64-v1"
        msg.custom_properties["GPS_Latitude"] = str(GPS_LAT_PLACEHOLDER)
        msg.custom_properties["GPS_Longitude"] = str(GPS_LONG_PLACEHOLDER)
        msg.custom_properties["Battery_Voltage"] = (
            f"{random.uniform(BATTERY_PLACEHOLDER - 0.1, BATTERY_PLACEHOLDER + 0.1):.2f}"
        )

        t0 = time.time()
        try:
            await client.send_message_to_output(msg, OUTPUT_NAME)

        except (
            iot_exceptions.ConnectionFailedError,
            iot_exceptions.ConnectionDroppedError,
        ) as e:
            # Transport not available right now. Log once per frame, back off briefly,
            # and let the loop keep consuming new frames (they will be dropped
            # while the hub is down).
            print(
                f"[publisher] transient IoT Hub transport error for frame {start_us}-{end_us}: {e}"
            )
            await asyncio.sleep(1.0)
            continue

        except Exception as e:
            # Unexpected failure – keep the module alive, but log for diagnostics.
            print(f"[publisher] unexpected error sending frame {start_us}-{end_us}: {e}")
            traceback.print_exc()
            await asyncio.sleep(0.5)
            continue

        else:
            dt = time.time() - t0
            # Uncomment if you want timing logs
            # print(f"[publisher] sent frame starting {start_us}, send took {dt:.3f}s")


async def connect_with_retries(client: IoTHubModuleClient, stop_evt: asyncio.Event):
    """
    Try to connect the IoT Hub client with exponential backoff.
    Exit cleanly if stop_evt is set while retrying.
    """
    delay = 2.0
    attempt = 0
    max_delay = 60.0

    while not stop_evt.is_set():
        attempt += 1
        try:
            print(f"[main] IoTHubModuleClient connect attempt {attempt}")
            await client.connect()
            print("[main] connected to IoT Edge Hub")
            return

        except iot_exceptions.ConnectionFailedError as e:
            # Underlying socket.connect() failed (ConnectionRefusedError, DNS, etc.)
            print(
                f"[main] connect failed (attempt {attempt}), "
                f"retrying in {delay:.0f}s: {e}"
            )
        except Exception as e:
            # Any other connect-time error; still treat as retryable, but log it.
            print(f"[main] unexpected error during connect (attempt {attempt}): {e}")
            traceback.print_exc()

        # Backoff before next attempt
        try:
            await asyncio.wait_for(stop_evt.wait(), timeout=delay)
            # If stop_evt is set during the wait, break out
            break
        except asyncio.TimeoutError:
            pass

        delay = min(delay * 2.0, max_delay)

    raise asyncio.CancelledError("[main] connect_with_retries cancelled by stop_evt")


async def main():
    # Inter-process queue and sampler process
    mp_queue = Queue(maxsize=16)

    sampler_proc = Process(target=sampler_process_main, args=(mp_queue,), daemon=True)
    sampler_proc.start()
    print("[main] sampler process started")

    # Stop event for signals and for connect retry loop
    stop_evt = asyncio.Event()

    def _handle_signal(signum, frame):
        print(f"[main] received signal {signum}, shutting down")
        stop_evt.set()

    for s in (signal.SIGINT, signal.SIGTERM):
        try:
            signal.signal(s, _handle_signal)
        except Exception:
            traceback.print_exc()

    # IoT Hub module client
    client = IoTHubModuleClient.create_from_edge_environment()

    try:
        await connect_with_retries(client, stop_evt)
    except asyncio.CancelledError as e:
        print(str(e))
        # Clean up sampler process if we never managed to connect
        sampler_proc.terminate()
        sampler_proc.join(timeout=2.0)
        return

    # Twin-controlled streaming flag
    streaming_enabled = asyncio.Event()
    streaming_enabled.set()

    try:
        await asyncio.gather(
            publisher_from_mpqueue(client, mp_queue, streaming_enabled),
            twin_control(client, streaming_enabled),
            stop_evt.wait(),
        )
    finally:
        try:
            print("[main] shutting down IoTHubModuleClient")
            await client.shutdown()
        except Exception as e:
            print(f"[main] error during client shutdown: {e}")
            traceback.print_exc()

        sampler_proc.terminate()
        sampler_proc.join(timeout=2.0)
        print("[main] sampler process terminated")
        

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except Exception:
        traceback.print_exc()
