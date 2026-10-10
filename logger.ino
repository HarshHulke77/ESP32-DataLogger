#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include "RTClib.h"
#include <Adafruit_BMP280.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_W 128
#define SCREEN_H 64
#define OLED_ADDR 0x3C
#define BMP_ADDR 0x76
#define SD_SCK  18
#define SD_MISO 19
#define SD_MOSI 23
#define SD_CS   5
#define LOG_FILE "/log.csv"

RTC_DS1307 rtc;
Adafruit_BMP280 bmp;
Adafruit_SSD1306 display(SCREEN_W, SCREEN_H, &Wire, -1);

unsigned long lastUpdate = 0;
bool sdOk = false;

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);

  if (!rtc.begin()) { Serial.println("RTC not found"); while (1) delay(10); }
  if (!bmp.begin(BMP_ADDR)) { Serial.println("BMP280 not found"); while (1) delay(10); }
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) { Serial.println("OLED not found"); while (1) delay(10); }
  display.setTextColor(SSD1306_WHITE);

  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  sdOk = SD.begin(SD_CS);
  Serial.println(sdOk ? "SD ready" : "SD mount FAILED");

  if (sdOk && !SD.exists(LOG_FILE)) {
    File f = SD.open(LOG_FILE, FILE_WRITE);
    if (f) { f.println("timestamp,temp_C,pressure_hPa"); f.close(); }
  }
}

void loop() {
  if (millis() - lastUpdate < 1000) return;
  lastUpdate = millis();

  DateTime now = rtc.now();
  float t = bmp.readTemperature();
  float p = bmp.readPressure() / 100.0;

  char ts[24];
  snprintf(ts, sizeof(ts), "%04d-%02d-%02d %02d:%02d:%02d",
           now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second());

  // Log: open, append, close each second (safe if power is cut)
  bool logged = false;
  unsigned long t0 = micros();
  if (sdOk) {
    File f = SD.open(LOG_FILE, FILE_APPEND);
    if (f) {
      f.printf("%s,%.2f,%.2f\n", ts, t, p);
      f.close();
      logged = true;
    }
  }
  unsigned long writeUs = micros() - t0;
  Serial.printf("%s | %.2f C | %.2f hPa | log=%s | write %lu us\n",
                ts, t, p, logged ? "OK" : "FAIL", writeUs);

  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.printf("%04d/%02d/%02d  SD:%s", now.year(), now.month(), now.day(), logged ? "OK" : "ERR");
  display.setTextSize(2);
  display.setCursor(0, 14);
  display.printf("%02d:%02d:%02d", now.hour(), now.minute(), now.second());
  display.setTextSize(1);
  display.setCursor(0, 38);
  display.printf("Temp: %.2f C", t);
  display.setCursor(0, 50);
  display.printf("Pres: %.2f hPa", p);
  display.display();
}
