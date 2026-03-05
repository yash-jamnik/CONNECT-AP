# This script runs send_serial1.py, waits 1 second, then runs twoatone.py logic (if any)
import subprocess
import time

# Run send_serial1.py
subprocess.run(["python", "send_serial1.py"], check=True)

# Wait for 1 second
time.sleep(1)

subprocess.run(["python", "src/send_serial.py"], check=True)

# (Optional) Add your own logic for twoatone.py below
print("twoatone.py logic running...")
