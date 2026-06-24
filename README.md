# OSLTT - Open-Source Latency Test Tool

A hardware-assisted tool for measuring input and display latency with microsecond precision. OSLTT combines a microcontroller (acting as a USB HID mouse) and a photodiode to capture screen changes directly from the display, bypassing OS-level capture latencies.

**Original Project:** https://github.com/OSRTT/OSLTT

## Features
- **Hardware-level measurement:** Uses a photodiode to detect screen changes, providing true end-to-end latency measurements.
- **Real USB HID Mouse:** The microcontroller acts as a physical mouse, bypassing most anti-cheat software and Raw Input filters in games.
- **Automated Benchmarking:** Includes a Python sweep runner to automatically test latency across different system configurations (e.g., kernel schedulers, CPU governors).
- **Web Interface:** Simple local web UI for configuring shots, thresholds, and viewing results.
- **Vulkan Lag Test:** A minimal Vulkan-based fullscreen toggle application for standardized input/display lag testing.

## Hardware Requirements
- **Microcontroller:** Seeed Studio XIAO SAMD21 or Adafruit Feather M0
- **Photodiode:** Fast response photodiode (e.g., BPW34)
- **Transistor:** NPN (e.g., BC547)
- **OLED Display:** 0.96" I2C OLED (SSD1306)
- **Misc:** Breadboard, jumper wires, resistors

## Repository Structure
- **`Hardware/`** – Arduino firmware. Handles HID mouse emulation, photodiode sampling, and microsecond timestamping.
- **`webapp/`** – Local web server (Flask + pyserial) and frontend for controlling the device.
- **`vulkan-lagtest/`** – Minimal Vulkan input lag test (fullscreen black/white toggle based on mouse movement).
- **`sweep/`** – Automated benchmarking suite for testing latency under different `scx` schedulers and CPU configurations.

## Quick Start

### 1. Flash the Firmware
1. Install the [Arduino IDE](https://www.arduino.cc/en/software).
2. Install the SAMD board packages via the Board Manager.
3. Open `Hardware/OSLTT/OSLTT.ino`.
4. Select your board (e.g., `Seeed XIAO SAMD21`) and port.
5. Upload the firmware.

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
