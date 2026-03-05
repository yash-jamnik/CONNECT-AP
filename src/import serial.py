import serial
import time

def send_array_to_console(port, baudrate, data_array, chunk_size=20):
    """
    Sends an array to the console in chunks.

    Args:
        port (str): The COM port to use (e.g., 'COM3').
        baudrate (int): The baud rate for the serial connection.
        data_array (bytes): The data to send as a byte array.
        chunk_size (int): The size of each chunk to send.
    """
    try:
        # Open the serial port
        with serial.Serial(port, baudrate, timeout=1) as ser:
            print(f"Connected to {port} at {baudrate} baud.")

            # Send the [+]upload command to initiate the upload
            ser.write(b'[+]upload\n')
            time.sleep(0.1)  # Wait for the device to process the command

            # Send the data in chunks
            for i in range(0, len(data_array), chunk_size):
                chunk = data_array[i:i + chunk_size]
                hex_chunk = chunk.hex()  # Convert to hex string
                ser.write(hex_chunk.encode() + b'\n')  # Send as a line
                time.sleep(0.05)  # Small delay between chunks

            # Send the end-of-upload marker
            ser.write(b'[+]end\n')
            print("Data sent successfully.")

    except serial.SerialException as e:
        print(f"Serial error: {e}")
    except Exception as e:
        print(f"Error: {e}")

if __name__ == "__main__":
    # Example usage
    # Replace 'COM3' with your actual COM port and adjust the baud rate as needed
    port = 'COM3'
    baudrate = 115200

    # Example data array (replace with your actual data)
    data_array = b'\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0A\x0B\x0C\x0D\x0E\x0F\x10'

    send_array_to_console(port, baudrate, data_array)