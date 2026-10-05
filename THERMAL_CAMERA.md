# Thermal camera setup (MLX90640-D110), Phase 1: USB view

This guide connects a **Waveshare MLX90640-D110** thermal camera to the **Arduino Nano ESP32** and shows the live raw thermal image on a computer over USB. Nothing is sent to the EmberSensor Worker, website, or iOS app, and the motor firmware is not changed.

Tracking issue: shauryarchs/embersensor-site#46

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

**Notes for the later mounting phase** (on or near the sprinkler slider and pan/tilt arm):

- **On the tilt arm next to the nozzle, aligned with it:** the image then shows where the sprinkler is aimed. Readings can be tagged with slider, pan, and tilt position.
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

- **Pull-ups:** I assume the module has onboard I2C pull-ups. Waveshare's wiring uses none, but this is not confirmed from the schematic.
- **Orientation:** which way is "up" depends on how the module is held. Use the viewer's Flip/Rotate controls.
- **Resolution:** 32 × 24 pixels shows heat shapes, not detail. Small or distant hot spots blend into neighbouring pixels.
- **Display only:** thermal data is not connected to fire risk, the FireGuard sprinkler trigger, or motor movement. Any automatic action stays disabled until thermal detection has been reviewed and validated.
- **Later phases:** adding the camera to the main firmware, a Wi-Fi viewer, and website/iOS upload are tracked in shauryarchs/embersensor-site#46 but not part of this phase.
