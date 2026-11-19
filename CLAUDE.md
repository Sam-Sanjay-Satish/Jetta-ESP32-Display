# ESP32 Jetta Gauge Display: Claude Code handover

(Context as of 2026-11-18; updated after the v2 sniffing session. The 2026-11-18 handover from macvagdiag is folded in below.)

## What this is
A small always-on touchscreen gauge display for Sam's **2013 VW Jetta TDI**. It shows engine values the dash doesn't (v1: **oil temperature** and **exhaust gas temperature**) by asking the engine ECU for them over the OBD port's diagnostic CAN bus.

- Owner: Sam (UVic CS student, new to embedded work; explain things simply, give direct and specific feedback).
- Sister project: **macvagdiag** (Mac Java diagnostic tool, repo `Sam-Sanjay-Satish/Mac-VAG-Diagnostic-Tool`). Its first real capture decides bit rate, request IDs and protocol for this project.
- Status: parts ordered 2026-10-26. ESP-IDF 5.4.1 installed; display bring-up done (see "Bench findings"). Car protocol mapped from sniffing sessions (see "Car diagnostic CAN: confirmed"). **EGT solved; oil temp is NOT available from the engine ECU** (see below). No CAN code yet.
- Constraint: **no soldering equipment**. Everything must be solderless (header pins, screw terminals, Wago lever connectors).

## Target car
- 2013 VW Jetta TDI, **CJAA** engine, 6-speed DSG.
- Engine ECU: **03L 997 030 M** (Bosch EDC17). VCDS shows old-style measuring blocks (group/field numbers).
- Diagnostic CAN on OBD pins **6 (CAN-H) / 14 (CAN-L)**, **500 kbit/s, 11-bit IDs** (confirmed 2026-11-13).
- The gateway (J533) keeps internal traffic off the OBD port: with no tester connected the bus is completely silent (60 s, 0 frames, confirmed). The display has to send requests and read replies.

## Hardware
**Board: LCDWIKI ES3N28P (no touch)** is the target. The board currently on the bench is an ES3C28P whose touch chip never answered (see "Bench findings"); a no-touch ES3N28P has been ordered. **No touch in the product:** pages are cycled with the BOOT button (GPIO0) or automatically, with no touch driver and no LVGL input device. ESP32-S3-WROOM-1 (16 MB flash), 2.8" IPS 240x320, ILI9341V display (SPI), FT6336G capacitive touch (I2C).

Other parts: SN65HVD230 CAN transceiver module (3.3 V; pins 3V3, GND, CTX, CRX; screw terminal CANH/CANL; likely a 2-pin termination jumper), OBD-II male to open-wire pigtail (colours not standard; map pins with a multimeter), female-female Dupont wires, Wago 221-413 connectors, car USB charger in an ignition-switched 12 V socket + USB-C cable.

### Board GPIO map (from the vendor wiki, https://www.lcdwiki.com/2.8inch_ESP32-S3_Display)
| Function | GPIO |
|---|---|
| LCD SPI: CS / DC / SCK / MOSI / MISO / backlight | 10 / 46 / 12 / 11 / 13 / 45 (reset shared with EN) |
| Touch I2C (shared bus, the audio codec answers at 0x18): SDA / SCL / RST / INT | 16 / 15 / 18 / 17 |
| RGB LED | 42 |
| MicroSD (SDIO): CLK / CMD / D0-D3 | 38 / 40 / 39, 41, 48, 47 |
| Audio I2S: amp enable / MCLK / BCLK / data out / L-R select / mic in | 1 / 4 / 5 / 8 / 7 / 6 |
| Battery ADC | 9 |
| BOOT key | 0 |
| UART0 socket: RXD0 / TXD0 | 43 / 44 |
| **Free expansion IO (1.25 mm 4-pin socket)** | **2, 3, 14, 21** |

**CAN pin plan: TWAI TX = GPIO2, TWAI RX = GPIO3.** IO14 and IO21 stay spare. Do not use IO15/16/17/18 (I2C bus shared with the audio codec, plus the unused touch lines). The UART pins aren't needed, because USB-C handles flashing and logs.

### Wiring
| From | To |
|---|---|
| Transceiver 3V3 | a 3.3 V source on the board (CHECK which socket provides it) |
| Transceiver GND | board GND **and** OBD pin 4/5 (via Wago) |
| Transceiver CTX | GPIO2 |
| Transceiver CRX | GPIO3 |
| Transceiver CANH / CANL | OBD pin 6 / pin 14 |
| Other pigtail wires | Unused. Insulate ends, **especially pin 16 (constant 12 V)** |

