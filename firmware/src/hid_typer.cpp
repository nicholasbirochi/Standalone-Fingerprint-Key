#include "hid_typer.h"

#include <USB.h>
#include <USBHIDKeyboard.h>

#include "config.h"

namespace fpkey {
namespace {

USBHIDKeyboard g_keyboard;

// Whether the host has the device configured. Written from the USB event task
// and read from the main loop, so it is volatile; it is a single bool, so no
// further synchronisation is warranted.
volatile bool g_host_present = false;

void OnUsbEvent(void* /*arg*/, esp_event_base_t base, int32_t id, void* /*data*/) {
  if (base != ARDUINO_USB_EVENTS) return;
  switch (id) {
    case ARDUINO_USB_STARTED_EVENT:
    case ARDUINO_USB_RESUME_EVENT:
      g_host_present = true;
      break;
    case ARDUINO_USB_STOPPED_EVENT:
    case ARDUINO_USB_SUSPEND_EVENT:
      // Suspend means the host went to sleep. Typing into a suspended host
      // goes nowhere, and a remote-wakeup capable device would need to ask
      // first; this one simply waits to be resumed.
      g_host_present = false;
      break;
    default:
      break;
  }
}

}  // namespace

void HidTyper::Begin() {
  if (begun_) return;

  USB.VID(kUsbVendorId);
  USB.PID(kUsbProductId);
  USB.manufacturerName(kUsbManufacturer);
  USB.productName(kUsbProduct);
  USB.onEvent(OnUsbEvent);

  g_keyboard.begin();
  USB.begin();
  begun_ = true;
}

bool HidTyper::Ready() const { return begun_ && g_host_present; }

void HidTyper::WakeHost() {
  if (!Ready()) return;
  g_keyboard.press(KEY_LEFT_SHIFT);
  delay(30);
  g_keyboard.releaseAll();
}

void HidTyper::TypeSecret(const char* secret) {
  if (!Ready() || secret == nullptr) return;

  for (const char* c = secret; *c != '\0'; ++c) {
    g_keyboard.write(static_cast<uint8_t>(*c));
    delay(kKeystrokeDelayMs);
  }
  delay(kTypeToEnterDelayMs);
  g_keyboard.write(KEY_RETURN);
  g_keyboard.releaseAll();
}

void HidTyper::LockHost() {
  if (!Ready()) return;
  g_keyboard.press(KEY_LEFT_CTRL);
  g_keyboard.press(KEY_LEFT_GUI);
  g_keyboard.press('q');
  delay(40);
  g_keyboard.releaseAll();
}

}  // namespace fpkey
