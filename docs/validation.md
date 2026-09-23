# What has actually been tested

This project is a prototype. It is easy, when a USB keyboard test works, to feel
like the device works. It does not: a keyboard that types a phrase into a text
editor and a device that authenticates you are separated by everything that
matters.

This page is the honest ledger. Nothing moves from the second table to the first
without evidence.

## Demonstrated on hardware

| # | What | How it was shown |
| --- | --- | --- |
| 1 | macOS enumerates the ESP32-S3 as a serial device | Port appeared as `/dev/cu.usbmodem101` in the Arduino IDE |
| 2 | The board runs code and the toolchain works | `ESP32S3 Dev Module` selected, [`bench/serial_hello`](../firmware/bench/serial_hello/serial_hello.ino) printing at 115200 |
| 3 | The board can act as a USB keyboard on this Mac | [`bench/hid_keyboard`](../firmware/bench/hid_keyboard/hid_keyboard.ino) typed a line into Notes on a BOOT press |

Point 3 is the load-bearing one. It is what makes the whole "no driver, no
daemon" premise credible, and it was checked before any of this firmware
existed.

## Verified on a host

| What | Result |
| --- | --- |
| The protocol tests | 11 of 11 pass |
| The framing library | Compiles clean under `-Wall -Wextra` |
| The full firmware | Compiles and links clean under `-Wall -Wextra` |

The firmware check was done against stubbed core headers -- `Arduino.h`,
`Preferences.h`, `USB.h`, `USBHIDKeyboard.h` -- written to match the real
signatures. That catches syntax errors, type errors, and anything declared but
never defined, which is most of what goes wrong in code nobody has compiled.

It is **not** the same as building against the real ESP32 core. A signature that
differs from the stub would still fail there. `pio run -e esp32s3` in CI is the
authoritative answer, and it has not run yet because nothing has been pushed.

## Not demonstrated

| What | Status |
| --- | --- |
| ESP32-S3 talking to the HLK-ZW111 over UART | Never attempted; the sensor was still in the post |
| Fingerprint enrolment | Never attempted |
| Matching a finger | Never attempted |
| Typing into the macOS login window | Never attempted |
| Unlocking a Mac with the lid closed | Never attempted |
| Behaviour after a reboot or a FileVault prompt | Never attempted |
| The firmware running on the actual chip | Never flashed |
| Physical fit of the printed enclosure | Never printed |

## What the enclosure checks do and do not prove

`python enclosure/build.py` verifies that the model is internally consistent --
the retaining lip overlaps the module, the seat clears the pigtail, the USB
opening stays inside the wall -- and the STLs it writes are watertight, manifold
and consistently wound.

None of that is a fit test. The mesh being closed says nothing about whether a
USB-C plug reaches through a 1.8 mm wall, whether the lid seats on your printer's
tolerances, or whether the opening lines up with the port you actually have.

Before paying for a final print, print the USB end and the sensor ring alone and
check them against the real parts.

## The open assumption

`board.usb_offset_from_center` is 4.7 mm. That number came from a CAD file, not
from a dimensioned drawing of the board, and it decides **which of the two USB-C
connectors ends up exposed**. Get it wrong and the enclosure opens onto the UART
bridge instead of the native port, which is the one that can present a keyboard.

Measure it before printing. Everything else in `params.json` is a measured
component dimension; this one is a guess that has never been checked.
