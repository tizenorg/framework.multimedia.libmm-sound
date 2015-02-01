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


#ifndef __MM_SOUND_MGR_CODEC_H__
#define __MM_SOUND_MGR_CODEC_H__

#include <mm_source.h>

typedef struct {
	int tone;
	int repeat_count;
	double volume;
	int session_type;
	int session_options;
	int session_handle;
	int keytone;
	void *stopcb;
	int (*callback)(int, void *, void *, int);
	void *msgcallback;		/* Client callback function */
	void *msgdata;			/* Client callback data */
	void *param;
	MMSourceType *source; /* Will free plugin */
	int samplerate;
	int channels;
	int volume_config;
	int priority;
	int handle_route;
	bool enable_session;
} mmsound_mgr_codec_param_t;

enum
{
	MM_SOUND_CODEC_OP_KEYTONE = 0,
	MM_SOUND_CODEC_OP_SOUND,
};

int MMSoundMgrCodecInit(const char *targetdir);
int MMSoundMgrCodecFini(void);

int MMSoundMgrCodecPlay(int *slotid, const mmsound_mgr_codec_param_t *param);
int MMSoundMgrCodecStop(const int slotid);
int MMSoundMgrCodecCreate(int *slotid, const mmsound_mgr_codec_param_t *param);
int MMSoundMgrCodecPlayWave(int slotid, const mmsound_mgr_codec_param_t *param);
int MMSoundMgrCodecPlayDtmf(int *slotid, const mmsound_mgr_codec_param_t *param);
int MMSoundMgrCodecDestroy(const int slotid);


#endif /* __MM_SOUND_MGR_CODEC_H__ */

