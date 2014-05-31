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

#ifndef __MM_SOUND_PLUGIN_RUN_H__
#define __MM_SOUND_PLUGIN_RUN_H__

#include "mm_sound_plugin.h"
#include <mm_types.h>

enum {
    MM_SOUND_PLUG_RUN_OP_RUN,
    MM_SOUND_PLUG_RUN_OP_STOP,
    MM_SOUND_PLUG_RUN_OP_LAST
};

/* Plugin Interface */
typedef struct {
    int (*run)(void);
    int (*stop)(void);
    int (*SetThreadPool) (int (*)(void*, void (*)(void*)));
} mmsound_run_interface_t;

int MMSoundRunRun(void);
int MMSoundRunStop(void);

/* Utility Functions */
#define RUN_GET_INTERFACE_FUNC_NAME "MMSoundPlugRunGetInterface"
#define MMSoundPlugRunCastGetInterface(func) ((int (*)(mmsound_run_interface_t*))(func))
int MMSoundPlugRunGetInterface(mmsound_run_interface_t *intf);

#endif /* __MM_SOUND_PLUGIN_RUN_H__ */

