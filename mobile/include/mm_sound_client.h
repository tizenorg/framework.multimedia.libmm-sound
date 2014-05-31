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

//#define MEMTYPE_TRANS_PER_MAX (1024 * 1024) /* 1MB */

int MMSoundClientInit(void);
int MMSoundClientCallbackFini(void);
int MMSoundClientPlayTone(int number, int volume_config, double volume, int time, int *handle);
int MMSoundClientPlaySound(MMSoundPlayParam *param, int tone, int keytone, int *handle);
int MMSoundClientStopSound(int handle);
int _mm_sound_client_is_route_available(mm_sound_route route, bool *is_available);
int _mm_sound_client_foreach_available_route_cb(mm_sound_available_route_cb, void *user_data);
int _mm_sound_client_set_active_route(mm_sound_route route);
int _mm_sound_client_get_active_device(mm_sound_device_in *device_in, mm_sound_device_out *device_out);
int _mm_sound_client_add_active_device_changed_callback(mm_sound_active_device_changed_cb func, void* user_data);
int _mm_sound_client_remove_active_device_changed_callback(void);
int _mm_sound_client_add_available_route_changed_callback(mm_sound_available_route_changed_cb func, void* user_data);
int _mm_sound_client_remove_available_route_changed_callback(void);
#ifdef PULSE_CLIENT
int MMSoundClientIsBtA2dpOn (bool *connected, char** bt_name);
#endif

#endif /* __MM_SOUND_CLIENT_H__ */
