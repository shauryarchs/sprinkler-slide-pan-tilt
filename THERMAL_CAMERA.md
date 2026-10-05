# Thermal camera setup (MLX90640-D110)

This guide connects a **Waveshare MLX90640-D110** thermal camera to the **Arduino Nano ESP32**.

- **Phase 1, USB view (sections 1–8):** a standalone test sketch streams the live raw thermal image to a Chrome viewer. Use it to check wiring and see the full image.
- **Phase 2, main firmware (section 9):** the camera runs inside the motor firmware and reports hotspots (direction, temperature) to the Serial Monitor while the motors work.

Nothing is sent to the EmberSensor Worker, website, or iOS app in either phase, and the camera never moves a motor or switches water.

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
