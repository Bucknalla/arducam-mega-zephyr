/*
 * Copyright (c) 2024 Arducam MEGA Zephyr Driver Contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Zephyr driver for the Arducam MEGA SPI camera module.
 *
 * Protocol reference:
 *   https://www.arducam.com/docs/arducam-mega/
 *
 * SPI protocol:
 *   - Mode 0 (CPOL=0, CPHA=0), max 8 MHz
 *   - Write: CS low, send (addr | 0x80), send data, CS high
 *   - Read:  CS low, send (addr & 0x7F), send 0x00, send 0x00, read byte, CS high
 *   - Burst FIFO read: CS low, send 0x3C, send 0x00 (dummy), read N bytes, CS high
 */

#define DT_DRV_COMPAT arducam_mega

#include <zephyr/kernel.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/video.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <string.h>

LOG_MODULE_REGISTER(arducam_mega, CONFIG_VIDEO_LOG_LEVEL);

/* ------------------------------------------------------------------ *
 * Register map
 * ------------------------------------------------------------------ */

/* FIFO control register */
#define ARDUCAM_REG_FIFO_CTRL          0x04
#define ARDUCAM_FIFO_CLEAR_MASK        0x01
#define ARDUCAM_FIFO_START_MASK        0x02

/* Sensor reset register */
#define ARDUCAM_REG_SENSOR_RESET       0x07
#define ARDUCAM_SENSOR_RESET_ENABLE    0x40

/* Image format register */
#define ARDUCAM_REG_FORMAT             0x20
#define ARDUCAM_FMT_JPEG               0x01
#define ARDUCAM_FMT_RGB565             0x02
#define ARDUCAM_FMT_YUV422             0x03

/* Image resolution register */
#define ARDUCAM_REG_RESOLUTION         0x21

/* Image quality / control registers */
#define ARDUCAM_REG_BRIGHTNESS         0x22
#define ARDUCAM_REG_CONTRAST           0x23
#define ARDUCAM_REG_SATURATION         0x24
#define ARDUCAM_REG_EV                 0x25
#define ARDUCAM_REG_WHITE_BALANCE      0x26
#define ARDUCAM_REG_COLOR_EFFECT       0x27
#define ARDUCAM_REG_POWER_MODE         0x28
#define ARDUCAM_REG_SHARPNESS          0x29

/* Capture status register */
#define ARDUCAM_REG_CAP_STATUS         0x44
#define ARDUCAM_CAP_DONE_MASK          0x04

/* FIFO size registers (24-bit, little-endian across 3 bytes) */
#define ARDUCAM_REG_FIFO_SIZE1         0x45
#define ARDUCAM_REG_FIFO_SIZE2         0x46
#define ARDUCAM_REG_FIFO_SIZE3         0x47

/* Camera model / version registers */
#define ARDUCAM_REG_VERSION_L          0x40
#define ARDUCAM_REG_VERSION_H          0x41
#define ARDUCAM_REG_MODEL_H            0x42
#define ARDUCAM_REG_MODEL_L            0x43
#define ARDUCAM_REG_SENSOR_ID          0x49

/* SPI command bytes */
#define ARDUCAM_CMD_BURST_READ         0x3C
#define ARDUCAM_CMD_SINGLE_READ        0x3D
#define ARDUCAM_WRITE_BIT              0x80

/* Resolution mode values for REG_RESOLUTION */
#define ARDUCAM_RES_96x96              0x00
#define ARDUCAM_RES_128x128            0x01
#define ARDUCAM_RES_QQVGA              0x02  /* 160x120  */
#define ARDUCAM_RES_QVGA               0x03  /* 320x240  */
#define ARDUCAM_RES_320x320            0x04
#define ARDUCAM_RES_VGA                0x05  /* 640x480  */
#define ARDUCAM_RES_SVGA               0x06  /* 800x600  */
#define ARDUCAM_RES_XGA                0x07  /* 1024x768 */
#define ARDUCAM_RES_HD                 0x08  /* 1280x720 */
#define ARDUCAM_RES_SXGA               0x09  /* 1280x1024*/
#define ARDUCAM_RES_UXGA               0x0A  /* 1600x1200*/
#define ARDUCAM_RES_FHD                0x0B  /* 1920x1080*/
#define ARDUCAM_RES_QXGA               0x0C  /* 2048x1536*/
#define ARDUCAM_RES_WQXGA2             0x0D  /* 2592x1944*/

