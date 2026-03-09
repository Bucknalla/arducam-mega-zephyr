/*
 * Copyright (c) 2024 Arducam MEGA Zephyr Driver Contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * End-to-end tests for the Arducam MEGA Zephyr driver.
 *
 * These tests require a physical Arducam MEGA camera module connected
 * to the target board via SPI.  Select the appropriate board overlay
 * from tests/e2e/boards/ when building.
 *
 * Test suites:
 *   - init          Driver initialisation and device presence
 *   - capabilities  video_get_caps() returns valid capabilities
 *   - format_set    video_set_format() / video_get_format() round-trip
 *   - capture_jpeg  Full JPEG frame capture flow
 *   - capture_rgb   Full RGB565 frame capture flow
 *   - streaming     Repeated frame capture in streaming mode
 *   - controls      Camera control set/get (brightness, WB, …)
 */

#include <zephyr/ztest.h>
#include <zephyr/drivers/video.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(arducam_e2e, LOG_LEVEL_DBG);

/* Obtain the camera device from the devicetree.  The board overlay must
 * define a node with compatible = "arducam,mega". */
#define CAM_NODE DT_NODELABEL(arducam_mega_cam)

/* Minimum expected JPEG header magic bytes */
#define JPEG_SOI_BYTE0 0xFF
#define JPEG_SOI_BYTE1 0xD8

/* ------------------------------------------------------------------ *
 * Fixture – shared camera device pointer
 * ------------------------------------------------------------------ */

struct e2e_fixture {
	const struct device *cam;
};

static void *e2e_setup(void)
{
	static struct e2e_fixture fixture;

	fixture.cam = DEVICE_DT_GET(CAM_NODE);
	return &fixture;
}

/* ------------------------------------------------------------------ *
 * Suite: init
 * ------------------------------------------------------------------ */

ZTEST_SUITE(init, NULL, e2e_setup, NULL, NULL, NULL);

ZTEST_F(init, test_device_is_ready)
{
	zassert_not_null(fixture->cam, "Camera device pointer is NULL");
	zassert_true(device_is_ready(fixture->cam),
		     "Camera device is not ready – check SPI wiring and DTS");
}

/* ------------------------------------------------------------------ *
 * Suite: capabilities
 * ------------------------------------------------------------------ */

ZTEST_SUITE(capabilities, NULL, e2e_setup, NULL, NULL, NULL);

ZTEST_F(capabilities, test_get_caps_returns_formats)
{
	struct video_caps caps;
	int ret = video_get_caps(fixture->cam, VIDEO_EP_OUT, &caps);

	zassert_ok(ret, "video_get_caps failed: %d", ret);
	zassert_not_null(caps.format_caps,
			 "format_caps list is NULL");
	/* Expect at least JPEG, RGB565, and YUV422 */
	int fmt_count = 0;

	for (int i = 0; caps.format_caps[i].pixelformat != 0; i++) {
		fmt_count++;
	}
	zassert_true(fmt_count >= 3,
		     "Expected at least 3 format caps, got %d", fmt_count);
}

ZTEST_F(capabilities, test_min_vbuf_count_is_one)
{
	struct video_caps caps;

	zassert_ok(video_get_caps(fixture->cam, VIDEO_EP_OUT, &caps), "");
	zassert_equal(caps.min_vbuf_count, 1,
		      "min_vbuf_count should be 1, got %u",
		      caps.min_vbuf_count);
}

ZTEST_F(capabilities, test_invalid_endpoint_returns_error)
{
	struct video_caps caps;
	int ret = video_get_caps(fixture->cam, VIDEO_EP_IN, &caps);

	zassert_not_equal(ret, 0,
			  "Expected error for VIDEO_EP_IN on a source device");
}

/* ------------------------------------------------------------------ *
 * Suite: format_set
 * ------------------------------------------------------------------ */

ZTEST_SUITE(format_set, NULL, e2e_setup, NULL, NULL, NULL);

