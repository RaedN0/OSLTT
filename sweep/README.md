# OSLTT Sweep Runner

Automated latency benchmarking across system configurations (scx schedulers, CPU governors, etc.) using the OSLTT hardware device.

## How It Works

```
For each run config:
  1. Swap scx scheduler (hot-swappable, no reboot)
  2. Apply CPU governor / pre_run commands
  3. Launch game (Steam, native, etc.)
  4. Run setup steps (ydotool clicks, mouse moves)
  5. Trigger OSLTT measurement (serial → device moves mouse + samples photodiode)
  6. Collect results (latency per shot in microseconds)
  7. Close game
  8. Save results (JSON + CSV)
```

## Requirements

```bash
sudo pacman -S python-pyserial python-yaml
```

Also needs:
- `scx` package installed and `scx` service available
- `ydotool` + `ydotoold` daemon running (for game setup automation)
- OSLTT device connected via USB (`/dev/ttyACM0`)

## Usage

```bash
cd sweep/

# Dry run — see what would happen without executing
python3 sweep_runner.py sweep_config.yaml --dry-run

# Full sweep
python3 sweep_runner.py sweep_config.yaml

# Skip game launch (game already running, just measure)
python3 sweep_runner.py sweep_config.yaml --skip-game

# Resume an interrupted sweep
python3 sweep_runner.py sweep_config.yaml --resume

# Use a different serial device
python3 sweep_runner.py sweep_config.yaml --device /dev/ttyACM1
```

## Configuration

### sweep_config.yaml

Defines the measurement settings and which system configs to test:

```yaml
game: kovaaks          # which game profile to use
device: /dev/ttyACM0   # OSLTT serial device
repeats: 3             # repeats per config (for averaging)

osltt:
  shots: 100
  delay_ms: 10
  move_px: 100
  window_ms: 20
  threshold: 0          # 0 = auto

runs:
  - name: scx_lavd
    scheduler: lavd
    governor: performance
  - name: cfs_default
    scheduler: none      # scx stop → back to kernel default
    governor: powersave
```

### Game Profiles (`profiles/<name>.yaml`)

Each game has its own profile defining how to launch, navigate, and close it:

```yaml
name: "KovaaK's"
launch: "steam steam://run/824270"
wait: 15                    # seconds for game to start

setup:                      # steps to get into testable state
  - cmd: "ydotool click 0xC0"
    delay_after: 5
  - cmd: "ydotool mousemove -- 10 0"
    delay_after: 1

cleanup: "steam -shutdown"
```

Create new profiles for other games by copying `profiles/example.yaml`.

## Output

Results are saved to `results/`:

- `sweep_<timestamp>.json` — Full data (all latencies, stats, config per run)
- `sweep_<timestamp>.csv` — Summary table (avg, median, min, max, p90, p95, p99 per run)

Results are saved incrementally after each run, so an interrupted sweep can be resumed with `--resume`.

## Adding a New Game

1. Copy `profiles/example.yaml` → `profiles/mygame.yaml`
2. Set the `launch` command and `wait` time
3. Add `setup` steps (ydotool clicks/keys to navigate menus)
4. Set the `cleanup` command
5. Update `sweep_config.yaml` → `game: mygame`

## Adding Extra System Tweaks

Use `pre_run` / `post_run` in the sweep config:

```yaml
runs:
  - name: scx_lavd_smt_off
    scheduler: lavd
    governor: performance
    pre_run:
      - "echo 0 | sudo tee /sys/devices/system/cpu/cpu1/online"
    post_run:
      - "echo 1 | sudo tee /sys/devices/system/cpu/cpu1/online"
```