/* Known camera model IDs */
#define ARDUCAM_MODEL_3MP              0x08
#define ARDUCAM_MODEL_5MP              0x09

/* Timeouts */
#define ARDUCAM_CAPTURE_TIMEOUT_MS     5000
#define ARDUCAM_POLL_INTERVAL_US       1000

/* ------------------------------------------------------------------ *
 * Driver private structures
 * ------------------------------------------------------------------ */

struct arducam_mega_config {
	struct spi_dt_spec spi;
	uint32_t spi_max_hz;
};

struct arducam_res_entry {
	uint8_t  reg_val;
	uint16_t width;
	uint16_t height;
};

/* Supported resolutions in ascending order */
static const struct arducam_res_entry arducam_resolutions[] = {
	{ ARDUCAM_RES_96x96,  96,   96   },
	{ ARDUCAM_RES_128x128, 128, 128  },
	{ ARDUCAM_RES_QQVGA,  160,  120  },
	{ ARDUCAM_RES_QVGA,   320,  240  },
	{ ARDUCAM_RES_320x320, 320, 320  },
	{ ARDUCAM_RES_VGA,    640,  480  },
	{ ARDUCAM_RES_SVGA,   800,  600  },
	{ ARDUCAM_RES_XGA,   1024,  768  },
	{ ARDUCAM_RES_HD,    1280,  720  },
	{ ARDUCAM_RES_SXGA,  1280, 1024  },
	{ ARDUCAM_RES_UXGA,  1600, 1200  },
	{ ARDUCAM_RES_FHD,   1920, 1080  },
	{ ARDUCAM_RES_QXGA,  2048, 1536  },
	{ ARDUCAM_RES_WQXGA2, 2592, 1944 },
};

/* Pixel formats supported by the camera */
static const struct video_format_cap arducam_fmts[] = {
	{
		.pixelformat = VIDEO_PIX_FMT_JPEG,
		.width_min   = 96,   .width_max  = 2592, .width_step  = 0,
		.height_min  = 96,   .height_max = 1944, .height_step = 0,
	},
	{
		.pixelformat = VIDEO_PIX_FMT_RGB565,
		.width_min   = 96,   .width_max  = 1920, .width_step  = 0,
		.height_min  = 96,   .height_max = 1080, .height_step = 0,
	},
	{
		.pixelformat = VIDEO_PIX_FMT_YUYV,
		.width_min   = 96,   .width_max  = 1920, .width_step  = 0,
		.height_min  = 96,   .height_max = 1080, .height_step = 0,
	},
};

struct arducam_mega_data {
	const struct device       *dev;         /* back-pointer for work handler */
	struct video_format        fmt;
	struct k_fifo              fifo_in;
	struct k_fifo              fifo_out;
	struct k_work              capture_work;
	struct k_poll_signal      *signal;
	bool                       streaming;
	uint8_t                    camera_model;
};

/* ------------------------------------------------------------------ *
 * Low-level SPI helpers
 * ------------------------------------------------------------------ */

/**
 * @brief Write a single byte to an ArduCAM register.
 *
 * The SPI write frame is: (addr | 0x80) followed by value.
 */
static int arducam_write_reg(const struct device *dev, uint8_t reg, uint8_t val)
{
	const struct arducam_mega_config *cfg = dev->config;
	uint8_t tx_buf[2] = { reg | ARDUCAM_WRITE_BIT, val };
	const struct spi_buf     tx_spi = { .buf = tx_buf, .len = sizeof(tx_buf) };
	const struct spi_buf_set tx_set = { .buffers = &tx_spi, .count = 1 };

	return spi_write_dt(&cfg->spi, &tx_set);
}

