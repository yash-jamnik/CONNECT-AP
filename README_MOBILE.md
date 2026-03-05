# NRF52840 Receiver and Mobile Device Communication

This project contains two applications:

## 1. Receiver Application (main.c)
- Connects to PAwR Access Point and listens for `[+]join,1234` command
- When command is received, stops PAwR and enters mobile connection mode
- Advertises as "NRF52_MOBILE" for 10 seconds
- Receives data from mobile devices via GATT characteristic

## 2. Mobile Device Application (mobile_device.c)
- Scans for receiver advertising as "NRF52_MOBILE"
- Connects to receiver when found
- Sends "hello" message via GATT write
- Stays connected for 30 seconds then disconnects

## Building and Running

### For Receiver (Normal build):
```bash
west build -b nrf52840dk_nrf52840
west flash
```

### For Mobile Device:
```bash
# Copy the mobile device files to separate directory
cp CMakeLists_mobile.txt CMakeLists.txt
cp prj_mobile.conf prj.conf
# Replace main.c with mobile_device.c content or rename files
west build -b nrf52840dk_nrf52840
west flash
```

## Usage Sequence:

1. **Flash receiver** on one nRF52840 board
2. **Flash mobile device** on another nRF52840 board
3. **Start receiver** - it will try to connect to PAwR AP
4. **Send `[+]join,1234` command** to receiver via PAwR
5. **Receiver stops PAwR** and enters mobile mode (advertises "NRF52_MOBILE")
6. **Mobile device automatically connects** and sends "hello" message
7. **Both devices stay connected** for data exchange
8. **After timeout, mobile disconnects** and receiver returns to PAwR mode

## Key Features:

### Receiver:
- Dual mode operation (PAwR sync + Mobile connection)
- Automatic mode switching on command reception
- 10-second timeout for mobile connections
- GATT service for receiving mobile data

### Mobile Device:
- Automatic scanning and connection
- Service discovery and characteristic finding
- Automatic "hello" message transmission
- Reconnection capability with retry logic

## GATT Service Details:
- Service UUID: `12345678-1234-5678-1234-56789abcdef0`
- Characteristic UUID: `12345678-1234-5678-1234-56789abcdef1`
- Both devices use the same UUIDs for communication