#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
if [ ! -f "$root/.dev/arduino-cli.yaml" ]; then
  echo 'Run sh tools/setup.sh first.' >&2
  exit 1
fi
if [ ! -f "$root/firmware/EpaperBLE/DeviceSecrets.h" ]; then
  echo 'Copy DeviceSecrets.example.h to DeviceSecrets.h and set your Wi-Fi password first.' >&2
  exit 1
fi
python3 "$root/tools/embed_web.py"
arduino-cli --config-file "$root/.dev/arduino-cli.yaml" compile \
  --fqbn 'esp32:esp32:esp32s3:FlashSize=8M,PartitionScheme=default_8MB,PSRAM=opi,CDCOnBoot=cdc,USBMode=hwcdc' \
  --build-path "$root/.dev/build" --warnings default "$root/firmware/EpaperBLE"
