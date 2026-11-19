# Jetta ESP32 Display

A small always-on gauge display for a 2013 VW Jetta TDI (CJAA engine). It shows engine values the dash doesn't, starting with **exhaust gas temperature** and **oil temperature**, by asking the engine ECU for them over the OBD port's diagnostic CAN bus.

**Read-only by design.** Every CAN transmit is checked against a fixed allowlist of request bytes. The display never clears codes, writes to modules, or opens programming/security sessions.

## Hardware
- LCDWIKI ES3N28P: ESP32-S3, 2.8" 240x320 IPS, ILI9341V
- SN65HVD230 CAN transceiver on GPIO2 (TX) / GPIO3 (RX)
- OBD-II pigtail (pins 6/14 CAN, 4/5 ground), all solderless

## Status
- Display bring-up done (`ripple_test/`, a throwaway bench test)
- Car protocol mapped from listen-only captures: VW TP 2.0 + KWP2000, plus OBD-II on `7E0`
- CAN firmware not started yet

## Building
ESP-IDF 5.4.1 (with Python 3.12):

```sh
. ~/esp/esp-idf/export.sh
cd ripple_test
idf.py build flash
```

## Related
- [Mac VAG Diagnostic Tool](https://github.com/Sam-Sanjay-Satish/Mac-VAG-Diagnostic-Tool): the Mac tool used to sniff and decode the car's diagnostic traffic.

## Credits
`reference/ILI9341V_Init.txt` is the display init sequence from LCDWIKI's vendor demo.

## License
MIT
