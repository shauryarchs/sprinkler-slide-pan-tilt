#!/usr/bin/env python3
"""Generates main-board.kicad_sch: the rig's breadboard circuit as one schematic.

Every pin connects to a named net label, so PARTS below is the whole netlist.
check_netlist.py compares KiCad's netlist with these nets and with main.ino.
"""

import uuid
from pathlib import Path

HERE = Path(__file__).resolve().parent
LIB = "main_board"
NAMESPACE = uuid.UUID("6f1f6f3a-58d7-4c0e-9d57-1a2b3c4d5e6f")
SCREW_FP = "TerminalBlock_Phoenix:TerminalBlock_Phoenix_MKDS-1,5-{n}-5.08_1x0{n}_P5.08mm_Horizontal"
JST_FP = "Connector_JST:JST_XH_B{n}B-XH-A_1x0{n}_P2.50mm_Vertical"
WIRE_PADS_FP = "Connector_PinHeader_2.54mm:PinHeader_1x0{n}_P2.54mm_Vertical"
CAP_FP = "Capacitor_THT:CP_Radial_D8.0mm_P3.50mm"
DRIVER_FP = "Module:Pololu_Breakout-16_15.2x20.3mm"


def driver(ref, name, x, y, dir_pin, step_pin, motor):
    """One TMC2209 V2.0 StepStick. EN, MS2 to GND and MS1, VDD to VBUS, as on the rig."""
    return {
        "ref": ref, "value": f"TMC2209 V2.0 ({name})", "fp": DRIVER_FP, "at": (x, y),
        "left": [("1", "EN", "GND"), ("2", "MS1", "VBUS"), ("3", "MS2", "GND"),
                 ("4", "PDN", None), ("5", "UART", None), ("6", "CLK", None),
                 ("7", "STEP", step_pin), ("8", "DIR", dir_pin)],
        "right": [("16", "VM", "+12V"), ("15", "GND", "GND"),
                  ("14", "A2", f"{motor}_A2"), ("13", "A1", f"{motor}_A1"),
                  ("12", "B1", f"{motor}_B1"), ("11", "B2", f"{motor}_B2"),
                  ("10", "VDD", "VBUS"), ("9", "GND", "GND")],
    }


def connector(ref, value, fp, x, y, pins):
    """Off-board connection. pins: (name, net), numbered from 1."""
    return {
        "ref": ref, "value": value, "fp": fp, "at": (x, y),
        "right": [(str(i + 1), name, net) for i, (name, net) in enumerate(pins)], "left": [],
    }


def two_pin(ref, value, fp, x, y, net1, net2, name1="1", name2="2"):
    return {
        "ref": ref, "value": value, "fp": fp, "at": (x, y),
        "left": [("1", name1, net1)], "right": [("2", name2, net2)],
    }


def motor_terminal(ref, name, x, y, motor):
    return connector(ref, f"{name} motor", SCREW_FP.format(n=4), x, y, [
        ("A1", f"{motor}_A1"), ("A2", f"{motor}_A2"), ("B1", f"{motor}_B1"), ("B2", f"{motor}_B2")])


