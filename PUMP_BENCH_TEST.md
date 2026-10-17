# Pump bench test (spare board)

First step toward automatic spraying (shauryarchs/embersensor-site#53):
switch the small submersible pump from the Serial Monitor using a
**spare** Arduino Nano ESP32 on a **spare** breadboard. Never wire this
to the working motor/camera board — if something is wired wrong, only
the spare parts are at risk.

Sketch: `arduino-source/pump-bench-test/pump-bench-test.ino`

## 1. Parts

| Part | Notes |
|---|---|
| Spare Arduino Nano ESP32 | Same model as the main board |
| Spare breadboard + jumper wires | |
| Pump kit relay module | Blue relay `SRD-05VDC-SL-C` (5 V coil, contacts 10 A 30 VDC); pins VCC / GND / IN; screw terminals NO / COM / NC |
| Small submersible pump | From the kit; label not found — these kits are typically DC 3–5 V |
| NPN transistor | e.g. 2N3904, PN2222A, S8050, BC547 |
| Resistor 1 kΩ | Brown-black-red. Anything 1 kΩ–4.7 kΩ works |
| Cup or bowl of water | The pump must be **under water** whenever it runs |
| Optional: capacitor 100–470 µF | Across VBUS/GND, smooths the dip when the pump starts |
| Optional: diode 1N4007 / 1N5819 | Across the pump, absorbs its switch-off spike |

## 2. Why the transistor is needed

Owner bench test of the relay module (IN wire touched to each pin):

| IN connected to | Relay |
|---|---|
| nothing | off |
| GND | **on** |
| 3.3 V | **on** |
| 5 V (VBUS) | off |

The module is **low-trigger**: it only turns off when IN is floating or
at 5 V. A Nano ESP32 pin outputs 0 V or 3.3 V — both mean "on" — and
must never be connected to 5 V. The NPN transistor turns the pin's
high/low into the two states the module understands:

| D2 | Transistor | Relay IN | Relay / pump |
|---|---|---|---|
| HIGH (3.3 V) | on | GND | **ON** |
| LOW (0 V) | off | floating | **OFF** |

While the Nano is booting, resetting or unplugged, D2 is not driven
high, so the pump stays off.

## 3. Wiring

**Unplug USB while wiring.**

Relay module power and signal:

| From | To |
|---|---|
| Relay VCC | Nano **VBUS** (USB 5 V) |
| Relay GND | Nano **GND** |
| Relay IN | Transistor **collector** |
| Transistor **emitter** | Nano **GND** |
| Transistor **base** | 1 kΩ resistor → Nano **D2** |

Pump (powered from the Nano's USB 5 V, switched by the relay):

| From | To |
|---|---|
| Nano **VBUS** | Relay **COM** (screw terminal) |
| Relay **NO** (screw terminal) | Pump **red / +** wire |
| Pump **black / −** wire | Nano **GND** |

Leave **NC** empty. COM + NO means the pump is off unless the relay is
switched on.

Optional parts:
- Capacitor: long leg (+) to VBUS, striped leg (−) to GND.
- Diode across the pump: striped end to the pump's + wire, other end to
  the pump's − wire.

### Transistor legs

Hold the transistor with the **flat face toward you and legs down**,
then read the legs left → right:

| Part number | Left | Middle | Right |
|---|---|---|---|
| 2N3904, PN2222A, S8050 | Emitter | Base | Collector |
| BC547, P2N2222A | Collector | Base | Emitter |

The base is the middle leg for all of these. If the label is different,
check its datasheet before wiring.

## 4. Power notes

- Relay coil ≈ 70 mA, Nano ≈ 100 mA, small pump ≈ 100–300 mA (more for
  a moment when it starts). A laptop USB port gives about 500 mA in
  total, so this is close to the limit.
- Possible side effects: the Nano resets when the pump starts (the
  sketch prints `reset reason: BROWNOUT` after the reset), or the
  laptop briefly disables the USB port. These are glitches, not damage
  — they mean the real system needs a separate pump supply.
- Powering the pump from VBUS is for this bench test only. The real
  system must not risk resetting the board that aims the sprinkler, and
  a real spray pump will need far more current than USB gives.

## 5. Software

No extra libraries. Board: **Arduino Nano ESP32** (same core as the
main firmware, Arduino ESP32 2.x). Open
`arduino-source/pump-bench-test/pump-bench-test.ino` and upload it to the
**spare** board. If the normal upload stops partway, use the esptool
method described in `THERMAL_CAMERA.md` (B1 → GND, reset, Upload Using
Programmer).

Serial Monitor at **115200** baud, line ending **Newline**:

| Command | Effect |
|---|---|
| `on` | Run for 3 s, then stop automatically |
| `on <s>` | Run for `s` seconds (1–30), then stop automatically |
| `off` | Stop now |
| `status` | Print pump state |
| `help` | List commands |

Every run stops by itself when its timer ends; there is no way to run
the pump without a timer.

## 6. Step-by-step test

1. **Relay only, no pump.** Wire section 3's first table (relay +
   transistor), leave COM/NO empty. Plug in USB, upload, open the Serial
   Monitor.
   - Expected: `# pump is OFF`, relay green light **off**, no click.
2. Type `on`.
   - Expected: click, green light on, `PUMP on (run #1, auto-off in 3s)`,
     then after 3 s a click, light off, `PUMP off (timer, ran 3.0s)`.
3. Type `on 10`, then `off` after a couple of seconds.
   - Expected: light goes off immediately, `PUMP off (command, ...)`.
4. Press the Nano's reset button.
   - Expected: no click, light stays off during and after boot.
5. **Add the pump.** Unplug USB, wire the pump table, put the pump in
   the water (fully submerged) and point its outlet tube into the cup.
6. Plug in USB, type `on`.
   - Expected: pump runs for 3 s and stops.
   - Watch for: the Serial Monitor printing the boot banner again with
     `BROWNOUT` (the Nano reset), or the port disappearing (laptop cut
     USB power). Report what happened.
7. Try `on 10` and `off` with the pump in water.

Report the results (especially step 6) on issue #57.

## 7. Done in software vs. needs hardware

- **Software (done):** sketch compiles for `arduino:esp32:nano_nora`
  with no warnings.
- **Needs hardware:** everything in section 6, the transistor pinout of
  the owner's part, and whether VBUS can carry the pump without a reset.

## 8. Limitations

- Pump voltage/current are assumed from typical kits; the label was not
  found.
- Bench test only: not connected to `main.ino`, the thermal camera, or
  auto-aim. Automatic spraying stays disabled until reviewed (#53).
