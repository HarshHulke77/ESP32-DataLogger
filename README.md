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

## 003 — SD logger FAIL at 4 Hz with open-write-close; SD card stalls of 0.5-0.63 s found while sizing the queue

2026-10-11 · Project 3, ESP32 Data Logger · SD logging, Logger task

**Symptom** With open-write-close on every row at SAMPLE_MS=250 and qLogger length 10, the OLED showed SD FAIL and drops appeared after about 18 s. The same design logged 327 clean rows at 1 Hz the day before.

**Hypotheses**

1. Per-row write is too slow for a 250 ms interval.
2. A card stall overflowed the queue.
3. The card or its 5 V supply dropped out on `open()` at the higher write rate.

**Falsification**

1. Rejected. Typical open-write-close was 17.8 ms, which is 7% of 250 ms.
2. Rejected as the sole cause. The worst stall later measured was 627 ms, but 10 slots at 250 ms hold 2.5 s.
3. Not tested. The old code did not record whether `open` or `write` failed.

**Localisation** Rebuilt the Logger task to keep the file open and flush every 4 rows, and added a `FAIL,<step>` serial line. Then ran three logs with `write_us`, `max_us`, `backlog`, `max_backlog` and `drops` on every row.

| Run | SAMPLE_MS | Queue | Duration | max_us | max_backlog | drops |
|---|---|---|---|---|---|---|
| A | 100 | 10 | 97 s+ | 627617 | not logged | 0 |
| B | 1000 | 10 | 300 s | 20767 | 0 | 0 |
| C | 100 | 4 | 300 s | 500760 | 3 | 3 |

No FAIL line appeared in any run. Flush-row cost was about 0.3 ms in run A, 4.0-6.2 ms in run C and 17-18 ms in run B. I have not explained this spread.

**Root cause** NOT established for the original FAIL. Established: the card produces stalls of 0.5-0.63 s under 10 Hz writes, and a queue sized below stall divided by interval drops readings (run C). A single 500 ms stall can't produce 3 drops at 4 slots, so run C most likely saw more than one stall.

**Fix** Keep the file open, flush every 4 rows. Queue length 10 at 1 Hz, which is 10 s of buffer against a worst case of 0.627 s.

**Cost** [FILL: 2hours]

**Prevention** Size queues from measured worst case, not typical: 17.8 ms typical versus 627 ms worst is a 35x gap. Log which step failed, not only that something failed.

**Power-cut check** 5 intended USB power cuts at 1 Hz plus accidental resets, giving 7 boots. sd_reader reported INTEGRITY: OK. Rows per boot were 68, 8, 4, 4, 4, 8, 44, all multiples of 4. Rows lost per cut were not measured (at most 3 by design). A cut during the flush itself is untested.

## Three-task FreeRTOS logger (`logger_rtos.ino`)

Logs BMP280 temperature and pressure to a microSD card as CSV at 1 Hz and shows live values on an SSD1306 OLED. Timestamps are `millis()` since boot, because the DS1307 RTC failed during testing. They are not wall-clock time.

### Wiring (ESP32 30-pin devkit)

| Module | Pin | ESP32 |
|---|---|---|
| BMP280 (0x76), SSD1306 (0x3C) | VCC / GND | 3V3 / GND |
| BMP280, SSD1306 | SDA / SCL | GPIO21 / GPIO22 |
| BMP280 | SDO | GND (sets address 0x76) |
| microSD module (5 V type, onboard level shifter) | VCC / GND | VIN (5 V) / GND |
| microSD module | CS / SCK / MOSI / MISO | GPIO5 / GPIO18 / GPIO23 / GPIO19 |

### Architecture

| Task | Priority | Stack | Role |
|---|---|---|---|
| Sensor | 3 | 3072 B | Reads the BMP280 every `SAMPLE_MS`, pushes to `qDisplay` and `qLogger` |
| Logger | 2 | 4096 B | Appends rows to `/log_rtos.csv`, flushes every 4 rows, reports SD status |
| Display | 1 | 4096 B | Redraws the OLED from `qDisplay` and `qSDStatus` |

| Queue | Length | Producer to consumer | Full behaviour |
|---|---|---|---|
| `qDisplay` | 1 | Sensor to Display | Overwrite. The screen only needs the latest reading. |
| `qLogger` | 10 | Sensor to Logger | Drop the new reading and count it. The Sensor task must never block: it is the only producer for both queues and does the sampling, so blocking it would freeze the display and miss samples. |
| `qSDStatus` | 1 | Logger to Display | Overwrite. Only the latest SD state matters. |

The BMP280 and OLED share one I²C bus, and `Wire` is not thread-safe, so every bus access is wrapped in a mutex.

Configuration: `SAMPLE_MS = 1000`, `LOGGER_Q_LEN = 10`, `FLUSH_EVERY = 4`.

### Measurements

The Logger task times each write with `micros()`. Stress runs were done at 10 Hz to find the stall size.

| Run | SAMPLE_MS | Queue | Duration | max write | max backlog | drops |
|---|---|---|---|---|---|---|
| A | 100 | 10 | 97 s+ | 627.6 ms | not logged | 0 |
| B | 1000 | 10 | 300 s | 20.8 ms | 0 | 0 |
| C | 100 | 4 | 300 s | 500.8 ms | 3 | 3 |

- Typical row write is 74-300 µs between flushes. Flush rows took about 0.3 ms (run A), 4.0-6.2 ms (run C) and 17-18 ms (run B). This spread is unexplained.
- The SD card stalls for 0.5-0.63 s at 10 Hz write rates. A queue shorter than stall divided by sample interval drops readings (run C). Ten slots at 1 Hz is 10 s of buffer.

### Power loss

- `flush()` runs every 4 rows, so at most 3 rows are lost per power cut by design. Actual loss per cut was not measured.
- Test: 5 intended USB power cuts at 1 Hz plus accidental resets, giving 7 boots. A read-only checker (`sd_reader.ino`) reported `INTEGRITY: OK`: valid header, no malformed rows, file ends on a newline. Rows per boot were 68, 8, 4, 4, 4, 8, 44, all multiples of 4.
- Not tested: a power cut during the flush itself. A flush takes 4-18 ms once every 4 s, so a random cut hits it about 0.4% of the time.

### Known limitations

- Timestamps are `millis()` and restart at 0 on every boot.
- The root cause of the original failure (open-write-close on every row, SD FAIL after about 18 s at 4 Hz) was not established. See bug journal entry 003.
