// Step 1 of the bench check: does the board talk to this machine at all?
//
// Arduino IDE: ESP32S3 Dev Module, USB CDC On Boot = Enabled, monitor at
// 115200. Plug into the native USB port, not the UART bridge.
//
// Expected: "ESP32-S3 alive" once a second in the serial monitor. If nothing
// appears, the problem is the board selection, the cable (charge-only cables
// are the usual culprit) or the port -- not the code.

void setup() {
  Serial.begin(115200);
}

void loop() {
  Serial.println("ESP32-S3 alive");
  delay(1000);
}