/**
 * @brief Read a single byte from an ArduCAM register.
 *
 * The SPI read frame is: (addr & 0x7F), dummy 0x00, dummy 0x00.
 * The actual data is returned in the third transferred byte (on MISO
 * during the third clock phase).
 */
static int arducam_read_reg(const struct device *dev, uint8_t reg, uint8_t *val)
{
	const struct arducam_mega_config *cfg = dev->config;
	uint8_t tx_buf[3] = { reg & ~ARDUCAM_WRITE_BIT, 0x00, 0x00 };
	uint8_t rx_buf[3] = { 0 };

	const struct spi_buf tx_spi = { .buf = tx_buf, .len = sizeof(tx_buf) };
	const struct spi_buf rx_spi = { .buf = rx_buf, .len = sizeof(rx_buf) };
	const struct spi_buf_set tx_set = { .buffers = &tx_spi, .count = 1 };
	const struct spi_buf_set rx_set = { .buffers = &rx_spi, .count = 1 };

	int ret = spi_transceive_dt(&cfg->spi, &tx_set, &rx_set);

	if (ret == 0) {
		*val = rx_buf[2];
	}
	return ret;
}

/**
 * @brief Burst-read N bytes from the camera FIFO.
 *
 * Sends the BURST_READ command (0x3C) followed by a dummy byte,
 * then clocks out @p len data bytes from the camera.
 */
static int arducam_burst_read_fifo(const struct device *dev,
				   uint8_t *buf, size_t len)
{
	const struct arducam_mega_config *cfg = dev->config;

	/* Command phase: BURST_READ cmd + 1 dummy byte */
	uint8_t cmd[2] = { ARDUCAM_CMD_BURST_READ, 0x00 };
	const struct spi_buf cmd_spi = { .buf = cmd, .len = sizeof(cmd) };

	/* Data phase: read len bytes */
	const struct spi_buf data_spi = { .buf = buf, .len = len };

	/* We need to keep CS asserted across both transfers.
	 * Build a two-buffer TX set (cmd + zeros) and a two-buffer RX set
	 * (discard cmd bytes + actual data).
	 */
	uint8_t dummy_tx[2] = { 0x00, 0x00 };
	const struct spi_buf tx_bufs[2] = {
		{ .buf = cmd,      .len = sizeof(cmd) },
		{ .buf = NULL,     .len = len },        /* MOSI = 0 during data */
	};
	const struct spi_buf rx_bufs[2] = {
		{ .buf = dummy_tx, .len = sizeof(cmd) }, /* discard cmd response */
		{ .buf = buf,      .len = len },
	};
	const struct spi_buf_set tx_set = { .buffers = tx_bufs, .count = 2 };
	const struct spi_buf_set rx_set = { .buffers = rx_bufs, .count = 2 };

	return spi_transceive_dt(&cfg->spi, &tx_set, &rx_set);
}

/* ------------------------------------------------------------------ *
 * Camera control helpers
 * ------------------------------------------------------------------ */

static int arducam_reset(const struct device *dev)
{
	int ret;

	ret = arducam_write_reg(dev, ARDUCAM_REG_SENSOR_RESET,
				ARDUCAM_SENSOR_RESET_ENABLE);
	if (ret < 0) {
		return ret;
	}
	k_msleep(100); /* allow sensor to come out of reset */
	return arducam_write_reg(dev, ARDUCAM_REG_SENSOR_RESET, 0x00);
}

static int arducam_flush_fifo(const struct device *dev)
{
	return arducam_write_reg(dev, ARDUCAM_REG_FIFO_CTRL,
				 ARDUCAM_FIFO_CLEAR_MASK);
}