PARTS = [
    # Pad numbers follow Module:Arduino_Nano; datasheet JP1 pin k = pad 15+k, JP2 pin k = pad 16-k.
    {
        "ref": "U1", "value": "Arduino Nano ESP32 (ABX00083)", "fp": "Module:Arduino_Nano", "at": (70, 80),
        "left": [("16", "D13", None), ("17", "3V3", "+3V3"), ("18", "B0", None), ("19", "A0", "THERM_SDA"),
                 ("20", "A1", "THERM_SCL"), ("21", "A2", None), ("22", "A3", None), ("23", "A4", "OLED_SDA"),
                 ("24", "A5", "OLED_SCL"), ("25", "A6", None), ("26", "A7", None), ("27", "VBUS", "VBUS"),
                 ("28", "B1", None), ("29", "GND", "GND"), ("30", "VIN", None)],
        "right": [("15", "D12", "LIMIT"), ("14", "D11", "TILT_STEP"), ("13", "D10", "TILT_DIR"),
                  ("12", "D9", "PAN_STEP"), ("11", "D8", "PAN_DIR"), ("10", "D7", "SLIDE_STEP"),
                  ("9", "D6", "SLIDE_DIR"), ("8", "D5", "ENC_CLK"), ("7", "D4", "ENC_DT"),
                  ("6", "D3", "ENC_SW"), ("5", "D2", "PUMP_CTRL"), ("4", "GND", "GND"),
                  ("3", "RST", None), ("2", "RX", None), ("1", "TX", None)],
    },
    driver("U2", "slider", 170, 60, "SLIDE_DIR", "SLIDE_STEP", "SLIDE"),
    driver("U3", "pan", 170, 120, "PAN_DIR", "PAN_STEP", "PAN"),
    driver("U4", "tilt", 170, 180, "TILT_DIR", "TILT_STEP", "TILT"),
    motor_terminal("J2", "Slider", 235, 55, "SLIDE"),
    motor_terminal("J3", "Pan", 235, 115, "PAN"),
    motor_terminal("J4", "Tilt", 235, 175, "TILT"),
    two_pin("C1", "100uF 50V", CAP_FP, 330, 30, "+12V", "GND", "+", "-"),
    connector("J1", "12 V input", SCREW_FP.format(n=2), 290, 50, [("+", "+12V_IN"), ("-", "GND")]),
    two_pin("F1", "5A fuse, 5x20mm",
            "Fuse:Fuseholder_Clip-5x20mm_Keystone_3517_Inline_P23.11x6.76mm_D1.70mm_Horizontal",
            330, 50, "+12V_IN", "+12V"),
    connector("J11", "Solenoid valve", SCREW_FP.format(n=2), 290, 80, [("+", "SOLENOID+"), ("-", "GND")]),
    two_pin("D1", "1N5408 flyback", "Diode_THT:D_DO-201AD_P15.24mm_Horizontal",
            330, 80, "SOLENOID+", "GND", "K", "A"),
    connector("J10", "Relay contacts (wires)", SCREW_FP.format(n=2), 290, 110,
              [("NO", "+12V"), ("COM", "SOLENOID+")]),
    connector("J9", "Relay control (wires)", WIRE_PADS_FP.format(n=3), 290, 140,
              [("VCC", "VBUS"), ("GND", "GND"), ("IN", "RELAY_IN")]),
    two_pin("R1", "1k", "Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal",
            290, 170, "PUMP_CTRL", "Q1_BASE"),
    {
        "ref": "Q1", "value": "2N2222A", "fp": "Package_TO_SOT_THT:TO-92_Inline_Wide", "at": (290, 195),
        "left": [("2", "B", "Q1_BASE")], "right": [("3", "C", "RELAY_IN"), ("1", "E", "GND")],
    },
    # The OLED and encoder modules solder straight into the board through their own header pins.
    connector("J5", "Encoder module", WIRE_PADS_FP.format(n=5), 350, 120,
              [("GND", "GND"), ("+", "VBUS"), ("SW", "ENC_SW"), ("DT", "ENC_DT"), ("CLK", "ENC_CLK")]),
    connector("J6", "Limit switch", JST_FP.format(n=2), 350, 150, [("SW", "LIMIT"), ("GND", "GND")]),
    connector("J7", "OLED module", WIRE_PADS_FP.format(n=4), 350, 175,
              [("GND", "GND"), ("VCC", "+3V3"), ("SCL", "OLED_SCL"), ("SDA", "OLED_SDA")]),
    connector("J8", "Thermal camera", JST_FP.format(n=4), 350, 205,
              [("VCC", "+3V3"), ("GND", "GND"), ("SDA", "THERM_SDA"), ("SCL", "THERM_SCL")]),
]

PITCH = 2.54
STUB = 5.08
PIN_LEN = 2.54


def uid(*key):
    return str(uuid.uuid5(NAMESPACE, "/".join(str(k) for k in key)))


def font():
    return "(effects (font (size 1.27 1.27)))"


def snap(v):
    return round(round(v / 1.27) * 1.27, 2)


def half_width(part):
    longest = max(len(name) for name, *_ in [(p[1],) for p in part["left"] + part["right"]])
    return snap(max(5.08, 1.27 * longest + 3.81))


def half_height(part):
    return PITCH * max(len(part["left"]), len(part["right"])) / 2 + 1.27


def pin_y(i, count):
    """Pin offset from the part's centre, in library coordinates (y up)."""
    return snap((count - 1) * PITCH / 2 - i * PITCH)


