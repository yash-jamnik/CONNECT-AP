
import subprocess
import time

# Run send_serial1.py
subprocess.run(["python", "send_serial1.py"], check=True)

# Wait for 1 second
time.sleep(1)

# Run src/send_serial.py
subprocess.run(["python", "src/send_serial.py"], check=True)
