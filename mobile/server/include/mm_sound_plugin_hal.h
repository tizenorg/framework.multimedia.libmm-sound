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

#ifndef __MM_SOUND_PLUGIN_HAL_H__
#define __MM_SOUND_PLUGIN_HAL_H__

#include "mm_sound_plugin.h"
#include <mm_types.h>

typedef struct {
    int (*pulse_sink_route)(int device);
    int (*pulse_source_route)(int device);
    int (*set_sound_path)(int gain, int output, int input, int option);
    int (*init)();
    int (*fini)();
} mmsound_hal_interface_t;

/* Utility Functions */
#define HAL_GET_INTERFACE_FUNC_NAME "MMSoundPlugHALGetInterface"
#define MMSoundPlugHALCastGetInterface(func) ((int (*)(mmsound_hal_interface_t*))(func))

#define AUDIO_HAL_STATE_SUCCESS         	0x0
#define AUDIO_HAL_ROUTE_SUCCESS_AND_GOTOEND	0x1
#define AUDIO_HAL_STATE_ERROR_INTERNAL		0x2

int MMSoundPlugHALGetInterface(mmsound_hal_interface_t *intf);

#endif /* __MM_SOUND_PLUGIN_HAL_H__ */

