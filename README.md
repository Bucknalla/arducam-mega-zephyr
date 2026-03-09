# arducam-mega-zephyr

A Zephyr out-of-tree driver for the **Arducam MEGA SPI camera module**
(3 MP and 5 MP variants), implementing the standard
[Zephyr video subsystem API](https://docs.zephyrproject.org/latest/hardware/peripherals/video.html).

## Contents

```
arducam-mega-zephyr/
├── CMakeLists.txt                  Module root CMake
├── Kconfig                         Root Kconfig (chains to driver)
├── west.yml                        West workspace manifest
├── zephyr/
│   └── module.yml                  West module descriptor
├── dts/
│   └── bindings/
│       └── video/
│           └── arducam,mega.yaml   Devicetree binding
├── drivers/
│   └── video/
│       ├── arducam_mega.c          Driver implementation
│       ├── CMakeLists.txt
│       └── Kconfig
├── include/
│   └── arducam_mega.h              Public API extensions
├── samples/
│   └── capture/                    JPEG capture sample
│       ├── src/main.c
│       ├── CMakeLists.txt
│       ├── prj.conf
│       ├── sample.yaml
│       └── boards/                 Board-specific DTS overlays
├── tests/
│   ├── unit/                       Unit tests (run on native_sim/qemu)
│   │   ├── src/main.c
│   │   ├── CMakeLists.txt
│   │   ├── prj.conf
│   │   ├── testcase.yaml
│   │   └── arducam_mega_unit_test.overlay
│   └── e2e/                        End-to-end tests (real hardware)
│       ├── src/main.c
│       ├── CMakeLists.txt
│       ├── prj.conf
│       ├── testcase.yaml
│       └── boards/                 Board-specific DTS overlays
└── scripts/
    └── decode_frame.py             Host-side helper to decode UART output
```

## Hardware

### Supported modules

| Module | Model   | Resolution (max) | Sensor |
|--------|---------|-----------------|--------|
| B0371  | 3 MP    | 2048 × 1536     | OV3640 |
| B0434  | 5 MP    | 2592 × 1944     | OV5642 |

### Wiring

The camera communicates over a 4-wire SPI bus (no I²C lines needed):

| Camera pin | Signal      |
|------------|-------------|
| MOSI       | SPI MOSI    |
| MISO       | SPI MISO    |
| SCK        | SPI CLK     |
| CS         | GPIO (any)  |
| VCC        | 3.3 V       |
| GND        | GND         |

> **Note:** The maximum SPI clock is **8 MHz** (SPI mode 0, CPOL=0, CPHA=0).

## Quick start

### 1. Set up the Zephyr workspace

```bash
pip install west
west init -m https://github.com/Bucknalla/arducam-mega-zephyr --mr main workspace
cd workspace
west update
west zephyr-export
pip install -r zephyr/scripts/requirements.txt
```

### 2. Add a board overlay

Create (or copy from `tests/e2e/boards/`) a DTS overlay for your board.
The overlay must define a node labelled `arducam_mega_cam`:

```dts
/* boards/<your_board>.overlay */
&spi1 {
    status = "okay";
    cs-gpios = <&gpioa 4 GPIO_ACTIVE_LOW>;

    arducam_mega_cam: camera@0 {
        compatible = "arducam,mega";
        reg = <0>;
        spi-max-frequency = <8000000>;
    };
};
```

### 3. Build and flash the sample

```bash
cd workspace
west build -b nucleo_f401re arducam-mega-zephyr/samples/capture \
    -- -DEXTRA_DTC_OVERLAY_FILE=boards/nucleo_f401re.overlay
west flash
```

### 4. Decode the JPEG output

```bash
# Capture UART output and convert hex to JPEG
python3 arducam-mega-zephyr/scripts/decode_frame.py \
    --port /dev/ttyUSB0 --baud 115200 --output frame.jpg
# Open frame.jpg with any image viewer
```

## Running the tests

### Unit tests (no hardware required)

```bash
# Using west + twister
cd workspace
./zephyr/scripts/twister -T arducam-mega-zephyr/tests/unit \
    -p native_sim --inline-logs

# Or build manually
west build -b native_sim arducam-mega-zephyr/tests/unit
./build/zephyr/zephyr.exe
```

### E2E tests (real hardware)

```bash
# Build for your board
west build -b nucleo_f401re arducam-mega-zephyr/tests/e2e

# Flash and observe results via serial
west flash
west espressif monitor   # or minicom / screen
```

## Using the driver API

The driver implements the standard Zephyr video API.  A minimal capture
loop looks like:

```c
#include <zephyr/drivers/video.h>

const struct device *cam = DEVICE_DT_GET(DT_NODELABEL(arducam_mega_cam));

/* 1. Choose format */
struct video_format fmt = {
    .pixelformat = VIDEO_PIX_FMT_JPEG,
    .width  = 640,
    .height = 480,
};
video_set_format(cam, VIDEO_EP_OUT, &fmt);

/* 2. Allocate and enqueue a buffer */
struct video_buffer *vbuf = video_buffer_alloc(640 * 480 * 2);
video_enqueue(cam, VIDEO_EP_OUT, vbuf);

/* 3. Start streaming and wait for a frame */
video_stream_start(cam);

struct video_buffer *filled;
video_dequeue(cam, VIDEO_EP_OUT, &filled, K_SECONDS(5));

video_stream_stop(cam);

/* 4. Use filled->buffer / filled->bytesused */
process_image(filled->buffer, filled->bytesused);

video_buffer_release(filled);
```

### Supported pixel formats

| Zephyr format       | Camera register | Notes                 |
|---------------------|-----------------|-----------------------|
| `VIDEO_PIX_FMT_JPEG`   | `0x01`       | Variable-length output|
| `VIDEO_PIX_FMT_RGB565` | `0x02`       | 2 bytes/pixel         |
| `VIDEO_PIX_FMT_YUYV`   | `0x03`       | 2 bytes/pixel         |

### Supported resolutions

| Mode  | Width | Height | Reg value |
|-------|-------|--------|-----------|
| 96×96  | 96   | 96     | `0x00`    |
| QQVGA  | 160  | 120    | `0x02`    |
| QVGA   | 320  | 240    | `0x03`    |
| VGA    | 640  | 480    | `0x05`    |
| SVGA   | 800  | 600    | `0x06`    |
| XGA    | 1024 | 768    | `0x07`    |
| HD     | 1280 | 720    | `0x08`    |
| SXGA   | 1280 | 1024   | `0x09`    |
| UXGA   | 1600 | 1200   | `0x0A`    |
| FHD    | 1920 | 1080   | `0x0B`    |
| QXGA   | 2048 | 1536   | `0x0C`    |
| WQXGA2 | 2592 | 1944   | `0x0D`    |

## Kconfig options

| Option                   | Default | Description                     |
|--------------------------|---------|---------------------------------|
| `CONFIG_VIDEO_ARDUCAM_MEGA` | `y` if DT node present | Enable the driver |

## SPI protocol reference

| Operation         | Command byte   | Data phase                         |
|-------------------|---------------|------------------------------------|
| Register write    | `addr \| 0x80` | 1 data byte                       |
| Register read     | `addr & 0x7F`  | dummy, dummy → read byte 2        |
| Burst FIFO read   | `0x3C`         | dummy → read N bytes              |
| Single FIFO read  | `0x3D`         | dummy, dummy → read byte 2        |

Key registers:

| Register | Address | Description                         |
|----------|---------|-------------------------------------|
| FIFO_CTRL | `0x04` | `0x01` clear, `0x02` start capture  |
| SENSOR_RESET | `0x07` | `0x40` assert reset             |
| FORMAT   | `0x20`  | `0x01` JPEG, `0x02` RGB, `0x03` YUV |
| RESOLUTION | `0x21` | Resolution mode (see table above)  |
| BRIGHTNESS | `0x22` | 0x00–0x08                          |
| CAP_STATUS | `0x44` | bit 2 = capture done               |
| FIFO_SIZE1 | `0x45` | FIFO length [7:0]                  |
| FIFO_SIZE2 | `0x46` | FIFO length [15:8]                 |
| FIFO_SIZE3 | `0x47` | FIFO length [23:16]                |

## License

Apache-2.0 – see [SPDX headers](drivers/video/arducam_mega.c) in each file.
