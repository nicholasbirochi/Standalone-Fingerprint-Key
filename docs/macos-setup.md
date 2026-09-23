# Using it with macOS

There is nothing to install. The device is a USB keyboard, macOS treats it as a
USB keyboard, and the "integration" consists of it typing your password into the
field that is already asking for one.

That simplicity buys a lot and costs a few specific things. They are all below.

## First plug-in: the keyboard setup assistant

macOS shows a *Keyboard Setup Assistant* the first time it sees a new keyboard,
asking you to press the key next to the left Shift. The device cannot answer it,
so dismiss the window. It does not come back for that device.

## Keyboard layout

The firmware sends HID usage codes, and macOS maps those through whatever input
source is active. The mapping in `USBHIDKeyboard` is US ANSI.

If your input source is ABNT2, or anything else, some characters will arrive
wrong -- `/`, `;`, `'`, `\` and the accented dead keys are the usual casualties.
Two ways out:

1. **Keep the secret to characters that map identically**: `a-z`, `A-Z`, `0-9`,
   `-`, `.`. Unexciting, and completely reliable.
2. **Add "U.S." as an input source** and make sure it is the one selected at the
   login window. System Settings > Keyboard > Text Input > Edit.

Always verify with `fpkey> test` into a text editor before trusting it against a
login field you cannot see.

## Waking the Mac

The firmware taps left Shift, waits, and then types. A modifier rather than a
character, so if the Mac was already awake with a text field focused, nothing is
inserted.

`kWakeToTypeDelayMs` in `config.h` is 450 ms. If characters go missing from the
start of the password, the login window had not finished drawing -- raise it.

## FileVault

At the FileVault pre-boot screen, macOS is running from EFI, not from macOS, and
USB HID support there is limited and machine-dependent. On many Macs an external
keyboard works at that screen; on some it does not, and this device is an
external keyboard like any other.

Practically: this unlocks the **login and lock screens** reliably. Treat the
FileVault pre-boot screen as untested, and never rely on this device being the
only way into a machine.

## What it cannot do

- **Not Touch ID.** It cannot authorise `sudo`, unlock the Keychain, approve
  System Settings changes, or confirm an Apple Pay prompt. Those go through
  Apple's Secure Enclave, and there is no way for a USB keyboard to reach them.
- **No feedback from the host.** The device cannot tell whether the Mac is
  locked, whether the password was accepted, or whether the field it typed into
  was a password field at all. It types and hopes.

The second point is the one with consequences. If you touch the sensor while a
chat window has focus, the device types your password into the chat window. The
cooldown limits the damage to one occurrence; `kConfirmWithButton` in `config.h`
removes it entirely at the cost of a button press per unlock.

## Keep a way in without it

macOS never stops accepting a typed password, so this device is additive: if it
is lost, broken or simply not in your pocket, you log in the way you always did.

That only holds if you still know the password. Do not let the device become the
only thing that remembers it.

## Locking

`fpkey> lock` sends Control-Command-Q, the macOS lock shortcut. It is there for
testing the HID path in the safe direction -- locking needs no secret.

## If nothing happens

| Symptom | Cause |
| --- | --- |
| No serial port appears | Charge-only USB cable, or `ARDUINO_USB_MODE` left at 1 |
| Console works, typing does nothing | HID not enumerated -- check `status` |
| Types into the console but not the login screen | Nothing focused the password field; touch the trackpad first |
| First characters missing | Raise `kWakeToTypeDelayMs` |
| Wrong characters | Keyboard layout, above |
