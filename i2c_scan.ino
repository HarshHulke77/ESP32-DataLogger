#include <Wire.h>

// ESP32 default I2C pins: SDA = GPIO21, SCL = GPIO22
void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);
  delay(1000);
  Serial.println("I2C scan start");
}

void loop() {
  int found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    uint8_t err = Wire.endTransmission();  // 0 = a device ACKed
    if (err == 0) {
      Serial.printf("Found device at 0x%02X\n", addr);
      found++;
    }
  }
  Serial.printf("Scan done, %d device(s)\n\n", found);
  delay(3000);
}
