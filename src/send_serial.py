import serial
import time
import bitmaps
import struct

def send_data(port, baudrate, data):
    """
    Sends data to the UART device in chunks.

    Args:
        port (str): The COM port to use (e.g., 'COM3').
        baudrate (int): The baud rate for the serial connection.
        data (bytes): The data to send.
    """
    try:
        with serial.Serial(port, baudrate, timeout=1) as ser:
            print(f"Connected to {port} at {baudrate} baud.")

            # Send the upload command
            ser.write(b'u')  # Command to initiate upload
            time.sleep(0.1)  # Allow the receiver to process the command

            # Send the size of the data (4 bytes, little-endian)
            data_size = len(data)
            ser.write(data_size.to_bytes(4, 'little'))
            print(f"Sent data size: {data_size} bytes")
            print(f"First 4 bytes (size): {data_size.to_bytes(4, 'little').hex()}")

            # Send the data in chunks
            chunk_size = 128
            for i in range(0, len(data), chunk_size):
                chunk = data[i:i + chunk_size]
                ser.write(chunk)
                print(f"Sent chunk: {chunk.hex()}")
                time.sleep(0.001)  # Small delay between chunks

            print("Data sent successfully.")

    except serial.SerialException as e:
        print(f"Serial error: {e}")
    except Exception as e:
        print(f"Error: {e}")

# Define the structure
class DataPacket:
    def __init__(self, data):
        self.size = len(data)
        self.data = data

    def to_byte_array(self):
        # Pack size as 4 bytes (little-endian) followed by the data
        return struct.pack('<I', self.size) + self.data

if __name__ == "__main__":
    port = 'COM11'  # Replace with your COM port
    baudrate = 115200

    # Convert the `apple` array from `bitmaps.py` to bytes
    data = bytes(bitmaps.EPD26INCH)

    # Create the data packet
    packet = DataPacket(data)

    # Convert to byte array
    byte_array = packet.to_byte_array()

    # Send the byte array over UART
    with serial.Serial(port, baudrate, timeout=1) as ser:
        ser.write(byte_array)
        print(f"Sent: {byte_array}")