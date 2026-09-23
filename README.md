# Standalone-Fingerprint-Key

DIY standalone fingerprint key for macOS, built with an ESP32-S3 and an HLK-ZW111
capacitive sensor, featuring USB HID integration and a custom 3D-printed enclosure.

46.4 x 46.4 x 13.0 mm, two printed parts, no screws.

<img src="docs/img/spec-sheet.png" alt="Technical sheet for the enclosure: assembled, exploded, orthographic and section views, with the height budget, bill of materials and print settings" width="900">

The device enumerates as an ordinary USB keyboard. Touch the sensor, and on a
match it wakes the Mac and types a stored secret. There is no driver to install,
no background daemon, and no permission to grant: from the host's point of view
somebody typed the password very quickly.

That is the whole design, and it is also the whole limitation. Read
[docs/security.md](docs/security.md) before you put a real password on one of
these.

---

## What is in here

| Path | What it is |
| --- | --- |
| [firmware/](firmware/) | PlatformIO / Arduino firmware for the ESP32-S3 |
| [firmware/bench/](firmware/bench/) | Two minimal sketches that prove the platform first |
| [firmware/lib/zw111_protocol/](firmware/lib/zw111_protocol/) | Packet framing for the sensor, with no Arduino dependency |
| [firmware/test/](firmware/test/) | Host-side unit tests for that framing |
| [enclosure/](enclosure/) | Parametric enclosure model and the exported STLs |
| [hardware/](hardware/) | Bill of materials and wiring |
| [docs/](docs/) | Assembly, macOS setup, protocol notes, threat model, test ledger |
| [tools/](tools/) | Renderers that produce every image in these docs |

## How it works

```
   finger
     |
  HLK-ZW111  --- UART 57600 ---.
  (matching happens             |
   on the module;          ESP32-S3  --- USB HID keyboard ---> macOS
   templates never              |
   leave it)              USB CDC console
                                |
                         enrol / set secret
```

Fingerprint matching runs entirely on the sensor module. The ESP32 never sees an
image or a template; it sends `GetImage`, `GenChar` and `Search`, and gets back a
slot number and a match score. What the ESP32 *does* hold is the secret to type,
which is the part worth thinking about.

## Build the firmware

```sh
cd firmware
pio run -e esp32s3 -t upload
pio device monitor
```

Then, on the console that opens:

```
fpkey> enroll
fpkey> secret throwaway-for-now
fpkey> status
```

`pio test -e native` runs the protocol tests on your own machine, no board
attached. They cover the framing, the checksum and the parser's resynchronisation
behaviour -- the things that turn into a silent, unhelpful timeout when they are
wrong.

## Test before printing

You do not need the 3D-printed enclosure for the first prototype. Wire the board
and sensor on the bench, enrol a finger, set a throwaway secret, and use
`fpkey> test` in a text editor before trusting it near the macOS login screen.

Before any of that, run the two sketches in
[firmware/bench/](firmware/bench/): one proves the board enumerates, the other
proves this Mac accepts it as a keyboard. They take two minutes and they are the
difference between debugging one thing and debugging four.

[docs/assembly.md](docs/assembly.md) starts with that bench flow. Once it works,
either print the included enclosure or use a small plastic electronics box with
a hole for the sensor and a slot for USB-C.

## Print the enclosure

The STLs are committed, so printing needs nothing but a slicer:

- [enclosure/stl/base.stl](enclosure/stl/base.stl)
- [enclosure/stl/lid.stl](enclosure/stl/lid.stl)

Two parts, **46.4 x 46.4 x 13.0 mm** assembled. Both print flat on the bed with
no supports: 0.2 mm layers, 3 perimeters, 25% infill. The lid is a friction fit
-- there are no screws, and no separate divider.

46.4 mm is the smallest square these parts fit in. A rectangular version is a
flag away if you would rather have a stick than a puck:

```sh
python enclosure/build.py --no-square --board-long-axis x   # 53.4 x 28.2 x 13.0
```

[docs/enclosure.md](docs/enclosure.md#footprint-options) has the comparison.

To change a dimension, edit [enclosure/params.json](enclosure/params.json) and
regenerate:

```sh
pip install manifold3d numpy
python enclosure/build.py
```

That prints the height budget and a set of sanity checks (does the sensor seat
leave room for the ribbon cable, does the retaining lip still overlap the module,
is the wall above the lid ledge still printable) before it writes anything.

### About the height

The first revision stacked the sensor on top of the board and came out 18 mm
tall. Run the same parts through this model stacked and it still says 18.00 mm,
so that is where those millimetres were going. Side by side:

```
floor                         1.60
standoff under the board      1.20
board envelope                8.00
clearance over the board      0.40
lid                           1.80
                            ------
external height              13.00 mm      46.4 x 46.4 mm footprint
```

Two things changed. The height is 13.0 mm instead of 18, and the lid went from
6.4 mm deep to a flat 1.8 mm plate, because it no longer has to contain the
sensor -- the sensor now sits on the floor next to the board and the lid is
just a plate with a hole in it.

The stack is now almost entirely the ESP32-S3: **8 of those 13 mm are the board
module itself**. Nothing in the enclosure will beat that until the board gets
shorter. [docs/enclosure.md](docs/enclosure.md) works through what that would
take.

## Status

**Prototype. Biometric authentication has not been tested.**

What has been shown on the actual board and Mac:

- It enumerates over USB and runs code -- [`firmware/bench/serial_hello`](firmware/bench/serial_hello/serial_hello.ino).
- It works as a USB keyboard, typing into Notes on a button press --
  [`firmware/bench/hid_keyboard`](firmware/bench/hid_keyboard/hid_keyboard.ino).
  That is the premise of the whole project, and it holds.

What has been verified on a host:

- The protocol tests pass, 11 of 11.
- The firmware compiles and links clean under `-Wall -Wextra`, against stubbed
  core headers. Not the same as building against the real ESP32 core -- that is
  what CI is for -- but it rules out the errors that live in unbuilt code.

What has not:

- The ESP32 has never talked to the HLK-ZW111. No enrolment, no match, nothing.
- The firmware has never been flashed to the chip.
- The enclosure has not been printed. The STLs are watertight, manifold and
  consistently wound, which is a statement about the mesh and not about fit.
- `board.usb_offset_from_center` is the one number in `params.json` that is a
  guess rather than a measurement, and it decides which of the two USB-C ports
  ends up exposed.

A working USB keyboard is not a working fingerprint reader.
[docs/validation.md](docs/validation.md) keeps the full ledger, and CI covers the
build and the tests on every push --
see [.github/workflows/ci.yml](.github/workflows/ci.yml).

## Licence

MIT -- see [LICENSE](LICENSE).
