# Pump bench test (single UNO R4 WiFi)

First step toward automatic spraying (shauryarchs/embersensor-site#53):
switch the small submersible pump from the Serial Monitor using **one**
Arduino UNO R4 WiFi and the kit relay, with the pump on **its own 5 V
supply**. Spare breadboard only — never the working motor/camera board.

Sketch: `arduino-source/pump-controller-r4/pump-controller-r4.ino`

## 1. Parts

| Part | Notes |
|---|---|
| Arduino UNO R4 WiFi | Drives the relay (5 V pins). An UNO R3 also works |
| Pump kit relay module | Blue relay `SRD-05VDC-SL-C` (5 V coil, contacts 10 A 30 VDC); pins VCC / GND / IN; screw terminals NO / COM / NC |
| Small submersible pump | From the kit; label not found — these kits are typically DC 3–5 V |
| 5 V wall charger + cut USB cable | Pump power. Red = +5 V, black = −. Plugged into the wall, **not** the laptop |
| 100 µF electrolytic capacitor (50 V) | Across the pump supply, smooths start/stop |
| Breadboard | Spare |
| Cup or bowl of water | The pump must be **under water** whenever it can run |
| Optional: push button | Starts/stops `loop` without the Serial Monitor |

## 2. Why this setup

Owner bench tests of the relay module (IN wire touched to each pin):

| IN connected to | Relay |
|---|---|
| nothing | off |
| GND | **on** |
| 3.3 V | **on** |
| 5 V | off |

- The module is **low-trigger** and only turns off with IN at 5 V or
  floating. A Nano ESP32 pin (0 V / 3.3 V, not 5 V tolerant) can't turn
  it off, so a 5 V board drives it: R4 pin 8 LOW = on, HIGH = off.
- A GPIO pin can supply only a few mA; the pump needs 100–300 mA, so
  the relay switches the pump's own supply.
- Earlier tests powered the pump from the Arduino's 5 V (and from a
  second Arduino on the same laptop). Each time the pump switched off,
  its spike reached the boards through the shared USB ground and the
  board's USB connection to the laptop dropped. A wall charger keeps the
  pump circuit electrically separate: its only link to the R4 is the
  switch inside the relay.

## 3. Wiring

**Unplug everything while wiring.**

**A. R4 → relay (control side)**

| Relay pin | R4 pin |
|---|---|
| VCC | **5V** |
| GND | **GND** |
| IN | **pin 8** |

