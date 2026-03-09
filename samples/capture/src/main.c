/*
 * Copyright (c) 2024 Arducam MEGA Zephyr Driver Contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Sample: Arducam MEGA – single-shot JPEG capture streamed over UART.
 *
 * The sample captures one JPEG frame and writes it to the UART console
 * as a hex dump.  This can be redirected to a file on a host PC and
 * decoded with any JPEG viewer:
 *
 *   west flash && minicom -D /dev/ttyUSB0 -b 115200 | \
 *       python3 -c "import sys,binascii; \
 *           open('out.jpg','wb').write(binascii.unhexlify(sys.stdin.read()))"
 *
 * Alternatively, set CONFIG_ARDUCAM_SAMPLE_RAW_OUTPUT=y to write raw
 * bytes directly to the UART (useful when the console can handle binary).
 *
 * Board overlay:
 *   Copy or symlink one of the overlays from tests/e2e/boards/ into
 *   samples/capture/boards/ and rename it to match your board.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/video.h>
#include <zephyr/logging/log.h>
#include <stdio.h>

LOG_MODULE_REGISTER(arducam_sample, LOG_LEVEL_INF);

/* Obtain the camera device at build time from the DTS label */
#define CAM_NODE DT_NODELABEL(arducam_mega_cam)

/* Maximum buffer size – enough for a JPEG at SVGA quality */
#define BUF_SIZE (800 * 600)

int main(void)
{
	const struct device *cam = DEVICE_DT_GET(CAM_NODE);

	if (!device_is_ready(cam)) {
		LOG_ERR("Arducam MEGA device not ready");
		return -ENODEV;
	}

	LOG_INF("Arducam MEGA sample started");

	/* ---------------------------------------------------------------- *
	 * 1. Configure: JPEG, QVGA (320x240)
	 * ---------------------------------------------------------------- */
	struct video_format fmt = {
		.pixelformat = VIDEO_PIX_FMT_JPEG,
		.width       = 320,
		.height      = 240,
	};

	int ret = video_set_format(cam, VIDEO_EP_OUT, &fmt);

	if (ret < 0) {
		LOG_ERR("video_set_format failed: %d", ret);
		return ret;
	}
	LOG_INF("Format: %ux%u JPEG", fmt.width, fmt.height);

	/* ---------------------------------------------------------------- *
	 * 2. Allocate a buffer
	 * ---------------------------------------------------------------- */
	struct video_buffer *vbuf = video_buffer_alloc(BUF_SIZE);

	if (!vbuf) {
		LOG_ERR("Failed to allocate video buffer (%u bytes)", BUF_SIZE);
		return -ENOMEM;
	}

	/* ---------------------------------------------------------------- *
	 * 3. Enqueue the buffer, start stream, wait for a frame
	 * ---------------------------------------------------------------- */
	ret = video_enqueue(cam, VIDEO_EP_OUT, vbuf);
	if (ret < 0) {
		LOG_ERR("video_enqueue failed: %d", ret);
		goto out;
	}

	ret = video_stream_start(cam);
	if (ret < 0) {
		LOG_ERR("video_stream_start failed: %d", ret);
		goto out;
	}

	struct video_buffer *filled;

	ret = video_dequeue(cam, VIDEO_EP_OUT, &filled, K_SECONDS(10));
	if (ret < 0) {
		LOG_ERR("video_dequeue failed: %d", ret);
		video_stream_stop(cam);
		goto out;
	}

	video_stream_stop(cam);

	/* ---------------------------------------------------------------- *
	 * 4. Print the captured frame
	 * ---------------------------------------------------------------- */
	LOG_INF("Captured %u bytes (timestamp=%u ms)",
		filled->bytesused, filled->timestamp);

	/* Emit the JPEG as hex so the console can carry it */
	const uint8_t *p   = (const uint8_t *)filled->buffer;
	const uint8_t *end = p + filled->bytesused;

	printk("JPEG_BEGIN\n");
	while (p < end) {
		/* Print up to 32 bytes per line */
		int chunk = MIN(end - p, 32);

		for (int i = 0; i < chunk; i++) {
			printk("%02x", p[i]);
		}
		printk("\n");
		p += chunk;
	}
	printk("JPEG_END\n");

out:
	video_buffer_release(vbuf);
	return ret;
}