## HARD RULES (never break; changing them needs Sam's explicit approval)
1. **Read-only, enforced in code.** A fixed allowlist of request bytes, matched on exact prefix and exact length, checked before every TWAI transmit. No other send path.
2. **Never send:** programming/extended-programming sessions, security access, write DID/memory, IO control, routines, clear DTCs, ECU reset, comm control, DTC setting.
3. **Physical addressing only, to the engine ECU.** No functional broadcast IDs unless Sam approves.
4. **Gentle polling.** A few requests per second total. Back off on errors or no reply. Stop transmitting entirely if the bus looks wrong (error-passive, bus-off, or no replies).
5. **Termination off.** Remove the transceiver's 120 ohm jumper/resistor before connecting to the car. Verify with a multimeter (CANH to CANL unpowered: open = good, about 120 ohm = still on).
6. **Never power from OBD pin 16.** It is constant battery power and would drain the battery.
7. **Bring-up starts in TWAI listen-only mode** (`TWAI_MODE_LISTEN_ONLY`). Switch to normal mode only after the bit rate and request IDs are confirmed from the macvagdiag capture.
8. **Display must not distract.** Readable at a glance, no animation-heavy UI, no touch needed while driving.

## Toolchain decision: ESP-IDF
- The vendor ships Arduino, MicroPython and ESP-IDF demos. The ESP-IDF demo uses **ESP-IDF 5.4.1** and **LVGL 8.4.0**, so use those versions.
- Install ESP-IDF on the Mac (Homebrew: `cmake`, `ninja`, `python3`, `git`; then clone `esp-idf`, run `./install.sh esp32s3`, and `source export.sh` per shell). Build/flash with `idf.py build flash`.
- USB-C shows up as `/dev/cu.usbmodem*`. If flashing fails: hold BOOT, tap RESET, release BOOT.
- **Python must be 3.12.** The default `python3` is 3.14, for which ESP-IDF 5.4.1 has no environment. Every shell: `export PATH=/opt/homebrew/opt/python@3.12/libexec/bin:$PATH; . ~/esp/esp-idf/export.sh`. ESP-IDF lives in `~/esp/esp-idf`. The first `idf.py` run is slow (over 2 minutes).
- `idf.py monitor` is interactive and doesn't work well from Claude Code. Use `tools/serial_log.py PORT SECONDS LOGFILE` (reads N seconds, saves a log; it resets the board first unless `--no-reset`). Logs go in `logs/`. Port is `/dev/cu.usbmodem101` (USB-Serial/JTAG).
- Host-side tests: keep the allowlist guard, ISO-TP / TP 2.0 framing and value decoding in plain C/C++ with no hardware dependency; test on the Mac with clang + Unity or doctest.
- Vendor resources: ESP-IDF demo instructions at https://www.lcdwiki.com/res/ES3C28P/2.8inch_ES3C28P_ES3N28P_ESP-IDF_Demo_Instructions.pdf; the data pack is linked from the wiki page above.

## Bench findings (2026-11-10)
- Board talks over USB: ESP32-S3 (QFN56, rev v0.2), 16 MB flash, 8 MB PSRAM. Do not enable PSRAM unless needed.
- **Factory firmware backup:** `~/esp/backup/factory_16MB.bin` (SHA-256 `cdd2e0d4...7513f60a`). Restore: `esptool.py --port /dev/cu.usbmodem101 write_flash 0 ~/esp/backup/factory_16MB.bin`. A full 16 MB read takes about 6.5 minutes. The ES3C28P board's stock firmware was replaced by the ripple test.
- **Display settings that work** (`ripple_test/main/main.c`): ILI9341V, colour inversion ON, BGR order, no mirroring, portrait 240x320. Needs the vendor init sequence (`reference/ILI9341V_Init.txt`), reset via software command (no reset GPIO).
- **SPI clock: use 40 MHz (25 FPS full-frame).** 80 MHz gave 50 FPS but the HUD flickered and shifted sideways, so don't use it. A full-frame push is limited by SPI. Draw only what changed, or keep frames cheap, if more speed is needed.
- Display code (`display_ili9341.c`, full-frame DMA push in 8 chunks) is reusable for the gauge firmware.
- **Touch:** on the ES3C28P bench board nothing answered at 0x38 (or anywhere except 0x18) with any reset state, same as the stock firmware. Believed to be a hardware fault. Touch is not needed (see Board).
- Reference code: `ripple_test/` is a throwaway test and is separate from the future gauge firmware.

## Software layers (mirror macvagdiag)
TWAI adapter, request guard/allowlist, transport (ISO-TP and/or TP 2.0), data service (PID or measuring block to value + unit), poll scheduler (rates, back-off), LVGL UI. Optional: log readings to microSD as CSV.

