#!/usr/bin/env python3
"""OSLTT serial client — talks to the OSLTT hardware device."""

import serial
import time


class OSLTT:
    """Client for the OSLTT USB device (XIAO M0 / Feather M0).

    Serial protocol (baud 2000000):
      Send: C,<shots>,<delayMs>,<movePx>,<threshold>,<windowMs>\\n   → configure
      Send: T\\n                                                      → run full test
      Send: S\\n                                                      → single shot
      Send: X\\n                                                      → abort
      Recv: R,<latencyUs>,<index>,<numSamples>,<baseline>,<threshold>\\n
            <sample1>,<sample2>,...\\n
            DONE\\n
    """

    def __init__(self, port, baud=2000000):
        self.ser = serial.Serial(port, baud, timeout=2)
        # Drain boot banner
        time.sleep(0.5)
        while self.ser.in_waiting:
            self.ser.readline()

    def configure(self, shots=100, delay_ms=10, move_px=100,
                 window_ms=20, threshold=0):
        cmd = f"C,{shots},{delay_ms},{move_px},{threshold},{window_ms}\n"
        self.ser.write(cmd.encode())
        ok = self._wait_for("OK", timeout=3)
        if not ok:
            raise RuntimeError("OSLTT did not acknowledge config")
        return True

    def run_test(self, on_result=None, timeout=120):
        """Run full test. Calls on_result(latency_us) per shot.

        Returns list of latencies in microseconds (None for timeouts).
        """
        self.ser.write(b"T\n")
        results = []
        deadline = time.time() + timeout

        while time.time() < deadline:
            raw = self.ser.readline()
            line = raw.decode(errors="replace").strip()
            if not line:
                continue

            if line == "DONE":
                break
            if line == "START":
                continue
            if line == "TIMEOUT":
                results.append(None)
                if on_result:
                    on_result(None)
                continue
            if line.startswith("R,"):
                parts = line.split(",")
                latency_us = int(parts[1])
                results.append(latency_us)
                if on_result:
                    on_result(latency_us)
                # Read the sample data line that follows
                self.ser.readline()
                continue
            # Ignore other lines (FW:, BOARD:, ERR:, etc.)

        return results

    def abort(self):
        self.ser.write(b"X\n")

    def close(self):
        self.ser.close()

    def _wait_for(self, expected, timeout=3):
        deadline = time.time() + timeout
        while time.time() < deadline:
            raw = self.ser.readline()
            line = raw.decode(errors="replace").strip()
            if line == expected:
                return True
        return False
