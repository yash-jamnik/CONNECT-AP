import serial
import struct
import time

# --- CONFIGURE THESE ---
PORT = "COM10"        # Change to your port
BAUDRATE = 115200
NEW_NAME = "ID=E0:E7:5C:23:4C:48"   # The name you want the NRF52 client to search for
# ------------------------


def send_setname_command(port: str, baudrate: int, name: str):
    """
    Send a SETNAME command in this format:
        [4-byte little-endian length][ASCII: "SETNAME:<name>"]
    """
    cmd_str = f"SETNAME:{name}"
    payload = cmd_str.encode("ascii")
    size_bytes = struct.pack("<I", len(payload))

    print(f"Opening {port} at {baudrate} baud...")
    with serial.Serial(port, baudrate, timeout=1) as ser:
        time.sleep(0.1)  # small delay after opening

        # Optionally clear input buffer
        ser.reset_input_buffer()

        # Send header + payload
        ser.write(size_bytes)
        ser.write(payload)

        print(f"Sent SETNAME command:")
        print(f"  Text : {cmd_str}")
        print(f"  Size : {len(payload)} bytes")
        print(f"  Size header (hex): {size_bytes.hex()}")
        print(f"  Payload (hex)    : {payload.hex()}")


if __name__ == "__main__":
    send_setname_command(PORT, BAUDRATE, NEW_NAME)
