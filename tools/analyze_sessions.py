"""Summarize BioSync session CSV data and optionally create plots."""

from __future__ import annotations

import argparse
import csv
from collections import Counter, defaultdict
import os
from pathlib import Path
import statistics


def number(row: dict[str, str], key: str) -> float | None:
    value = row.get(key, "").strip()
    if not value:
        return None
    try:
        return float(value)
    except ValueError:
        return None


def fmt(value: float | None, suffix: str = "") -> str:
    return "not available" if value is None else f"{value:.1f}{suffix}"


def mean(values: list[float]) -> float | None:
    return statistics.fmean(values) if values else None


def read_sessions(path: Path) -> dict[int, list[dict[str, str]]]:
    grouped: dict[int, list[dict[str, str]]] = defaultdict(list)
    with path.open(newline="", encoding="utf-8-sig") as handle:
        for row in csv.DictReader(handle):
            try:
                session_id = int(row.get("session_id", "0"))
            except ValueError:
                continue
            if session_id > 0:
                grouped[session_id].append(row)
    return dict(sorted(grouped.items()))


def read_waiting_samples(path: Path) -> list[dict[str, str]]:
    waiting = []
    with path.open(newline="", encoding="utf-8-sig") as handle:
        for row in csv.DictReader(handle):
            if row.get("session_id", "").strip() == "0":
                waiting.append(row)
    return waiting


def read_firmware_summaries(path: Path) -> dict[int, dict[str, str]]:
    if not path.exists():
        return {}
    summaries: dict[int, dict[str, str]] = {}
    with path.open(newline="", encoding="utf-8-sig") as handle:
        for row in csv.DictReader(handle):
            try:
                summaries[int(row.get("session_id", "0"))] = row
            except ValueError:
                continue
    return summaries


def read_events(path: Path) -> dict[int, Counter[str]]:
    grouped: dict[int, Counter[str]] = defaultdict(Counter)
    if not path.exists():
        return {}
    with path.open(newline="", encoding="utf-8-sig") as handle:
        for row in csv.DictReader(handle):
            try:
                session_id = int(row.get("session_id", "0"))
            except ValueError:
                continue
            if session_id > 0:
                grouped[session_id][row.get("event", "UNKNOWN")] += 1
    return dict(grouped)


def session_summary(
    session_id: int,
    rows: list[dict[str, str]],
    firmware: dict[str, str] | None = None,
    events: Counter[str] | None = None,
) -> dict[str, object]:
    rows.sort(key=lambda row: number(row, "uptime_ms") or 0)
    postures = [row.get("posture", "UNKNOWN") for row in rows]
    active_postures = [p for p in postures if p not in ("NO_PRESSURE", "UNKNOWN", "")]
    counts = Counter(active_postures)
    transitions = sum(a != b for a, b in zip(active_postures, active_postures[1:]))
    center_percent = 100 * counts.get("CENTER_DOMINANT", 0) / len(active_postures) if active_postures else None
    temps = [v for row in rows if (v := number(row, "temp_c")) is not None]
    humidity = [v for row in rows if (v := number(row, "humidity_pct")) is not None]
    uptime = [number(row, "uptime_ms") for row in rows]
    valid_uptime = [v for v in uptime if v is not None]
    elapsed_minutes = (max(valid_uptime) - min(valid_uptime)) / 60000 if len(valid_uptime) > 1 else 0.0
    user_marks = events.get("USER_MARK", 0) if events is not None else sum(row.get("event") == "USER_MARK" for row in rows)
    alert_episodes = events.get("ALERT_START", 0) if events is not None else sum(
        row.get("alert") == "ACTIVE" and (index == 0 or rows[index - 1].get("alert") != "ACTIVE")
        for index, row in enumerate(rows)
    )
    snoozes = events.get("SNOOZE", 0) if events is not None else sum(row.get("event") == "SNOOZE" for row in rows)
    dismissals = events.get("ALERT_DISMISSED", 0) if events is not None else sum(row.get("event") == "ALERT_DISMISSED" for row in rows)
    cumulative_motor_ms = max(
        (value for row in rows if (value := number(row, "vibration_on_ms")) is not None),
        default=0.0,
    )
    firmware_motor_ms = number(firmware or {}, "vibration_on_ms")
    firmware_duration_s = number(firmware or {}, "duration_s")
    return {
        "id": session_id,
        "rows": rows,
        "samples": len(rows),
        "elapsed_minutes": elapsed_minutes,
        "counts": counts,
        "posture_samples": len(active_postures),
        "transitions": transitions,
        "center_percent": center_percent,
        "temp_mean": mean(temps),
        "temp_min": min(temps) if temps else None,
        "temp_max": max(temps) if temps else None,
        "humidity_mean": mean(humidity),
        "humidity_min": min(humidity) if humidity else None,
        "humidity_max": max(humidity) if humidity else None,
        "user_marks": user_marks,
        "motor_on_seconds": firmware_motor_ms / 1000 if firmware_motor_ms is not None else cumulative_motor_ms / 1000,
        "motor_time_exact": firmware_motor_ms is not None,
        "firmware_duration_s": firmware_duration_s,
        "alert_episodes": alert_episodes,
        "snoozes": snoozes,
        "dismissals": dismissals,
    }