static int arducam_start_capture(const struct device *dev)
{
	return arducam_write_reg(dev, ARDUCAM_REG_FIFO_CTRL,
				 ARDUCAM_FIFO_START_MASK);
}

static int arducam_wait_capture_done(const struct device *dev)
{
	int64_t deadline = k_uptime_get() + ARDUCAM_CAPTURE_TIMEOUT_MS;
	uint8_t status;
	int ret;

	do {
		ret = arducam_read_reg(dev, ARDUCAM_REG_CAP_STATUS, &status);
		if (ret < 0) {
			return ret;
		}
		if (status & ARDUCAM_CAP_DONE_MASK) {
			return 0;
		}
		k_usleep(ARDUCAM_POLL_INTERVAL_US);
	} while (k_uptime_get() < deadline);

	LOG_ERR("Capture timeout");
	return -ETIMEDOUT;
}

static int arducam_read_fifo_length(const struct device *dev, uint32_t *length)
{
	uint8_t s1, s2, s3;
	int ret;

	ret = arducam_read_reg(dev, ARDUCAM_REG_FIFO_SIZE1, &s1);
	if (ret < 0) {
		return ret;
	}
	ret = arducam_read_reg(dev, ARDUCAM_REG_FIFO_SIZE2, &s2);
	if (ret < 0) {
		return ret;
	}
	ret = arducam_read_reg(dev, ARDUCAM_REG_FIFO_SIZE3, &s3);
	if (ret < 0) {
		return ret;
	}

	*length = ((uint32_t)s3 << 16) | ((uint32_t)s2 << 8) | s1;
	return 0;
}

static int arducam_set_format_reg(const struct device *dev,
				  uint32_t pixelformat)
{
	uint8_t fmt_val;

	switch (pixelformat) {
	case VIDEO_PIX_FMT_JPEG:
		fmt_val = ARDUCAM_FMT_JPEG;
		break;
	case VIDEO_PIX_FMT_RGB565:
		fmt_val = ARDUCAM_FMT_RGB565;
		break;
	case VIDEO_PIX_FMT_YUYV:
		fmt_val = ARDUCAM_FMT_YUV422;
		break;
	default:
		LOG_ERR("Unsupported pixel format: 0x%08x", pixelformat);
		return -EINVAL;
	}

	return arducam_write_reg(dev, ARDUCAM_REG_FORMAT, fmt_val);
}

static int arducam_set_resolution_reg(const struct device *dev,
				      uint16_t width, uint16_t height)
{
	for (size_t i = 0; i < ARRAY_SIZE(arducam_resolutions); i++) {
		if (arducam_resolutions[i].width  == width &&
		    arducam_resolutions[i].height == height) {
			return arducam_write_reg(dev, ARDUCAM_REG_RESOLUTION,
						 arducam_resolutions[i].reg_val);
		}
	}
	LOG_ERR("Unsupported resolution %ux%u", width, height);
	return -EINVAL;
}

/* Find the closest supported resolution that fits within the requested
 * dimensions (round down). */
static int arducam_find_best_resolution(uint16_t req_w, uint16_t req_h,
					uint16_t *out_w, uint16_t *out_h)
{
	int best = -1;

	for (int i = (int)ARRAY_SIZE(arducam_resolutions) - 1; i >= 0; i--) {
		if (arducam_resolutions[i].width  <= req_w &&
		    arducam_resolutions[i].height <= req_h) {
			best = i;
			break;
		}
	}

	if (best < 0) {
		/* Smaller than smallest; use smallest */
		best = 0;
	}

	*out_w = arducam_resolutions[best].width;
	*out_h = arducam_resolutions[best].height;
	return 0;
}

/* ------------------------------------------------------------------ *
 * Capture work item – runs in system work queue
 * ------------------------------------------------------------------ */

