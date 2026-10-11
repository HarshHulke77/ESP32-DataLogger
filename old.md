# ESP32 Data Logger — Project 3

Logs temperature and pressure with a real-time timestamp to an SD card as CSV, and shows the live values on an OLED. ESP32 + BMP280 + DS1307 RTC + SSD1306 OLED + microSD module. Arduino framework, single-loop firmware (no RTOS).

## Hardware

| Part | Interface | Address / pins |
|---|---|---|
| BMP280 (temp + pressure) | I²C | 0x76 |
| SSD1306 OLED 128x64 | I²C | 0x3C |
| DS1307 RTC (with coin cell) | I²C | 0x68 |
| EEPROM on the RTC module | I²C | 0x50 (not used, do not reuse this address) |
| microSD module | SPI (VSPI) | SCK 18, MISO 19, MOSI 23, CS 5 |

I²C bus: SDA = GPIO21, SCL = GPIO22. All I²C modules run from 3V3. The SD module is powered according to its board (check for an onboard regulator before choosing 5 V or 3V3).

## Architecture

```
loop() every 1000 ms (millis(), no delay())
  ├─ read RTC time          (DS1307)
  ├─ read temp + pressure   (BMP280)
  ├─ append one CSV line    (open → write → close on every sample)
  ├─ print line + write time to Serial
  └─ redraw OLED            (date, time, temp, pressure, SD:OK/ERR)
```

Design choice: the log file is opened and closed on every sample. This costs a few milliseconds but means a power cut loses at most the last line, not the whole file.

CSV format: `timestamp,temp_C,pressure_hPa`, e.g. `2026-10-10 17:05:00,34.72,974.58`.

## Measured results (2026-10-10)

| Measurement | Result |
|---|---|
| I²C pull-up, SDA to 3V3 | 1.202 kΩ |
| I²C pull-up, SCL to 3V3 | 1.127 kΩ |
| I²C devices found by scan | 0x3C, 0x50, 0x68, 0x76 (4 of 4) |
| SD card | SDHC, 7680 MB |
| SD write time (open + append + close) | about 17.8 ms typical |
| File check after logging | 327 lines, 10,789 bytes |
| Byte arithmetic | 31 B header + 326 rows × 33 B = 10,789 B (exact match) |
| RTC power-cycle test | USB unplugged 10 s, time continued correctly (battery backup works on 3V3) |

The pull-ups are the parallel result of the modules' own resistors, so no external pull-ups are fitted and none should be added. SCL draws about 2.9 mA at 3.3 V, just under the 3 mA I²C limit, so do not add more I²C modules to this bus.

## Notes and limitations

- **RTC drift is not yet measured.** The DS1307 is rated for 4.5 V and above but runs here from 3V3. It counts and survives power loss, but the drift in seconds per day is still to be measured against a reference clock.
- The BMP280 sits next to the ESP32 and reads warm (about 34.7 °C on a 34 °C day, creeping up as the board heats). Mount it away from the ESP32 for true room temperature.
- The log is verified by counting lines on the ESP32 itself (no card reader was available), not by opening the file on a PC.
- No data is buffered if the SD card is missing or fails: that sample is lost and the OLED shows `SD:ERR`.

## Files

| File | Purpose |
|---|---|
| `i2c_scan.ino` | Prints every I²C address that answers |
| `sd_test.ino` | Mounts the SD card, writes and reads back a test file |
| `oled_display.ino` | RTC + BMP280 on the OLED |
| `logger.ino` | Full firmware: OLED + CSV logging |

## Bug Journal

One entry per non-trivial defect. Never delete entries.

### 001 — SD.begin() fails intermittently on cold power-up

2026-09-23 · SD card module (SPI)

**Symptom** First power-on after wiring: "Initializing SD card..." followed by "Card failed, or not present." Immediate power-cycle retry, no physical change to card or wiring: succeeded, printed SDHC / 7680MB.

**Hypotheses** 1. Bad wiring/connection. 2. Card needed physical reinsertion. 3. Card power-on settle time not met before `SD.begin()` call.

**Falsification** Wiring was already confirmed correct (same connections on both attempts). Card was NOT physically removed or reinserted between attempts, only the board was power-cycled, which rules out hypothesis 2 directly.

**Localisation** Only variable that changed between the failing and succeeding attempt was elapsed time since power was applied to the card.

**Root cause** SD cards require a brief settle/power-on time before responding correctly to SPI commands. ESP32 boot is fast enough to reach `SD.begin()` before the card is ready, causing an intermittent false failure, not a wiring or hardware fault.

**Fix** Replaced single `SD.begin()` call with a retry loop: attempt `SD.begin()`, retry every 100 ms, up to a 3-second timeout. Adapts to actual settle time instead of guessing a fixed delay.

**Cost** About 20 minutes.

**Prevention** For any SPI/I²C peripheral with a power-on/settle spec, wrap first-contact init calls in a bounded retry loop by default, not a single call, even if it works on the first try in testing.

### 002 — `log=OK` does not prove the data is on the card (near-miss, caught)

2026-10-10 · SD logging · verification

**Symptom that could have occurred** The Serial Monitor printed `log=OK` for every sample over a 5-minute run, but no card reader was available to check the file.

**Hypotheses** 1. `log=OK` means the rows are on the card. 2. It only means `SD.open()` returned a file and the write call was accepted locally, with no guarantee about the stored file.

**Falsification** Treated hypothesis 1 as unproven. Added a read-back at the end of `setup()` that counts newline characters in `/log.csv` and prints the line count and file size.

**Localisation** Result: 327 lines, 10,789 bytes. Byte arithmetic: 31 B header + 326 rows × 33 B = 10,789 B, an exact match, so no partial or corrupted rows.

**Root cause** None, no defect. The risk was trusting a success return value as end-to-end proof.

**Fix** Keep the read-back check in the verification routine.

**Cost** 0 hours (caught before it became a bug).

**Prevention** A success return only means the operation was accepted locally. Verify the guarantee in the layer that provides it: count rows, compare byte sizes. Same lesson as the QoS 0 discovery in Project 1.
