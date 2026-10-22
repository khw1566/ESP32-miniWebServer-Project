"""Change only M3's saved/default SDIO clock. Does not build, flash or access a serial port."""
import argparse
import re
from pathlib import Path


def update_clock(text: str, clock_mhz: int) -> str:
    if clock_mhz not in (20, 40):
        raise ValueError("Supported profiles: 20 or 40 MHz")
    key = "CONFIG_ESP_HOSTED_SDIO_CLOCK_FREQ_KHZ"
    pattern = rf"(?m)^{key}=\d+$"
    if len(re.findall(pattern, text)) != 1:
        raise ValueError("Expected exactly one SDIO clock setting")
    return re.sub(pattern, f"{key}={clock_mhz * 1000}", text)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--clock-mhz", type=int, choices=(20, 40), required=True)
    parser.add_argument("--c6-high-speed-installed", action="store_true",
                        help="Confirm the matching C6 HIGH_SPEED image has been installed")
    args = parser.parse_args()
    if args.clock_mhz == 40 and not args.c6_high_speed_installed:
        parser.error("Install the matching C6 HIGH_SPEED firmware first; then pass --c6-high-speed-installed")
    root = Path(__file__).resolve().parents[1]
    targets = [root / "sdkconfig.defaults", root / "sdkconfig"]
    changes = []
    for target in targets:
        if target.exists():
            text = target.read_text(encoding="utf-8")
            changes.append((target, update_clock(text, args.clock_mhz)))
    for target, text in changes:
        target.write_text(text, encoding="utf-8", newline="\n")
        print(f"Updated SDIO clock only: {target.name} -> {args.clock_mhz} MHz")
    print("No build, flash or hardware check performed. Roll back with --clock-mhz 20.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
