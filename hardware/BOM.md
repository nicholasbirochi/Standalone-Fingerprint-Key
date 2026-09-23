# Bill of materials

| Qty | Part | Notes |
| --- | --- | --- |
| 1 | ESP32-S3 dev board, 25 x 18 x 8 mm | Dual USB-C: the native port and a UART bridge. **Native USB is mandatory** -- an S2 works, an ESP32 classic or a C3 does not, because neither has the USB peripheral that HID needs. |
| 1 | HLK-ZW111 fingerprint module | Round capacitive, body 21 x 5 mm, reading face 15 mm. UART, on-module matching. The ZW101 is pin- and protocol-compatible. |
| 1 | JST or FPC pigtail for the module | Usually supplied with it. Check the pinout against yours; the ordering is not standardised. |
| 1 | Printed base | [enclosure/stl/base.stl](../enclosure/stl/base.stl) |
| 1 | Printed lid | [enclosure/stl/lid.stl](../enclosure/stl/lid.stl) |
| ~6 | 30 AWG silicone wire | Stranded. Solid core will fatigue where the lid closes on it. |
| 1 | USB-C cable | Data, not charge-only. This trips people up more than it should. |

Optional:

| Qty | Part | Why |
| --- | --- | --- |
| 1 | 100 uF electrolytic across the sensor's 3V3 | The module draws a current spike when it lights its ring; on a long USB cable that can brown out the ESP32. |
| 1 | Small square of foam | Under the sensor, if your module is at the thin end of tolerance and rattles in the seat. |

## Notes on the board

The enclosure is cut for a board measuring 25 x 18 x 8 mm with two USB-C
connectors on one short edge. Only the native port gets an opening; the UART
bridge stays behind the wall, which is deliberate -- it is the port that can
reflash the device.

If your board is a different size, the three numbers that matter are
`board.length`, `board.width` and `board.height` in
[`enclosure/params.json`](../enclosure/params.json). The height is the one to
measure carefully: it sets the height of the entire enclosure.

## Notes on the sensor

The HLK-ZW111 does its own matching and stores its own templates. That is the
reason to choose it over a bare imaging sensor: no fingerprint image and no
template ever crosses the UART, so a firmware bug cannot leak one.

It ships at 57600 baud with a module password of `0x00000000`. Both are settable
and both are checked at startup -- if `status` reports the sensor is not
responding, mismatched baud is the first thing to check, mismatched password the
second.

## What this project deliberately does not use

No battery, no Bluetooth, no Wi-Fi. The device is powered by the port it is
plugged into and speaks only USB HID. That is not minimalism for its own sake:
every radio is a way for the stored secret to leave the device, and it does not
need one.
