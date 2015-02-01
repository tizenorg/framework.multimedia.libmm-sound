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

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <vconf.h>

#include <mm_debug.h>

#include "include/mm_sound.h"
#include "include/mm_sound_device.h"

bool is_new_device_list = true;

static int check_for_valid_mask (mm_sound_device_flags_e flags)
{
	int ret = MM_ERROR_NONE;
	bool at_least_cond = false;

	if (flags > 0 && flags <= MM_SOUND_DEVICE_ALL_FLAG) {
		if (flags & MM_SOUND_DEVICE_IO_DIRECTION_IN_FLAG)
			at_least_cond = true;
		if (!at_least_cond && (flags & MM_SOUND_DEVICE_IO_DIRECTION_OUT_FLAG))
			at_least_cond = true;
		if (!at_least_cond && (flags & MM_SOUND_DEVICE_IO_DIRECTION_BOTH_FLAG))
			at_least_cond = true;
		if (!at_least_cond && (flags & MM_SOUND_DEVICE_TYPE_INTERNAL_FLAG))
			at_least_cond = true;
		if (!at_least_cond && (flags & MM_SOUND_DEVICE_TYPE_EXTERNAL_FLAG))
			at_least_cond = true;
		if (!at_least_cond && (flags & MM_SOUND_DEVICE_STATE_DEACTIVATED_FLAG))
			at_least_cond = true;
		if (!at_least_cond && (flags & MM_SOUND_DEVICE_STATE_ACTIVATED_FLAG))
			at_least_cond = true;
	} else {
		ret = MM_ERROR_INVALID_ARGUMENT;
	}
	if (!at_least_cond) {
		ret = MM_ERROR_INVALID_ARGUMENT;
	}
	if (ret) {
		debug_error("flags[0x%x] is not valid\n", flags);
	}
	return ret;
}

EXPORT_API
int mm_sound_add_device_connected_callback(mm_sound_device_flags_e flags, mm_sound_device_connected_cb func, void *user_data)
{
	int ret = MM_ERROR_NONE;

	if (func == NULL) {
		debug_error("argument is not valid\n");
		return MM_ERROR_INVALID_ARGUMENT;
	}
	ret = check_for_valid_mask(flags);
	if (ret == MM_ERROR_NONE) {
		ret = _mm_sound_client_add_device_connected_callback(flags, func, user_data);
		if (ret < 0) {
			debug_error("Could not add device connected callback, ret = %x\n", ret);
		}
	}

	return ret;
}

EXPORT_API
int mm_sound_remove_device_connected_callback(void)
{
	int ret = MM_ERROR_NONE;

	ret = _mm_sound_client_remove_device_connected_callback();
	if (ret < 0) {
		debug_error("Could not remove device connected callback, ret = %x\n", ret);
	}

	return ret;
}

EXPORT_API
int mm_sound_add_device_information_changed_callback(mm_sound_device_flags_e flags, mm_sound_device_info_changed_cb func, void *user_data)
{
	int ret = MM_ERROR_NONE;

	if (func == NULL) {
		debug_error("argument is not valid\n");
		return MM_ERROR_INVALID_ARGUMENT;
	}
	ret = check_for_valid_mask(flags);
	if (ret == MM_ERROR_NONE) {
		ret = _mm_sound_client_add_device_info_changed_callback(flags, func, user_data);
		if (ret < 0) {
			debug_error("Could not add device information changed callback, ret = %x\n", ret);
		}
	}

	return ret;
}

EXPORT_API
int mm_sound_remove_device_information_changed_callback()
{
	int ret = MM_ERROR_NONE;

	ret = _mm_sound_client_remove_device_info_changed_callback();
	if (ret < 0) {
		debug_error("Could not remove device information changed callback, ret = %x\n", ret);
	}

	return ret;
}

EXPORT_API
int mm_sound_get_current_device_list(mm_sound_device_flags_e flags, MMSoundDeviceList_t *device_list)
{
	int ret = MM_ERROR_NONE;

	if (!device_list) {
		return MM_ERROR_INVALID_ARGUMENT;
	}
	ret = check_for_valid_mask(flags);
	if (ret == MM_ERROR_NONE) {
		ret = _mm_sound_client_get_current_connected_device_list(flags, (mm_sound_device_list_t**)device_list);
		if (ret < 0) {
			debug_error("Could not get current connected device list, ret = %x\n", ret);
		} else {
			is_new_device_list = true;
		}
	}

	return ret;
}

