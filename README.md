# OSLTT - Open-Source Latency Test Tool

A hardware-assisted tool for measuring input and display latency with microsecond precision. OSLTT combines a microcontroller (acting as a USB HID mouse) and a photodiode to capture screen changes directly from the display, bypassing OS-level capture latencies.

**Original Project:** https://github.com/OSRTT/OSLTT

## Repository Structure
- **`Hardware/`** – Arduino firmware and flash script. Handles HID mouse emulation, photodiode sampling, and microsecond timestamping.
- **`webapp/`** – Local web server (Flask + pyserial) and frontend for controlling the device.
- **`vulkan-lagtest/`** – Minimal Vulkan input lag test (fullscreen black/white toggle based on mouse movement).
- **`sweep/`** – Automated benchmarking suite for testing latency under different `scx` schedulers and CPU configurations.

## Quick Start

### 1. Flash the Firmware
Use the included flash script:
```bash
cd Hardware/OSLTT
./flash_firmware.sh xiao /dev/ttyACM0
```
Supported boards:
- `xiao` — Seeed XIAO M0
- `feather` — Adafruit Feather M0

If the port is omitted, the script auto-detects the device.

### 2. Run the Web App
```bash
cd webapp
pip install pyserial flask
python3 server.py
```
Access the app at `https://<your-ip>:8443/`

### 3. Run the Sweep Benchmark
```bash
cd sweep
pip install pyserial pyyaml
python3 sweep_runner.py sweep_config.yaml
```

## License
This project is based on the original [OSRTT/OSLTT](https://github.com/OSRTT/OSLTT).
