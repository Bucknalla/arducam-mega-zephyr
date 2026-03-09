# Zephyr Video Subsystem API

Source: https://docs.zephyrproject.org/latest/hardware/peripherals/video.html

## Architecture

The video subsystem provides a generic interface for devices that produce, process, consume, or transform video data. A driver exposes a `video_driver_api` struct of function pointers that the generic API dispatches through.

## video_driver_api Callbacks

```c
struct video_driver_api {
    video_api_enqueue_t    enqueue;       /* hand empty buffer to driver */
    video_api_dequeue_t    dequeue;       /* retrieve filled buffer */
    video_api_flush_t      flush;         /* discard pending buffers */
    video_api_stream_start_t stream_start;
    video_api_stream_stop_t  stream_stop;
    video_api_set_fmt_t    set_fmt;
    video_api_get_fmt_t    get_fmt;
    video_api_set_ctrl_t   set_ctrl;
    video_api_get_ctrl_t   get_ctrl;
};
```

## Key Structures

### video_format
| Field | Description |
|-------|-------------|
| `width` / `height` | Frame dimensions in pixels |
| `pitch` | Bytes per row (≥ width) |
| `pixelformat` | FourCC: `VIDEO_PIX_FMT_JPEG`, `VIDEO_PIX_FMT_RGB565`, `VIDEO_PIX_FMT_YUYV`, … |

### video_buffer
| Field | Description |
|-------|-------------|
| `buffer` | Pointer to data memory |
| `size` | Total allocation size |
| `bytesused` | Bytes written by driver after capture |
| `timestamp` | ms timestamp when frame was captured |
| `flags` | Status bits |

## Common API Calls

```c
/* Format */
video_set_format(dev, VIDEO_EP_OUT, &fmt);
video_get_format(dev, VIDEO_EP_OUT, &fmt);

/* Buffer lifecycle */
struct video_buffer *vbuf = video_buffer_alloc(size);
video_enqueue(dev, VIDEO_EP_OUT, vbuf);
video_stream_start(dev);
video_dequeue(dev, VIDEO_EP_OUT, &vbuf, K_SECONDS(5));
video_stream_stop(dev);
video_buffer_release(vbuf);

/* Controls */
video_set_ctrl(dev, VIDEO_CID_CAMERA_BRIGHTNESS, &val);
video_get_ctrl(dev, VIDEO_CID_CAMERA_BRIGHTNESS, &val);
```

## Endpoint Types

- **`VIDEO_EP_OUT`** — source endpoint (camera sensor produces frames here)
- **`VIDEO_EP_IN`**  — sink endpoint (display/encoder consumes frames here)

## Buffer Management Notes

The subsystem is transitioning from a custom queue to **RTIO** (Real-Time I/O) for reduced context switching and a unified polling API. An RTIO shim maintains backwards compatibility during the transition. New drivers are encouraged to target RTIO.

## Reference Driver: MT9M114

A good implementation reference. Demonstrates:
- I2C register access for sensor configuration
- Three resolution modes (480×272, 640×480, 1280×720)
- RGB565 and YUYV formats
- Horizontal/vertical flip controls via `set_ctrl`
- Chip-ID verification during `init`

Source: https://github.com/zephyrproject-rtos/zephyr/blob/main/drivers/video/mt9m114.c

## References

- Video peripheral docs: https://docs.zephyrproject.org/latest/hardware/peripherals/video.html
- API group reference: https://docs.zephyrproject.org/apidoc/latest/group__video__interface.html
- Video capture sample: https://docs.zephyrproject.org/latest/samples/drivers/video/capture/README.html
- RTIO discussion (PR #17194): https://github.com/zephyrproject-rtos/zephyr/pull/17194
- API enhancement issue: https://github.com/zephyrproject-rtos/zephyr/issues/72959
