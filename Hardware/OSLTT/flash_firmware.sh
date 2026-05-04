#!/usr/bin/env bash
set -euo pipefail

# OSLTT v3.0 Firmware Flash Script
# Works with Seeed XIAO M0 and Adafruit Feather M0

BOARD_TYPE="${1:-}"
USER_PORT="${2:-}"

# ANSI colors
R='\033[0;31m'
G='\033[0;32m'
Y='\033[1;33m'
C='\033[0;36m'
N='\033[0m'

info()  { echo -e "${G}[INFO]${N} $*"; }
warn()  { echo -e "${Y}[WARN]${N} $*"; }
err()   { echo -e "${R}[ERROR]${N} $*"; }
step()  { echo -e "${C}[STEP]${N} $*"; }

usage() {
  cat <<EOF
Usage: $0 <board> [port]

Boards:
  xiao      Seeed XIAO M0
  feather   Adafruit Feather M0

Port (optional):
  Serial port path, e.g. /dev/ttyACM0, /dev/ttyUSB0, COM3

Examples:
  $0 xiao
  $0 xiao /dev/ttyACM0
  $0 feather /dev/ttyUSB0
EOF
}

# Determine FQBN and board manager URL from board type
case "${BOARD_TYPE:-}" in
  xiao)
    FQBN="Seeeduino:samd:seeed_XIAO_m0:usbstack=arduino"
    BOARD_URL="https://files.seeedstudio.com/arduino/package_seeeduino_boards_index.json"
    BOARD_NAME="Seeed XIAO M0"
    ;;
  feather)
    FQBN="adafruit:samd:adafruit_feather_m0"
    BOARD_URL="https://adafruit.github.io/arduino-board-package/package_adafruit_index.json"
    BOARD_NAME="Adafruit Feather M0"
    ;;
  *)
    usage
    exit 1
    ;;
esac

info "Board: $BOARD_NAME"
info "FQBN:  $FQBN"

# Check for arduino-cli
if ! command -v arduino-cli &>/dev/null; then
  err "arduino-cli not found."
  echo "Install it from: https://arduino.github.io/arduino-cli/latest/installation/"
  exit 1
fi
info "arduino-cli $(arduino-cli version | awk '{print $2}')"

# Helper: ensure board manager URL is in config
ensure_index() {
  if ! arduino-cli config dump 2>/dev/null | grep -qF "$BOARD_URL"; then
    step "Adding board manager URL..."
    arduino-cli config add board_manager.additional_urls "$BOARD_URL" 2>/dev/null || true
  fi
}

# Helper: wait until a port reappears
wait_for_port() {
  local target="$1"
  local attempts=30
  for ((i=1; i<=attempts; i++)); do
    if [[ "$target" == /dev/* ]]; then
      [ -e "$target" ] && return 0
    else
      # Windows COM port or abstract path — just check if arduino-cli sees it
      arduino-cli board list 2>/dev/null | grep -qF "$target" && return 0
    fi
    sleep 0.2
  done
  return 1
}

# Helper: detect port dynamically
detect_port() {
  # Use board list first, then glob fallback
  local ports
  ports=$(arduino-cli board list 2>/dev/null | awk '/ttyACM|ttyUSB|usbmodem|COM/ {print $1}')
  if [ -z "$ports" ]; then
    # Linux fallback
    ports=$(ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null || true)
  fi
  echo "$ports" | head -n1
}

# 1. Index setup
ensure_index
step "Updating core index..."
arduino-cli core update-index --additional-urls "$BOARD_URL"

# 2. Install cores if missing
ARDUINO_FLAGS="--additional-urls $BOARD_URL"

has_core() {
  arduino-cli core list $ARDUINO_FLAGS 2>/dev/null | grep -q "$1"
}

if [ "$BOARD_TYPE" = "xiao" ] && ! has_core "Seeeduino:samd"; then
  step "Installing Seeeduino SAMD core..."
  arduino-cli core install Seeeduino:samd $ARDUINO_FLAGS
fi

if [ "$BOARD_TYPE" = "feather" ] && ! has_core "adafruit:samd"; then
  step "Installing Adafruit SAMD core..."
  arduino-cli core install adafruit:samd $ARDUINO_FLAGS
fi

if ! has_core "arduino:samd"; then
  step "Installing Arduino SAMD core..."
  arduino-cli core install arduino:samd
fi

# 3. Determine port
PORT="${USER_PORT:-}"
if [ -z "$PORT" ]; then
  step "Auto-detecting serial port..."
  PORT=$(detect_port)
  if [ -z "$PORT" ]; then
    err "No serial ports found. Connect the device and try again."
    echo "  Linux: check \`ls /dev/ttyACM*\`"
    echo "  macOS: check \`ls /dev/tty.usbmodem*\`"
    exit 1
  fi
  info "Found port: $PORT"
fi

info "Port:  $PORT"

# 4. Pre-upload port check
if [[ "$PORT" == /dev/* && ! -e "$PORT" ]]; then
  err "Port $PORT does not exist."
  exit 1
fi

# 5. Compile
step "Compiling firmware..."
arduino-cli compile --fqbn "$FQBN" $ARDUINO_FLAGS .

# 6. Upload
step "Flashing firmware to $PORT..."
arduino-cli upload -p "$PORT" --fqbn "$FQBN" $ARDUINO_FLAGS .

info "Flash complete. Board is resetting..."

# 7. Wait for port to come back after reset
if [[ "$PORT" == /dev/* ]]; then
  step "Waiting for port to reappear after reset (max 10s)..."
  if wait_for_port "$PORT"; then
    info "Port ready."
  else
    warn "Port didn't come back at $PORT. It may have moved."
    NEW_PORT=$(detect_port)
    if [ -n "$NEW_PORT" ] && [ "$NEW_PORT" != "$PORT" ]; then
      PORT="$NEW_PORT"
      info "Detected new port: $PORT"
    fi
  fi
fi

# 8. Verify firmware via serial greeting
step "Verifying firmware..."
sleep 0.8

verify_firmware() {
  local p="$1"
  python3 -c "
import serial, time, sys
try:
    s = serial.Serial('$p', 2000000, timeout=2)
    time.sleep(0.3)
    s.flushInput()
    deadline = time.monotonic() + 3
    while time.monotonic() < deadline:
        line = s.readline().decode('utf-8', errors='replace').strip()
        if line:
            print('  ', line)
            if 'FW:3.0' in line:
                print('\n  SUCCESS: Firmware v3.0 is running.')
                sys.exit(0)
    s.close()
except Exception as e:
    print(f'  Could not verify: {e}')
    sys.exit(1)
"
}

if python3 -c "import serial" 2>/dev/null; then
  if verify_firmware "$PORT"; then
    : # success
  else
    warn "Verification didn't see FW:3.0. The firmware may still be OK — the board may have taken longer to boot."
    warn "You can verify manually: python3 -c \"import serial; s=serial.Serial('$PORT',2000000); print(s.readline().decode())\""
  fi
else
  warn "python3/pyserial not available; skipping automatic verification."
  echo "  You can verify manually with: python3 -c \"import serial; s=serial.Serial('$PORT',2000000); print(s.readline().decode())\""
fi

echo ""
info "Done."
