#!/usr/bin/env python3
"""
OSLTT Sweep Runner — automated latency testing across system configurations.

Runs the OSLTT measurement tool against a game, swapping scx schedulers
(or other system settings) between runs to compare their impact on input latency.

Usage:
    python3 sweep_runner.py sweep_config.yaml
    python3 sweep_runner.py sweep_config.yaml --dry-run
    python3 sweep_runner.py sweep_config.yaml --skip-game   # game already running
    python3 sweep_runner.py sweep_config.yaml --resume       # skip completed runs

Requires: pyserial, pyyaml
Install:  sudo pacman -S python-pyserial python-yaml
"""

import argparse
import csv
import json
import statistics
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

import yaml

from osltt import OSLTT


# ─── Helpers ──────────────────────────────────────────────────────────────────

def run_cmd(cmd, check=False):
    """Run a shell command, return CompletedProcess."""
    print(f"  $ {cmd}")
    result = subprocess.run(cmd, shell=True, capture_output=True, text=True)
    if check and result.returncode != 0:
        raise RuntimeError(
            f"Command failed (exit {result.returncode}): {cmd}\n"
            f"stderr: {result.stderr.strip()}"
        )
    return result


def compute_stats(latencies_ms):
    """Compute statistics for a list of latency values in ms."""
    if not latencies_ms:
        return {k: 0 for k in
                ("avg", "median", "min", "max", "range", "sd", "p90", "p95", "p99")}

    s = sorted(latencies_ms)
    n = len(s)

    def pct(p):
        k = (n - 1) * p / 100
        f = int(k)
        c = min(f + 1, n - 1)
        return s[f] + (s[c] - s[f]) * (k - f) if f != c else s[f]

    return {
        "avg":    statistics.mean(latencies_ms),
        "median": statistics.median(latencies_ms),
        "min":    min(latencies_ms),
        "max":    max(latencies_ms),
        "range":  max(latencies_ms) - min(latencies_ms),
        "sd":     statistics.stdev(latencies_ms) if n > 1 else 0,
        "p90":    pct(90),
        "p95":    pct(95),
        "p99":    pct(99),
    }


# ─── System Config ────────────────────────────────────────────────────────────

def apply_scheduler(scheduler, dry_run=False):
    """Swap scx scheduler. 'none' = scx stop (back to CFS/EEVDF)."""
    if dry_run:
        print(f"  [dry-run] scheduler: {scheduler}")
        return

    run_cmd("sudo scx stop", check=False)
    if scheduler and scheduler != "none":
        run_cmd(f"sudo scx {scheduler}", check=True)


def apply_governor(governor, dry_run=False):
    """Set CPU frequency governor."""
    if not governor:
        return
    if dry_run:
        print(f"  [dry-run] governor: {governor}")
        return
    run_cmd(f"sudo cpupower frequency-set -g {governor}", check=False)


def run_extra_commands(commands, label="", dry_run=False):
    """Run a list of arbitrary shell commands."""
    for cmd in commands or []:
        if dry_run:
            print(f"  [dry-run] {label}: {cmd}")
        else:
            run_cmd(cmd, check=False)


# ─── Game Management ──────────────────────────────────────────────────────────

def launch_game(profile, dry_run=False):
    """Launch the game and run setup steps."""
    launch_cmd = profile.get("launch", "")
    if not launch_cmd:
        return

    if dry_run:
        print(f"  [dry-run] launch: {launch_cmd}")
        return

    run_cmd(launch_cmd)
    wait = profile.get("wait", 10)
    print(f"  Waiting {wait}s for game to start...")
    time.sleep(wait)


def run_setup(profile, dry_run=False):
    """Run setup steps (ydotool clicks, mouse moves, etc.)."""
    for step in profile.get("setup", []):
        cmd = step.get("cmd", "")
        delay = step.get("delay_after", 0)

        if dry_run:
            print(f"  [dry-run] setup: {cmd} (delay_after={delay})")
            continue

        if cmd:
            run_cmd(cmd, check=False)
        if delay:
            print(f"  Waiting {delay}s...")
            time.sleep(delay)


def cleanup_game(profile, dry_run=False):
    """Close the game."""
    cleanup_cmd = profile.get("cleanup", "")
    if not cleanup_cmd:
        return

    if dry_run:
        print(f"  [dry-run] cleanup: {cleanup_cmd}")
        return

    run_cmd(cleanup_cmd, check=False)
    time.sleep(3)


