#include <Wire.h>
#include "RTClib.h"
#include <Adafruit_BMP280.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_W 128
#define SCREEN_H 64          // change to 32 if your OLED is the small one
#define OLED_ADDR 0x3C
#define BMP_ADDR 0x76

RTC_DS1307 rtc;
Adafruit_BMP280 bmp;
Adafruit_SSD1306 display(SCREEN_W, SCREEN_H, &Wire, -1);

unsigned long lastUpdate = 0;

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);

  if (!rtc.begin()) { Serial.println("RTC not found"); while (1) delay(10); }
  if (!bmp.begin(BMP_ADDR)) { Serial.println("BMP280 not found"); while (1) delay(10); }
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("OLED not found"); while (1) delay(10);
  }
  // No rtc.adjust() here, the RTC already holds the right time.

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
}

void loop() {
  if (millis() - lastUpdate < 1000) return;   // update once per second, no delay()
  lastUpdate = millis();

  DateTime now = rtc.now();
  float t = bmp.readTemperature();
  float p = bmp.readPressure() / 100.0;       // Pa -> hPa

  char dateBuf[16], timeBuf[16];
  snprintf(dateBuf, sizeof(dateBuf), "%04d/%02d/%02d", now.year(), now.month(), now.day());
  snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d", now.hour(), now.minute(), now.second());

  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print(dateBuf);

  display.setTextSize(2);
  display.setCursor(0, 14);
  display.print(timeBuf);

  display.setTextSize(1);
  display.setCursor(0, 38);
  display.printf("Temp: %.2f C", t);
  display.setCursor(0, 50);
  display.printf("Pres: %.2f hPa", p);
  display.display();
}
