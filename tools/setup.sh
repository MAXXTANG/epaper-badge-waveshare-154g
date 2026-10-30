#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
command -v arduino-cli >/dev/null 2>&1 || { echo 'Install Arduino CLI first: https://arduino.github.io/arduino-cli/' >&2; exit 1; }
mkdir -p "$root/.dev/data" "$root/.dev/downloads" "$root/.dev/user"
config="$root/.dev/arduino-cli.yaml"
secrets="$root/firmware/EpaperBLE/DeviceSecrets.h"
if [ ! -f "$secrets" ]; then
  cp "$root/firmware/EpaperBLE/DeviceSecrets.example.h" "$secrets"
  echo 'Choose an 8-63 character Wi-Fi password in firmware/EpaperBLE/DeviceSecrets.h before compiling.'
fi
cat > "$config" <<EOF
directories:
  data: "$root/.dev/data"
  downloads: "$root/.dev/downloads"
  user: "$root/.dev/user"
board_manager:
  additional_urls:
    - https://espressif.github.io/arduino-esp32/package_esp32_index.json
EOF
if arduino-cli --config-file "$config" core list | awk '$1 == "esp32:esp32" && $2 == "3.3.12" {found=1} END {exit !found}'; then
  echo 'Arduino-ESP32 3.3.12 is already installed.'
else
  arduino-cli --config-file "$config" core update-index
  arduino-cli --config-file "$config" core install esp32:esp32@3.3.12
fi
