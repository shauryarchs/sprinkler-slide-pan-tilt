# Main board parts list

All parts are through-hole and hand-soldered. "Have" means the part already runs on the breadboard rig.

| Ref | Part | Qty | Have / buy | Notes |
|---|---|---|---|---|
| U1 | Arduino Nano ESP32 (ABX00083) | 1 | Have | Solder in, or use 2x 1x15 female headers (2.54 mm) to keep it removable |
| U2-U4 | TMC2209 V2.0 StepStick (Teyleten) | 3 | Have | 6x 1x8 female headers (2.54 mm) recommended so drivers can be swapped and Vref adjusted |
| J7 | 0.96 in SSD1306 OLED (Hosyond) | 1 | Have | Solders in by its own pins; foam tape under the far edge |
| J5 | KY-040 encoder (WMYCONGCONG) | 1 | Have | Pins bent straight; solders in by its own pins; foam tape under the far edge |
| C1 | 100 uF 50 V electrolytic | 1 | Have | 8 mm body, 3.5 mm lead spacing; long lead (+) to the square pad |
| R1 | 1 kOhm resistor | 1 | Have | |
| Q1 | 2N2222A NPN (TO-92) | 1 | Have | Holes are E, B, C left to right with the flat face toward the board's bottom edge. Check your transistor's markings: some TO-92 2N2222A parts are C, B, E |
| - | Relay module (AEDIKO, SRD-05VDC-SL-C) | 1 | Have | Foam-taped in the marked area; VCC/GND/IN wires to J9, NO/COM wires to J10 |
| D1 | 1N5408 diode | 1 | Buy | Stripe (cathode) to the square pad marked K |
| F1 | Keystone 3517 5x20 mm fuse clip | 2 | Buy | Use this exact clip (DigiKey 36-3517-ND); other clips have different pin spacing |
| F1 | 5 A fuse, 5x20 mm | 1 + spares | Buy | Fast-blow glass |
| J1, J11 | Wires soldered direct | - | - | 12 V jack leads and solenoid wires solder into the 1.3 mm holes; the screw-terminal footprint is only the hole pattern |
| J2-J4 | Wires soldered direct | - | - | Motor jumper wires solder into the 1.3 mm holes; unplug the motors at the jumper joints |
| J6 | Wires soldered direct | - | - | Limit switch jumper wires solder into the 1.0 mm holes; unplug at the halfway extension joint |
| J8 | Wires soldered direct | - | - | Thermal camera jumper wires solder into the 0.95 mm holes; unplug at the halfway extension joint |
| J9 | Solder pads | - | - | Relay VCC, GND, IN wires solder straight in |
| J10 | Solder pads | - | - | Relay NO, COM wires solder straight in |
| H1-H4 | M3 standoffs and screws | 4 | Optional | Board corners |

Only the fuse, the fuse clips, and the diode need to be bought. Every wire soldered straight to the board
needs strain relief (a zip tie or hot glue near the joint) so flexing doesn't break it at the solder. The
12 V input wires carry up to about 4 A, so use 18 AWG or thicker there, not thin jumper wire.

## Board order settings

Upload `main-board-gerbers.zip`. Settings: 2 layers, 190 x 102 mm, 1.6 mm FR-4, 1 oz copper
(2 oz is better for the 12 V traces if it costs little extra), HASL finish, any solder mask color.