## Car diagnostic CAN: confirmed (sniffing sessions 2026-11-13 and 2026-11-18)
Listen-only captures with the CANable + macvagdiag `can-log` while VCDS talked. Raw captures and VCDS CSVs are in macvagdiag's git-ignored `local/captures/` and `local/vcds-logs/` (they contain the VIN / VCID: never commit them). v2 captures: `2026-11-18-002922.log` (groups 007/099/100), `2026-11-18-003345.log` (001/011/033), `2026-11-18-003611.log` (OBD-II mode 01). All 0 errors.

- **The engine ECU talks VW TP 2.0 + KWP2000** (not UDS), and **also answers standard OBD-II** on `7E0 → 7E8` (ISO-TP, padded `00`).
- **Never read or show the VIN** (same rule as macvagdiag).

### TP 2.0 to the engine (exact bytes, `ID#data`)
| Step | Tester | Engine |
|---|---|---|
| Channel setup (engine = logical address `01`) | `200#01 C0 00 10 00 03 01` | `201#00 D0 00 03 40 07 01` → tester sends on **`740`**, engine replies on **`300`** |
| Timing parameters | `740#A0 0F 8A FF 32 FF` | `300#A1 0F 8A FF 4A FF` |
| Diagnostic session | `740#10 00 02 10 89` | `300#B1`, then `300#10 00 02 50 89`; tester ACKs `740#B1` |
| Read group | `740#1n 00 02 21 <group>` | `300#Bn`, then the `61 <group> ...` reply in several packets; tester ACKs |
| Keep-alive | `740#A3` | `300#A1 0F 8A FF 4A FF` |
| Close | `740#A8` | `300#A8` |

Packet byte 0 is `[op][seq]`: op `0` more + ACK wanted, `1` last + ACK wanted, `2` more no ACK, `3` last no ACK; `B[n]` = ACK ready (expecting n), `9[n]` = not ready. The first packet carries a 2-byte length then 5 data bytes; later packets 7. Each side counts its own sequence mod 16. Before opening the engine channel, VCDS briefly opens the gateway (`200#1F C0...`) for identification; the ESP32 doesn't need that and must only open address `01`.

