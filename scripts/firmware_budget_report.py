from __future__ import annotations

import argparse
import json
import os
import re
import sys
from pathlib import Path
from typing import Any


APP_PARTITION_SIZE = 6_553_600
RAM_RE = re.compile(r"RAM:.*?(\d+)\s+bytes\s+from\s+(\d+)\s+bytes")
FLASH_RE = re.compile(r"Flash:.*?(\d+)\s+bytes\s+from\s+(\d+)\s+bytes")


def fail(message: str) -> None:
    raise RuntimeError(message)


def parse_size(regex: re.Pattern[str], output: str, label: str) -> tuple[int, int]:
    match = regex.search(output)
    if not match:
        fail(f"Could not parse {label} usage from build log")
    return int(match.group(1)), int(match.group(2))


def pct(used: int, total: int) -> float:
    return used / total * 100 if total else 0.0


def format_bytes(value: int) -> str:
    sign = "-" if value < 0 else ""
    value = abs(value)
    if value >= 1024 * 1024:
        return f"{sign}{value / 1024 / 1024:.2f} MB"
    if value >= 1024:
        return f"{sign}{value / 1024:.1f} KB"
    return f"{sign}{value} B"


def read_metadata(path: Path) -> dict[str, Any]:
    if not path.exists():
        return {}
    return json.loads(path.read_text(encoding="utf-8"))


def build_report(tag: str, build_log: Path, metadata_path: Path, flash_budget_percent: float) -> dict[str, Any]:
    output = build_log.read_text(encoding="utf-8", errors="replace")
    flash_used, flash_total = parse_size(FLASH_RE, output, "flash")
    ram_used, ram_total = parse_size(RAM_RE, output, "RAM")
    metadata = read_metadata(metadata_path)
    firmware_bytes = metadata.get("firmwareBytes")
    artifact_name = metadata.get("artifactName") or f"{tag}.bin"
    budget_bytes = int(APP_PARTITION_SIZE * flash_budget_percent / 100)

    return {
        "tag": tag,
        "artifactName": artifact_name,
        "firmwareBytes": firmware_bytes,
        "flash": {
            "used": flash_used,
            "total": flash_total,
            "percent": round(pct(flash_used, flash_total), 2),
            "remaining": flash_total - flash_used,
            "budgetPercent": flash_budget_percent,
            "budgetBytes": budget_bytes,
            "budgetRemaining": budget_bytes - flash_used,
        },
        "ram": {
            "used": ram_used,
            "total": ram_total,
            "percent": round(pct(ram_used, ram_total), 2),
            "remaining": ram_total - ram_used,
        },
    }


def render_markdown(report: dict[str, Any]) -> str:
    flash = report["flash"]
    ram = report["ram"]
    firmware_bytes = report.get("firmwareBytes")
    firmware_display = format_bytes(firmware_bytes) if isinstance(firmware_bytes, int) else "n/a"
    budget_status = "OK" if flash["budgetRemaining"] >= 0 else "OVER"

    return "\n".join(
        [
            f"## Firmware budget: {report['tag']}",
            "",
            f"- Artifact: `{report['artifactName']}`",
            f"- Packaged firmware: {firmware_display}",
            f"- Flash budget: {flash['budgetPercent']:.1f}% ({budget_status})",
            "",
            "| Area | Used | Total | Usage | Remaining |",
            "|---|---:|---:|---:|---:|",
            (
                f"| Flash | {format_bytes(flash['used'])} | {format_bytes(flash['total'])} | "
                f"{flash['percent']:.2f}% | {format_bytes(flash['remaining'])} |"
            ),
            (
                f"| RAM | {format_bytes(ram['used'])} | {format_bytes(ram['total'])} | "
                f"{ram['percent']:.2f}% | {format_bytes(ram['remaining'])} |"
            ),
            "",
            f"Budget headroom: {format_bytes(flash['budgetRemaining'])}",
            "",
        ]
    )


def write_report(report: dict[str, Any], json_path: Path, md_path: Path) -> None:
    json_path.parent.mkdir(parents=True, exist_ok=True)
    json_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8", newline="\n")
    markdown = render_markdown(report)
    md_path.write_text(markdown, encoding="utf-8", newline="\n")

    summary_path = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary_path:
        with open(summary_path, "a", encoding="utf-8", newline="\n") as summary:
            summary.write(markdown)
            summary.write("\n")


def main() -> int:
    parser = argparse.ArgumentParser(description="Create a visible flash/RAM budget report from a PlatformIO build log.")
    parser.add_argument("--tag", required=True)
    parser.add_argument("--build-log", type=Path, required=True)
    parser.add_argument("--metadata", type=Path)
    parser.add_argument("--out-json", type=Path)
    parser.add_argument("--out-md", type=Path)
    parser.add_argument("--flash-budget-percent", type=float, default=97.5)
    parser.add_argument("--fail-over-budget", action="store_true")
    args = parser.parse_args()

    try:
        metadata = args.metadata or Path("artifacts") / f"{args.tag}.json"
        out_json = args.out_json or Path("artifacts") / f"{args.tag}-firmware-budget.json"
        out_md = args.out_md or Path("artifacts") / f"{args.tag}-firmware-budget.md"
        report = build_report(args.tag, args.build_log, metadata, args.flash_budget_percent)
        write_report(report, out_json, out_md)
        print(render_markdown(report))
        if args.fail_over_budget and report["flash"]["budgetRemaining"] < 0:
            fail("Flash usage exceeds the configured firmware budget")
        return 0
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
