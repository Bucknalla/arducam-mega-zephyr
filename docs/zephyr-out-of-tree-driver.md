# Zephyr Out-of-Tree Driver Structure

Source: https://docs.zephyrproject.org/3.4.0/samples/application_development/out_of_tree_driver/README.html

## Module Directory Layout

```
arducam-mega-zephyr/
├── zephyr/
│   └── module.yml              # REQUIRED: module manifest
├── dts/
│   └── bindings/
│       └── video/
│           └── arducam,mega.yaml
├── drivers/
│   └── video/
│       ├── CMakeLists.txt
│       ├── Kconfig
│       └── arducam_mega.c
├── CMakeLists.txt
├── Kconfig
└── west.yml
```

## zephyr/module.yml

```yaml
name: arducam-mega-zephyr
build:
  cmake: .
  kconfig: Kconfig
settings:
  dts_root: .
```

- `build.cmake` — path to root `CMakeLists.txt` (relative to module root)
- `build.kconfig` — path to root `Kconfig`
- `settings.dts_root` — directory that contains the `dts/` bindings tree

## CMakeLists.txt (root)

```cmake
zephyr_library()
add_subdirectory(drivers/video)
```

## drivers/video/CMakeLists.txt

```cmake
zephyr_library_sources_ifdef(CONFIG_ARDUCAM_MEGA arducam_mega.c)
```

## Kconfig (root)

```kconfig
menu "Arducam MEGA"

config ARDUCAM_MEGA
    bool "Arducam MEGA SPI camera driver"
    depends on SPI
    depends on VIDEO
    help
      Enable the Arducam MEGA SPI camera driver.

endmenu
```

## Device Tree Binding: dts/bindings/video/arducam,mega.yaml

```yaml
description: Arducam MEGA SPI camera

compatible: "arducam,mega"

include:
  - name: spi-device.yaml

properties:
  spi-max-frequency:
    type: int
    default: 8000000
  reset-gpios:
    type: phandle-array
  pwdn-gpios:
    type: phandle-array
```

## west.yml Integration (consumer application)

```yaml
manifest:
  remotes:
    - name: arducam
      url-base: https://github.com/your-org
  projects:
    - name: arducam-mega-zephyr
      remote: arducam
      revision: main
      path: modules/arducam-mega-zephyr
  self:
    path: app
```

Then run `west update` to fetch the module.

## Driver Source Skeleton

```c
struct arducam_mega_config {
    struct spi_dt_spec spi;
    struct gpio_dt_spec reset_gpio;
    struct gpio_dt_spec pwdn_gpio;
};

struct arducam_mega_data {
    struct video_format fmt;
    bool streaming;
};

static const struct video_driver_api arducam_mega_api = {
    .enqueue      = arducam_mega_enqueue,
    .dequeue      = arducam_mega_dequeue,
    .set_fmt      = arducam_mega_set_fmt,
    .get_fmt      = arducam_mega_get_fmt,
    .stream_start = arducam_mega_stream_start,
    .stream_stop  = arducam_mega_stream_stop,
    .set_ctrl     = arducam_mega_set_ctrl,
    .get_ctrl     = arducam_mega_get_ctrl,
};

static int arducam_mega_init(const struct device *dev) { ... }

DEVICE_DT_INST_DEFINE(0, arducam_mega_init, NULL,
    &arducam_mega_data, &arducam_mega_config,
    POST_KERNEL, CONFIG_VIDEO_INIT_PRIORITY,
    &arducam_mega_api);
```

## References

- Out-of-tree driver sample: https://docs.zephyrproject.org/3.4.0/samples/application_development/out_of_tree_driver/README.html
- Example application repo: https://github.com/zephyrproject-rtos/example-application
- Golioth guide: https://blog.golioth.io/adding-an-out-of-tree-sensor-driver-to-zephyr/
- Jonas Otto guide: https://jonasotto.com/posts/zephyr_out_of_tree_driver/
- Device tree bindings syntax: https://docs.zephyrproject.org/latest/build/dts/bindings-syntax.html
- Devicetree HOWTOs: https://docs.zephyrproject.org/latest/build/dts/howtos.html
- Beyondlogic DT overlays: https://www.beyondlogic.org/devicetree-overlays-on-zephyr-rtos-adding-i2c-or-spi/