ZTEST_F(format_set, test_set_jpeg_qvga)
{
	struct video_format fmt = {
		.pixelformat = VIDEO_PIX_FMT_JPEG,
		.width  = 320,
		.height = 240,
	};
	int ret = video_set_format(fixture->cam, VIDEO_EP_OUT, &fmt);

	zassert_ok(ret, "video_set_format JPEG QVGA failed: %d", ret);
	zassert_equal(fmt.width,  320, "width after set");
	zassert_equal(fmt.height, 240, "height after set");
}

ZTEST_F(format_set, test_set_rgb565_vga)
{
	struct video_format fmt = {
		.pixelformat = VIDEO_PIX_FMT_RGB565,
		.width  = 640,
		.height = 480,
	};
	int ret = video_set_format(fixture->cam, VIDEO_EP_OUT, &fmt);

	zassert_ok(ret, "video_set_format RGB565 VGA failed: %d", ret);
	zassert_equal(fmt.width,  640, "width");
	zassert_equal(fmt.height, 480, "height");
	zassert_equal(fmt.pitch,  1280, "RGB565 stride should be 2*width");
}

ZTEST_F(format_set, test_set_yuyv_vga)
{
	struct video_format fmt = {
		.pixelformat = VIDEO_PIX_FMT_YUYV,
		.width  = 640,
		.height = 480,
	};
	int ret = video_set_format(fixture->cam, VIDEO_EP_OUT, &fmt);

	zassert_ok(ret, "video_set_format YUYV VGA failed: %d", ret);
	zassert_equal(fmt.pitch, 1280, "YUYV stride");
}

ZTEST_F(format_set, test_get_format_matches_set)
{
	struct video_format set_fmt = {
		.pixelformat = VIDEO_PIX_FMT_JPEG,
		.width  = 1280,
		.height = 720,
	};
	struct video_format get_fmt;

	zassert_ok(video_set_format(fixture->cam, VIDEO_EP_OUT, &set_fmt), "");
	zassert_ok(video_get_format(fixture->cam, VIDEO_EP_OUT, &get_fmt), "");

	zassert_equal(get_fmt.pixelformat, VIDEO_PIX_FMT_JPEG, "pixelformat");
	zassert_equal(get_fmt.width,  set_fmt.width,  "width");
	zassert_equal(get_fmt.height, set_fmt.height, "height");
}

ZTEST_F(format_set, test_unsupported_format_returns_einval)
{
	struct video_format fmt = {
		.pixelformat = 0xDEADBEEF, /* invalid */
		.width  = 320,
		.height = 240,
	};
	int ret = video_set_format(fixture->cam, VIDEO_EP_OUT, &fmt);

	zassert_equal(ret, -EINVAL, "Expected -EINVAL for bad pixelformat");
}

/* ------------------------------------------------------------------ *
 * Suite: capture_jpeg – single JPEG frame
 * ------------------------------------------------------------------ */

#define JPEG_BUF_SIZE (320 * 240 * 2) /* worst case for QVGA JPEG */

ZTEST_SUITE(capture_jpeg, NULL, e2e_setup, NULL, NULL, NULL);

ZTEST_F(capture_jpeg, test_capture_single_frame)
{
	/* Configure JPEG QVGA */
	struct video_format fmt = {
		.pixelformat = VIDEO_PIX_FMT_JPEG,
		.width  = 320,
		.height = 240,
	};
	zassert_ok(video_set_format(fixture->cam, VIDEO_EP_OUT, &fmt), "");

	/* Allocate a buffer */
	struct video_buffer *vbuf = video_buffer_alloc(JPEG_BUF_SIZE);

	zassert_not_null(vbuf, "Failed to allocate video buffer");

	/* Enqueue, start stream, dequeue */
	zassert_ok(video_enqueue(fixture->cam, VIDEO_EP_OUT, vbuf), "");
	zassert_ok(video_stream_start(fixture->cam), "");

	struct video_buffer *filled;
	int ret = video_dequeue(fixture->cam, VIDEO_EP_OUT,
				&filled, K_SECONDS(10));

	zassert_ok(video_stream_stop(fixture->cam), "");

	zassert_ok(ret, "video_dequeue failed: %d", ret);
	zassert_not_null(filled, "Dequeued buffer is NULL");
	zassert_true(filled->bytesused > 0,
		     "Captured frame is empty (bytesused=0)");

	/* Validate JPEG SOI marker */
	uint8_t *data = (uint8_t *)filled->buffer;

	zassert_equal(data[0], JPEG_SOI_BYTE0,
		      "JPEG SOI byte 0 mismatch: 0x%02x", data[0]);
	zassert_equal(data[1], JPEG_SOI_BYTE1,
		      "JPEG SOI byte 1 mismatch: 0x%02x", data[1]);

	LOG_INF("JPEG frame size: %u bytes", filled->bytesused);

	video_buffer_release(filled);
}

