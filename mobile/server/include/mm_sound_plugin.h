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

 
#ifndef __MM_SOUND_PLUGIN_H__
#define __MM_SOUND_PLUGIN_H__

enum {
    MM_SOUND_PLUGIN_TYPE_NONE = 0,
    MM_SOUND_PLUGIN_TYPE_CODEC,
    MM_SOUND_PLUGIN_TYPE_RUN,
    MM_SOUND_PLUGIN_TYPE_HAL,
    MM_SOUND_PLUGIN_TYPE_LAST,
};

typedef struct {
    int type;
    void *module;
} MMSoundPluginType;

#define MMSOUND_TRUE 1
#define MMSOUND_FALSE 0

/* Plugin Interface */
int MMSoundGetPluginType(void);

/* Utility Interfaces */
char* MMSoundPluginGetTypeName(int type);
int MMSoundPluginScan(const char *plugindir, const int type, MMSoundPluginType **pluginlist);
int MMSoundPluginRelease(MMSoundPluginType *pluginlist);

int MMSoundPluginOpen(char *file, MMSoundPluginType *plugin);
int MMSoundPluginClose(MMSoundPluginType *plugin);
int MMSoundPluginGetSymbol(MMSoundPluginType *plugin, const char *symbol, void **func);

#endif /* __MM_SOUND_PLUGIN_H__ */

