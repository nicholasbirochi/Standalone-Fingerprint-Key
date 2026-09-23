# Bench sketches

Two minimal Arduino sketches that prove the platform before any of the real
firmware is involved. Both of these have been run on the actual board against
macOS; see [docs/validation.md](../../docs/validation.md) for what that did and
did not demonstrate.

Run them in this order. If either fails, the full firmware will fail too, and it
will be much harder to tell why.

| Sketch | Proves |
| --- | --- |
| [`serial_hello/serial_hello.ino`](serial_hello/serial_hello.ino) | The board enumerates, the toolchain works, and the serial monitor is talking to it |
| [`hid_keyboard/hid_keyboard.ino`](hid_keyboard/hid_keyboard.ino) | The board can act as a USB keyboard on this Mac, which is the entire premise of the project |

## Arduino IDE settings

These matter more than the code does.

| Setting | Value |
| --- | --- |
| Board | ESP32S3 Dev Module |
| USB CDC On Boot | Enabled |
| USB Mode | USB-OTG (TinyUSB) |
| Upload speed | 921600 |
| Monitor baud | 115200 |

**USB Mode is the one people get wrong.** The default, "Hardware CDC and JTAG",
gives you a working serial monitor and a keyboard that silently does nothing:
the HID interface needs the TinyUSB stack. The PlatformIO build in
[`../platformio.ini`](../platformio.ini) sets the equivalent flags
(`ARDUINO_USB_MODE=0`, `ARDUINO_USB_CDC_ON_BOOT=1`) so the real firmware does
not depend on remembering this.

**Plug into the native USB port**, not the UART bridge. The board has two USB-C
connectors and only the native one can present a keyboard.

## Safety

`hid_keyboard` types into whatever window has focus. Test it with a plain text
editor focused -- never a password field or a terminal.
