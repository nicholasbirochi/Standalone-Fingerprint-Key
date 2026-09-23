# Wiring

Six wires. The pin numbers are the defaults in
[`firmware/include/config.h`](../firmware/include/config.h); change them there
if your board routes something differently.

```
          ESP32-S3 board                   HLK-ZW111
       +-------------------+            +--------------+
       |          3V3      |------------| VCC   3.3 V  |
       |          GND      |------------| GND          |
       |       GPIO 4 (RX) |<-----------| TX           |
       |       GPIO 5 (TX) |----------->| RX           |
       |       GPIO 6      |<-----------| TOUCH / WAKE |
       |       GPIO 7      |----------->| VT (enable)  |
       |                   |            +--------------+
       | [USB-C] [USB-C]   |   native port and UART bridge;
       |  native  uart     |   only the native one gets an opening
       +-------------------+
              |
              '--- to the Mac
```

| Signal | ESP32 pin | Direction | Purpose |
| --- | --- | --- | --- |
| `VCC` | 3V3 | -- | **3.3 V only.** 5 V destroys the module. |
| `GND` | GND | -- | Common ground |
| `TX` | GPIO 4 | module to ESP32 | UART receive. This is the crossover people get wrong. |
| `RX` | GPIO 5 | ESP32 to module | UART transmit |
| `TOUCH` | GPIO 6 | module to ESP32 | Goes high while a finger rests on the platen |
| `VT` | GPIO 7 | ESP32 to module | Enables the module's internal regulator |

## The two that are optional

**TOUCH** lets the firmware sleep until a finger actually arrives. Without it,
set `kSensorTouchPin` to `-1` and the firmware polls `GetImage` continuously
instead -- functionally identical, roughly 15 mA more idle draw, and the module
stays warm.

**VT** gates the sensor's own regulator. Without it, tie the module's VT to 3V3
and set `kSensorPowerPin` to `-1`. You lose the ability to power-cycle the sensor
from firmware, which is occasionally useful when it wedges.

## Things that will cost you an evening

- **3.3 V, not 5 V.** The ZW111 has no input protection worth relying on.
- **TX to RX, RX to TX.** If `status` says the sensor is not responding and the
  wiring looks right, swap these before anything else.
- **Baud rate.** The module's default is 57600, and it is settable -- a module
  someone else configured may be at 9600 or 115200.
- **Check the pigtail pinout against your module, not against this diagram.**
  Vendors ship the same connector with different orderings, and reversing VCC
  and GND ends the module immediately.
- **USB-C data cable.** A charge-only cable gives you a board that powers up,
  blinks, and enumerates nothing.

## Assembling into the shell

Full sequence in [docs/assembly.md](../docs/assembly.md). The short version:
solder everything and test it on the bench *before* the parts go into the
enclosure, because the friction-fit lid is easy to open but the wiring is not
easy to reach once the board is on its posts.
