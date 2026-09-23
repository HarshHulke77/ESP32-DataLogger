# ESP32 Data Logger — Project 3

BMP280 + SSD1306 OLED + DS1307 RTC + SD card logger on ESP32, using FreeRTOS.

## Bug Journal

One entry per non-trivial defect. Never delete entries.

### 001 — SD.begin() fails intermittently on cold power-up

2026-09-23 · SD card module (SPI)

**Symptom** First power-on after wiring: "Initializing SD card..." followed by "Card failed, or not present." Immediate power-cycle retry, no physical change to card or wiring: succeeded, printed SDHC / 7680MB.

**Hypotheses** 1. Bad wiring/connection. 2. Card needed physical reinsertion. 3. Card power-on settle time not met before `SD.begin()` call.

**Falsification** Wiring was already confirmed correct (same connections on both attempts). Card was NOT physically removed or reinserted between attempts — only the board was power-cycled — which rules out hypothesis 2 directly.

**Localisation** Only variable that changed between the failing and succeeding attempt was elapsed time since power was applied to the card.

**Root cause** SD cards require a brief settle/power-on time before responding correctly to SPI commands. ESP32 boot is fast enough to reach `SD.begin()` before the card is ready, causing an intermittent false failure — not a wiring or hardware fault.

**Fix** Replaced single `SD.begin()` call with a retry loop: attempt `SD.begin()`, retry every 100ms, up to a 3-second timeout. Adapts to actual settle time instead of guessing a fixed delay.

**Cost** ~20 minutes.

**Prevention** For any SPI/I2C peripheral with a power-on/settle spec, wrap first-contact init calls in a bounded retry loop by default, not a single call — even if it works on the first try in testing.