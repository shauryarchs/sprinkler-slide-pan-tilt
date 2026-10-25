# Pump prototype: Nano ESP32 + NPN transistor

Prototype (branch `shaurya/test-pump-nano`): one **spare** Arduino Nano
ESP32 drives the pump kit's relay through an NPN transistor, and the
pump runs from **its own 5 V wall charger**. No second Arduino. Spare
breadboard only — never the working motor/camera board.

Sketch: `arduino-source/pump-nano-transistor/pump-nano-transistor.ino`

Background (relay behaviour, why the pump has its own supply, USB-drop
results with the UNO R4 version): see branch
`shaurya/57_pump-bench-test`, `PUMP_BENCH_TEST.md`.

## 1. Parts

| Part | Notes |
|---|---|
| Spare Arduino Nano ESP32 | Same model as the main board |
| NPN transistor | Owner's part: **2N2222A** (plastic TO-92, box says PN2222) |
| Resistor 1 kΩ | Brown-black-red; 1–4.7 kΩ works. Between D2 and the transistor base |
| Pump kit relay module | `SRD-05VDC-SL-C`, low-trigger, pins VCC / GND / IN, screws NO / COM / NC |
| Small submersible pump | Kit pump, assumed 3–5 V |
| 5 V wall charger + cut USB cable | Pump power. Red = +5 V, black = − |
| 100 µF electrolytic capacitor | Across the pump supply lanes, stripe to − |
| Optional: push button | On/off switch for the pump, works without the Serial Monitor |

## 2. How it works

The relay turns on with IN at GND and off with IN floating or at 5 V. A
Nano pin outputs only 0 V or 3.3 V (both mean "on") and must never see
5 V. The transistor connects IN to GND only when D2 is HIGH:

| D2 | Transistor | Relay IN | Relay / pump |
|---|---|---|---|
| HIGH (3.3 V) | on | GND | **on** |
| LOW (0 V) | off | floating | off |
| Nano booting / unplugged | off | floating | off |

The base needs only ~0.7 V, so 3.3 V switches it fully; the 1 kΩ limits
the base current to ~2.6 mA.

## 3. Transistor legs

Hold it with the **flat face toward you, legs down**. For PN2222 / most
plastic 2N2222A parts, left → right:

| Left | Middle | Right |
|---|---|---|
| **E**mitter | **B**ase | **C**ollector |

Some makers swap the outer legs (C-B-E). The base is the middle leg
either way. Section 6 step 2 checks the order safely: if the relay won't
click, swap the two outer legs. Nothing is damaged if they're reversed —
the relay just won't switch properly.

## 4. Wiring

**Unplug everything while wiring.**

**A. Nano → transistor → relay (control side)**

| From | To |
|---|---|
| Relay **VCC** | Nano **VBUS** (USB 5 V) |
| Relay **GND** | Nano **GND** |
| Relay **IN** | Transistor **collector** (right leg) |
| Transistor **emitter** (left leg) | Nano **GND** |
| Transistor **base** (middle leg) | 1 kΩ resistor → Nano **D2** |

**B. Pump circuit (5 V charger only — no connection to the Nano)**

| From | To |
|---|---|
| Charger **red (+5 V)** | Breadboard **+** power lane |
| Charger **black (−)** | Breadboard **−** power lane |
| **+** lane | Relay **COM** screw |
| Relay **NO** screw | Pump **red (+)** |
| Pump **black (−)** | **−** lane |
| 100 µF capacitor | Across the **+** and **−** lanes, **stripe → −** |

Leave **NC** empty. No Nano wire touches the charger lanes. Keep the pump
wiring away from the Nano, twist the pump wires together, pump fully
under water whenever it can run.

**C. Optional button**

| From | To |
|---|---|
| Button leg 1 | Nano **A2** |
| Button leg 2 | Nano **GND** |

No resistor needed (internal pull-up). Touching a wire from A2 to GND
briefly does the same.

D2, A2 and the built-in LED are unused on the main board, so this wiring
carries over if the pump moves to the main Nano later.

## 5. Software

No extra libraries. Board: **Arduino Nano ESP32**. Serial Monitor at
**115200** baud, line ending **Newline**:

| Command | Effect |
|---|---|
| `on` | Run 3 s, then stop |
| `on <s>` | Run `s` seconds (1–30), then stop |
| `cycle` | 5 × (5 s on, 5 s off) |
| `loop` | 10 s on, 5 s off, repeating until stopped |
| `off` | Stop now (also cancels `cycle` / `loop`) |
| `status` | Pump state, cycle/loop progress, run count |
| `help` | List commands |

- Button on A2: on/off switch. Press once — pump stays on; press again —
  it stops (also stops `cycle` / `loop`). A button run shuts off by
  itself after 10 minutes in case it's left on. Works after the USB
  connection drops.
- Built-in LED: slow blink while running, fast while the pump is on. If
  the Serial Monitor drops but the LED keeps blinking, only USB dropped.
- Nothing runs at power-up. D2 is set LOW before and after `pinMode`.

If the normal upload fails (`LIBUSB_ERROR`), close the Serial Monitor,
replug the Nano, or double-tap reset; the esptool method in
`THERMAL_CAMERA.md` also works.

## 6. Step-by-step test

1. **Control side only.** Wire table A (no pump, no charger). Upload the
   sketch, open the Serial Monitor.
   - Expected: `# pump is OFF`, relay green light **off**, LED blinking
     slowly.
2. Type `on`.
   - Expected: click, green light on for 3 s, then off.
   - **If nothing clicks:** unplug, swap the transistor's two **outer**
     legs (collector ↔ emitter), try again. Check the 1 kΩ is in the
     base (middle) leg.
3. Press the Nano's reset button and unplug/replug it.
   - Expected: green light stays off throughout.
4. **Add the pump.** Unplug, wire table B, pump under water, plug in the
   charger and the Nano. Type `on`, then `on 30`, then `status`.
5. Press the button: pump stays on. Press again: it stops.
6. `loop`, leave it running.
   - Expected: 10 s on / 5 s off repeating. If the Serial Monitor drops,
     the pump should keep looping and the LED keep blinking; the button
     should still stop it.

### Bench results (owner, 2026-10-24)

- Nano + 2N2222A drives the relay; pump on its own 5 V wall charger.
- `loop` and the on/off button work.
- **The Nano did not disconnect from the laptop** (unlike the UNO R4
  version on `shaurya/57_pump-bench-test`, which dropped USB after a few
  pump runs).

## 7. Done in software vs. needs hardware

- **Software (done):** compiles for `arduino:esp32:nano_nora` (core
  2.0.18-arduino.5) with all warnings on, no warnings from the sketch.
  Host simulation, 39 checks pass: off at boot, timers, input checks,
  `on 30`, `off`, `cycle`, `loop`, button on/off toggle (10 min safety
  stop, stops a loop or cycle) and bounce, D2
  re-asserted every loop.
- **Hardware (owner):** section 6 passed — see bench results above.
