/*
 * libmm-sound
 *
 * Copyright (c) 2000 - 2011 Samsung Electronics Co., Ltd. All rights reserved.
 *
 * Contact: Seungbae Shin <seungbae.shin@samsung.com>
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


#ifndef __MM_SOUND_MGR_DEVICE_H__
#define __MM_SOUND_MGR_DEVICE_H__

#include "../include/mm_sound.h"
#include "../include/mm_sound_device.h"
#include "../include/mm_ipc.h"

typedef struct {
	int pid;
	mm_sound_route route;
	void *callback;
	void *cbdata;
	int device_flags;
	bool need_broadcast;
	char name[MM_SOUND_NAME_NUM];
} _mm_sound_mgr_device_param_t;

typedef enum
{
	DEVICE_EARJACK_TYPE_SPK_WITH_MIC = 3,
} device_earjack_type_e;

#define DEVICE_ID_AUTO      -1

#define DEVICE_NAME_BUILTIN_SPK       "built-in speaker"
#define DEVICE_NAME_BUILTIN_RCV       "built-in receiver"
#define DEVICE_NAME_BUILTIN_MIC       "built-in mic"
#define DEVICE_NAME_AUDIOJACK_3P      "headphone"
#define DEVICE_NAME_AUDIOJACK_4P      "headset"
#define DEVICE_NAME_HDMI              "hdmi"
#define DEVICE_NAME_MIRRORING         "mirroring"

int _mm_sound_mgr_device_init(void);
int _mm_sound_mgr_device_fini(void);
int _mm_sound_mgr_device_is_route_available(const _mm_sound_mgr_device_param_t *param, bool *is_available);
int _mm_sound_mgr_device_foreach_available_route_cb(mm_ipc_msg_t *msg);
int _mm_sound_mgr_device_set_active_route(const _mm_sound_mgr_device_param_t *param);
int _mm_sound_mgr_device_get_active_device(const _mm_sound_mgr_device_param_t *param, mm_sound_device_in *device_in, mm_sound_device_out *device_out);
int _mm_sound_mgr_device_add_active_device_callback(const _mm_sound_mgr_device_param_t *param);
int _mm_sound_mgr_device_remove_active_device_callback(const _mm_sound_mgr_device_param_t *param);
int _mm_sound_mgr_device_active_device_callback(mm_sound_device_in device_in, mm_sound_device_out device_out);
int _mm_sound_mgr_device_add_available_route_callback(const _mm_sound_mgr_device_param_t *param);
int _mm_sound_mgr_device_remove_available_route_callback(const _mm_sound_mgr_device_param_t *param);
int _mm_sound_mgr_device_available_device_callback(mm_sound_device_in device_in, mm_sound_device_out device_out, bool available);
int _mm_sound_mgr_device_get_volume_value_by_active_device(char *buf, mm_sound_device_out device_out, unsigned int *value);
int _mm_sound_mgr_device_set_volume_value_by_active_device(char *buf, mm_sound_device_out device_out, int value);
int _mm_sound_mgr_device_set_active_route_auto(void);
int _mm_sound_mgr_device_set_sound_path_for_active_device(mm_sound_device_out playback, mm_sound_device_in capture);
int _mm_sound_mgr_device_add_volume_callback(const _mm_sound_mgr_device_param_t *param);
int _mm_sound_mgr_device_remove_volume_callback(const _mm_sound_mgr_device_param_t *param);
int _mm_sound_mgr_device_get_audio_path(mm_sound_device_in *device_in, mm_sound_device_out *device_out);
int _mm_sound_mgr_device_get_current_connected_dev_list(const _mm_sound_mgr_device_param_t *param, GList **device_list);
int _mm_sound_mgr_device_add_connected_callback(const _mm_sound_mgr_device_param_t *param);
int _mm_sound_mgr_device_remove_connected_callback(const _mm_sound_mgr_device_param_t *param);
int _mm_sound_mgr_device_add_info_changed_callback(const _mm_sound_mgr_device_param_t *param);
int _mm_sound_mgr_device_remove_info_changed_callback(const _mm_sound_mgr_device_param_t *param);
int MMSoundMgrDeviceGetIoDirectionById (int id, device_io_direction_e *io_direction);
int MMSoundMgrDeviceUpdateStatus (device_update_status_e update_status, device_type_e device_type, device_io_direction_e io_direction, int id, const char* name, device_state_e state, int *alloc_id);
int MMSoundMgrDeviceUpdateStatusWithoutNotification (device_update_status_e update_status, device_type_e device_type, device_io_direction_e io_direction, int id, const char* name, device_state_e state, int *alloc_id);

#endif /* __MM_SOUND_MGR_DEVICE_H__ */

