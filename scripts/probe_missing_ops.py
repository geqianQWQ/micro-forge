#!/usr/bin/env python3
"""Run hal_blink probe and generate a deduplicated missing-instruction report."""

import re
import subprocess
import sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ELF = ROOT / "build/examples/hal_blink/firmware/hal_blink.elf"
RUNNER = ROOT / "build/examples/hal_blink/hal_blink_runner"
REPORT = ROOT / "build/probe_report.txt"

FAULT_RE = re.compile(
    r"\[FAULT\] PC=(0x[0-9A-Fa-f]+)\s+hw1=(0x[0-9A-Fa-f]+)(?:\s+hw2=(0x[0-9A-Fa-f]+))?"
)


def run_probe():
    result = subprocess.run(
        [str(RUNNER), str(ELF)],
        capture_output=True,
        text=True,
        timeout=30,
    )
    return result.stderr


def parse_faults(stderr: str):
    faults = []
    for line in stderr.splitlines():
        m = FAULT_RE.match(line)
        if m:
            pc = m.group(1)
            hw1 = m.group(2)
            hw2 = m.group(3) or ""
            faults.append((pc, hw1, hw2))
    return faults


def addr2line(elf: str, addrs: list[str]) -> dict[str, str]:
    if not addrs:
        return {}
    result = subprocess.run(
        ["arm-none-eabi-addr2line", "-e", elf] + addrs,
        capture_output=True,
        text=True,
    )
    lines = result.stdout.strip().splitlines()
    return dict(zip(addrs, lines))


def main():
    if not RUNNER.exists():
        print(f"Runner not found: {RUNNER}", file=sys.stderr)
        sys.exit(1)

    print("Running probe...")
    stderr = run_probe()

    faults = parse_faults(stderr)
    if not faults:
        print("No faults detected!")
        return

    # Unique opcode patterns with count
    pattern_counts: Counter = Counter()
    pattern_pcs: dict[tuple, list[str]] = {}
    for pc, hw1, hw2 in faults:
        key = (hw1, hw2) if hw2 else (hw1,)
        pattern_counts[key] += 1
        if key not in pattern_pcs:
            pattern_pcs[key] = [pc]
        elif len(pattern_pcs[key]) < 3:
            pattern_pcs[key].append(pc)

    # Unique PCs for addr2line
    unique_pcs = sorted({pc for pc, _, _ in faults})
    pc_map = addr2line(str(ELF), unique_pcs)

    # Generate report
    lines = []
    lines.append(f"Missing Instruction Report")
    lines.append(f"{'=' * 60}")
    lines.append(f"Total faults: {len(faults)}")
    lines.append(f"Unique opcode patterns: {len(pattern_counts)}")
    lines.append("")

    # Sort by frequency
    for (pattern, count) in pattern_counts.most_common():
        hw1 = pattern[0]
        hw2 = pattern[1] if len(pattern) > 1 else ""
        is32 = hw2 != ""
        hw1_int = int(hw1, 16)
        hw2_int = int(hw2, 16) if hw2 else 0

        pcs = pattern_pcs[pattern]
        srcs = [pc_map.get(p, "?") for p in pcs]

        lines.append(f"Count: {count:>5}  {'32-bit' if is32 else '16-bit'}")
        lines.append(f"  hw1={hw1}  hw2={hw2}" if is32 else f"  hw1={hw1}")
        lines.append(f"  Example PCs: {', '.join(pcs)}")
        lines.append(f"  Source: {', '.join(srcs)}")
        lines.append("")

    report = "\n".join(lines)
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(report)
    print(report)
    print(f"\nReport saved to {REPORT}")


if __name__ == "__main__":
    main()
