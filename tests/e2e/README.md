# Arducam MEGA – End-to-End Tests

These tests run on **real hardware** with a physical Arducam MEGA camera module
connected over SPI.  They exercise the full driver stack from the Zephyr video
API down to the SPI bus.

## Prerequisites

| Requirement | Notes |
|-------------|-------|
| Zephyr SDK | ≥ 0.16 |
| `west` | installed and workspace initialised (`west update`) |
| Supported board | see table below |
| Arducam MEGA module | 3MP or 5MP variant |

## Supported Boards & Wiring

### STM32 NUCLEO-F401RE

| Camera pin | Board pin |
|------------|-----------|
| MOSI | PA7 (SPI1_MOSI) |
| MISO | PA6 (SPI1_MISO) |
| SCK | PA5 (SPI1_SCK) |
| CS | PA4 |
| VCC | 3.3 V |
| GND | GND |

### Nordic nRF52840 DK

| Camera pin | Board pin |
|------------|-----------|
| MOSI | P0.13 (Arduino D11) |
| MISO | P0.12 (Arduino D12) |
| SCK | P0.14 (Arduino D13) |
| CS | P0.15 (Arduino D10) |
| VCC | VDD (3.3 V) |
| GND | GND |

### Raspberry Pi Pico (RP2040)

| Camera pin | Board pin |
|------------|-----------|
| MOSI | GP19 (SPI0_TX) |
| MISO | GP16 (SPI0_RX) |
| SCK | GP18 (SPI0_SCK) |
| CS | GP17 |
| VCC | 3.3 V |
| GND | GND |

### ESP32-DevKitC (WROOM)

| Camera pin | Board pin |
|------------|-----------|
| MOSI | GPIO23 (VSPI MOSI) |
| MISO | GPIO19 (VSPI MISO) |
| SCK | GPIO18 (VSPI CLK) |
| CS | GPIO5 |
| VCC | 3.3 V |
| GND | GND |

## Building & Flashing

Replace `<board>` with one of:
`nucleo_f401re`, `rpi_pico`, `nrf52840dk/nrf52840`, `esp32_devkitc_wroom/esp32/procpu`

```sh
west build -p always -b <board> tests/e2e
west flash
```

The board overlay in `tests/e2e/boards/<board>.overlay` is picked up
automatically by the build system.

## Viewing Test Output

Connect a serial terminal at 115200 8N1 (adjust port as needed):

```sh
# Linux
minicom -D /dev/ttyACM0 -b 115200

# macOS
screen /dev/cu.usbmodem* 115200

# west (any platform)
west espressif monitor   # ESP32 only
```

A passing run ends with:

```
PROJECT EXECUTION SUCCESSFUL
```

A failing run ends with:

```
PROJECT EXECUTION FAILED
```

## Test Suites

| Suite | What it checks |
|-------|---------------|
| `init` | Driver initialises and device is ready |
| `capabilities` | `video_get_caps()` returns ≥ 3 formats |
| `format_set` | `video_set_format` / `video_get_format` round-trip for JPEG, RGB565, YUYV |
| `capture_jpeg` | Single JPEG frame at QVGA and 720p; validates SOI marker (`0xFF 0xD8`) |
| `capture_rgb` | RGB565 QVGA frame; validates exact byte count (320×240×2) |
| `streaming` | 5 consecutive JPEG frames with monotonic timestamps |
| `controls` | Brightness and white-balance controls; unknown CID returns `-ENOTSUP` |

## Running with Twister (build-only by default)

`testcase.yaml` marks the suite `build_only: true` because twister cannot
attach to physical hardware in CI.  To run on hardware, remove that flag and
attach the board:

```sh
west twister -T tests/e2e -p nucleo_f401re --device-testing \
             --device-serial /dev/ttyACM0
```

To do a build-only check across all supported platforms:

```sh
west twister -T tests/e2e
```

## Troubleshooting

| Symptom | Likely cause |
|---------|-------------|
| `Camera device is not ready` | SPI wiring incorrect or camera not powered |
| `video_dequeue` timeout | Camera not responding; check CS and SCK connections |
| `JPEG SOI byte mismatch` | Frame buffer overflow or incorrect format selection |
| Build error: `arducam,mega` not found | Wrong board selected or overlay not applied |