These go straight from the R4 to the relay, not through the breadboard's
power lanes. VCC comes from the R4 because the relay compares IN with
its own VCC: IN HIGH (R4's 5 V) must equal VCC for the relay to turn
off. The coil draws about 70–90 mA, well within the R4's 5V pin.

**B. Pump circuit (5 V charger only)**

| From | To |
|---|---|
| Charger **red (+5 V)** | Breadboard **+** power lane |
| Charger **black (−)** | Breadboard **−** power lane |
| **+** lane | Relay **COM** screw |
| Relay **NO** screw | Pump **red (+)** |
| Pump **black (−)** | **−** lane |
| 100 µF capacitor | Across the **+** and **−** lanes: **stripe (−) → − lane** |

- Leave **NC** empty: COM + NO means the pump is off unless the relay
  is on.
- Use one pair of breadboard power lanes for the charger only. **No R4
  wire touches these lanes**, and no pump wire touches an R4 pin.
- Keep the pump, its wires and the charger as far from the R4 as the
  wires allow; twist the two pump wires together.
- Do **not** use a 9 V or 12 V supply: it would burn out a 3–5 V pump.

**C. Optional start/stop button**

| From | To |
|---|---|
| Button leg 1 | R4 **pin 2** |
| Button leg 2 | R4 **GND** |

No resistor needed (the sketch uses the internal pull-up). Without a
button, briefly touching a wire from pin 2 to GND does the same.

## 4. Software

No extra libraries. Board: **Arduino UNO R4 WiFi** (or **Arduino Uno**
for an R3).

Serial Monitor at **115200** baud, line ending **Newline**:

| Command | Effect |
|---|---|
| `on` | Run 3 s, then stop |
| `on <s>` | Run `s` seconds (1–30), then stop |
| `cycle` | 5 × (5 s on, 5 s off) |
| `loop` | 30 s on, 5 s off, repeating until stopped |
| `off` | Stop now (also cancels `cycle` / `loop`) |
| `status` | Pump state, cycle progress, run count |
| `help` | List commands |

Button on pin 2: each press starts `loop`, or stops whatever is running.
It works even after the R4's USB connection to the laptop drops.

Safety:
- Nothing runs at power-up; `loop` only starts from the command or the
  button.
- Pin 8 is set HIGH (relay off) both before and after `pinMode` at
  start-up — on the R4 the first write alone didn't stick and the pump
  started at power-up — and the relay state is re-applied every loop.
- Every run stops by itself; one command can ask for at most 30 s.

## 5. Step-by-step test

1. **Upload with the relay disconnected.** Unplug relay IN from pin 8
   (an old sketch could set pin 8 LOW and start the pump). Plug the R4
   into the laptop, select **Arduino UNO R4 WiFi** and its port, upload
   `pump-controller-r4`. If the port doesn't appear, double-tap the R4's
   reset button and try again.
2. Unplug the R4, reconnect relay IN to **pin 8**, put the pump fully
   under water.
3. Plug in the R4 and the charger.
   - Expected: relay green light **off**, pump off,
     `# pump is OFF` in the Serial Monitor.
4. `on`
   - Expected: click, pump runs, `PUMP on (run #1, command, auto-off in 3s)`;
     after 3 s `PUMP off (timer, ran 3.0s)`.
5. `on 10`, then `off` after a few seconds.
   - Expected: stops immediately, `PUMP off (command, ...)`.
6. `on 30` and wait.
   - Expected: runs the full 30 s and stops. Then type `status` —
     **the R4 should still answer** (no USB disconnect).
7. `cycle`
   - Expected: pump on 5 s / off 5 s, five times, then `CYCLE done`, and
     the R4 still answers `status`.
8. `loop` (or press the button) and leave it running.
   - Expected: 30 s on, 5 s off, repeating. Count how many runs pass
     before the R4 drops off USB, if it does. Stop with the button,
     `off`, or by unplugging the charger or the R4.
   - If the Serial Monitor drops, the pump should keep looping and the
     button should still stop it — that shows the R4 keeps running.
9. Press the R4's reset button.
   - Expected: the pump stays off during and after the reset.

Report the results on issue #57.

### Bench results so far

- Relay only (pump disconnected from NO): `on 30` and `cycle` 4/4 with
  no USB disconnect.
- With the pump on its own 5 V charger: 3 runs fine, disconnected on the
  4th. So the pump's switch-off noise, not the relay, causes the drops.
  Next mitigations: ceramic `104` capacitor across the pump terminals,
  pump wiring moved away from the R4, flyback diode when available.
- `loop` (30 s on / 5 s off): the R4 dropped off USB, but the pump kept
  turning on and off on schedule. The R4 keeps running; only its USB
  link to the laptop drops. Powered from a wall charger (no laptop) this
  does not affect pump control.

## 6. Done in software vs. needs hardware

- **Software (done):**
  - Compiles for `arduino:renesas_uno:unor4wifi` and `arduino:avr:uno`
    with all warnings on; no warnings from the sketch.
  - Host simulation (all pass): off at boot, `on` timer, out-of-range
    and junk input refused, `on 30` stops at 30 s, `off`, timer reset,
    `cycle` gives exactly 5 × 5 s, `off` and `on` cancel a cycle, relay
    state re-applied every loop; `loop` 30 s on / 5 s off repeating,
    stopped by `off`, `on`, `cycle` or the button; button starts and
    stops it (also during the off gap) and ignores a 20 ms bounce.
- **Needs hardware:** everything in section 5, especially that the R4
  stays connected to the laptop after long runs with the pump on its own
  supply.

## 7. Limitations and next steps

- Pump voltage/current assumed from typical kits; the label was not
  found.
- Bench test only: not connected to `main.ino`, the thermal camera, or
  auto-aim. Automatic spraying stays disabled until reviewed (#53).
- How the aiming Nano ESP32 tells this board to spray is still open
  (a wire, Wi-Fi, or an NPN transistor so the Nano drives the relay
  directly — the owner has NPN transistors). Decided in a later step.
- A flyback diode (1N4007 / 1N5819) across the pump, striped end to the
  pump's +, is still recommended before the main build.
