#!/usr/bin/env python3
"""Checks a KiCad netlist against the intended nets and the pins in main.ino.

Usage: check_netlist.py <netlist.net>   (kicad-cli sch export netlist --format kicadsexpr)
"""

import re
import sys
from pathlib import Path

from generate_schematic import PARTS

MAIN_INO = Path(__file__).resolve().parents[2] / "arduino-source" / "main" / "main.ino"

# main.ino constant -> net it must reach on the Nano.
FIRMWARE_NETS = {
    "kEncoderSw": "ENC_SW", "kEncoderDt": "ENC_DT", "kEncoderClk": "ENC_CLK",
    "kMotor1Dir": "SLIDE_DIR", "kMotor1Step": "SLIDE_STEP",
    "kMotor2Dir": "PAN_DIR", "kMotor2Step": "PAN_STEP",
    "kMotor3Dir": "TILT_DIR", "kMotor3Step": "TILT_STEP",
    "kLimit": "LIMIT", "kPump": "PUMP_CTRL",
}


def read_netlist(path):
    """Returns {net name: {(ref, pad)}} with nets as KiCad resolved them."""
    nets = {}
    for block in re.split(r"\(net\s+\(code", Path(path).read_text())[1:]:
        name = re.search(r'\(name "([^"]*)"\)', block).group(1).lstrip("/")
        nodes = re.findall(r'\(node\s+\(ref "([^"]+)"\)\s+\(pin "([^"]+)"\)', block)
        nets[name] = set(nodes)
    return nets


def intended_nets():
    nets = {}
    for part in PARTS:
        for number, _, net in part["left"] + part["right"]:
            if net is not None:
                nets.setdefault(net, set()).add((part["ref"], number))
    return nets


def main():
    errors = []
    actual = {k: v for k, v in read_netlist(sys.argv[1]).items() if not k.startswith("unconnected-")}
    expected = intended_nets()
    for net in sorted(set(actual) | set(expected)):
        if actual.get(net) != expected.get(net):
            errors.append(f"net {net}: netlist {actual.get(net)} != intended {expected.get(net)}")

    nano = next(p for p in PARTS if p["ref"] == "U1")
    nano_pins = {name: net for _, name, net in nano["left"] + nano["right"]}
    source = MAIN_INO.read_text()
    for constant, net in FIRMWARE_NETS.items():
        pin = re.search(rf"const int {constant} = (\w+);", source).group(1)
        if nano_pins.get(pin) != net:
            errors.append(f"{constant} is {pin} in main.ino, but {pin} is on {nano_pins.get(pin)}, not {net}")

    for net, nodes in sorted(expected.items()):
        if len(nodes) < 2:
            errors.append(f"net {net} has only one connection: {nodes}")

    print("\n".join(errors) or f"OK: {len(expected)} nets match, {len(FIRMWARE_NETS)} firmware pins match main.ino")
    sys.exit(1 if errors else 0)


if __name__ == "__main__":
    main()
