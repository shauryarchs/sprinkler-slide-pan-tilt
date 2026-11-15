# Main board build checklist

Steps to do while the board ships and once it arrives.

## 1. Buy the remaining parts

- [x] ***1N5408 diode (1)***: Diodes Inc 1N5408G-T, https://www.digikey.com/en/products/detail/diodes-incorporated/1N5408G-T/128597
- [x] ***Keystone 3517 5x20 mm fuse clips (2)***, this exact part since the holes are spaced for it: https://www.digikey.com/en/products/detail/keystone-electronics/3517/316010 (or Mouser 534-3517, https://www.mouser.com/ProductDetail/534-3517)
- [x] ***5 A fast-blow 5x20 mm glass fuses (3-5, for spares)***: Littelfuse 0217005.HXP, https://www.digikey.com/en/products/detail/littelfuse-inc/0217005-HXP/777111

Amazon alternatives: Keystone 3517 clips (50 pack) https://www.amazon.com/dp/B00LPOV3UG,
1N5408 (10 pack) https://www.amazon.com/dp/B07PCQD8YY or CHANZON (50 pack) https://www.amazon.com/dp/B079KCH1VC,
F5AL250V 5 A fast-blow fuses (10 pack) https://www.amazon.com/dp/B0HC6QDYDB or BOJACK (20 pack) https://www.amazon.com/dp/B07S8BSS37.
Don't substitute generic fuse clips: their pin spacing may not match the board's holes.

Chosen instead of clips: DIGITEN 5x20 mm inline screw-type fuse holder with 16 AWG wire (10 pack)
https://www.amazon.com/dp/B00VLBAF84. Cut both leads to 5-10 cm, strip and tin them, and solder one lead into a
hole of the left fuse-clip footprint (12 V input side) and the other into a hole of the right footprint (driver
and relay side). Leave the other two holes empty. Zip-tie the holder down so it doesn't dangle. For the
continuity check, "12 V after the fuse" is the right footprint's empty hole.

Everything else is already on the breadboard rig or soldered as bare wires. See `fab/parts-list.md`.

## 2. Solder ground joints hot enough (STEPS 1-4 IMPORTANT)

Ground holes connect solidly to the large ground fill, which pulls heat away from the iron.

- [x] ***C******************************************heck transistor pin order: E-B-C or C-B-E (its correct i checked)***********
- [x] ***DON'T use existing switch on power jack cause it blocks current with small jumpers jst directly solder jack to pcb board*** ***(use thicker wire)***
- [x] ***Make wires for relay, motor, input jack, and solenoid thick; recommend 18 AWG***
- [x] ***Ensure solder points of screen and encoder don't touch ground plane (maybe put tape underneath)***
- [x] ***Also remember look at bottom of this checklist for PROPER MOTOR labels (MOTOR LABELS INCORRECT)***
- [x] ***Make sure gnd solder joints hot enough***
- [ ] ********Use a chisel or flat tip (2-3 mm), clean and freshly tinned.
- [ ] For ground joints, set the iron to about 350-370 C (leaded solder) or 380-400 C (lead-free).
- [ ] Put a small blob of solder on the tip, hold it on the pad and pin for 2-3 s, then feed solder into the joint.
- [ ] If the solder hasn't flowed after about 10 s, stop, let it cool, and retry hotter.
- [ ] Every joint should be smooth, shiny and cone-shaped. Reheat any that are dull or balled up.
- [ ] Solder the female headers first, then plug in the Nano and drivers. Don't solder them in directly.
- [ ] When finished, inspect every joint under bright light for solder bridges.

## 3. Continuity check before the first power-up

Do this with everything soldered, USB unplugged, the 12 V adapter unplugged, and the Nano and drivers out of their sockets.

Set the multimeter to continuity (beep) mode and put the black probe on ground (the 12 V input's - hole). None of these should beep steadily:

- [x] 12 V input: the 12 V input's + hole
- [x] 12 V after the fuse: the fuse clip on the side away from the input, with a fuse fitted
- [x] 125 V: the Nano socket's VBUS pin, or any driver's VDD pin
- [x] 3.3 V: the Nano socket's 3V3 pin, or the OLED's VCC pin

A short chirp on 12 V that stops is just the 100 uF capacitor charging, which is normal. A steady beep means a short: don't power on. Find the bridge and fix it, then test again.

When all four are quiet, plug in the Nano and drivers and repeat the check. Then power up in stages:

1. USB only: the OLED lights and the encoder responds.
2. Add 12 V.
3. Test motors and the solenoid one at a time.

## Motor wire labels

The motor holes (J2 slider, J3 pan, J4 tilt) use the driver board's own silkscreen names (A1, A2, B1, B2).
The breadboard wiring followed a reference schematic that names the same driver pins M1A, M1B, M2A, M2B.
Both names refer to the same physical driver pins, so the wiring is unchanged. Only the names differ.

Holes left to right, with the Nano at the bottom of the board:

| Hole (left to right) | PCB label | Reference schematic name | Driver pin (from top of the motor-side row) |
|---|---|---|---|
| 1st | B2 | M2B | 6th: pin 11, just above VDD |
| 2nd | B1 | M2A | 5th: pin 12 |
| 3rd | A2 | M1B | 3rd: pin 14, just below GND |
| 4th | A1 | M1A | 4th: pin 13 |

The holes are not in the same order as the driver pins (or the breadboard header), so don't keep the
wires in their old left-to-right order. Instead, for each motor wire, note which driver pin it's on now
(M1B, M1A, M2A, M2B), then solder it into the hole with that reference name above.

Before moving the motor wires from the breadboard, note each motor's wire colours against M1A, M1B, M2A,
M2B. Swapping the two wires of one coil makes that motor run backwards. Mixing wires between coils stops
it from turning.

## If the fuse blows

The fuse only cuts the 12 V side, so the Nano, OLED and encoder keep working on USB. Signs: the motors turn
freely by hand and don't move when commanded, and the solenoid doesn't open (the relay may still click).

1. Unplug the 12 V adapter, unscrew the holder's cap and take the fuse out.
2. A good fuse has a continuous wire inside the glass. A blown one has a broken wire or a dark smudge.
   To be sure, check with the multimeter in continuity mode across the two end caps: beep means good.
3. Find the cause before fitting a spare: a pinched or frayed motor or solenoid wire, a solder bridge, a
   driver fitted the wrong way round, or the diode reversed (stripe away from K).
4. Replace it only with another 5 A fast-blow 5x20 mm fuse. Never fit a higher rating or bypass the fuse.
