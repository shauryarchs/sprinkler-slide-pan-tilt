# Thermal camera setup (MLX90640-D110)

This guide connects a **Waveshare MLX90640-D110** thermal camera to the **Arduino Nano ESP32**.

- **Phase 1, USB view (sections 1–8):** a standalone test sketch streams the live raw thermal image to a Chrome viewer. Use it to check wiring and see the full image.
- **Phase 2, main firmware (section 9):** the camera runs inside the motor firmware and reports hotspots (direction, temperature) to the Serial Monitor while the motors work.
- **Auto-aim (section 10):** slides, then pans/tilts, to center a hotspot. **On at boot.**

Nothing is sent to the EmberSensor Worker, website, or iOS app. Auto-aim (section 10) moves the motors on its own when the camera sees a hotspot. The water pump (section 11) sprays when auto-aim is on target, also on at boot (`spray off` disables it until reboot).

Tracking issues: shauryarchs/embersensor-site#46 (Phase 1), shauryarchs/embersensor-site#49 (Phase 2)

| Item | Value |
|---|---|
| Camera | Waveshare MLX90640-D110 Thermal Camera ([wiki](https://www.waveshare.com/wiki/MLX90640-D110_Thermal_Camera)) |
| Sensor | Melexis MLX90640, 32 × 24 pixels, 110° × 75° field of view |
| Range / accuracy | Targets −40 to 300 °C, about ±1 °C; works best at roughly 1–9 m |
| Interface | I2C, address `0x33`, up to 1 MHz |
| Supply | 3.3 V or 5 V, under 23 mA (onboard voltage translator) |
| Board | Arduino Nano ESP32 (ESP32-S3) |
| Test sketch | `arduino-source/thermal-usb-test/thermal-usb-test.ino` |
| Viewer | `tools/thermal-viewer/thermal-serial.html` (Chrome or Edge) |
| Main firmware | `arduino-source/main/` (`ThermalCamera.h/.cpp`) |

## 1. Wiring

The camera gets its **own I2C bus** on A0/A1. The OLED stays on the default bus (A4/A5), so the two never interfere once the camera is added to the main firmware.

| Camera pin | Wire to Nano ESP32 pin | Notes |
|---|---|---|
| VCC | **3V3** | Use 3.3 V, **not** 5 V (see below) |
| GND | **GND** | Any GND pin |
| SDA | **A0** | I2C data (`Wire1`) |
| SCL | **A1** | I2C clock (`Wire1`) |

Pins already used by the motor firmware, for reference: D3–D5 encoder, D6–D11 stepper DIR/STEP, D12 limit switch, A4/A5 OLED. A0 and A1 are free.

**Why 3.3 V and not 5 V:** the Nano ESP32's pins are 3.3 V only and are **not 5 V tolerant**. The camera's onboard voltage translator sets the I2C signal level from VCC. Powering it from 5 V could put 5 V on A0/A1 and damage the board.

## 2. Power and extra parts

- **Power:** about 23 mA from the Nano ESP32's 3V3 pin while the board is USB-powered. No separate supply needed.
- **Wires:** four female-to-female (or female-to-male) jumper wires. Keep them **short, under about 20 cm**. Waveshare warns that long wires cause I2C and EEPROM read errors.
- **Pull-up resistors:** not normally needed (Waveshare's wiring has no external pull-ups). Only if the camera is not detected *and* wiring is correct, add **4.7 kΩ** from SDA to 3V3 and from SCL to 3V3.
- **Nothing else** is required for Phase 1.

## 3. Mounting

**For Phase 1** the position doesn't matter. Hold the camera, or tape it to a box, pointing at the room, 0.5–2 m from what you want to see.

- Keep the lens clear. **Glass and most clear plastics block thermal infrared**, so the camera cannot see through a window or a clear case.
- Don't point it at the sun.

**For Phase 2 — mounting on the arm:**

- **Where:** on the tilt head, right next to the nozzle, with the camera's lens pointing the **same direction as the nozzle**. When a hotspot is at the image centre (`dx` ≈ 0, `dy` ≈ 0), the nozzle points at it. A few centimetres of offset between lens and nozzle doesn't matter at fire distances.
- **Upright:** mount the module the same way up as during the Phase 1 test (the image looked correct with no Flip/Rotate). If you mount it rotated or upside down, set `kFlipX` / `kFlipY` in `ThermalCamera.h` (see section 9).
- **Secure it:** screws, standoffs, or a bracket — not tape. Vibration from the steppers can loosen it and shift the aim.
- **Cable movement:** tilt can rotate ±360°, which will wind up a cable. Use a slack loop with strain relief, or limit tilt travel while the camera is mounted.
- **Water:** the module is not waterproof. It needs an enclosure with an **IR-transparent window** (germanium, zinc selenide, or thin HDPE film), not glass or acrylic.
- **Heat sources:** keep the camera's view and its body away from the stepper drivers and motors. They get warm and will show up as hot spots.
- **Distance:** longer cable runs to a moving arm may need a slower I2C speed or a shielded cable.

## 4. Software setup (Arduino IDE)

1. **Board package:** Tools → Board → Boards Manager → install **"Arduino ESP32 Boards" by Arduino, version 2.0.18-arduino.5** (the 2.x line).
   - Stay on 2.x. The main motor firmware uses the 2.x hardware-timer API and will not compile on 3.x.
2. **Libraries:** Tools → Manage Libraries → install **"Adafruit MLX90640"** (tested with 1.1.2). Accept the prompt to also install **Adafruit BusIO**.
3. **Board settings:**
   - Tools → Board → **Arduino Nano ESP32**.
   - Tools → Pin Numbering → **By Arduino pin (default)**.
4. **Open the sketch:** `arduino-source/thermal-usb-test/thermal-usb-test.ino`.

No `secrets.h` or Wi-Fi setup is needed. The test sketch does not use the network.

## 5. Step-by-step first test

**Before you start:** the test sketch temporarily replaces the motor firmware on the board. The slider, pan, and tilt won't respond while it's loaded. Re-upload `arduino-source/main` afterwards (step 8).

| Step | Action | Expected result |
|---|---|---|
| 1 | Unplug USB. Wire the camera per section 1. Double-check VCC → **3V3**. | — |
| 2 | Plug the Nano ESP32 into your Mac by USB. Upload `thermal-usb-test.ino`. | Upload completes. If upload fails, double-tap the board's reset button and try again. |
| 3 | Open Tools → Serial Monitor at **115200 baud**. Press the board's reset button once. | `# EmberSensor thermal USB test (MLX90640)`, then `# rate=8Hz (~4.0 fps) i2c=400000Hz`, then `# MLX90640 serial XXXXXXXXXXXX at 0x33`. |
| 4 | Point the camera at the room. | One line per second, e.g. `min  20.8 C  max  26.3 C  avg  22.9 C  hottest x=17 y= 9  fps 4.0`. Values close to room temperature; fps about 4.0. |
| 5 | Hold your hand 20–50 cm in front of the lens. | `max` rises to roughly **30–36 °C** and `hottest` moves when your hand moves. |
| 6 | **Close the Serial Monitor** (only one program can use the port). Open `tools/thermal-viewer/thermal-serial.html` in **Chrome** (double-click it, or drag it into Chrome). Click **Connect** and choose the Nano ESP32 port. | Status turns green **Live**. A colour heat image appears with your hand bright yellow or white; FPS about 4. The log shows `# mode=frames`. |
| 7 | Try the controls: **Rate** (1–16 fps), **Range** (Auto / 15–40 °C / 0–300 °C), **Flip** / **Rotate** to match how the camera is held, hover over the image to read a pixel. | The image updates accordingly. If left and right look swapped, tick **Flip ↔**. |
| 8 | Click **Disconnect**. Re-open `arduino-source/main/main.ino` and upload it to restore the motor firmware. | Board boots into the normal homing sequence and menu. |

**Without hardware:** open the viewer and click **Demo** to see synthetic frames (a moving warm blob and a small hot spot).

### Serial commands (Serial Monitor or viewer)

| Command | Effect |
|---|---|
| `F` | Frame mode (machine-readable, used by the viewer) |
| `T` | Summary mode (one readable line per second) |
| `R8` | Sensor refresh rate in Hz: `1`, `2`, `4`, `8`, `16`, `32`. Full frames per second = Hz ÷ 2 |
| `I` | Print camera serial number, mode, rate, read errors |

## 6. Troubleshooting

| Symptom | Likely cause / fix |
|---|---|
| `# ERROR MLX90640 not found at 0x33` and `no devices found` | Wiring or power. Check VCC → 3V3, GND, SDA → A0, SCL → A1 (SDA/SCL swapped is common). The sketch retries every 3 s. |
| Scan shows a device but not `0x33` | The camera's address was reprogrammed. Use the address shown, by changing `kCameraAddr`. |
| `# WARN frame read failed` repeating | Wires too long or loose, or rate too high. Shorten wires, use `R8` or lower, add 4.7 kΩ pull-ups. |
| Viewer Connect button greyed out | Browser lacks Web Serial. Use desktop Chrome or Edge. |
| Viewer says "Failed to open serial port" | Serial Monitor (or another app) still has the port open. Close it. |
| Image is noisy or speckled | Expected at 16 or 32 Hz. Use 4 fps (8 Hz) or lower. |
| Everything reads slightly off | Normal: ±1 °C accuracy, and shiny surfaces (metal, glass) reflect heat and read wrong. |

## 7. What was validated where

| Check | Done in software | Needs your hardware |
|---|---|---|
| Sketch compiles for Arduino Nano ESP32 (core 2.0.18-arduino.5, Adafruit MLX90640 1.1.2), no warnings from sketch code | ✅ | |
| Main motor firmware unchanged and still compiles | ✅ | |
| Viewer script syntax; frame parser accepts the sketch's exact line format and rejects malformed lines; invalid pixels handled | ✅ | |
| Hover readout and hotspot marker map to the same pixel for every rotate / flip combination | ✅ | |
| Viewer layout and demo mode in a browser | | ✅ open the page and click Demo |
| Camera detected over I2C on A0/A1; frames read at ~4 fps | | ✅ steps 3–4 |
| Hand-heat response and image orientation | | ✅ steps 5–7 |
| Web Serial connection on your Mac | | ✅ step 6 |

## 8. Assumptions and limitations

- **Pull-ups:** the module is assumed to have onboard I2C pull-ups. Waveshare's wiring uses none, but this is not confirmed from the schematic.
- **Orientation:** which way is "up" depends on how the module is held. Use the viewer's Flip/Rotate controls.
- **Resolution:** 32 × 24 pixels shows heat shapes, not detail. Small or distant hot spots blend into neighbouring pixels.
- **Display only:** thermal data is not connected to fire risk, the FireGuard sprinkler trigger, or motor movement. Any automatic action stays disabled until thermal detection has been reviewed and validated.
- **Later phases:** automatic aiming (shauryarchs/embersensor-site#52), the water pump and nozzle (#53), and the overview camera (#50) are separate issues.

## 9. Phase 2: camera in the main firmware

With Phase 2, `arduino-source/main` reads the camera in the background while the motors run, finds the hot blob, and prints its status to the **Serial Monitor**. It does **not** move any motor or switch any water; the motors behave exactly as before.

### What it does
- Reads frames at ~4 fps on core 0 (next to the Wi-Fi tasks), so motor stepping, the encoder, and the OLED are unaffected.
- **Hotspot rule:** hottest pixel ≥ **50 °C** *and* ≥ **15 °C** above the scene's median. People (skin ~33–36 °C) never count as fire.
- Takes the temperature-weighted **centre of the hot blob** (not the single hottest pixel), then **smooths** it over ~1 s and **debounces** detection (on after 3 frames, off after ~2 s), so flame flicker doesn't make the output jump.
- Converts the blob centre to **degrees from the image centre**.
- If the camera is missing or unplugged, it prints one message and keeps retrying every 10 s. Everything else works normally.

### Serial Monitor output (115200 baud, about once per second)

```
THERMAL camera=ok serial=XXXXXXXXXXXX rate=8Hz (~4 fps) threshold=50C rise=15C
THERMAL hotspot=no max=24.1C bg=22.0C
THERMAL hotspot=yes dx=+12.3deg dy=-5.1deg max=180.2C bg=22.4C blob=6px dist=n/a
```

| Field | Meaning |
|---|---|
| `hotspot` | `yes` once heat is confirmed (debounced) |
| `dx` | Degrees **right** (+) or **left** (−) of the image centre, from the camera's point of view |
| `dy` | Degrees **above** (+) or **below** (−) the image centre |
| `max` | Smoothed hottest-pixel temperature |
| `bg` | Scene median ("room") temperature |
| `blob` | Size of the hot area in pixels |
| `dist` | Flat-ground distance estimate; `n/a` until configured (see tuning) |

A `hotspot=yes` line is also printed immediately when heat appears or clears.

### Step-by-step test

| Step | Action | Expected result |
|---|---|---|
| 1 | Wire the camera as in section 1. Turn the **motor power supply off** for the first run. | — |
| 2 | Upload `arduino-source/main/main.ino` (needs your `secrets.h`). Open the Serial Monitor at **115200**. | Slider homing runs as usual. `THERMAL camera=ok ...` appears, then `THERMAL hotspot=no max=... bg=...` once per second with room temperatures. |
| 3 | Hold your hand in front of the camera. | Still `hotspot=no` (a hand is below 50 °C), but `max` rises to ~33–36 °C. |
| 4 | Hold something **hot** in view: a mug of just-boiled water, a lit candle, or a lighter flame (carefully, ~0.5–1 m away). | Within ~1 s: `hotspot=yes` with `max` well above 50 °C. |
| 5 | **Direction check.** Stand behind the camera, looking the way it looks. Move the hot object to **your right**, then **your left**, then **up**, then **down**. | Right → `dx` positive; left → `dx` negative; up → `dy` positive; down → `dy` negative. Centred → both near 0. |
| 6 | **Flicker check.** Hold a candle or lighter still in view for ~10 s. | `hotspot` stays `yes` the whole time; `dx`/`dy` change by only a degree or two per line. |
| 7 | Remove the hot object. | `hotspot=no` after ~2 s. |
| 8 | Turn the motor power back on. Run the slider, pan, tilt, and Patrol mode from the encoder and the website while the camera runs. | Motors move as smoothly as before; THERMAL lines keep printing. |
| 9 | Unplug the camera's SDA wire for ~5 s, then plug it back in. | `THERMAL camera=lost ...`, motors unaffected; within ~10 s of reconnecting, `THERMAL camera=ok` again. |

**If step 5 is reversed** (e.g. right shows negative), toggle `kFlipX` (or `kFlipY` for up/down) in `ThermalCamera.h` and re-upload. The default `kFlipX = true` already corrects the module's mirrored raw image.

**Tip:** do the direction check from **behind** the camera. Facing the camera reverses left and right from your point of view.

### Tuning (`arduino-source/main/ThermalCamera.h`)

| Constant | Default | When to change |
|---|---|---|
| `kHotspotMinC` | 50 | Lower **temporarily** (e.g. 30) to test detection with a hand; never leave it below skin temperature for real use |
| `kMinRiseC` | 15 | Raise if sun-warmed surfaces trigger detection |
| `kFramesToDetect` / `kFramesToClear` | 3 / 8 | Faster or slower on/off response |
| `kSmoothing` | 0.4 | Lower = steadier but slower to follow; higher = faster but jumpier |
| `kFlipX` / `kFlipY` | **true** / false | Direction check (step 5) is reversed. `kFlipX` is `true` because the module's raw image is mirrored left-right (confirmed on hardware); change only if you mount the camera differently |
| `kCameraHeightM`, `kTiltLevelDeg`, `kTiltUpIsPositive` | 0, 0, true | Enables the experimental flat-ground distance estimate: camera height above ground, the tilt reading when the camera points level, and whether positive tilt points up |

### What was validated where (Phase 2)

| Check | Done in software | Needs your hardware |
|---|---|---|
| Main firmware compiles for Arduino Nano ESP32 with the camera code; no warnings from project code; 29% flash, 22% RAM | ✅ | |
| Detection logic on synthetic frames (host test): empty room and a 34 °C person not detected; 3-frame detect / 8-frame clear; right/up give +dx/+dy; centred blob ≈ 0°; NaN pixels tolerated | ✅ | |
| Flicker: flame jittering ±5° per frame → smoothed output moves < 1 pixel per frame, detection never drops | ✅ | |
| Camera detected inside the main firmware; THERMAL lines print | | ✅ step 2 |
| Real hot object detected; direction signs match the mounting | | ✅ steps 4–5 |
| Real flame flicker stays stable | | ✅ step 6 |
| Motors unaffected while the camera runs | | ✅ step 8 |
| Camera unplug / replug recovery | | ✅ step 9 |

### Notes
- The Chrome viewer does **not** work with the main firmware (it only prints status lines, not full images). To see the full live image, upload the Phase 1 test sketch.
- The distance estimate assumes flat ground and needs calibration; it stays `n/a` until configured. A more general estimate (triangulating from two slider positions) belongs with automatic aiming (#52).

## 10. Auto-aim

Auto-aim moves the arm so a confirmed hotspot ends up in the **centre** of the thermal image, which means the nozzle points at it. Tracking issue: shauryarchs/embersensor-site#52.

**On at every boot** (shauryarchs/embersensor-site#56). Once you finish the boot pan/tilt setup screens and the device is at the menu, the arm aims at any confirmed hotspot by itself. Type `aim off` in the Serial Monitor to disable it until the next reboot. Auto-aim itself never switches water; spraying is section 11 (on at boot).

**Starting position:** after homing (at boot and after any re-home), the slider carriage parks at the middle of the rail (490 mm), so the camera's 110° view covers both sides of the rail before aiming starts (shauryarchs/embersensor-site#58).

**What counts as a hotspot:** ≥ 50 °C and ≥ 15 °C above the scene. People never trigger it, but grills, car engines, and sun-baked surfaces can. To go back to requiring `aim on` after every boot, set `AutoAim::kEnabledAtBoot = false`.

### How it aims
1. **Left/right: slide first.** The carriage slides along the rail toward the hotspot, always in the fixed direction set by `kSlideSign` (it never reverses on its own). The first slide is a short probe (50 mm). How far the hotspot moved per mm tells the firmware how far to slide next, and gives a **distance estimate**. If a probe barely moves the view, the next one is bigger (100 mm, then up to 300 mm).
2. **Pan only if needed:** when the carriage reaches the end of the rail (limit switch at the home end, 975 mm soft limit at the far end), or when even a long slide barely changes the view (a fire roughly 15 m+ away).
   - **Pan returns to 0° before sliding again.** If pan is turned (e.g. it aimed at a fire beyond the end of the rail) and the next target can be reached by sliding, pan first goes back to 0°, then the carriage slides. The decision uses the hotspot's angle from straight out (pan angle + `dx`), so it doesn't flip-flop. If returning pan would put the hotspot outside the camera's view (more than ~50° to the side), it slides first and returns pan once the hotspot is closer.
3. **Up/down: tilt.**
4. After every move it stops, waits for 3 fresh frames (~0.75 s), and looks again. Once the hotspot is within about one pixel of centre it holds still (`AIM on-target ...`).

### Safety
- **Only at the menu.** Entering Motor 1/2/3 or Patrol mode, turning or pressing the encoder, or any command from the website stops auto-aim's motion immediately and pauses it for 5 s. Your controls always win.
- **Limits:** every move goes through the motors' existing limits: slider limit switch and 5–975 mm, pan ±105°, **tilt never more than 360° either way from the boot zero** (cable protection).
- **Wrong-way check (pan / tilt only):** if a pan or tilt move makes the error **bigger**, that axis is reversed once and the firmware prints which constant to change. A second wrong-way move just **pauses** for 5 s (usually a moving flame). Returning pan to 0° is excluded from this check. Slide direction is fixed and never reversed.
- **Never turns itself off.** Only `aim off` turns auto-aim off, and it comes back on at the next boot.
- Gives up after 25 steps, or when a limit stops it from centring, and pauses instead of hunting forever.

### Commands (Serial Monitor, 115200 baud)

| Command | Effect |
|---|---|
| `aim on` | Re-enable auto-aim after `aim off` (it's already on at boot) |
| `aim off` | Disable and stop any auto-aim motion until the next reboot |
| `aim status` | Print state, whether it's at the menu (`atMenu`), positions, current dx/dy, learned directions. If `atMenu=no`, finish the boot pan/tilt setup or leave the motor mode |

### Output

```
AIM on at boot. Acts only at the menu; any input pauses it. Type 'aim off' to disable until reboot.
AIM hotspot acquired
AIM step 1: slide+50mm pan+0.0deg tilt+0.0deg (dx=+30.9 dy=-0.5)
AIM small slide effect, probing 100mm next
AIM slide effect -0.010 deg/mm, distance ~4.2m (est)
AIM slider reached its limit
AIM step 5: slide+0mm pan+10.0deg tilt+0.0deg (dx=+26.8 dy=-0.3)
AIM on-target slider=975mm pan=25.1deg tilt=0.0deg dist=~6.4m(est)
AIM step 6: returning pan to 0deg before sliding (pan-25.1deg tilt+0.0deg)
AIM paused (user input)
AIM lost, back to waiting
```

### Before the first test
- The camera must be **mounted on the arm** (section 3) so it moves with the slider, pan, and tilt. It can be temporarily clamped for testing.
- At the boot **pan-setup screen**, set pan 0° so the camera faces **straight out, perpendicular to the rail**. Sliding only shifts the view sideways in that position.
- At the **tilt-setup screen**, set tilt 0° roughly level.
- Clear the rail and the arm's swing area.

### Step-by-step test (needs the mounted camera)

| Step | Action | Expected result |
|---|---|---|
| 1 | Upload `main.ino`, open the Serial Monitor, finish the boot pan/tilt setup, stay at the menu. | `AIM on at boot ...` and `THERMAL camera=ok ...`. Nothing moves during the setup screens. |
| 2 | Put a safe heat source (mug of just-boiled water, candle in a holder) ~1–2 m in front, **off to one side**. (No command needed — it's already on.) | `AIM hotspot acquired`, then `AIM step` lines with **slide** moves; the carriage slides toward the heat source. |
| 3 | Watch until it settles. | `AIM on-target ...`; `THERMAL ... dx` near 0. Pan stays near 0° if the source is within the rail's reach. |
| 4 | Move the heat source **beyond the end of the rail** (further to that side). | Slider runs to the end (`AIM slider reached its limit`), then **pan** finishes centring. |
| 5 | Raise the heat source higher than the camera. | **Tilt** moves up; `dy` ends near 0. |
| 6 | While it's moving, turn the encoder one click. | Motion stops immediately; `AIM paused (user input)`; it resumes ~5 s later. |
| 7 | Enter Motor 1 mode from the menu. | Auto-aim never moves anything while you're in a motor mode. |
| 8 | Remove the heat source. | After ~2 s `hotspot=no`, then `AIM hotspot not visible, holding position` and `AIM lost, back to waiting`. |
| 9 | Type `aim off`. | `AIM off`; nothing moves on its own. |

**If you see `AIM pan direction reversed` or `AIM tilt direction reversed`:** set the constant it names (`kPanSign` or `kTiltSign` in `AutoAim.h`) to that value and re-upload, so it is correct from the first move next time.

### Tuning (`arduino-source/main/AutoAim.h`)

| Constant | Default | Meaning |
|---|---|---|
| `kSlideSign` | **-1** | Fixed slide direction for a hotspot on the right; never reversed at runtime. −1 confirmed on hardware. Flip only if the camera is mounted facing the other way |
| `kPanSign` | **-1** | Pan direction vs. image direction. −1 confirmed on hardware; with +1 pan turned away from the hotspot |
| `kTiltSign` | 1 | Tilt direction vs. image direction (confirmed on hardware) |
| `aim::kDeadbandXDeg` / `kDeadbandYDeg` | 3.4 / 3.1 | "Close enough" (≈1 pixel) |
| `aim::kGain` | 0.7 | Fraction of the error corrected per step |
| `aim::kSlideProbeMm`, `kSlideMaxMm` | 50 / 300 | First probe slide; largest single slide |
| `aim::kPanZeroTolDeg` | 1 | Pan is returned to 0° before sliding if it's further off than this |
| `aim::kReturnPanMaxBearingDeg` | 50 | Don't return pan first if the hotspot would leave the camera's view; slide first |
| `kSlideSpeed`, `kPanTiltSpeed` | 18 / 6 | Movement speeds (same 0–20 scale as the encoder) |
| `kMaxStepsPerEngagement` | 25 | Steps before giving up |

### What was validated where (auto-aim)

| Check | Done in software | Needs your hardware |
|---|---|---|
| Firmware compiles; no warnings from project code; 30% flash, 22% RAM | ✅ | |
| Simulated rail + motors + thermal camera geometry, scenarios pass: on at boot (#56); fire within rail reach centred by sliding only (pan stays 0) with distance estimate; fire beyond rail → slider to end, then pan; distant fire (30 m) → pan; higher ground → tilt up; camera tilted 180° (looking behind) centres with pan inversion; reversed pan / tilt motor learned and named; reversed slide direction measured; tilt never past 360°; user input stops motion instantly; no motion outside the menu; lost hotspot handled; `aim off` | ✅ | |
| Real motor directions vs. image (constants) | | ✅ steps 2–5 |
| Real centring, settle time, no oscillation | | ✅ steps 3–5 |
| User override on real hardware | | ✅ steps 6–7 |

## 11. Water pump: spray when on target

shauryarchs/embersensor-site#53. The main Nano drives the pump kit's relay through an NPN transistor, and the pump runs from **its own 5 V supply**. This is the setup validated on a spare Nano on branch `shaurya/test-pump-nano` (`PUMP_NANO_TRANSISTOR.md` there): the loop and on/off tests passed, and the Nano didn't drop USB.

**Automatic spraying is ON at every boot** (owner sign-off after hardware validation). Type `spray off` in the Serial Monitor to disable it until the next reboot, and `spray on` to re-enable it. To go back to requiring `spray on` after every boot, set `Sprayer::kAutoAtBoot = false`.

### Wiring

**A. Nano → transistor → relay** (transistor 2N2222A / PN2222, flat face toward you, legs down: **E – B – C**)

| From | To |
|---|---|
| Relay **VCC** | Nano **VBUS** (USB 5 V, so the Nano must be USB powered) |
| Relay **GND** | Nano **GND** |
| Relay **IN** | Transistor **collector** (right leg) |
| Transistor **emitter** (left leg) | Nano **GND** |
| Transistor **base** (middle leg) | 1 kΩ resistor → Nano **D2** |

**B. Pump circuit** (its own 5 V supply, e.g. a wall charger with a cut USB cable; no connection to the Nano)

| From | To |
|---|---|
| Supply **+5 V** | Relay **COM** |
| Relay **NO** | Pump **red (+)** |
| Pump **black (−)** | Supply **−** |
| 100 µF capacitor | Across supply + and − (stripe to −) |

D2 HIGH turns the transistor on, which pulls the relay's IN to GND: the relay turns on and the pump runs. D2 LOW leaves IN floating: relay off. The pump is also off while the Nano boots or is unplugged. Keep the pump wiring away from the Nano and the thermal camera wires. Never use a 9 V or 12 V supply for a 3–5 V pump. A flyback diode (1N4007 / 1N5819) across the pump, striped end to +, is recommended.

### How it decides

1. Auto-aim centres on a hotspot and prints `AIM on-target ...`. That's the cue: with spraying enabled (the default), the pump starts (`SPRAY pump on (aim on target, ...)`).
2. It keeps spraying while auto-aim stays engaged with the target, including small re-aim corrections if the flame moves.
3. It stops when:
   - the hotspot disappears (fire out or out of view);
   - auto-aim pauses or leaves the menu, or you type `aim off` / `spray off`;
   - **any user input** (encoder or website command);
   - it has sprayed **60 s** non-stop. It then waits 10 s before it may spray again.

### Serial commands

| Command | Effect |
|---|---|
| `spray on` | Re-enable automatic spraying after `spray off` (it's already on at boot) |
| `spray off` | Disable it until reboot (stops an automatic spray) |
| `spray status` | Auto on/off, pump state, aim on-target, cooldown |
| `pump on` / `pump on <s>` | Manual test run, 3 s or 1–30 s |
| `pump off` | Stop the pump now |

### Step-by-step test

| # | Do | Expected |
|---|---|---|
| 1 | Upload `main.ino`, open the Serial Monitor. Pump in water, pump supply plugged in. | `SPRAY auto on at boot ...`; relay light off; pump off through boot homing and setup (auto-aim only acts at the menu). |
| 2 | Finish the pan/tilt setup, stay at the menu. Type `pump on`. | Relay clicks, pump runs 3 s, `SPRAY pump off (timer ...)`. |
| 3 | Type `spray off`, then put a safe heat source in view. | Auto-aim centres (`AIM on-target`), **no water**. |
| 4 | Type `spray on`. | Pump starts within a moment: `SPRAY pump on (aim on target ...)`. |
| 5 | Turn the encoder one click. | Pump stops: `SPRAY pump off (user input ...)`. Auto-aim resumes ~5 s later and the pump restarts once it's on target again. |
| 6 | Remove the heat source. | Pump stops: `SPRAY pump off (target lost or aim paused ...)`. |
| 7 | Leave it spraying on target for over a minute. | Stops at 60 s, `SPRAY cooling down 10s`, then restarts if still on target. |
| 8 | Type `spray off`. | Pump stops and doesn't restart on target. |

**Point the nozzle and the heat source somewhere safe to get wet,** and keep water off the electronics and the thermal camera (it isn't waterproof).

### Tuning (`arduino-source/main/Sprayer.h`)

| Constant | Default | Meaning |
|---|---|---|
| `kAutoAtBoot` | **true** | Spray on target from boot (owner sign-off after hardware validation). false = require `spray on` after every boot |
| `kMaxSprayMs` | 60000 | Longest continuous spray |
| `kCooldownMs` | 10000 | Pause after a max-length spray |
| `kManualDefaultS` / `kManualMaxS` | 3 / 30 | `pump on` run length |

### What was validated where (spray)

| Check | Done in software | Needs your hardware |
|---|---|---|
| Firmware compiles; no warnings from project code; 30% flash, 22% RAM | ✅ | |
| Simulated against a stand-in auto-aim, all pass: pump off at `begin()`; no spray on target until `spray on`; sprays on target; keeps spraying through re-aim; stops on target lost, user input, `spray off`; 60 s limit and 10 s cooldown; manual `pump on` timer, refusal over 30 s, `pump off`; aim commands left to auto-aim; pin re-applied every loop | ✅ | |
| Nano + transistor drives the relay; pump on its own supply; no USB drops | ✅ on a spare Nano (`shaurya/test-pump-nano`) | Re-check on the main board |
| Steps 1–8 above (spray on target, stops on input / target lost) | | ✅ Owner: "spray feature worked amazing" |

