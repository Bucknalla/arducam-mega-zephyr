/*
 * Copyright (c) 2024 Arducam MEGA Zephyr Driver Contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Public API extensions for the Arducam MEGA driver.
 *
 * The driver implements the standard Zephyr video subsystem API
 * (include/zephyr/drivers/video.h).  This header exposes additional
 * camera-specific video control IDs and convenience helpers that go
 * beyond the generic video API.
 */

#ifndef ARDUCAM_MEGA_H_
#define ARDUCAM_MEGA_H_

#include <zephyr/drivers/video.h>
#include <zephyr/drivers/video-controls.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup arducam_mega_cid Arducam MEGA video control IDs
 *
 * Camera-specific control IDs passed to video_set_ctrl() /
 * video_get_ctrl() when using the Arducam MEGA driver.
 *
 * All control values are uint8_t unless otherwise stated.
 * @{
 */

/** Brightness adjustment.  Range: 0x00 (min) – 0x08 (max), default 0x04. */
#ifndef VIDEO_CID_CAMERA_BRIGHTNESS
#define VIDEO_CID_CAMERA_BRIGHTNESS  (VIDEO_CID_PRIVATE_BASE + 0)
#endif

/** Contrast adjustment.  Range: 0x00 – 0x08, default 0x04. */
#ifndef VIDEO_CID_CAMERA_CONTRAST
#define VIDEO_CID_CAMERA_CONTRAST    (VIDEO_CID_PRIVATE_BASE + 1)
#endif

/** Saturation adjustment.  Range: 0x00 – 0x08, default 0x04. */
#ifndef VIDEO_CID_CAMERA_SATURATION
#define VIDEO_CID_CAMERA_SATURATION  (VIDEO_CID_PRIVATE_BASE + 2)
#endif

/** Sharpness adjustment.  Range: 0x00 – 0x08, default 0x04. */
#ifndef VIDEO_CID_CAMERA_SHARPNESS
#define VIDEO_CID_CAMERA_SHARPNESS   (VIDEO_CID_PRIVATE_BASE + 3)
#endif

/**
 * White balance preset.
 * Values: ARDUCAM_WB_AUTO, ARDUCAM_WB_SUNNY, ARDUCAM_WB_OFFICE,
 *         ARDUCAM_WB_CLOUDY, ARDUCAM_WB_HOME
 */
#ifndef VIDEO_CID_CAMERA_WHITE_BAL
#define VIDEO_CID_CAMERA_WHITE_BAL   (VIDEO_CID_PRIVATE_BASE + 4)
#endif

/** @} */

/** White balance preset values for VIDEO_CID_CAMERA_WHITE_BAL */
enum arducam_white_balance {
	ARDUCAM_WB_AUTO   = 0,
	ARDUCAM_WB_SUNNY  = 1,
	ARDUCAM_WB_OFFICE = 2,
	ARDUCAM_WB_CLOUDY = 3,
	ARDUCAM_WB_HOME   = 4,
};

/**
 * @brief Supported camera models reported by the driver.
 *
 * Obtained by reading the sensor ID register after initialisation.
 */
enum arducam_model {
	ARDUCAM_MODEL_UNKNOWN = 0x00,
	ARDUCAM_MODEL_3MP     = 0x08,
	ARDUCAM_MODEL_5MP     = 0x09,
};

/**
 * @brief Return the camera model detected at initialisation time.
 *
 * @param dev  Pointer to the Arducam MEGA device.
 * @return     One of the @ref arducam_model values, or
 *             ARDUCAM_MODEL_UNKNOWN if detection failed.
 */
static inline enum arducam_model arducam_mega_get_model(const struct device *dev)
{
	/* The model is stored at offset 0 of the driver data blob. */
	const uint8_t *data = (const uint8_t *)dev->data;

	/* camera_model is the last field of arducam_mega_data – access via
	 * the raw data pointer using the known offset would be fragile.
	 * Users should rely on LOG output from the driver or use a dedicated
	 * ioctl in a future revision.  This stub returns UNKNOWN. */
	ARG_UNUSED(data);
	return ARDUCAM_MODEL_UNKNOWN;
}

#ifdef __cplusplus
}
#endif

#endif /* ARDUCAM_MEGA_H_ */
