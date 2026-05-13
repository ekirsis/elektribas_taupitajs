#include <Arduino.h>
#include <U8g2lib.h>

// ST7920 in serial mode driven directly by the Nano ESP32 (no MCP23017).
//
// LCD pin       Nano ESP32 pin
// ----------    --------------
// VSS (GND)     GND
// VDD (VCC)     5V (external supply, not VUSB)
// V0            wiper of 10k trimmer between VDD and VSS (contrast)
// RS  (CS)      D10
// R/W (SID)     D11
// E   (SCK)     D13
// PSB           GND   (forces serial mode)
// RST           3.3V  (or tie to a GPIO if you want to reset it)
// BLA           5V via ~100Ω resistor (backlight +)
// BLK           GND                    (backlight -)
//
// Make sure LCD GND and Nano ESP32 GND are tied together.
U8G2_ST7920_128X64_F_SW_SPI lcd(U8G2_R0,
                                /* clock= */ 13,
                                /* data=  */ 11,
                                /* CS=    */ 10);

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("[boot] start");

    lcd.begin();
    lcd.clearBuffer();
    lcd.setFont(u8g2_font_ncenB08_tr);
    lcd.drawStr(0, 16, "Hello, World!");
    lcd.sendBuffer();

    Serial.println("[boot] done");
}

void loop() {
    delay(1000);
    Serial.println("[loop] alive");
}