**Timing lessons (from macvagdiag's real transmit, 2026-11-17):** wait **~20 ms after your ACK** before the next request (VCDS waits 18–27 ms; less makes modules miss the ACK and resend ~150 ms later, or answer `9x`). On `9x` naming the packet just sent: wait for the ready ACK and resend (with a cap). A repeated packet (same seq and bytes): ACK again and drop it. Response pending `7F xx 78` comes with length `80 03` (mask bit 15, keep waiting). Port macvagdiag's `transport/.../Tp20Channel.java` and its tests.

**VCDS polling rhythm (2026-11-18):** with 3 groups open, one `21 xx` request every **~140 ms** (~7/s total, round-robin), plus `740#A3` keep-alive about every **0.55 s**, even while polling. Treat that as the ceiling; the ESP32 should poll much slower (rule 4). No `9x` replies in any v2 capture.

### Measuring blocks (KWP `21 <group>`, VCDS group number decimal → hex)
Reply: `61 <group>` then 8 triplets `[formula] [a] [b]` (24 bytes). **Only the first 4 triplets are VCDS's fields 1–4**; the rest are placeholders (`25 00 00`, `30 00 00`). Formula `25` with `00 00` = **no value** (VCDS shows a blank).

| Group | Request | Fields 1–4 (decoded idle sample, all matched VCDS's CSV) |
|---|---|---|
| **007** Temperatures | `21 07` | fuel temp `05 09 8F` 38.7 °C, **oil temp `25 00 00` = EMPTY**, intake air `05 09 83` 27.9 °C, coolant `05 09 AF` 67.5 °C |
| **099** EGT | `21 63` | RPM `01 73 24` 828, **EGT before turbo `05 3C 7F` 162.0 °C**, before ox. cat `05 3C 79` 126.0, after ox. cat `05 3C 78` 120.0 |
| 100 DPF | `21 64` | after ox. cat 120.0 °C, after DPF 96.0 °C, DPF differential pressure ×2 (formula `32`) |
| 033 | `21 21` | RPM, **EGT pre-turbo `05 3C 7E`**, 2 regulation factors (formula `1F`) |
| 001 | `21 01` | RPM, injection qty (`27`), rail pressure (`64`, bar), coolant |
| 011 | `21 0B` | RPM, boost specified / actual (`60`, mbar), duty cycle |

Formulas confirmed against VCDS: `01` RPM = 0.2·a·b; **`05` °C = a·(b−100)·0.1**; `07` km/h = 0.01·a·b; `14` % = a·(b−128)/128; `21` % = 100·b/a; `27` mg/str = a·b/255; `31` mg/stroke = a·b/40; `60` mbar = a·b/10; `64` bar = a·b/10.

### OBD-II mode 01 (engine on `7E8`)
Supported PIDs (decoded from the `00/20/40/60/80` bitmaps): 01 04 05 0C 0D 10 13 1C 1F 21 24 25 30 31 33 3C 41 42 46 49 4A 4C 4F 51 5D 5E 61 62 63 65 67 69 6B 6D 70 71 73 77 **78** 7A 7F 81 82 89 8B.
- **`5C` (oil temp): NOT supported. `78` (EGT bank 1): supported.** Also `05` coolant, `0C` RPM, `0D` speed, `70` boost.
- `78` reply (ISO-TP, 2 frames): `41 78 0F` + 4 sensors × 2 bytes, °C = value/10 − 40. Idle sample: `084F` 172.7, `0708` 140.0, `06F2` 137.8, `062B` 117.9 (sensor 1 should be pre-turbo; check against group 099 before relying on it).
- VCDS sent mode 01 to the **functional `7DF`**, which rule 3 forbids. The ESP32 must use **physical `7E0`**. The engine answered `09 02` on `7E0` (2026-11-13), so `7E0` mode 01 is expected to work but is **not yet proven**; it's the first thing phase 3 checks.
- `7E9` (the DSG) also answers `7DF`; ignore it.

## Where the data comes from
- **EGT: solved.** Primary: TP 2.0 group 099 field 2 (`21 63`, EGT before turbo). Alternative: OBD-II PID `78` on `7E0` (simpler ISO-TP single-frame request).
- **Oil temp: NOT available from the engine ECU** (no `5C`, group 007 field 2 is empty, VCDS shows it blank). Next candidate: the **instrument cluster (module 17)**, which likely reads the oil level/temperature sender (UNCONFIRMED). Reading it means talking to a second module, which **needs Sam's approval to change rule 3**. Fallback: a physical oil temp sender + ADC.
- Route decision is pending the cluster check. If the cluster has oil temp over TP 2.0, a single TP 2.0 stack can serve both values.

Whichever route works, its exact request bytes go into the allowlist: for TP 2.0 to the engine, the `200` setup for address `01` only, `A0`, `A3`, `A8`, ACKs, `10 89`, `21 63` (and any other approved groups); for OBD-II, exact `7E0` frames such as `02 01 78`, `02 01 05`.

## Phases
1. **Bench bring-up (current):** display running (done), transceiver wired to GPIO2/3. No car. Touch is no longer required.
2. **Listen-only on the car** at 500 kbit/s with VCDS or macvagdiag talking. Confirm clean frames and no bus errors. (The CANable + `can-log` can also record exactly what the ESP32 sends, to verify its allowlist on the car.)
3. **One read:** a single allowlisted request (e.g. coolant PID `05` on `7E0`, which also proves physical mode 01) shown as text. Before/after VCDS scans to confirm no new faults.
4. **v1 gauges:** EGT + oil temp (source TBD), poll scheduler, back-off, warning thresholds, optional beep.
5. **Polish:** gauge pages, brightness, mount/enclosure (heat, sun), auto-sleep when the ECU stops replying.

## Open questions
1. Which socket on the board provides 3.3 V and GND for the CAN module, and what is the expansion socket's pin order? (Check the printed labels or a photo.)
2. ~~Does the CJAA ECU support PIDs `5C` / `78`?~~ Answered 2026-11-18: `5C` no, `78` yes.
3. ~~Which groups hold oil temp and EGT?~~ EGT: group 099 field 2. Oil temp: the engine has none (group 007 field 2 empty).
4. **Where does oil temp come from?** Check VCDS → 17-Instruments → Measuring Blocks for an oil temperature value (listen-only capture + VCDS log, like 2026-11-18). If it's there: which protocol, request and formula; then Sam decides whether rule 3 extends to the cluster.
5. Does the engine answer mode 01 on physical `7E0` (VCDS only used `7DF`)?
6. Is the transceiver's termination a jumper or a soldered resistor?
7. Is the car's 12 V / USB socket ignition-switched?

## Next steps
1. ~~Install ESP-IDF 5.4.1~~ done. ~~Display working~~ done. ~~v2 sniffing session~~ done 2026-11-18.
2. Next car session: cluster (17) measuring blocks, listen-only, to find oil temp (open question 4).
3. Create the project skeleton with the allowlist guard first (with host-side tests), before any code that can transmit.
