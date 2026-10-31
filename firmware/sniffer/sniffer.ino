/*
 * EasyWatch - IR remote sniffer
 * -----------------------------
 * Flash this to the ESP32 FIRST, before the real firmware.
 * Its only job: print the IR code of whatever button you press on the
 * old set-top-box remote, so we can map 4 buttons to 4 genres.
 *
 * HARDWARE (3-pin IR RECEIVER module - VS1838B / TSOP1738 / KY-022):
 *   Board: generic ESP32 DevKit (ESP32-WROOM, 30-pin DOIT DevKit V1).
 *          Arduino IDE board = "ESP32 Dev Module".
 *   Receiver  OUT  ->  pin labelled  D4   (= GPIO4, bottom row)
 *   Receiver  VCC  ->  pin labelled  3V3  (bottom-left corner; NOT 5V / NOT VIN)
 *   Receiver  GND  ->  pin labelled  GND
 *
 * LIBRARY:
 *   Arduino IDE -> Tools -> Manage Libraries -> search "IRremote"
 *   by Armin Joachimsmeyer -> install (v4.x).
 *
 * HOW TO USE:
 *   1. Select board: Tools -> Board -> esp32 -> "ESP32 Dev Module".
 *   2. Upload this sketch.
 *   3. Open Tools -> Serial Monitor, set baud to 115200.
 *   4. Point the old remote at the receiver and press ONE button.
 *   5. Write down the "Protocol", "Address" and "Command" it prints.
 *      Do this for the 4 buttons you want (comedy / south / movies / songs).
 *   6. Send me those 4 lines and I'll wire them into the real firmware.
 *
 * Tip: press each button a few times. The line that repeats is the real
 * code; ignore any "Protocol=UNKNOWN" noise on the first press.
 */

#define RAW_BUFFER_LENGTH 750   // big buffer so long frames are never truncated
#include <IRremote.hpp>

#define IR_RECEIVE_PIN 4    // GPIO4 = pin labelled "D4" on the DevKit (change if you wire elsewhere)

void setup() {
  Serial.begin(115200);
  delay(300);
  IrReceiver.begin(IR_RECEIVE_PIN, ENABLE_LED_FEEDBACK);
  Serial.println();
  Serial.println(F("EasyWatch IR sniffer ready."));
  Serial.println(F("Point the old remote at the sensor and press a button..."));
  Serial.println();
}

void loop() {
  if (IrReceiver.decode()) {
    // Compact one-line fingerprint, easy to paste from a phone.
    // For a GOOD button: "bits" and "rawlen" stay the SAME every press,
    // and "data" repeats identically. If they jump around, the capture
    // is incomplete (tap slower / hold steady / reseat the wires).
    auto &d = IrReceiver.decodedIRData;
    Serial.print(F("proto="));
    Serial.print(getProtocolString(d.protocol));
    Serial.print(F("  bits="));
    Serial.print(d.numberOfBits);
    Serial.print(F("  data=0x"));
    Serial.println(d.decodedRawData, HEX);

    IrReceiver.resume();   // ready for the next button press
  }
}