ZTEST_F(capture_jpeg, test_capture_hd_frame)
{
	struct video_format fmt = {
		.pixelformat = VIDEO_PIX_FMT_JPEG,
		.width  = 1280,
		.height = 720,
	};
	zassert_ok(video_set_format(fixture->cam, VIDEO_EP_OUT, &fmt), "");

	/* For HD JPEG we may need a larger buffer */
	struct video_buffer *vbuf = video_buffer_alloc(1280 * 720);

	zassert_not_null(vbuf, "Failed to allocate HD buffer");

	zassert_ok(video_enqueue(fixture->cam, VIDEO_EP_OUT, vbuf), "");
	zassert_ok(video_stream_start(fixture->cam), "");

	struct video_buffer *filled;
	int ret = video_dequeue(fixture->cam, VIDEO_EP_OUT,
				&filled, K_SECONDS(15));

	video_stream_stop(fixture->cam);

	zassert_ok(ret, "HD dequeue failed: %d", ret);
	zassert_true(filled->bytesused > 0, "HD frame is empty");

	uint8_t *img = (uint8_t *)filled->buffer;

	zassert_equal(img[0], JPEG_SOI_BYTE0, "HD JPEG SOI[0]");
	zassert_equal(img[1], JPEG_SOI_BYTE1, "HD JPEG SOI[1]");

	LOG_INF("HD JPEG frame size: %u bytes", filled->bytesused);

	video_buffer_release(filled);
}

/* ------------------------------------------------------------------ *
 * Suite: capture_rgb – RGB565 frame
 * ------------------------------------------------------------------ */

ZTEST_SUITE(capture_rgb, NULL, e2e_setup, NULL, NULL, NULL);

ZTEST_F(capture_rgb, test_capture_rgb565_qvga)
{
	struct video_format fmt = {
		.pixelformat = VIDEO_PIX_FMT_RGB565,
		.width  = 320,
		.height = 240,
	};
	zassert_ok(video_set_format(fixture->cam, VIDEO_EP_OUT, &fmt), "");

	/* RGB565 QVGA = 320*240*2 = 153 600 bytes */
	size_t expected_size = 320 * 240 * 2;
	struct video_buffer *vbuf = video_buffer_alloc(expected_size);

	zassert_not_null(vbuf, "Failed to allocate RGB buffer");

	zassert_ok(video_enqueue(fixture->cam, VIDEO_EP_OUT, vbuf), "");
	zassert_ok(video_stream_start(fixture->cam), "");

	struct video_buffer *filled;
	int ret = video_dequeue(fixture->cam, VIDEO_EP_OUT,
				&filled, K_SECONDS(10));

	video_stream_stop(fixture->cam);

	zassert_ok(ret, "RGB565 dequeue failed: %d", ret);
	zassert_equal(filled->bytesused, expected_size,
		      "RGB565 QVGA frame size wrong: expected %zu got %u",
		      expected_size, filled->bytesused);

	video_buffer_release(filled);
}

/* ------------------------------------------------------------------ *
 * Suite: streaming – continuous multi-frame capture
 * ------------------------------------------------------------------ */

#define STREAM_FRAME_COUNT 5
#define STREAM_BUF_SIZE    (320 * 240 * 2)

ZTEST_SUITE(streaming, NULL, e2e_setup, NULL, NULL, NULL);

