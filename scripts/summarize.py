#!/usr/bin/env python3
"""Summarise one verify.sh result directory as a markdown table."""
import re
import sys
from pathlib import Path

TAGS = [("pre", "balanced"), ("on", "performance"), ("post", "restored")]


def read(path: Path) -> str:
    try:
        return path.read_text(errors="replace")
    except OSError:
        return ""


def status(tag: str, root: Path) -> dict:
    text = read(root / f"status-{tag}.txt")
    out = {}
    m = re.search(r"Power unlock\s*:\s*(ON|OFF)", text)
    if m:
        out["unlock"] = m.group(1)
    m = re.search(r"Governor:\s*(\S+)", text)
    if m:
        out["governor"] = m.group(1)
    m = re.search(r"EPP\s*:\s*(\S+)", text)
    if m:
        out["epp"] = m.group(1)
    for label in ("MSR", "MMIO"):
        m = re.search(
            rf"RAPL\s+{label}\s+PL1=\s*([\d.]+)\s*W\s+PL2=\s*([\d.]+)\s*W", text
        )
        if m:
            out[f"{label}_pl1"] = float(m.group(1))
            out[f"{label}_pl2"] = float(m.group(2))
    m = re.search(r"Fan 0\s*:\s*(\d+)\s*RPM", text)
    if m:
        out["fan"] = int(m.group(1))
    return out


def sample(tag: str, root: Path, kind: str) -> dict:
    text = read(root / f"{kind}-{tag}.txt").strip()
    parts = text.split()
    if len(parts) != 5:
        return {}
    try:
        return {
            f"{kind}_watts": float(parts[0]),
            f"{kind}_temp": float(parts[1]),
            f"{kind}_mhz": float(parts[2]),
            f"{kind}_bzy": float(parts[3]),
        }
    except ValueError:
        return {}


def sevenzip(tag: str, root: Path) -> float | None:
    text = read(root / f"7z-{tag}.txt")
    for line in text.splitlines():
        if line.startswith("Tot:"):
            fields = line.split()
            for value in reversed(fields[1:]):
                try:
                    return float(value)
                except ValueError:
                    continue
    return None


def burn(tag: str, root: Path) -> float | None:
    text = read(root / f"burn-{tag}.txt")
    m = re.search(r"rate=([\d.]+)\s+it/s", text)
    return float(m.group(1)) if m else None


def fmt(value, unit="", digits=2):
    if value is None:
        return "-"
    if isinstance(value, float):
        return f"{value:.{digits}f}{unit}"
    return f"{value}{unit}"


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: summarize.py <result-dir>", file=sys.stderr)
        return 2
    root = Path(sys.argv[1])
    data = []
    for tag, label in TAGS:
        d = {"label": label}
        d.update(status(tag, root))
        d.update(sample(tag, root, "idle"))
        d.update(sample(tag, root, "load"))
        d["sevenzip"] = sevenzip(tag, root)
        d["burn_rate"] = burn(tag, root)
        data.append(d)

    rows = [
        ("power_unlock", lambda d: d.get("unlock", "-")),
        ("governor", lambda d: d.get("governor", "-")),
        ("EPP", lambda d: d.get("epp", "-")),
        (
            "RAPL MSR PL1 / PL2",
            lambda d: f"{fmt(d.get('MSR_pl1'), ' W')} / {fmt(d.get('MSR_pl2'), ' W')}",
        ),
        (
            "RAPL MMIO PL1 / PL2",
            lambda d: f"{fmt(d.get('MMIO_pl1'), ' W')} / {fmt(d.get('MMIO_pl2'), ' W')}",
        ),
        ("idle package", lambda d: fmt(d.get("idle_watts"), " W")),
        ("idle temp", lambda d: fmt(d.get("idle_temp"), " °C", 1)),
        ("burn package", lambda d: fmt(d.get("load_watts"), " W")),
        ("burn max temp", lambda d: fmt(d.get("load_temp"), " °C", 1)),
        ("burn Avg_MHz", lambda d: fmt(d.get("load_mhz"), " MHz", 0)),
        ("burn Bzy_MHz", lambda d: fmt(d.get("load_bzy"), " MHz", 0)),
        ("7z total rating", lambda d: fmt(d.get("sevenzip"), "", 0)),
        ("burn rate", lambda d: fmt(d.get("burn_rate"), " it/s", 0)),
    ]

    print("# Huawei power-unlock measurement\n")
    print("| metric | " + " | ".join(d["label"] for d in data) + " |")
    print("|---|" + "---|" * len(data))
    for name, get in rows:
        print(f"| {name} | " + " | ".join(get(d) for d in data) + " |")

    pre, on = data[0], data[1]
    print()
    for key, label, unit in (
        ("load_watts", "burn package", " W"),
        ("load_mhz", "Avg_MHz", " MHz"),
        ("sevenzip", "7z rating", ""),
        ("burn_rate", "burn rate", " it/s"),
    ):
        a, b = pre.get(key), on.get(key)
        if isinstance(a, (int, float)) and isinstance(b, (int, float)) and a:
            print(
                f"- {label}: {a:.2f}{unit} → {b:.2f}{unit} "
                f"(**{(b - a) / a * 100:+.1f}%**)"
            )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
