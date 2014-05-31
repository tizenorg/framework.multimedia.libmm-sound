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

typedef struct {
	int pid;
	mm_sound_route route;
	void *callback;
	void *cbdata;
} _mm_sound_mgr_device_param_t;

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

#endif /* __MM_SOUND_MGR_DEVICE_H__ */