static void arducam_capture_work_handler(struct k_work *work)
{
	struct arducam_mega_data *data =
		CONTAINER_OF(work, struct arducam_mega_data, capture_work);
	const struct device *dev = data->dev;

	/* Dequeue the next empty buffer from the incoming FIFO */
	struct video_buffer *vbuf = k_fifo_get(&data->fifo_in, K_NO_WAIT);

	if (!vbuf) {
		return;
	}

	/* 1. Flush camera FIFO and start a capture */
	int ret = arducam_flush_fifo(dev);
	if (ret < 0) {
		LOG_ERR("flush_fifo failed: %d", ret);
		goto err;
	}

	ret = arducam_start_capture(dev);
	if (ret < 0) {
		LOG_ERR("start_capture failed: %d", ret);
		goto err;
	}

	/* 2. Wait for capture to complete */
	ret = arducam_wait_capture_done(dev);
	if (ret < 0) {
		LOG_ERR("wait_capture_done failed: %d", ret);
		goto err;
	}

	/* 3. Read FIFO length */
	uint32_t fifo_len = 0;

	ret = arducam_read_fifo_length(dev, &fifo_len);
	if (ret < 0) {
		LOG_ERR("read_fifo_length failed: %d", ret);
		goto err;
	}

	if (fifo_len == 0) {
		LOG_ERR("FIFO length is zero after capture");
		ret = -EIO;
		goto err;
	}

	if (fifo_len > vbuf->size) {
		LOG_WRN("Frame too large (%u > %u), truncating", fifo_len,
			vbuf->size);
		fifo_len = vbuf->size;
	}

	/* 4. Burst-read the image data */
	ret = arducam_burst_read_fifo(dev, vbuf->buffer, fifo_len);
	if (ret < 0) {
		LOG_ERR("burst_read_fifo failed: %d", ret);
		goto err;
	}

	vbuf->bytesused = fifo_len;
	vbuf->timestamp = k_uptime_get();

	k_fifo_put(&data->fifo_out, vbuf);

	if (data->signal) {
		k_poll_signal_raise(data->signal, VIDEO_BUF_DONE);
	}

	/* Re-submit work if still streaming */
	if (data->streaming) {
		struct video_buffer *next =
			k_fifo_peek_head(&data->fifo_in);
		if (next) {
			k_work_submit(&data->capture_work);
		}
	}
	return;

err:
	vbuf->bytesused = 0;
	k_fifo_put(&data->fifo_out, vbuf);
	if (data->signal) {
		k_poll_signal_raise(data->signal, VIDEO_BUF_ERROR);
	}
}

/* ------------------------------------------------------------------ *
 * Video driver API callbacks
 * ------------------------------------------------------------------ */

static int arducam_set_format(const struct device *dev,
			      struct video_format *fmt)
{
	struct arducam_mega_data *data = dev->data;
	uint16_t best_w, best_h;
	int ret;

	/* Find the closest supported resolution */
	arducam_find_best_resolution(fmt->width, fmt->height,
				     &best_w, &best_h);

	/* Update format fields that we will actually use */
	fmt->width  = best_w;
	fmt->height = best_h;

	/* Set pixel stride based on format */
	switch (fmt->pixelformat) {
	case VIDEO_PIX_FMT_JPEG:
		/* JPEG size is variable; stride not meaningful */
		fmt->pitch = best_w;
		break;
	case VIDEO_PIX_FMT_RGB565:
		fmt->pitch = best_w * 2;
		break;
	case VIDEO_PIX_FMT_YUYV:
		fmt->pitch = best_w * 2;
		break;
	default:
		return -EINVAL;
	}

	ret = arducam_set_format_reg(dev, fmt->pixelformat);
	if (ret < 0) {
		return ret;
	}

	ret = arducam_set_resolution_reg(dev, best_w, best_h);
	if (ret < 0) {
		return ret;
	}

	data->fmt = *fmt;
	LOG_INF("Format set: %ux%u fmt=0x%08x", best_w, best_h,
		fmt->pixelformat);
	return 0;
}

