/*
 * libmm-sound
 *
 * Copyright (c) 2000 - 2014 Samsung Electronics Co., Ltd. All rights reserved.
 *
 * Contact: Sangchul Lee <sc11.lee@samsung.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

/**
 * @file		mm_sound_device.h
 * @brief		Application interface library for sound module.
 * @date
 *
 * Application interface library for sound module.
 */

#ifndef	__MM_SOUND_DEVICE_H__
#define	__MM_SOUND_DEVICE_H__

#include <glib.h>

#ifdef __cplusplus
	extern "C" {
#endif

#define MAX_DEVICE_NAME_NUM 256
typedef struct {
	int type;
	int io_direction;
	int id;
	char name[MAX_DEVICE_NAME_NUM];
	int state;
} mm_sound_device_t;

typedef struct {
	GList *list;
} mm_sound_device_list_t;

typedef enum {
	DEVICE_IO_DIRECTION_IN,
	DEVICE_IO_DIRECTION_OUT,
	DEVICE_IO_DIRECTION_BOTH,
} device_io_direction_e;

typedef enum {
	DEVICE_STATE_DEACTIVATED,
	DEVICE_STATE_ACTIVATED,
} device_state_e;

typedef enum {
	DEVICE_CHANGED_INFO_STATE,
	DEVICE_CHANGED_INFO_IO_DIRECTION,
} device_changed_info_e;

typedef enum
{
	DEVICE_UPDATE_STATUS_DISCONNECTED = 0,
	DEVICE_UPDATE_STATUS_CONNECTED,
	DEVICE_UPDATE_STATUS_CHANGED_INFO_STATE,
	DEVICE_UPDATE_STATUS_CHANGED_INFO_IO_DIRECTION,
} device_update_status_e;

typedef enum
{
	DEVICE_TYPE_BUILTIN_SPEAKER,   /**< Built-in speaker. */
	DEVICE_TYPE_BUILTIN_RECEIVER,  /**< Built-in receiver. */
	DEVICE_TYPE_BUILTIN_MIC,       /**< Built-in mic. */
	DEVICE_TYPE_AUDIOJACK,         /**< Audio jack such as headphone, headset, and so on. */
	DEVICE_TYPE_BLUETOOTH,         /**< Bluetooth */
	DEVICE_TYPE_HDMI,              /**< HDMI. */
	DEVICE_TYPE_MIRRORING,         /**< MIRRORING. */
	DEVICE_TYPE_USB_AUDIO,         /**< USB Audio. */
} device_type_e;

typedef enum {
	DEVICE_IO_DIRECTION_IN_FLAG      = 0x0001,  /**< Flag for input devices */
	DEVICE_IO_DIRECTION_OUT_FLAG     = 0x0002,  /**< Flag for output devices */
	DEVICE_IO_DIRECTION_BOTH_FLAG    = 0x0004,  /**< Flag for input/output devices (both directions are available) */
	DEVICE_TYPE_INTERNAL_FLAG        = 0x0010,  /**< Flag for built-in devices */
	DEVICE_TYPE_EXTERNAL_FLAG        = 0x0020,  /**< Flag for external devices */
	DEVICE_STATE_DEACTIVATED_FLAG    = 0x1000,  /**< Flag for deactivated devices */
	DEVICE_STATE_ACTIVATED_FLAG      = 0x2000,  /**< Flag for activated devices */
	DEVICE_ALL_FLAG                  = 0xFFFF,  /**< Flag for all devices */
} device_flag_e;

typedef enum {
	DEVICE_IO_DIRECTION_FLAGS      = 0x000F,  /**< Flag for io direction */
	DEVICE_TYPE_FLAGS              = 0x00F0,  /**< Flag for device type */
	DEVICE_STATE_FLAGS             = 0xF000,  /**< Flag for device state */
} device_flags_type_e;

#define IS_INTERNAL_DEVICE(x_device_type) (((DEVICE_TYPE_BUILTIN_SPEAKER <= x_device_type) && (x_device_type <= DEVICE_TYPE_BUILTIN_MIC)) ? true : false)

#ifdef __cplusplus
}
#endif

#endif	/* __MM_SOUND_DEVICE_H__ */

