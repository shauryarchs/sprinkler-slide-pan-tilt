# Pump bench test (UNO R3 + spare Nano ESP32)

First step toward automatic spraying (shauryarchs/embersensor-site#53).
An **Arduino UNO R3** switches the pump through the kit relay; a
**spare** Nano ESP32 sends it a one-way "spray" signal on a single wire.
Use a **spare** breadboard and never the working motor/camera board —
if something is wired wrong, only the spare parts are at risk.

Sketches:
- `arduino-source/pump-controller-uno/pump-controller-uno.ino` — UNO R3
- `arduino-source/pump-signal-nano/pump-signal-nano.ino` — Nano ESP32

## 1. Parts

| Part | Notes |
|---|---|
| Arduino UNO R3 | Drives the relay (5 V pins) |
| Spare Arduino Nano ESP32 | Same model as the main board; sends the signal |
| Spare breadboard + jumper wires | |
| Pump kit relay module | Blue relay `SRD-05VDC-SL-C` (5 V coil, contacts 10 A 30 VDC); pins VCC / GND / IN; screw terminals NO / COM / NC |
| Small submersible pump | From the kit; label not found — these kits are typically DC 3–5 V |
| 1 resistor, 4.7 kΩ–100 kΩ (10 kΩ ideal) | Keeps the signal "off" by default. 1 kΩ also works |
| Cup or bowl of water | The pump must be **under water** whenever it runs |

No transistor is needed.

## 2. Why two boards

Owner bench test of the relay module (IN wire touched to each pin):

| IN connected to | Relay |
|---|---|
| nothing | off |
| GND | **on** |
| 3.3 V | **on** |
| 5 V | off |

The module is **low-trigger** and only turns off with IN floating or at
5 V. The Nano ESP32's pins output 0 V or 3.3 V (both mean "on") and must
never be connected to 5 V, so it can't drive this module directly. The
UNO R3's pins output 5 V, so the UNO drives it directly
(LOW = on, HIGH = off).

Benefits of the split:
- The pump's power and start-up surge stay on the UNO, so they can't
  reset the board that aims.
- The UNO has its own maximum runtime, independent of the Nano.

## 3. The signal wire

| Nano D2 | UNO pin 7 | Pump |
|---|---|---|
| HIGH (3.3 V) | HIGH | **on** |
| LOW | LOW | off |
| Nano booting / unplugged / wire loose | held LOW by the resistor | off |

- Signal only goes Nano → UNO. The UNO never drives this wire, so 5 V
  never reaches the Nano.
- The UNO reads 3.3 V as HIGH (its threshold is about 3.0 V at 5 V).
  That margin is small but normally reliable; test step 7 confirms it.
- The resistor from UNO pin 7 to GND is what makes "off" the default.
  Without it the input floats and noise could turn the pump on.

## 4. Wiring

**Unplug both USB cables while wiring.**

UNO ↔ relay:

| From | To |
|---|---|
| Relay VCC | UNO **5V** |
| Relay GND | UNO **GND** |
| Relay IN | UNO **pin 8** |

Pump (powered from the UNO's 5 V, switched by the relay):

| From | To |
|---|---|
| UNO **5V** | Relay **COM** (screw terminal) |
| Relay **NO** (screw terminal) | Pump **red / +** wire |
| Pump **black / −** wire | UNO **GND** |

Leave **NC** empty. COM + NO means the pump is off unless the relay is
switched on.

Nano → UNO signal:

| From | To |
|---|---|
| Nano **D2** | UNO **pin 7** |
| Nano **GND** | UNO **GND** |
| Resistor (either way round) | UNO **pin 7** ↔ UNO **GND** |

## 5. Power notes

- Relay coil ≈ 70 mA, UNO ≈ 50 mA, small pump ≈ 100–300 mA (more for a
  moment when it starts). A laptop USB port gives about 500 mA, so this
  is close to the limit.
- If the pump's start-up surge resets the UNO, the UNO reboots with the
  relay off (safe) and prints its start-up banner again. That means the
  real system needs a separate pump supply.
- Powering the pump from the UNO's 5 V is for this bench test only.

## 6. Software

No extra libraries.

| Board | Sketch | Arduino IDE board |
|---|---|---|
| UNO R3 | `pump-controller-uno` | Arduino Uno |
| Spare Nano ESP32 | `pump-signal-nano` | Arduino Nano ESP32 |

Both use the Serial Monitor at **115200** baud, line ending **Newline**,
with the same commands:

| Command | UNO | Nano |
|---|---|---|
| `on` | Run the pump 3 s | Send "spray" for 3 s |
| `on <s>` | Run `s` seconds (1–30) | Send "spray" `s` seconds (1–30) |
| `cycle` | — | 5 × (5 s spray, 5 s off), timed by the Nano itself |
| `off` | Stop now | Stop now (also cancels `cycle`) |
| `status` | Pump, signal, run count | Signal state |
| `help` | List commands | List commands |

UNO safety rules:
- Pump off at boot and whenever the signal is LOW.
- A run never lasts longer than **30 s**. After that the pump stays off
  until the Nano's signal goes LOW and HIGH again.
- `off` on the UNO also ignores a HIGH signal until it goes LOW.
- Signal changes shorter than 50 ms are ignored (noise).

Nano heartbeat: the built-in LED blinks once a second while the sketch
runs, fast while signalling "spray". If the Nano's Serial Monitor stops
responding but the LED keeps blinking, only the USB connection dropped.

Both Serial Monitors can be open at once (one per board/port). If the
Nano's normal upload stops partway, use the esptool method in
`THERMAL_CAMERA.md` (B1 → GND, reset, Upload Using Programmer).

## 7. Step-by-step test

**UNO alone (no Nano yet):**

1. Wire the UNO ↔ relay table only (no pump). Upload
   `pump-controller-uno`, open its Serial Monitor.
   - Expected: `# pump is OFF`, relay green light **off**.
2. Type `on`.
   - Expected: click, green light on, `PUMP on (run #1, command)`; after
     3 s a click, light off, `PUMP off (timer, ran 3.0s)`.
3. Type `on 10`, then `off` after a couple of seconds.
   - Expected: stops immediately, `PUMP off (command, ...)`.
4. Press the UNO's reset button.
   - Expected: light stays off during and after the reset.
5. **Add the pump.** Unplug USB, wire the pump table, put the pump fully
   under water with its outlet tube in the cup. Plug in, type `on`.
   - Expected: water flows for 3 s, then stops.
   - Watch for: the UNO's start-up banner printing again (it reset).

**Add the Nano:**

6. Unplug both. Wire the Nano → UNO table (D2, GND, resistor). Plug in
   the UNO, then the Nano. Upload `pump-signal-nano` to the **spare**
   Nano.
   - Expected: the pump stays off the whole time, including while the
     Nano boots and uploads.
7. In the **Nano's** Serial Monitor type `on`.
   - Expected: Nano prints `SIGNAL on`; UNO prints
     `SIGNAL HIGH (spray)` and `PUMP on (..., signal)`; water flows; after
     3 s both print off and the pump stops.
8. In the Nano's monitor type `on 30` and wait.
   - Expected: pump runs the full 30 s and stops.
9. Type `on 10` on the Nano, then unplug the Nano's USB while it runs.
   - Expected: pump stops within a moment (resistor pulls the signal low).
10. Plug the Nano back in, type `on 10` on it, then pull the D2 wire out.
    - Expected: pump stops.

11. **Nano deciding on its own:** type `cycle` on the Nano.
    - Expected: the pump runs 5 s, stops 5 s, five times (50 s total),
      even if the Nano's Serial Monitor stops responding partway. The
      UNO's monitor logs each `SIGNAL HIGH` / `LOW`.

### Known issue: pump noise drops the Nano's USB serial

Owner bench test: with the pump connected, the Nano's Serial Monitor
stops accepting commands after the pump switches off; with the pump
disconnected it works every time. The heartbeat LED keeps blinking, so
the Nano keeps running and only its native USB connection drops (the
motor's switch-off spike and relay arcing). Workarounds: reopen the
Serial Monitor or replug the Nano. Mitigations: pump on its own UNO GND
pin, pump wires twisted and kept away from the Nano, a capacitor across
the pump. Proper fix: a flyback diode (1N4007 / 1N5819) across the pump,
striped end to the pump's + wire. Needed before the main build.

Report the results on issue #57.

## 8. Done in software vs. needs hardware

- **Software (done):**
  - Both sketches compile with all warnings on (`arduino:avr:uno` and
    `arduino:esp32:nano_nora`); no warnings from the sketches.
  - Host simulation of the UNO logic (20 checks): off at boot, manual
    timer, `on 99` refused, `off`, debounce and 20 ms glitch ignored,
    signal on/off, 30 s max runtime with lock-out until the signal drops,
    `off` overriding a HIGH signal, repeated `on` still capped at 30 s.
- **Needs hardware:** everything in section 7, especially that the UNO
  reads the Nano's 3.3 V as HIGH, and whether the UNO's USB 5 V carries
  the pump without a reset.

## 9. Limitations

- Pump voltage/current are assumed from typical kits; the label was not
  found.
- Bench test only: not connected to `main.ino`, the thermal camera, or
  auto-aim. Automatic spraying stays disabled until reviewed (#53).