def make_plot(summary: dict[str, object], output_dir: Path) -> Path | None:
    try:
        import matplotlib.pyplot as plt
    except ImportError:
        return None

    rows = summary["rows"]
    assert isinstance(rows, list)
    times = [((number(row, "uptime_ms") or 0) - (number(rows[0], "uptime_ms") or 0)) / 60000 for row in rows]
    fig, (pressure_ax, climate_ax, vibration_ax) = plt.subplots(3, 1, figsize=(10, 9), sharex=True)
    for field, label in (("fsr_left", "Left"), ("fsr_middle", "Middle"), ("fsr_right", "Right")):
        values = [number(row, field) for row in rows]
        pressure_ax.plot(times, values, label=label, linewidth=1.4)
    pressure_ax.set_title(f"BioSync session {summary['id']}: sensor trends")
    pressure_ax.set_ylabel("FSR ADC reading (raw)")
    pressure_ax.legend(ncol=3, frameon=False)
    pressure_ax.grid(alpha=0.2)

    temperatures = [number(row, "temp_c") for row in rows]
    humidities = [number(row, "humidity_pct") for row in rows]
    climate_ax.plot(times, temperatures, color="tab:red", label="Temperature (°C)", linewidth=1.4)
    climate_ax.set_ylabel("Temperature (°C)")
    climate_ax.set_xlabel("Minutes since first captured sample")
    humidity_ax = climate_ax.twinx()
    humidity_ax.plot(times, humidities, color="tab:blue", label="Humidity (%)", linewidth=1.4)
    humidity_ax.set_ylabel("Relative humidity (%)")
    lines, labels = climate_ax.get_legend_handles_labels()
    lines2, labels2 = humidity_ax.get_legend_handles_labels()
    climate_ax.legend(lines + lines2, labels + labels2, frameon=False, ncol=2)
    climate_ax.grid(alpha=0.2)
    vibration = [number(row, "vibration_pwm") or 0 for row in rows]
    vibration_ax.step(times, vibration, where="post", color="tab:purple", linewidth=1.4)
    vibration_ax.set_ylabel("Motor PWM duty (0–255)")
    vibration_ax.set_xlabel("Minutes since first captured sample")
    vibration_ax.set_ylim(-5, 260)
    vibration_ax.grid(alpha=0.2)
    fig.tight_layout()
    output_dir.mkdir(parents=True, exist_ok=True)
    path = output_dir / f"session-{summary['id']}.png"
    fig.savefig(path, dpi=150)
    plt.close(fig)
    return path


