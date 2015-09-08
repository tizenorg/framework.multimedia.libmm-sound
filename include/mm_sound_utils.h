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

/**
 * @file		mm_sound_utils.h
 * @brief		Internal utility library for sound module.
 * @date
 * @version		Release
 *
 * Internal utility library for sound module.
 */

#ifndef __MM_SOUND_UTILS_H__
#define __MM_SOUND_UTILS_H__

#include <mm_types.h>
#include <mm_error.h>

#include "../include/mm_sound.h"

#ifdef __cplusplus
	extern "C" {
#endif

int _mm_sound_get_valid_route_list(mm_sound_route **route_list);
bool _mm_sound_is_route_valid(mm_sound_route route);
void _mm_sound_get_devices_from_route(mm_sound_route route, mm_sound_device_in *device_in, mm_sound_device_out *device_out);
bool _mm_sound_check_hibernation (const char *path);
int _mm_sound_volume_add_callback(volume_type_t type, void *func, void* user_data);
int _mm_sound_volume_remove_callback(volume_type_t type, void *func);
int _mm_sound_volume_get_value_by_type(volume_type_t type, unsigned int *value);
int _mm_sound_volume_set_value_by_type(volume_type_t type, unsigned int value);
int _mm_sound_muteall_add_callback(void *func);
int _mm_sound_muteall_remove_callback(void *func);
int _mm_sound_volume_set_balance(float balance);
int _mm_sound_volume_get_balance(float *balance);
int _mm_sound_set_muteall(int muteall);
int _mm_sound_get_muteall(int *muteall);
int _mm_sound_set_stereo_to_mono(int ismono);
int _mm_sound_get_stereo_to_mono(int *ismono);
int _mm_sound_get_earjack_type (int *type);
int _mm_sound_get_dock_type (int *type);
mm_sound_device_in _mm_sound_get_device_in_from_path (int path);
mm_sound_device_out _mm_sound_get_device_out_from_path (int path);

bool _mm_sound_is_recording (void);
bool _mm_sound_is_mute_policy (void);

#ifdef __cplusplus
}
#endif

#endif /* __MM_SOUND_UTILS_H__ */

