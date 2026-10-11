// SD reader / integrity checker for /log_rtos.csv (read-only, never writes to the card)
// SD_CS = 5, SCK 18, MISO 19, MOSI 23. Serial 115200.
// Send any character in the Serial Monitor to run the check again.

#include <SD.h>

#define SD_CS 5

const char*    LOG_PATH   = "/log_rtos.csv";
const char*    HEADER     = "timestamp,temperature,pressure";
const uint32_t SPACING_MS = 1000;   // must equal SAMPLE_MS in logger_rtos.ino
const uint8_t  TAIL       = 20;     // rows printed at the end
const uint8_t  MAX_SEG    = 24;     // boots tracked

struct Seg { uint32_t first, last, rows, missing; };

static bool sdMount() {
  uint32_t t0 = millis();
  while (!SD.begin(SD_CS)) {
    if (millis() - t0 > 3000) return false;
    delay(100);
  }
  return true;
}

static void report() {
  Serial.println("\n===== SD READER =====");
  if (!sdMount()) { Serial.println("SD mount FAILED"); return; }

  File f = SD.open(LOG_PATH, FILE_READ);
  if (!f) { Serial.printf("open FAILED: %s\n", LOG_PATH); SD.end(); return; }

  size_t size = f.size();
  bool endsWithNewline = false;
  if (size > 0) {
    f.seek(size - 1);
    endsWithNewline = (f.read() == '\n');
    f.seek(0);
  }

  String tail[TAIL];
  Seg seg[MAX_SEG];
  Seg* cur = nullptr;
  uint32_t lineNo = 0, seen = 0, good = 0, bad = 0, segTotal = 0, prevMs = 0;
  bool headerOk = false, havePrev = false;

  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    lineNo++;

    if (lineNo == 1) {
      headerOk = (line == HEADER);
      if (!headerOk) Serial.printf("Header mismatch: \"%s\"\n", line.c_str());
      continue;
    }

    tail[seen % TAIL] = line;
    seen++;

    unsigned long ms = 0;
    float t = 0, p = 0;
    char extra;
    int n = line.length() ? sscanf(line.c_str(), "%lu,%f,%f%c", &ms, &t, &p, &extra) : 0;
    bool ok = (n == 3) && t > -40 && t < 85 && p > 300 && p < 1100;

    if (!ok) {
      bad++;
      if (bad <= 5) Serial.printf("BAD line %lu: \"%s\"\n", (unsigned long)lineNo, line.c_str());
      continue;
    }

    good++;
    if (!havePrev || ms < prevMs) {                 // millis() restarted = new boot
      segTotal++;
      if (segTotal <= MAX_SEG) { cur = &seg[segTotal - 1]; *cur = Seg{ms, ms, 0, 0}; }
      else cur = nullptr;
    } else if (cur && ms - prevMs > SPACING_MS * 3 / 2) {
      cur->missing += (ms - prevMs + SPACING_MS / 2) / SPACING_MS - 1;
    }
    if (cur) { cur->last = ms; cur->rows++; }
    prevMs = ms;
    havePrev = true;
  }
  f.close();
  SD.end();

  Serial.printf("File size       : %u bytes\n", (unsigned)size);
  Serial.printf("Data rows       : %lu (valid %lu, bad %lu)\n",
                (unsigned long)seen, (unsigned long)good, (unsigned long)bad);
  Serial.printf("Header OK       : %s\n", headerOk ? "yes" : "NO");
  Serial.printf("Ends with \\n    : %s\n", endsWithNewline ? "yes" : "NO (last row cut off)");
  Serial.printf("Boots in file   : %lu\n", (unsigned long)segTotal);

  Serial.println("\nboot, first_ms, last_ms, rows, missing_inside");
  for (uint32_t i = 0; i < segTotal && i < MAX_SEG; i++)
    Serial.printf("%lu, %lu, %lu, %lu, %lu\n", (unsigned long)(i + 1), (unsigned long)seg[i].first,
                  (unsigned long)seg[i].last, (unsigned long)seg[i].rows, (unsigned long)seg[i].missing);
  if (segTotal > MAX_SEG) Serial.printf("(%lu more boots not listed)\n", (unsigned long)(segTotal - MAX_SEG));

  Serial.printf("\nLast %u rows:\n", (unsigned)TAIL);
  uint32_t start = seen > TAIL ? seen - TAIL : 0;
  for (uint32_t i = start; i < seen; i++) Serial.println(tail[i % TAIL]);

  bool clean = headerOk && bad == 0 && endsWithNewline;
  Serial.printf("\nINTEGRITY: %s\n", clean ? "OK" : "PROBLEM (see above)");
  Serial.println("Send any character to run again.");
}

void setup() {
  Serial.begin(115200);
  delay(500);
  report();
}

void loop() {
  if (Serial.available()) {
    while (Serial.available()) Serial.read();
    report();
  }
}
