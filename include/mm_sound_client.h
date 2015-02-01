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

#ifndef __MM_SOUND_CLIENT_H__
#define __MM_SOUND_CLIENT_H__

#include "mm_sound_private.h"
#include "mm_sound_device.h"

//#define MEMTYPE_TRANS_PER_MAX (1024 * 1024) /* 1MB */

int MMSoundClientInit(void);
int MMSoundClientCallbackFini(void);
int MMSoundClientPlayTone(int number, int volume_config, double volume, int time, int *handle, bool enable_session);
int MMSoundClientPlaySound(MMSoundPlayParam *param, int tone, int keytone, int *handle);
int MMSoundClientStopSound(int handle);
int _mm_sound_client_is_route_available(mm_sound_route route, bool *is_available);
int _mm_sound_client_foreach_available_route_cb(mm_sound_available_route_cb, void *user_data);
int _mm_sound_client_set_active_route(mm_sound_route route, bool need_broadcast);
int _mm_sound_client_get_active_device(mm_sound_device_in *device_in, mm_sound_device_out *device_out);
int _mm_sound_client_add_active_device_changed_callback(const char *name, mm_sound_active_device_changed_cb func, void* user_data);
int _mm_sound_client_remove_active_device_changed_callback(const char *name);
int _mm_sound_client_add_available_route_changed_callback(mm_sound_available_route_changed_cb func, void* user_data);
int _mm_sound_client_remove_available_route_changed_callback(void);
int _mm_sound_client_add_volume_changed_callback(mm_sound_volume_changed_cb func, void* user_data);
int _mm_sound_client_remove_volume_changed_callback(void);
int _mm_sound_client_set_sound_path_for_active_device(mm_sound_device_out device_out, mm_sound_device_in device_in);
int _mm_sound_client_get_current_connected_device_list(int device_flgas, mm_sound_device_list_t **device_list);
int _mm_sound_client_add_device_connected_callback(int device_flags, mm_sound_device_connected_cb func, void* user_data);
int _mm_sound_client_remove_device_connected_callback(void);
int _mm_sound_client_add_device_info_changed_callback(int device_flags, mm_sound_device_info_changed_cb func, void* user_data);
int _mm_sound_client_remove_device_info_changed_callback(void);

#ifdef PULSE_CLIENT
int MMSoundClientIsBtA2dpOn (bool *connected, char** bt_name);
#endif
int _mm_sound_client_set_active_route_auto(void);
int _mm_sound_client_get_audio_path(mm_sound_device_in *device_in, mm_sound_device_out *device_out);

#endif /* __MM_SOUND_CLIENT_H__ */
