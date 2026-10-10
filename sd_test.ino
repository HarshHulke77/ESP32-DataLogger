#include <SPI.h>
#include <SD.h>

// ESP32 VSPI pins
#define SD_SCK  18
#define SD_MISO 19
#define SD_MOSI 23
#define SD_CS   5

void setup() {
  Serial.begin(115200);
  delay(1000);

  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS)) {
    Serial.println("SD mount FAILED (check wiring, FAT32 format, module VCC)");
    return;
  }

  uint8_t type = SD.cardType();
  Serial.print("Card type: ");
  if (type == CARD_MMC) Serial.println("MMC");
  else if (type == CARD_SD) Serial.println("SDSC");
  else if (type == CARD_SDHC) Serial.println("SDHC");
  else Serial.println("UNKNOWN");
  Serial.printf("Card size: %llu MB\n", SD.cardSize() / (1024ULL * 1024ULL));

  // Write test
  File f = SD.open("/test.txt", FILE_WRITE);
  if (!f) { Serial.println("Open for write FAILED"); return; }
  f.println("hello from esp32");
  f.close();

  // Read-back test
  f = SD.open("/test.txt");
  if (!f) { Serial.println("Open for read FAILED"); return; }
  Serial.println("Read back:");
  while (f.available()) Serial.write(f.read());
  f.close();
  Serial.println("SD test PASSED");
}

void loop() {}
