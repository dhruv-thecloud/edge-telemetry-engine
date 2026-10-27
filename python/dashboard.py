#!/usr/bin/env python3
"""Live terminal telemetry, optional JSONL logs, and interactive calibration."""
from __future__ import annotations
import argparse
from collections import defaultdict, deque
from contextlib import ExitStack
import json
import math
from pathlib import Path
import queue
import sys
import threading
import time
from simulator_client import EngineClient


def commands(destination: queue.Queue[str]) -> None:
    for line in sys.stdin:
        # Bounded queue prevents pasted input from growing memory indefinitely.
        destination.put(line.strip())


def sparkline(values: deque[float]) -> str:
    low, high = min(values), max(values)
    levels = " .:-=+*#%@"
    if high == low:
        return "=" * len(values)
    return "".join(levels[min(9, max(0, int((v-low)/(high-low)*9)))] for v in values)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", type=Path, default=Path("build/edge_telemetry"))
    parser.add_argument("--interval", type=float, default=0.5, help="seconds between polls")
    parser.add_argument("--count", type=int, default=0, help="frame limit; 0 runs until quit")
    parser.add_argument("--log", type=Path, help="append telemetry as JSON lines")
    parser.add_argument("--calibrate", nargs=2, action="append", default=[], metavar=("ID", "OFFSET"))
    parser.add_argument("--no-interactive", action="store_true")
    args = parser.parse_args()
    if not math.isfinite(args.interval) or args.interval <= 0 or args.count < 0:
        parser.error("interval must be finite and positive; count must be nonnegative")
    pending: queue.Queue[str] = queue.Queue(maxsize=32)
    history: dict[int, deque[float]] = defaultdict(lambda: deque(maxlen=40))
    interactive = sys.stdin.isatty() and not args.no_interactive
    try:
        with ExitStack() as stack:
            engine = stack.enter_context(EngineClient(args.engine))
            log = stack.enter_context(args.log.open("a", encoding="utf-8")) if args.log else None
            for sensor_id, offset in args.calibrate:
                engine.calibrate(int(sensor_id), float(offset))
            if interactive:
                print("Commands: calibrate ID OFFSET | quit", file=sys.stderr)
                threading.Thread(target=commands, args=(pending,), daemon=True).start()
            frame_count = 0
            deadline = time.monotonic()
            while not args.count or frame_count < args.count:
                # Wake for commands between polls; slow polls skip missed deadlines.
                wait = max(0.0, deadline - time.monotonic())
                try:
                    command = pending.get(timeout=wait)
                except queue.Empty:
                    command = None
                if command is not None:
                    parts = command.split()
                    if parts == ["quit"]:
                        break
                    try:
                        if len(parts) != 3 or parts[0] != "calibrate":
                            raise ValueError("usage: calibrate ID OFFSET | quit")
                        print(engine.calibrate(int(parts[1]), float(parts[2])), file=sys.stderr)
                        history[int(parts[1])].clear()
                    except ValueError as error:
                        print(f"command error: {error}", file=sys.stderr)
                    continue
                frame = engine.poll()
                if log:
                    log.write(json.dumps(frame, allow_nan=False) + "\n")
                    log.flush()
                for sample in frame["samples"]:
                    values = history[sample["id"]]
                    values.append(sample["value"])
                    flag = "ok" if sample["accepted"] else "ANOMALY"
                    print(f'{frame["sequence"]:6d} sensor={sample["id"]} '
                          f'{sample["value"]:9.3f} {sample["unit"]:<4} '
                          f'avg={sample["average"]:9.3f} {flag:<7} {sparkline(values)}', flush=True)
                frame_count += 1
                deadline = max(deadline + args.interval, time.monotonic())
    except KeyboardInterrupt:
        return 0
    except (OSError, ValueError, RuntimeError, TimeoutError) as error:
        print(f"dashboard: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
