// Project 3 — three-task FreeRTOS logger (no RTC, timestamps from millis())
// Tasks: Sensor -> (qDisplay, qLogger) ; Logger -> qSDStatus -> Display
// Pins: I2C SDA=21 SCL=22 | SD CS=5 SCK=18 MOSI=23 MISO=19

#include <Wire.h>
#include <SD.h>
#include <Adafruit_BMP280.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SD_CS 5

// ---- Test knobs ---------------------------------------------------------
const uint32_t SAMPLE_MS    = 1000;  // stress runs: 100, 20, 10
const uint8_t  LOGGER_Q_LEN = 10;    // stress runs: 1, 2, 5, 10
const char*    LOG_PATH     = "/log_rtos.csv";
// -------------------------------------------------------------------------

struct Reading { uint32_t ms; float tempC; float pressHpa; };
enum SdState : uint8_t { SD_OK = 0, SD_FAIL = 1 };

QueueHandle_t     qDisplay, qLogger, qSDStatus;
SemaphoreHandle_t i2cMutex;           // Wire is not thread-safe: BMP280 + OLED share it
volatile uint32_t dropCount = 0;

Adafruit_BMP280   bmp;
Adafruit_SSD1306  oled(128, 64, &Wire, -1);

static bool sdBegin() {
  uint32_t t0 = millis();
  while (!SD.begin(SD_CS)) {          // retry-with-timeout (bug journal 001)
    if (millis() - t0 > 3000) return false;
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  return true;
}

static void pushStatus(bool ok) {
  uint8_t s = ok ? SD_OK : SD_FAIL;
  xQueueOverwrite(qSDStatus, &s);
}

void sensorTask(void*) {
  TickType_t last = xTaskGetTickCount();
  for (;;) {
    Reading r;
    r.ms = millis();
    xSemaphoreTake(i2cMutex, portMAX_DELAY);
    r.tempC    = bmp.readTemperature();
    r.pressHpa = bmp.readPressure() / 100.0f;
    xSemaphoreGive(i2cMutex);

    xQueueOverwrite(qDisplay, &r);
    if (xQueueSend(qLogger, &r, 0) != pdPASS) dropCount++;   // drop-on-full

    vTaskDelayUntil(&last, pdMS_TO_TICKS(SAMPLE_MS));
  }
}

void loggerTask(void*) {
  bool ok = sdBegin();
  pushStatus(ok);
  if (ok && !SD.exists(LOG_PATH)) {
    File f = SD.open(LOG_PATH, FILE_WRITE);
    if (f) { f.print("timestamp,temperature,pressure\n"); f.close(); }
  }

  Reading r;
  bool lastGood = ok;
  uint32_t maxUs = 0;
  Serial.println("ms,write_us,max_us,backlog,drops");

  for (;;) {
    xQueueReceive(qLogger, &r, portMAX_DELAY);
    UBaseType_t backlog = uxQueueMessagesWaiting(qLogger);   // still waiting behind this one

    uint32_t t = micros();
    bool good = false;
    File f = SD.open(LOG_PATH, FILE_APPEND);                 // open-write-close every row
    if (f) {
      good = f.printf("%lu,%.2f,%.2f\n", (unsigned long)r.ms, r.tempC, r.pressHpa) > 0;
      f.close();
    }
    uint32_t us = micros() - t;
    if (us > maxUs) maxUs = us;

    if (good != lastGood) { pushStatus(good); lastGood = good; }
    if (!good) { SD.end(); SD.begin(SD_CS); }

    Serial.printf("%lu,%lu,%lu,%u,%lu\n", (unsigned long)r.ms, (unsigned long)us,
                  (unsigned long)maxUs, (unsigned)backlog, (unsigned long)dropCount);
  }
}

void displayTask(void*) {
  Reading r = {0, 0, 0};
  uint8_t sd = SD_OK;
  for (;;) {
    xQueueReceive(qDisplay, &r, pdMS_TO_TICKS(500));
    xQueueReceive(qSDStatus, &sd, 0);

    xSemaphoreTake(i2cMutex, portMAX_DELAY);
    oled.clearDisplay();
    oled.setCursor(0, 0);
    oled.printf("Up  %lus\n", (unsigned long)(r.ms / 1000));
    oled.printf("T   %.2f C\n", r.tempC);
    oled.printf("P   %.2f hPa\n", r.pressHpa);
    oled.printf("SD  %s\n", sd == SD_OK ? "OK" : "FAIL");
    oled.printf("Drop %lu\n", (unsigned long)dropCount);
    oled.display();
    xSemaphoreGive(i2cMutex);
  }
}

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);
  i2cMutex = xSemaphoreCreateMutex();

  if (!bmp.begin(0x76)) { Serial.println("BMP280 not found"); while (true) delay(1000); }
  if (!oled.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { Serial.println("OLED not found"); while (true) delay(1000); }
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);

  qDisplay  = xQueueCreate(1, sizeof(Reading));
  qLogger   = xQueueCreate(LOGGER_Q_LEN, sizeof(Reading));
  qSDStatus = xQueueCreate(1, sizeof(uint8_t));

  xTaskCreate(sensorTask,  "Sensor",  3072, NULL, 3, NULL);
  xTaskCreate(loggerTask,  "Logger",  4096, NULL, 2, NULL);
  xTaskCreate(displayTask, "Display", 4096, NULL, 1, NULL);
}

void loop() { vTaskDelay(portMAX_DELAY); }
