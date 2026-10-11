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
