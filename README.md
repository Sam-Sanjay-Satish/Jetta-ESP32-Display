# Jetta ESP32 Display

A small always-on gauge display for a 2013 VW Jetta TDI (CJAA engine). It shows engine values the dash doesn't, starting with **exhaust gas temperature** and **oil temperature**, plus **instant fault code readout** with plain-language descriptions, by asking the engine ECU for them over the OBD port's diagnostic CAN bus.

**Read-only by design.** Every CAN transmit is checked against a fixed allowlist of request bytes. The display never clears codes, writes to modules, or opens programming/security sessions.

## Hardware
- LCDWIKI ES3C28P: ESP32-S3, 2.8" 240x320 IPS, ILI9341V, FT6336G capacitive touch
- SN65HVD230 CAN transceiver on GPIO2 (TX) / GPIO3 (RX)
- OBD-II pigtail (pins 6/14 CAN, 4/5 ground), all solderless

## Status
- Display bring-up done (`ripple_test/`, a throwaway bench test)
- Car protocol mapped from listen-only captures: VW TP 2.0 + KWP2000, plus OBD-II on `7E0`
- Gauge firmware written (`firmware/`): tested diagnostic core, LVGL gauge screen. Not yet run on the car.
- Touch: replacement board on the way. Fault code readout: next.

## Layout
- `firmware/components/jd_core/`: the diagnostic core in plain C (allowlist, ISO-TP, TP 2.0, decoders, scheduler). No hardware code, no `malloc`.
- `firmware/main/`: ESP32 side (CAN controller, display, LVGL screen, tasks).
- `test/host/`: unit tests for the core, run on a Mac or PC.

## Tests
```sh
make -C test/host
```
Needs clang. Tests run with AddressSanitizer and UndefinedBehaviorSanitizer.

## Building
ESP-IDF 5.4.1 (with Python 3.12):

```sh
. ~/esp/esp-idf/export.sh
cd firmware
idf.py build flash
```
The default build is **listen-only** (the CAN controller cannot transmit). Transmitting needs `idf.py menuconfig` → *Jetta display* → a non-passive mode **and** "Allow CAN transmit".

## Related
- [Mac VAG Diagnostic Tool](https://github.com/Sam-Sanjay-Satish/Mac-VAG-Diagnostic-Tool): the Mac tool used to sniff and decode the car's diagnostic traffic.

## Credits
`reference/ILI9341V_Init.txt` is the display init sequence from LCDWIKI's vendor demo. `test/host/unity/` is [Unity](https://github.com/ThrowTheSwitch/Unity) (MIT).

## License
MIT