EXPORT_API
int mm_sound_get_next_device (MMSoundDeviceList_t device_list, MMSoundDevice_t *device)
{
	int ret = MM_ERROR_NONE;
	mm_sound_device_list_t *device_list_t = NULL;
	mm_sound_device_t *device_h = NULL;
	GList *node = NULL;
	if (!device_list || !device) {
		return MM_ERROR_INVALID_ARGUMENT;
	}
	device_list_t = (mm_sound_device_list_t*) device_list;
	if (is_new_device_list) {
		node = g_list_first(device_list_t->list);
	} else {
		node = g_list_next(device_list_t->list);
	}
	if (!node) {
		ret = MM_ERROR_SOUND_NO_DATA;
	} else {
		if (is_new_device_list) {
			is_new_device_list = false;
		} else {
			device_list_t->list = node;
		}
		*device = (mm_sound_device_t*)node->data;
		debug_log("next device[0x%x]\n", *device);
	}
	return ret;
}

EXPORT_API
int mm_sound_get_prev_device (MMSoundDeviceList_t device_list, MMSoundDevice_t *device)
{
	int ret = MM_ERROR_NONE;
	mm_sound_device_list_t *device_list_t = NULL;
	mm_sound_device_t *device_h = NULL;
	GList *node = NULL;
	if (!device_list || !device) {
		return MM_ERROR_INVALID_ARGUMENT;
	}
	device_list_t = (mm_sound_device_list_t*) device_list;
	node = g_list_previous(device_list_t->list);
	if (!node) {
		ret = MM_ERROR_SOUND_NO_DATA;
		debug_error("Could not get previous device, ret = %x\n", ret);
	} else {
		device_list_t->list = node;
		*device = (mm_sound_device_t*)node->data;
		debug_log("previous device[0x%x]\n", *device);
	}
	return ret;
}

EXPORT_API
int mm_sound_get_device_type(MMSoundDevice_t device_h, mm_sound_device_type_e *type)
{
	mm_sound_device_t *device = (mm_sound_device_t*)device_h;
	if(!device) {
		debug_error("invalid handle\n");
		return MM_ERROR_INVALID_ARGUMENT;
	}
	*type = device->type;
	debug_log("device_handle:0x%x, type:%d\n", device, *type);

	return MM_ERROR_NONE;
}

EXPORT_API
int mm_sound_get_device_io_direction(MMSoundDevice_t device_h, mm_sound_device_io_direction_e *io_direction)
{
	mm_sound_device_t *device = (mm_sound_device_t*)device_h;
	if(!device) {
		debug_error("invalid handle\n");
		return MM_ERROR_INVALID_ARGUMENT;
	}
	*io_direction = device->io_direction;
	debug_log("device_handle:0x%x, io_direction:%d (0:IN,1:OUT,2:INOUT)\n", device, *io_direction);

	return MM_ERROR_NONE;
}

EXPORT_API
int mm_sound_get_device_id(MMSoundDevice_t device_h, int *id)
{
	mm_sound_device_t *device = (mm_sound_device_t*)device_h;
	if(!device) {
		debug_error("invalid handle\n");
		return MM_ERROR_INVALID_ARGUMENT;
	}
	debug_log("device_handle:0x%x, id:%d\n", device, *id);
	*id = device->id;

	return MM_ERROR_NONE;
}

EXPORT_API
int mm_sound_get_device_state(MMSoundDevice_t device_h, mm_sound_device_state_e *state)
{
	mm_sound_device_t *device = (mm_sound_device_t*)device_h;
	if(!device) {
		debug_error("invalid handle\n");
		return MM_ERROR_INVALID_ARGUMENT;
	}
	debug_log("device_handle:0x%x, state:%d (0:INACTIVATED,1:ACTIVATED)\n", device, *state);
	*state = device->state;

	return MM_ERROR_NONE;
}

EXPORT_API
int mm_sound_get_device_name(MMSoundDevice_t device_h, char **name)
{
	mm_sound_device_t *device = (mm_sound_device_t*)device_h;
	if(!device) {
		debug_error("invalid handle\n");
		return MM_ERROR_INVALID_ARGUMENT;
	}
	*name = device->name;
	debug_log("device_handle:0x%x, name:%s\n", device, *name);

	return MM_ERROR_NONE;
}