def lib_symbol(part):
    name = part["ref"]
    w, h = half_width(part), snap(half_height(part))
    pins = []
    for side, x, angle in (("left", -w - PIN_LEN, 0), ("right", w + PIN_LEN, 180)):
        for i, (number, pin_name, _) in enumerate(part[side]):
            pins.append(
                f'(pin passive line (at {x} {pin_y(i, len(part[side]))} {angle}) (length {PIN_LEN}) '
                f'(name "{pin_name}" {font()}) (number "{number}" {font()}))')
    return (
        f'(symbol "{LIB}:{name}" (pin_names (offset 1.016)) (exclude_from_sim no) (in_bom yes) (on_board yes)\n'
        f'  (property "Reference" "{name[0]}" (at 0 {h + 1.27} 0) {font()})\n'
        f'  (property "Value" "{name}" (at 0 {-h - 1.27} 0) {font()})\n'
        f'  (property "Footprint" "" (at 0 0 0) (effects (font (size 1.27 1.27)) (hide yes)))\n'
        f'  (property "Datasheet" "" (at 0 0 0) (effects (font (size 1.27 1.27)) (hide yes)))\n'
        f'  (symbol "{name}_0_1" (rectangle (start {-w} {h}) (end {w} {-h}) '
        f'(stroke (width 0.254) (type default)) (fill (type background))))\n'
        f'  (symbol "{name}_1_1" {" ".join(pins)}))')


def placed_symbol(part, root):
    name, (x, y) = part["ref"], part["at"]
    x, y = snap(x), snap(y)
    h = snap(half_height(part))
    pins = " ".join(f'(pin "{p[0]}" (uuid "{uid(name, "pin", p[0])}"))' for p in part["left"] + part["right"])
    return (
        f'(symbol (lib_id "{LIB}:{name}") (at {x} {y} 0) (unit 1) (exclude_from_sim no) (in_bom yes) '
        f'(on_board yes) (dnp no) (uuid "{uid(name)}")\n'
        f'  (property "Reference" "{name}" (at {x} {snap(y - h - 1.27)} 0) {font()})\n'
        f'  (property "Value" "{part["value"]}" (at {x} {snap(y + h + 1.27)} 0) {font()})\n'
        f'  (property "Footprint" "{part["fp"]}" (at {x} {y} 0) (effects (font (size 1.27 1.27)) (hide yes)))\n'
        f'  (property "Datasheet" "" (at {x} {y} 0) (effects (font (size 1.27 1.27)) (hide yes)))\n'
        f'  {pins}\n'
        f'  (instances (project "main-board" (path "/{root}" (reference "{name}") (unit 1)))))')


def connections(part):
    """Net labels (or no-connect flags) on every pin end of a part."""
    name, (x, y) = part["ref"], part["at"]
    x, y = snap(x), snap(y)
    w = half_width(part) + PIN_LEN
    out = []
    for side, sign, justify, angle in (("left", -1, "right", 180), ("right", 1, "left", 0)):
        for i, (number, _, net) in enumerate(part[side]):
            px, py = snap(x + sign * w), snap(y - pin_y(i, len(part[side])))
            if net is None:
                out.append(f'(no_connect (at {px} {py}) (uuid "{uid(name, "nc", number)}"))')
                continue
            end = snap(px + sign * STUB)
            out.append(f'(wire (pts (xy {px} {py}) (xy {end} {py})) (stroke (width 0) (type default)) '
                       f'(uuid "{uid(name, "wire", number)}"))')
            out.append(f'(label "{net}" (at {end} {py} {angle}) (effects (font (size 1.27 1.27)) '
                       f'(justify {justify} bottom)) (uuid "{uid(name, "label", number)}"))')
    return out


def main():
    root = uid("root")
    body = [
        '(kicad_sch (version 20250114) (generator "eeschema") (generator_version "9.0")',
        f'(uuid "{root}") (paper "A3")',
        '(title_block (title "EmberSensor sprinkler rig main board") (rev "A"))',
        "(lib_symbols", *[lib_symbol(p) for p in PARTS], ")",
        *[line for p in PARTS for line in connections(p)],
        *[placed_symbol(p, root) for p in PARTS],
        '(sheet_instances (path "/" (page "1"))))',
    ]
    (HERE / "main-board.kicad_sch").write_text("\n".join(body) + "\n")


if __name__ == "__main__":
    main()
