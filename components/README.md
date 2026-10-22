# Local ESP-IDF components (2.x track only)

Used by the `esp32-devkitc-ve-ble-v2` environment, which rebuilds ESP-IDF with its own
memory settings. The Espressif component registry is replaced by these copies so the
build does not depend on it.

| Component | Version | Source | License |
|---|---|---|---|
| espressif__esp-dsp | v1.5.2 | https://github.com/espressif/esp-dsp | Apache-2.0 |
| joltwallet__littlefs | v1.16.4 (with littlefs submodule) | https://github.com/joltwallet/esp_littlefs | MIT (littlefs: BSD-3-Clause) |

Same versions as in the Arduino 3.1.3 prebuilt libraries (dependencies.lock), so 1.x and
2.x use identical DSP and filesystem code. Examples, tests and docs were removed.