def render_report(
    summaries: list[dict[str, object]], plots: list[Path], waiting_rows: list[dict[str, str]]
) -> str:
    lines = [
        "# BioSync session analysis",
        "",
        "This report describes the captured sensor data. It does not diagnose sleep quality or health conditions.",
        "",
    ]
    if not summaries:
        lines.append("No active pressure session was recorded.")
        if waiting_rows:
            fsr_values = {
                field: [v for row in waiting_rows if (v := number(row, field)) is not None]
                for field in ("fsr_left", "fsr_middle", "fsr_right")
            }
            temps = [v for row in waiting_rows if (v := number(row, "temp_c")) is not None]
            humidity = [v for row in waiting_rows if (v := number(row, "humidity_pct")) is not None]
            lines.extend([
                "",
                f"- Waiting-state samples: {len(waiting_rows)}",
                "- FSR ranges: " + "; ".join(
                    f"{field.removeprefix('fsr_')} {min(values):.0f}–{max(values):.0f}"
                    if values else f"{field.removeprefix('fsr_')} unavailable"
                    for field, values in fsr_values.items()
                ),
                f"- Mean waiting-state temperature: {fmt(mean(temps), ' °C')}",
                f"- Mean waiting-state relative humidity: {fmt(mean(humidity), ' %')}",
                "",
                "All-zero FSR ranges are below the default 500-count pressure threshold. Check each FSR voltage divider, its signal wire to GPIO 35/32/34, and the shared ground; press each sensor and confirm its raw reading changes before expecting a session.",
                "",
            ])
        else:
            lines.extend(["", "No telemetry rows were found. Record data while the ESP32 is connected, then run the analyzer again.", ""])
        return "\n".join(lines)

    for idx, item in enumerate(summaries):
        counts = item["counts"]
        assert isinstance(counts, Counter)
        active_total = int(item["posture_samples"])
        lines.extend([
            f"## Session {item['id']}",
            "",
            f"- Captured rows: {item['samples']}",
            f"- Approximate captured span: {item['elapsed_minutes']:.1f} minutes (from first to last saved sample)",
            f"- Firmware session duration: {fmt(item['firmware_duration_s'], ' seconds')}",
            f"- Posture category changes: {item['transitions']}",
            f"- Center-dominant samples: {fmt(item['center_percent'], '%')} of classified samples",
            f"- Touch markers: {item['user_marks']}",
            f"- Alert episodes observed: {item['alert_episodes']}",
            f"- Snoozed alerts: {item['snoozes']}; dismissed alerts: {item['dismissals']}",
            f"- Motor-on time: {fmt(item['motor_on_seconds'], ' seconds')}" + (" (firmware counter)" if item["motor_time_exact"] else " (last sampled cumulative counter)"),
            f"- Mean temperature: {fmt(item['temp_mean'], ' °C')} (range {fmt(item['temp_min'], '–')} to {fmt(item['temp_max'], ' °C')})",
            f"- Mean relative humidity: {fmt(item['humidity_mean'], ' %')} (range {fmt(item['humidity_min'], '–')} to {fmt(item['humidity_max'], ' %')})",
            "",
            "Posture category shares:",
            "",
        ])
        if active_total:
            for posture, count in sorted(counts.items()):
                lines.append(f"- {posture}: {100 * count / active_total:.1f}% ({count} samples)")
        else:
            lines.append("- No pressure-present posture samples were captured.")
        if idx < len(plots):
            lines.extend(["", f"![Session {item['id']} sensor trends]({plots[idx].as_posix()})"])
        lines.append("")

    lines.extend([
        "## How to interpret these summaries",
        "",
        "- FSR readings are raw ADC values; sensor placement, resistor values, and the pillow affect their scale. They are not calibrated force measurements.",
        "- LEFT_DOMINANT, CENTER_DOMINANT, RIGHT_DOMINANT, and DISTRIBUTED are simple categories based on the current thresholds and relative readings. Validate the categories by manually checking the sensor placement.",
        "- Temperature and humidity describe the sensor's local environment. They are not direct measures of sleep quality or medical risk.",
        "- The analysis samples every two seconds, so brief movements between samples can be missed. The captured span can be shorter than the firmware's session duration because capture starts/stops at sample intervals.",
        "",
    ])
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv", nargs="?", type=Path, default=Path("data/raw/telemetry.csv"))
    parser.add_argument("--report", type=Path, default=Path("reports/session_analysis.md"))
    parser.add_argument("--plots", type=Path, default=Path("reports/plots"))
    parser.add_argument("--sessions", type=Path, help="Firmware summary CSV (defaults to a matching .sessions.csv file)")
    parser.add_argument("--events", type=Path, help="Event CSV (defaults to a matching .events.csv file)")
    args = parser.parse_args()

    if not args.csv.exists():
        parser.error(f"input CSV not found: {args.csv}")

    sessions = read_sessions(args.csv)
    waiting_rows = read_waiting_samples(args.csv)
    summary_path = args.sessions or args.csv.with_name(args.csv.stem + ".sessions.csv")
    event_path = args.events or args.csv.with_name(args.csv.stem + ".events.csv")
    firmware_summaries = read_firmware_summaries(summary_path)
    event_summaries = read_events(event_path)
    summaries = [
        session_summary(key, rows, firmware_summaries.get(key), event_summaries.get(key))
        for key, rows in sessions.items()
    ]
    plots = []
    for summary in summaries:
        plot = make_plot(summary, args.plots)
        if plot:
            plots.append(Path(os.path.relpath(plot, args.report.parent)))

    args.report.parent.mkdir(parents=True, exist_ok=True)
    report = render_report(summaries, plots, waiting_rows)
    args.report.write_text(report, encoding="utf-8")
    print(report)
    print(f"\nReport saved to {args.report}.")
    if summaries and not plots:
        print("Install matplotlib to also generate session plots (python -m pip install matplotlib).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