static int arducam_get_format(const struct device *dev,
			      struct video_format *fmt)
{
	const struct arducam_mega_data *data = dev->data;

	*fmt = data->fmt;
	return 0;
}

static int arducam_get_caps(const struct device *dev,
			    enum video_endpoint_id ep,
			    struct video_caps *caps)
{
	if (ep != VIDEO_EP_OUT && ep != VIDEO_EP_ALL) {
		return -EINVAL;
	}

	caps->format_caps = arducam_fmts;
	caps->min_vbuf_count = 1;
	return 0;
}

static int arducam_stream_start(const struct device *dev)
{
	struct arducam_mega_data *data = dev->data;

	if (data->streaming) {
		return 0;
	}

	data->streaming = true;

	/* Kick off capture if there is already a buffer enqueued */
	if (k_fifo_peek_head(&data->fifo_in)) {
		k_work_submit(&data->capture_work);
	}

	return 0;
}

static int arducam_stream_stop(const struct device *dev)
{
	struct arducam_mega_data *data = dev->data;

	data->streaming = false;
	return 0;
}

static int arducam_enqueue(const struct device *dev,
			   enum video_endpoint_id ep,
			   struct video_buffer *vbuf)
{
	struct arducam_mega_data *data = dev->data;

	if (ep != VIDEO_EP_OUT && ep != VIDEO_EP_ALL) {
		return -EINVAL;
	}

	k_fifo_put(&data->fifo_in, vbuf);

	if (data->streaming) {
		k_work_submit(&data->capture_work);
	}

	return 0;
}

static int arducam_dequeue(const struct device *dev,
			   enum video_endpoint_id ep,
			   struct video_buffer **vbuf,
			   k_timeout_t timeout)
{
	struct arducam_mega_data *data = dev->data;

	if (ep != VIDEO_EP_OUT && ep != VIDEO_EP_ALL) {
		return -EINVAL;
	}

	*vbuf = k_fifo_get(&data->fifo_out, timeout);
	if (*vbuf == NULL) {
		return -EAGAIN;
	}

	return 0;
}

static int arducam_flush(const struct device *dev,
			 enum video_endpoint_id ep,
			 bool cancel)
{
	struct arducam_mega_data *data = dev->data;
	struct video_buffer *vbuf;

	if (ep != VIDEO_EP_OUT && ep != VIDEO_EP_ALL) {
		return -EINVAL;
	}

	/* Move all pending incoming buffers to the outgoing queue */
	while ((vbuf = k_fifo_get(&data->fifo_in, K_NO_WAIT)) != NULL) {
		vbuf->bytesused = 0;
		k_fifo_put(&data->fifo_out, vbuf);
		if (data->signal) {
			k_poll_signal_raise(data->signal, VIDEO_BUF_ABORTED);
		}
	}

	return 0;
}

static int arducam_set_signal(const struct device *dev,
			      enum video_endpoint_id ep,
			      struct k_poll_signal *signal)
{
	struct arducam_mega_data *data = dev->data;

	data->signal = signal;
	return 0;
}

static int arducam_set_ctrl(const struct device *dev,
			    unsigned int cid, void *value)
{
	uint8_t regval = *(uint8_t *)value;

	switch (cid) {
	case VIDEO_CID_CAMERA_BRIGHTNESS:
		return arducam_write_reg(dev, ARDUCAM_REG_BRIGHTNESS, regval);
	case VIDEO_CID_CAMERA_CONTRAST:
		return arducam_write_reg(dev, ARDUCAM_REG_CONTRAST, regval);
	case VIDEO_CID_CAMERA_SATURATION:
		return arducam_write_reg(dev, ARDUCAM_REG_SATURATION, regval);
	case VIDEO_CID_CAMERA_SHARPNESS:
		return arducam_write_reg(dev, ARDUCAM_REG_SHARPNESS, regval);
	case VIDEO_CID_CAMERA_WHITE_BAL:
		return arducam_write_reg(dev, ARDUCAM_REG_WHITE_BALANCE, regval);
	default:
		return -ENOTSUP;
	}
}