ZTEST_F(streaming, test_continuous_jpeg_capture)
{
	struct video_format fmt = {
		.pixelformat = VIDEO_PIX_FMT_JPEG,
		.width  = 320,
		.height = 240,
	};
	zassert_ok(video_set_format(fixture->cam, VIDEO_EP_OUT, &fmt), "");

	/* Pre-allocate buffers and enqueue them all */
	struct video_buffer *bufs[STREAM_FRAME_COUNT];

	for (int i = 0; i < STREAM_FRAME_COUNT; i++) {
		bufs[i] = video_buffer_alloc(STREAM_BUF_SIZE);
		zassert_not_null(bufs[i], "Buffer alloc failed at %d", i);
		zassert_ok(video_enqueue(fixture->cam, VIDEO_EP_OUT, bufs[i]),
			   "Enqueue failed at %d", i);
	}

	zassert_ok(video_stream_start(fixture->cam), "");

	uint32_t prev_ts = 0;
	int captured = 0;

	for (int i = 0; i < STREAM_FRAME_COUNT; i++) {
		struct video_buffer *filled;
		int ret = video_dequeue(fixture->cam, VIDEO_EP_OUT,
					&filled, K_SECONDS(10));

		if (ret != 0) {
			LOG_ERR("Dequeue %d failed: %d", i, ret);
			break;
		}

		zassert_true(filled->bytesused > 0,
			     "Frame %d is empty", i);
		zassert_true(filled->timestamp >= prev_ts,
			     "Timestamp not monotonic at frame %d", i);
		prev_ts = filled->timestamp;
		captured++;

		/* Optionally re-enqueue for the next iteration */
		/* video_enqueue(fixture->cam, VIDEO_EP_OUT, filled); */
	}

	video_stream_stop(fixture->cam);

	zassert_equal(captured, STREAM_FRAME_COUNT,
		      "Expected %d frames, got %d",
		      STREAM_FRAME_COUNT, captured);

	for (int i = 0; i < STREAM_FRAME_COUNT; i++) {
		if (bufs[i]) {
			video_buffer_release(bufs[i]);
		}
	}
}

ZTEST_F(streaming, test_stream_stop_allows_flush)
{
	struct video_format fmt = {
		.pixelformat = VIDEO_PIX_FMT_JPEG,
		.width  = 320,
		.height = 240,
	};
	zassert_ok(video_set_format(fixture->cam, VIDEO_EP_OUT, &fmt), "");

	struct video_buffer *vbuf = video_buffer_alloc(STREAM_BUF_SIZE);

	zassert_not_null(vbuf, "");

	zassert_ok(video_enqueue(fixture->cam, VIDEO_EP_OUT, vbuf), "");
	zassert_ok(video_stream_start(fixture->cam), "");
	zassert_ok(video_stream_stop(fixture->cam), "");

	/* Flush should succeed and move remaining buffers to out queue */
	zassert_ok(video_flush(fixture->cam, VIDEO_EP_OUT, true), "");

	video_buffer_release(vbuf);
}

/* ------------------------------------------------------------------ *
 * Suite: controls – camera image controls
 * ------------------------------------------------------------------ */

ZTEST_SUITE(controls, NULL, e2e_setup, NULL, NULL, NULL);

ZTEST_F(controls, test_set_brightness)
{
	uint8_t val = 0x06; /* +2 steps above default */
	int ret = video_set_ctrl(fixture->cam, VIDEO_CID_CAMERA_BRIGHTNESS,
				 &val);

	/* Accept -ENOTSUP if the control is not yet implemented */
	zassert_true(ret == 0 || ret == -ENOTSUP,
		     "Unexpected error setting brightness: %d", ret);
}

ZTEST_F(controls, test_set_white_balance)
{
	uint8_t val = 1; /* ARDUCAM_WB_SUNNY */
	int ret = video_set_ctrl(fixture->cam, VIDEO_CID_CAMERA_WHITE_BAL,
				 &val);

	zassert_true(ret == 0 || ret == -ENOTSUP,
		     "Unexpected error setting WB: %d", ret);
}

ZTEST_F(controls, test_set_invalid_control_returns_enotsup)
{
	uint8_t val = 0;
	int ret = video_set_ctrl(fixture->cam, 0xDEAD, &val);

	zassert_equal(ret, -ENOTSUP,
		      "Expected -ENOTSUP for unknown CID, got %d", ret);
}
