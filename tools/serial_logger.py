"""Capture BioSync DATA rows from an ESP32 serial port into a CSV file."""

from __future__ import annotations

import argparse
import csv
from datetime import datetime
from pathlib import Path
import sys
import time

import serial


FIELDS = [
    "received_at", "uptime_ms", "session_id", "fsr_left", "fsr_middle",
    "fsr_right", "temp_c", "humidity_pct", "posture", "state",
    "vibration_pwm", "vibration_on_ms", "alert", "event",
]
EVENT_FIELDS = ["received_at", "session_id", "event"]
SUMMARY_FIELDS = [
    "received_at", "session_id", "duration_s", "posture_changes",
    "center_dominant_pct", "mean_temp_c", "mean_humidity_pct", "alert_count",
    "vibration_on_ms",
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM4", help="Serial port (default: COM4)")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--output", type=Path, default=Path("data/raw/telemetry.csv"))
    parser.add_argument("--duration", type=float, help="Stop after this many seconds")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    needs_header = not args.output.exists() or args.output.stat().st_size == 0
    event_path = args.output.with_name(args.output.stem + ".events.csv")
    summary_path = args.output.with_name(args.output.stem + ".sessions.csv")
    event_needs_header = not event_path.exists() or event_path.stat().st_size == 0
    summary_needs_header = not summary_path.exists() or summary_path.stat().st_size == 0
    started = time.monotonic()
    rows = 0

    try:
        with serial.Serial(args.port, args.baud, timeout=1) as device, \
             args.output.open("a", newline="", encoding="utf-8") as csv_file, \
             event_path.open("a", newline="", encoding="utf-8") as event_file, \
             summary_path.open("a", newline="", encoding="utf-8") as summary_file:
            writer = csv.DictWriter(csv_file, fieldnames=FIELDS)
            event_writer = csv.DictWriter(event_file, fieldnames=EVENT_FIELDS)
            summary_writer = csv.DictWriter(summary_file, fieldnames=SUMMARY_FIELDS)
            if needs_header:
                writer.writeheader()
                csv_file.flush()
            if event_needs_header:
                event_writer.writeheader()
                event_file.flush()
            if summary_needs_header:
                summary_writer.writeheader()
                summary_file.flush()
            print(f"Listening on {args.port} at {args.baud} baud; writing {args.output}.")
            print("Press Ctrl+C to stop recording.")

            while args.duration is None or time.monotonic() - started < args.duration:
                raw = device.readline()
                if not raw:
                    continue
                line = raw.decode("utf-8", errors="replace").strip()
                if not line:
                    continue
                print(line)

                parts = line.split(",")
                received_at = datetime.now().astimezone().isoformat(timespec="seconds")
                if parts[0] == "EVENT" and len(parts) == 3:
                    event_writer.writerow({
                        "received_at": received_at, "session_id": parts[1], "event": parts[2],
                    })
                    event_file.flush()
                    continue
                if parts[0] == "SUMMARY" and len(parts) == 9:
                    summary_writer.writerow(dict(zip(SUMMARY_FIELDS, [received_at, *parts[1:]])))
                    summary_file.flush()
                    continue
                if parts[0] != "DATA":
                    continue
                if len(parts) != 14:
                    print(f"Skipped malformed DATA row ({len(parts) - 1} fields).", file=sys.stderr)
                    continue

                values = parts[1:]
                row = dict(zip(FIELDS[1:], values))
                row["received_at"] = received_at
                writer.writerow(row)
                csv_file.flush()
                rows += 1

    except KeyboardInterrupt:
        print("\nRecording stopped.")
    except serial.SerialException as exc:
        print(f"Serial error: {exc}", file=sys.stderr)
        return 1

    print(f"Saved {rows} telemetry rows to {args.output}.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
