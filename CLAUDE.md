# ESP32 Jetta Gauge Display: Claude Code handover

(Context as of 2026-11-19, end of the firmware session. The 2026-11-18 handover from macvagdiag is folded in below; the 2026-11-19 session also re-checked the v2 captures in macvagdiag's `local/` for timing, flow control and the init sequence.)

## What this is
A small always-on touchscreen gauge display for Sam's **2013 VW Jetta TDI**. It shows engine values the dash doesn't (v1: **oil temperature** and **exhaust gas temperature**) by asking the engine ECU for them over the OBD port's diagnostic CAN bus. **Major feature: instant fault code readout**, showing stored DTCs with a plain-language description (see "Fault codes (DTC readout)").

- Owner: Sam (UVic CS student, new to embedded work; explain things simply, give direct and specific feedback).
- Sister project: **macvagdiag** (Mac Java diagnostic tool, repo `Sam-Sanjay-Satish/Mac-VAG-Diagnostic-Tool`, local copy `~/Dev/Mac VAG Diagnostic Tool`). Its captures and notes decide bit rate, request IDs and protocol for this project. Most useful: its `CLAUDE.md`, `transport/` and `diag/CLAUDE.md`, `local/analysis/2026-11-13-sniffing-analysis.md` (DTC/freeze-frame layout, timing) and the v2 captures/CSVs in `local/captures/` and `local/vcds-logs/` (never copy them here: VIN/VCID).
- Repo: GitHub `Sam-Sanjay-Satish/Jetta-ESP32-Display` (public, MIT). Commit only when Sam asks; one-line lowercase summaries like macvagdiag's.
- Status: parts ordered 2026-10-26. ESP-IDF 5.4.1 installed; display bring-up done (see "Bench findings"). Car protocol mapped from sniffing sessions (see "Car diagnostic CAN: confirmed"). **EGT solved; oil temp is NOT available from the engine ECU** (see below). **Firmware written 2026-11-19** (tested core + ESP32 app, see "Software"; commit `bd9baf1`); builds, but **not yet run on the board** (it wasn't plugged in), so CPU load is unmeasured.
- Constraint: **no soldering equipment**. Everything must be solderless (header pins, screw terminals, Wago lever connectors).

## Target car
- 2013 VW Jetta TDI, **CJAA** engine, 6-speed DSG.
- Engine ECU: **03L 997 030 M** (Bosch EDC17). VCDS shows old-style measuring blocks (group/field numbers).
- Diagnostic CAN on OBD pins **6 (CAN-H) / 14 (CAN-L)**, **500 kbit/s, 11-bit IDs** (confirmed 2026-11-13).
- The gateway (J533) keeps internal traffic off the OBD port: with no tester connected the bus is completely silent (60 s, 0 frames, confirmed). The display has to send requests and read replies.

## Hardware
**Board: LCDWIKI ES3C28P (with touch)** is the target. The first bench board's touch chip never answered (see "Bench findings"). **The replacement touch board arrived 2026-11-25 and its touch works**; it is now the bench board. Touch is to be used (FT6336G driver + LVGL input device, e.g. for browsing pages and fault codes when parked). A no-touch ES3N28P was ordered earlier; it's a fallback only. Everything needed while driving must still work without touch (rule 8): the BOOT button (GPIO0) or automatic page cycling. ESP32-S3-WROOM-1 (16 MB flash), 2.8" IPS 240x320, ILI9341V display (SPI), FT6336G capacitive touch (I2C).

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

**CAN pin plan: TWAI TX = GPIO2, TWAI RX = GPIO3.** IO14 and IO21 stay spare. Do not use IO15/16/17/18 (I2C bus shared by the touch chip and the audio codec, plus touch RST/INT). The UART pins aren't needed, because USB-C handles flashing and logs.

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
- **Touch board backup (2026-11-25):** `~/esp/backup/factory_touchboard_16MB.bin` (SHA-256 `da2b04e7...8c97eaee`; an LCDWIKI Arduino LVGL widgets demo, different from the first board's image). Read took 208 s at 643 kbit/s. Restore the same way.
- **Display settings that work** (`ripple_test/main/main.c`): ILI9341V, colour inversion ON, BGR order, no mirroring, portrait 240x320. Needs the vendor init sequence (`reference/ILI9341V_Init.txt`), reset via software command (no reset GPIO).
- **SPI clock: use 40 MHz (25 FPS full-frame).** 80 MHz gave 50 FPS but the HUD flickered and shifted sideways, so don't use it. A full-frame push is limited by SPI. Draw only what changed, or keep frames cheap, if more speed is needed.
- Display code (`display_ili9341.c`, full-frame DMA push in 8 chunks) is reusable for the gauge firmware.
- **Touch:** on the ES3C28P bench board nothing answered at 0x38 (or anywhere except 0x18) with any reset state, same as the stock firmware. Believed to be a hardware fault. **Replacement board (2026-11-25): touch works.** Probe 0x38 OK, chip id 0x64, vendor id 0x11; 28 touches incl. two-finger, 0 I2C errors, ~0.5 ms per read (`logs/2026-11-25-touchboard-*.log`). In the ripple test (portrait 240x320) no swap/mirror is needed: Sam confirmed ripples land under the finger and the picture fills the screen (same ILI9341V settings). The gauge firmware is landscape (MADCTL `0x68`), so touch coordinates must be rotated to match there. `touch_ft6336.c` in `ripple_test/` is the starting point for the driver.
- Reference code: `ripple_test/` is a throwaway test and is separate from the future gauge firmware.

## Software (written 2026-11-19, test-driven)
```
firmware/                        ESP-IDF project (idf.py build flash), LVGL 8.4 via managed component
  components/jd_core/            pure C core: NO ESP-IDF calls, NO malloc, integer maths only
    guard.c    allowlist (rules 1-3)          txgate.c  the ONE transmit path (rules 1, 4, 7)
    isotp.c    OBD-II ISO-TP client           tp20.c    TP 2.0 channel (port of Tp20Channel.java)
    decode.c   KWP formulas + OBD PIDs        sched.c   poll scheduler, back-off, quiet mode
    store.c    latest value per signal        view.c    gauge text + warning levels (hysteresis)
    sniff.c    passive decoder (listen-only)  diag.c    ties it all together per mode
  main/        app_main.c (tasks), can_twai.c (TWAI), lcd.c (ILI9341V strips), ui.c (LVGL task + touch input),
               touch.c (FT6336G, landscape mapping), pages.c (screens, swipe navigation, diagnostics page),
               gauges.c (gauge page), demo.c (fake values), sysmon.c   (pages/gauges: no ESP-IDF calls)
  partitions.csv                 4 MB app + 8 MB `storage` (reserved: DTC text table, CSV logs)
test/host/                       Unity tests for jd_core on the Mac: `make -C test/host`
tools/sim/                       renders gauges.c + demo.c with LVGL on the Mac: `make -C tools/sim` → out/*.png
```
- **Gauge page (2026-11-19, Sam's layout):** landscape 320x240. Left: oil (40–150 °C) and coolant (40–130 °C) as vertical bars, value in the middle of the bar, title with the unit in brackets under the bar (2026-11-25: Sam wanted it less crowded; no range numbers), amber line at the warning level; bar colour blue below 60 °C (display only), green, amber, red. Right: boost specified (grey) vs actual (cyan) as **gauge pressure** (Sam: absolute − fixed 1013 mbar, `JD_BOOST_ATMOS_MBAR`, in `jd_view_number`), numbers on top + a 16 s graph (−100 to 1700 mbar). No EGT for now (Sam); RPM not shown. Orientation: menuconfig "Screen orientation", MADCTL `0x68` (A, default, **confirmed right way up on the bench board 2026-11-19**) or `0xA8` (B, 180°). Demo build ran on the board 2026-11-19 and Sam confirmed everything draws properly. Units always in brackets (`OIL (°C)`, `BOOST (mbar)`). The top line of the gauge page shows only warnings (CAN ERROR, TX HALTED), otherwise it is empty.
- **Pages + touch (2026-11-25, Sam's choice):** swipe left on the gauges → diagnostics page (link, mode, frames received, ECU replies, frames sent, error counters, demo/halt note); swipe right → back. Instant switch, no animation. Gauges keep refreshing while hidden (boost graph history). Sam confirmed on the board that swiping works. Touch is mapped for landscape A as x = raw y, y = 239 − raw x (B: rotated 180°); swipe direction proves x, **the y direction is unverified** (tap the top-left corner and check the `touch: down at` log line before using taps on y positions). No touch chip → the firmware runs without touch. `tools/sim` renders `screen_*` and `diag_*` PNGs.
- **Demo mode** (menuconfig "Demo mode", default off): the screen shows generated values (3-minute loop: warm-up, boost pulses with an underboost every 4th, a hot spell) and the diagnostics page says DEMO in magenta (Sam 2026-11-25: no demo text on the gauge page). diag/CAN run unchanged. Separate build dir so the real `sdkconfig` stays clean: `idf.py -B build-demo -D SDKCONFIG=build-demo/sdkconfig -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.demo" build flash`.
- **TDD rule:** every jd_core change starts with a failing test in `test/host/` (clang, ASan + UBSan, `-Werror`). 136 tests. Key behaviours were checked by mutation (break the code, a test must fail).
- **Transmit path:** `jd_isotp`/`jd_tp20` → `jd_txgate_send` (listen-only? latched halt? `jd_guard_check`?) → `can_hw_send` → `twai_transmit`. Nothing else calls `twai_transmit`. A guard refusal latches a permanent halt (it means a bug). Bus-off, error-passive or TEC/REC ≥ 96 also latch a halt until reboot. `app_main` aborts at boot if the guard passes known-forbidden frames.
- **Build switches** (`idf.py menuconfig` → "Jetta display"): `JD_MODE` = PASSIVE (default) / OBD / TP20; `JD_CAN_TX_ENABLE` (default **off**: TWAI runs `TWAI_MODE_LISTEN_ONLY`, so it can't send anything, not even ACKs). Transmit needs both a non-passive mode and the switch.
- **PASSIVE mode** decodes another tester's traffic (engine replies on `300` and `7E8`), so phase 2 already shows live EGT on screen while VCDS polls group 099.
- **Polling (OBD mode):** PID 78 every 1 s, 0C every 2 s, 05 every 5 s, never closer than 300 ms (≈1.7 req/s). TP20 mode: group 011 (boost) every 500 ms + 007 (coolant) every 5 s ≈ 2.2 req/s; 099 is allowlisted but not polled (Sam 2026-11-19: no EGT on screen for now). On failure an item backs off (×2, max 30 s); after 5 failures in a row the scheduler goes quiet (one probe every 5 s). A TP 2.0 channel that won't open is retried after 2, 4, then every 5 s.
- **Tasks:** `can_rx` (core 0, prio 10) → queue → `diag` (core 0, prio 8, owns all of jd_core) → snapshot → `ui` (core 1, prio 3; labels refresh at 5 Hz and only when their text/colour changed). `sysmon` logs CPU busy % per core, free internal RAM and stack headroom every 30 s.
- **Budget (2026-11-19 build):** image 609 KB (85% of the 4 MB app partition free; LVGL + fonts are 346 KB of it, jd_core 7 KB). Static internal RAM 97 KB of 342 KB (LVGL's 32 KB pool included); at runtime + 38 KB LVGL draw buffers (2 × 240×40 px instead of a 150 KB frame buffer) + ~20 KB stacks. PSRAM (8 MB) unused. **Measured on the board 2026-11-19 (demo build, 320x240 gauge page, every value changing, graph redrawn at 5 Hz):** CPU core0 1 %, core1 (UI) 13 %; internal RAM free 285 KB (lowest 282 KB); stack free: diag 1872, ui 2944, can_rx 2160, sysmon 2332 bytes. Log: `logs/2026-11-19-demo-first-run.log`. (sysmon's first report comes at 60 s: it needs two samples.)
- **Decisions to confirm with Sam / on the car (all are named constants):** warning thresholds in `view.h` are PLACEHOLDERS (EGT 700/800 °C, coolant 110/118, oil 130/140); PID 78 sensor 1 is assumed pre-turbo (`store.c`). Settled 2026-11-19: allowlist additions `02 01 0C`, `A1` keep-alive answer, flow control, `1A 9B`, `18 00 FF 00` and `12 00 04 [hi] [lo]` (only the DTC number is free) approved by Sam (flow control then changed to the captured `30 00 01`); keep-alive 500 ms; when the ECU is silent, one probe every 5 s and TP 2.0 reopen every 5 s at most (Sam: 30 s was too slow; the display is on an ignition-switched socket, so a silent ECU is usually still waking up).
- **Measuring blocks:** a `21 xx` request always returns the whole group (4 VCDS fields); the screen can mix fields from several groups, each group costing one request per refresh. Allowlisted: 099, **007 and 011 (Sam approved 2026-11-19)**. The store decodes 011 fields 2–3 (boost specified/actual) and 007 field 4 (coolant).
- **Not done yet:** BOOT-button page switching, DTC readout in `diag` + screen (requests and decoding are ready), oil temp source, brightness/auto-sleep, CSV logging.

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

**What VCDS sends before reading groups (2026-11-18 `003345` capture, engine channel from setup):** `10 89` → `1A 9B` (identification) → `31 B8 00 00` (**routine control: never allowed**) → `1A 91` → then only `21 xx`. Our firmware sends `10 89` → `1A 9B` (approved 2026-11-19; a refusal is ignored) → `21 xx`. **Whether the engine answers `21 xx` without `31 B8` is UNCONFIRMED**: the first thing the phase-3 TP 2.0 test shows. `31 B8` stays forbidden (rule 2); if the engine insists on it, the OBD-II PID 78 route is the fallback.

**Group replies (2026-11-18, 1950 replies over 6 groups):** always 4 packets (`2x 2x 2x 1x`, one ACK), 25–33 ms after the request, never `7F` or `78`. The formula byte of each field never changed between samples, so each field can be decoded with a fixed formula. Formula `21` (duty cycle, group 011): `21 FF FF` = 100.0 % ✓ VCDS. Formula `1F` (033 fields 3–4, `1F 50 20` = 1.00 in VCDS) and `32` (100 fields 3–4, DPF pressure, `32 27 82` = 5 mbar) **can't be pinned down from these samples** (one value / rounding ambiguous): still shown as no value.

**VCDS polling rhythm (2026-11-18):** with 3 groups open, one `21 xx` request every **~140 ms** (~7/s total, round-robin), plus `740#A3` keep-alive about every **0.55 s** (0.49–0.64 s), even while polling. Our keep-alive is 500 ms (`JD_TP20_KEEPALIVE_MS`). How long the engine keeps an idle channel open was never measured, so don't go slower than VCDS. When a module sends `A3` itself, VCDS answers `A1 0F 64 9C [module T3] 9C`; we (like macvagdiag) answer `A1 0F 8A FF 32 FF` (allowlisted; unverified but it worked for macvagdiag). Treat that as the ceiling; the ESP32 should poll much slower (rule 4). No `9x` replies in any v2 capture.

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
- **Flow control:** VCDS sent `7E0#30 00 01 00 00 00 00 00` (STmin 1 ms) before every PID 78 second frame (329×). We send exactly that. The engine answered `7DF` in 1–13 ms.

## Where the data comes from
- **EGT: solved.** Primary: TP 2.0 group 099 field 2 (`21 63`, EGT before turbo). Alternative: OBD-II PID `78` on `7E0` (simpler ISO-TP single-frame request).
- **Oil temp: NOT available from the engine ECU** (no `5C`, group 007 field 2 is empty, VCDS shows it blank). Next candidate: the **instrument cluster (module 17)**, which likely reads the oil level/temperature sender (UNCONFIRMED). Reading it means talking to a second module, which **needs Sam's approval to change rule 3**. Fallback: a physical oil temp sender + ADC.
- Route decision is pending the cluster check. If the cluster has oil temp over TP 2.0, a single TP 2.0 stack can serve both values.

Whichever route works, its exact request bytes go into the allowlist: for TP 2.0 to the engine, the `200` setup for address `01` only, `A0`, `A3`, `A8`, ACKs, `10 89`, `21 63` (and any other approved groups); for OBD-II, exact `7E0` frames such as `02 01 78`, `02 01 05`.

## Fault codes (DTC readout): planned major feature
Goal: show the car's stored fault codes on the display right away (at start-up and on demand), each with a plain-language description, so Sam doesn't need a laptop to see why a light came on.
- **Read only.** Reading DTCs is allowed; **clearing them stays forbidden** (rule 2: no `14`, no OBD-II mode `04`). Request bytes go in the allowlist like any other.
- **Engine fault-code request is known (macvagdiag, 2026-11-13 capture, and macvagdiag's own scan on the car 2026-11-18 matched VCDS):** KWP `18 00 FF 00` over the TP 2.0 engine channel (the engine wants status `00`; other KWP modules `18 02 FF 00`). Reply `58 [count] ([hi][lo][status])…`. **P-code = `P` + the 16-bit value in hex** (`04 01` → P0401); VCDS's 6-digit number = its decimal. Status: bit 7 = MIL on, bit 6 clear = intermittent, bits 0–4 = the "- 00x" sub-type. Freeze frame per DTC: `12 00 04 [hi] [lo]` (once per list entry, in order: the ECU keeps a cursor). Layout and decoding: macvagdiag `diag/CLAUDE.md` and `local/analysis/2026-11-13-sniffing-analysis.md` §5. **On the allowlist since 2026-11-19 (Sam approved)**, with `jd_kwp_parse_dtcs` / `jd_dtc_pcode` decoding it (tested on the 12 captured engine faults); not yet requested by `diag` or shown on screen. OBD-II mode `03` on `7E0` was never captured.
- **Engine only for now** (rule 3). Codes from other modules (DSG, ABS, airbag, cluster) would need Sam's approval to extend rule 3.
- **Descriptions:** a code → text table stored in flash. Generic SAE P-code descriptions are fine to use. **Don't copy Ross-Tech/VCDS's text or database** (proprietary); write descriptions ourselves or use openly licensed sources, since the repo is public (MIT).
- UI: readable list with code, short description and status (stored/pending/intermittent). Touch to scroll/open details when parked; nothing that needs touch while driving.

## Phases
1. **Bench bring-up (current):** display running (done), transceiver wired to GPIO2/3. No car. Bring up touch on the replacement board when it arrives.
2. **Listen-only on the car** at 500 kbit/s with VCDS or macvagdiag talking. Confirm clean frames and no bus errors. (The CANable + `can-log` can also record exactly what the ESP32 sends, to verify its allowlist on the car.)
3. **One read:** a single allowlisted request (e.g. coolant PID `05` on `7E0`, which also proves physical mode 01) shown as text. Before/after VCDS scans to confirm no new faults.
4. **v1 gauges:** EGT + oil temp (source TBD), poll scheduler, back-off, warning thresholds, optional beep.
5. **Fault code readout (major feature):** read stored DTCs from the engine and show code + description (see "Fault codes (DTC readout)").
6. **Polish:** gauge pages, brightness, mount/enclosure (heat, sun), auto-sleep when the ECU stops replying.

## Open questions
1. Which socket on the board provides 3.3 V and GND for the CAN module, and what is the expansion socket's pin order? (Check the printed labels or a photo.)
2. ~~Does the CJAA ECU support PIDs `5C` / `78`?~~ Answered 2026-11-18: `5C` no, `78` yes.
3. ~~Which groups hold oil temp and EGT?~~ EGT: group 099 field 2. Oil temp: the engine has none (group 007 field 2 empty).
4. **Where does oil temp come from?** Check VCDS → 17-Instruments → Measuring Blocks for an oil temperature value (listen-only capture + VCDS log, like 2026-11-18). If it's there: which protocol, request and formula; then Sam decides whether rule 3 extends to the cluster.
5. Does the engine answer mode 01 on physical `7E0` (VCDS only used `7DF`)?
6. ~~Which request reads DTCs from the engine?~~ `18 00 FF 00` over TP 2.0, allowlisted 2026-11-19 (see "Fault codes"). **Still open:** where the descriptions come from.
7. Is the transceiver's termination a jumper or a soldered resistor?
8. Is the car's 12 V / USB socket ignition-switched? (If it's always-on, revisit the 5 s probe: frames with the ignition off might wake the bus.)
9. Does the engine answer `21 xx` without the `31 B8` step VCDS does first? (phase 3)
10. ~~Which groups go on the screen?~~ Sam 2026-11-19: oil, coolant (007), boost specified/actual (011), no EGT for now. Still open: final warning thresholds (placeholders, Sam: fine for now).

## Next steps
1. ~~Install ESP-IDF 5.4.1~~ done. ~~Display working~~ done. ~~v2 sniffing session~~ done 2026-11-18. ~~Project skeleton with the allowlist guard first~~ done 2026-11-19 (see "Software").
2. **Bench:** flash `firmware/` (default: PASSIVE, transmit off) on the bench board. Check the gauge screen draws correctly, read the sysmon CPU/RAM log (`tools/serial_log.py`), then wire the transceiver (open questions 1, 6).
3. ~~Pick the groups for the screen~~ done 2026-11-19 (011 + 007, allowlisted and polled).
4. **Car, phase 2:** that build in listen-only while VCDS reads groups 011 and 007: boost and coolant should appear on screen with 0 bus errors. Same session: cluster (17) measuring blocks, listen-only, to find oil temp (open question 4).
5. **Car, phase 3:** enable transmit (menuconfig: mode + "Allow CAN transmit"). OBD mode first (`02 01 05` on `7E0`, open question 5), then TP20 mode (open question 9). VCDS fault scan before and after; CANable logging what the ESP32 sends.
6. Fault-code page: request `18 00 FF 00` from `diag`, show P-codes (decoding ready), touch to browse when the replacement board arrives. Descriptions source still open.