# ─── Main ─────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description="OSLTT Sweep Runner — automated latency benchmarking"
    )
    parser.add_argument("config", help="Path to sweep config YAML")
    parser.add_argument("--device", help="OSLTT serial device (default: /dev/ttyACM0)")
    parser.add_argument("--dry-run", action="store_true",
                        help="Show what would be done without executing")
    parser.add_argument("--skip-game", action="store_true",
                        help="Skip game launch/cleanup (game already running)")
    parser.add_argument("--resume", action="store_true",
                        help="Skip runs already in the results file")
    parser.add_argument("--profile-dir", default="profiles",
                        help="Directory for game profiles (default: profiles)")
    args = parser.parse_args()

    # Load sweep config
    with open(args.config) as f:
        config = yaml.safe_load(f)

    device = args.device or config.get("device", "/dev/ttyACM0")
    game_name = config.get("game", "example")
    repeats = config.get("repeats", 1)
    dry_run = args.dry_run

    # Load game profile
    profile_path = Path(args.profile_dir) / f"{game_name}.yaml"
    if not profile_path.exists():
        print(f"Error: Game profile not found: {profile_path}")
        sys.exit(1)

    with open(profile_path) as f:
        profile = yaml.safe_load(f)

    print("=" * 60)
    print("OSLTT Sweep Runner")
    print("=" * 60)
    print(f"  Game:     {profile.get('name', game_name)}")
    print(f"  Device:   {device}")
    print(f"  Repeats:  {repeats}")
    print(f"  Runs:     {len(config['runs'])}")
    print(f"  Dry run:  {dry_run}")
    print(f"  Skip game: {args.skip_game}")
    print("=" * 60)

    # Resume: load previous results
    results_dir = Path("results")
    results_dir.mkdir(exist_ok=True)
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")

    # Find existing results to resume from
    completed = set()
    if args.resume:
        json_files = sorted(results_dir.glob("sweep_*.json"))
        if json_files:
            with open(json_files[-1]) as f:
                prev = json.load(f)
            for r in prev:
                completed.add((r["run_name"], r["repeat"]))
            # Use the same timestamp to continue appending
            timestamp = json_files[-1].stem.replace("sweep_", "")
            print(f"\nResuming from {json_files[-1].name} ({len(completed)} runs already done)")

    all_results = []
    # If resuming, load previous results
    if completed:
        json_files = sorted(results_dir.glob("sweep_*.json"))
        if json_files:
            with open(json_files[-1]) as f:
                all_results = json.load(f)

    # Connect to OSLTT (skip if dry run)
    osltt = None
    if not dry_run:
        print(f"\nConnecting to OSLTT on {device}...")
        try:
            osltt = OSLTT(device)
            print("Connected.")
        except Exception as e:
            print(f"Error connecting to OSLTT: {e}")
            sys.exit(1)

        # Configure OSLTT
        ocfg = config.get("osltt", {})
        osltt.configure(
            shots=ocfg.get("shots", 100),
            delay_ms=ocfg.get("delay_ms", 10),
            move_px=ocfg.get("move_px", 100),
            window_ms=ocfg.get("window_ms", 20),
            threshold=ocfg.get("threshold", 0),
        )
        print("OSLTT configured.")

    # ─── Sweep Loop ───────────────────────────────────────────────────────────

    for run in config["runs"]:
        run_name = run["name"]
        scheduler = run.get("scheduler", "none")
        governor = run.get("governor", "")
        pre_run = run.get("pre_run", [])
        post_run = run.get("post_run", [])

        for rep in range(1, repeats + 1):
            # Resume check
            if (run_name, rep) in completed:
                print(f"\n[SKIP] {run_name} (repeat {rep}/{repeats}) — already done")
                continue

            print(f"\n{'─' * 60}")
            print(f"Run: {run_name} (repeat {rep}/{repeats})")
            print(f"  Scheduler: {scheduler}")
            print(f"  Governor:  {governor or '(unchanged)'}")
            print(f"{'─' * 60}")

            # 1. Apply system configuration
            print("\n[1] Applying system configuration...")
            apply_scheduler(scheduler, dry_run)
            apply_governor(governor, dry_run)
            run_extra_commands(pre_run, "pre_run", dry_run)

            # 2. Launch game + setup
            if not args.skip_game:
                print("\n[2] Launching game...")
                launch_game(profile, dry_run)

                print("\n[2b] Running setup steps...")
                run_setup(profile, dry_run)
            else:
                print("\n[2] Skipping game launch (--skip-game)")

            # 3. Run measurement
            print("\n[3] Running OSLTT measurement...")
            if dry_run:
                print("  [dry-run] would send T to OSLTT and collect results")
                continue

            shot_num = 0
            def on_result(us):
                nonlocal shot_num
                shot_num += 1
                if us is not None:
                    print(f"  Shot {shot_num}: {us/1000:.3f} ms")
                else:
                    print(f"  Shot {shot_num}: TIMEOUT")

            raw_results = osltt.run_test(on_result=on_result) if osltt else []

            # 4. Cleanup
            if not args.skip_game:
                print("\n[4] Cleaning up...")
                cleanup_game(profile, dry_run)

            run_extra_commands(post_run, "post_run", dry_run)

            # 5. Store results
            latencies_ms = [r / 1000 for r in raw_results if r is not None]
            timeouts = raw_results.count(None)
            stats = compute_stats(latencies_ms)

            result_entry = {
                "run_name": run_name,
                "scheduler": scheduler,
                "governor": governor,
                "repeat": rep,
                "timestamp": datetime.now().isoformat(),
                "latencies_ms": latencies_ms,
                "timeouts": timeouts,
                "stats": stats,
            }
            all_results.append(result_entry)

            # Print summary
            print(f"\n{'─' * 40}")
            print(f"Results: {len(latencies_ms)} shots, {timeouts} timeouts")
            print(f"  Avg:    {stats['avg']:.3f} ms")
            print(f"  Median: {stats['median']:.3f} ms")
            print(f"  Min:    {stats['min']:.3f} ms")
            print(f"  Max:    {stats['max']:.3f} ms")
            print(f"  StdDev: {stats['sd']:.3f} ms")
            print(f"  p99:    {stats['p99']:.3f} ms")
            print(f"{'─' * 40}")

            # Save after each run (incremental — crash-safe)
            _save_results(all_results, results_dir, timestamp)
            print(f"Results saved to results/sweep_{timestamp}.json")

    # ─── Final Summary ────────────────────────────────────────────────────────

    if all_results:
        print(f"\n{'=' * 60}")
        print("SWEEP COMPLETE")
        print(f"{'=' * 60}")

        # Print comparison table
        print(f"\n{'Run':<25} {'Rep':>3} {'Shots':>5} {'Avg':>8} {'Median':>8} "
              f"{'Min':>8} {'Max':>8} {'SD':>8} {'p99':>8}")
        print("─" * 95)
        for r in all_results:
            s = r["stats"]
            print(f"{r['run_name']:<25} {r['repeat']:>3} {len(r['latencies_ms']):>5} "
                  f"{s['avg']:>8.3f} {s['median']:>8.3f} {s['min']:>8.3f} "
                  f"{s['max']:>8.3f} {s['sd']:>8.3f} {s['p99']:>8.3f}")

        _save_results(all_results, results_dir, timestamp)
        print(f"\nFull results: results/sweep_{timestamp}.json")
        print(f"CSV summary:  results/sweep_{timestamp}.csv")

    if osltt:
        osltt.close()


def _save_results(results, results_dir, timestamp):
    """Save results as JSON + CSV."""
    # JSON (full data)
    json_path = results_dir / f"sweep_{timestamp}.json"
    with open(json_path, "w") as f:
        json.dump(results, f, indent=2)

    # CSV (summary table)
    csv_path = results_dir / f"sweep_{timestamp}.csv"
    with open(csv_path, "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow([
            "run", "scheduler", "governor", "repeat", "shots", "timeouts",
            "avg_ms", "median_ms", "min_ms", "max_ms", "range_ms",
            "sd_ms", "p90_ms", "p95_ms", "p99_ms",
        ])
        for r in results:
            s = r["stats"]
            writer.writerow([
                r["run_name"], r["scheduler"], r["governor"], r["repeat"],
                len(r["latencies_ms"]), r["timeouts"],
                f"{s['avg']:.3f}", f"{s['median']:.3f}",
                f"{s['min']:.3f}", f"{s['max']:.3f}", f"{s['range']:.3f}",
                f"{s['sd']:.3f}", f"{s['p90']:.3f}", f"{s['p95']:.3f}",
                f"{s['p99']:.3f}",
            ])


if __name__ == "__main__":
    main()
