# Third-party sources

## Waveshare

The files in `vendor/waveshare/` are retained reference sources from:

- Repository: https://github.com/waveshareteam/ESP32-S3-ePaper-1.54G
- Commit: `55eb1278f482c0cfd1e0cba5611a4132022eea48`
- Original directory: `Example/Arduino_3.2.0/examples/08_E_paper_test/`

The display driver, drawing helpers, and fonts in `firmware/EpaperBLE/` derive from those sources. Original author and permission notices are preserved in each file. The display driver has local timeout handling changes.

## Adafruit

The Swift files in `vendor/adafruit/` are reference material used to verify the Bluefruit Connect image transfer protocol and image picker behavior. They are not built into this firmware.

- Repository: https://github.com/adafruit/Bluefruit_LE_Connect_v2
- Commit: `26da88b2a667e1de42e029959f405f40130a10d8`
- License: MIT, reproduced in `vendor/adafruit/LICENSE`.

## Build dependencies

Arduino-ESP32 and its bundled libraries are installed separately by `tools/setup.sh`; they are not included in this repository. Their upstream licenses apply.