/* ------------------------------------------------------------------ *
 * Driver initialisation
 * ------------------------------------------------------------------ */

static int arducam_mega_init(const struct device *dev)
{
	const struct arducam_mega_config *cfg = dev->config;
	struct arducam_mega_data        *data = dev->data;
	uint8_t model_h, model_l;
	int ret;

	/* Verify the SPI bus is ready */
	if (!spi_is_ready_dt(&cfg->spi)) {
		LOG_ERR("SPI bus not ready");
		return -ENODEV;
	}

	data->dev = dev;
	k_fifo_init(&data->fifo_in);
	k_fifo_init(&data->fifo_out);
	k_work_init(&data->capture_work, arducam_capture_work_handler);

	/* Hardware reset */
	ret = arducam_reset(dev);
	if (ret < 0) {
		LOG_ERR("Camera reset failed: %d", ret);
		return ret;
	}

	/* Read camera model ID to verify SPI communication */
	ret = arducam_read_reg(dev, ARDUCAM_REG_MODEL_H, &model_h);
	if (ret < 0) {
		LOG_ERR("Failed to read model ID: %d", ret);
		return ret;
	}
	ret = arducam_read_reg(dev, ARDUCAM_REG_MODEL_L, &model_l);
	if (ret < 0) {
		LOG_ERR("Failed to read model ID: %d", ret);
		return ret;
	}

	data->camera_model = model_l;
	LOG_INF("Arducam MEGA detected: model=0x%02x%02x", model_h, model_l);

	/* Set default format: JPEG QVGA */
	struct video_format default_fmt = {
		.pixelformat = VIDEO_PIX_FMT_JPEG,
		.width       = 320,
		.height      = 240,
		.pitch       = 320,
	};

	ret = arducam_set_format(dev, &default_fmt);
	if (ret < 0) {
		LOG_ERR("Failed to set default format: %d", ret);
		return ret;
	}

	LOG_INF("Arducam MEGA driver initialized");
	return 0;
}

/* ------------------------------------------------------------------ *
 * Driver API struct
 * ------------------------------------------------------------------ */

static const struct video_driver_api arducam_mega_api = {
	.set_format   = arducam_set_format,
	.get_format   = arducam_get_format,
	.get_caps     = arducam_get_caps,
	.stream_start = arducam_stream_start,
	.stream_stop  = arducam_stream_stop,
	.enqueue      = arducam_enqueue,
	.dequeue      = arducam_dequeue,
	.flush        = arducam_flush,
	.set_signal   = arducam_set_signal,
	.set_ctrl     = arducam_set_ctrl,
};

/* ------------------------------------------------------------------ *
 * Device instantiation macro
 * ------------------------------------------------------------------ */

#define ARDUCAM_MEGA_DEFINE(inst)                                        \
	static struct arducam_mega_data arducam_mega_data_##inst;        \
                                                                         \
	static const struct arducam_mega_config arducam_mega_cfg_##inst = { \
		/* SPI mode 0: CPOL=0, CPHA=0 – do NOT set CPOL/CPHA flags */\
		.spi = SPI_DT_SPEC_INST_GET(                             \
			inst,                                            \
			SPI_OP_MODE_MASTER | SPI_TRANSFER_MSB |          \
			SPI_WORD_SET(8),                                 \
			0),                                              \
	};                                                               \
                                                                         \
	DEVICE_DT_INST_DEFINE(inst,                                      \
			      arducam_mega_init,                         \
			      NULL,                                      \
			      &arducam_mega_data_##inst,                 \
			      &arducam_mega_cfg_##inst,                  \
			      POST_KERNEL,                               \
			      CONFIG_VIDEO_INIT_PRIORITY,                \
			      &arducam_mega_api);

DT_INST_FOREACH_STATUS_OKAY(ARDUCAM_MEGA_DEFINE)
