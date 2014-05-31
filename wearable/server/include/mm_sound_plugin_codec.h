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

#ifndef __MM_SOUND_PLUGIN_CODEC_H__
#define __MM_SOUND_PLUGIN_CODEC_H__

#include "mm_sound_plugin.h"
#include <mm_source.h>
#include <mm_types.h>

enum MMSoundSupportedCodec {
	MM_SOUND_SUPPORTED_CODEC_INVALID = -1,	/**< Invalid codec type */
	MM_SOUND_SUPPORTED_CODEC_WAVE,			/**< WAVE codec		*/
	MM_SOUND_SUPPORTED_CODEC_OGG,		/**< OGG codec		*/
	MM_SOUND_SUPPORTED_CODEC_DTMF,			/**< DTMF codec		*/
	MM_SOUND_SUPPORTED_CODEC_MP3,			/**< MP3 codec		*/
	MM_SOUND_SUPPORTED_CODEC_NUM,			/**< Number of audio codec type	*/
};

typedef struct {
	int codec;
	int channels;
	int samplerate;
	int format;
	int doffset;
	int size;
	int duration;			/**the wav file play duration, Unit: ms*/
} mmsound_codec_info_t;

typedef struct {
	int (*stop_cb)(int);
	int pid;
	int param;
	int tone;
	int repeat_count;
	double volume;
	int priority;
	int volume_config;
	int keytone;
	MMSourceType *source;
	int handle_route;
	pthread_mutex_t *codec_wave_mutex;
} mmsound_codec_param_t;

typedef struct {
    int* (*GetSupportTypes)(void);
    int (*SetThreadPool) (int (*)(void*, void (*)(void*)));
    int (*Parse)(MMSourceType*, mmsound_codec_info_t*);
    int (*Create)(mmsound_codec_param_t*, mmsound_codec_info_t*, MMHandleType*);
    int (*Play_wav)(mmsound_codec_param_t *, mmsound_codec_info_t *, MMHandleType );
    int (*Play)(MMHandleType);
    int (*Stop)(MMHandleType);
    int (*Destroy)(MMHandleType);
} mmsound_codec_interface_t;

/* Utility Functions */
#define CODEC_GET_INTERFACE_FUNC_NAME "MMSoundPlugCodecGetInterface"
#define MMSoundPlugCodecCastGetInterface(func) ((int (*)(mmsound_codec_interface_t*))(func))

int MMSoundPlugCodecGetInterface(mmsound_codec_interface_t *intf);

#endif /* __MM_SOUND_PLUGIN_CODEC_H__ */

