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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <semaphore.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <avsys-audio.h>
#include <mm_error.h>
#include <mm_debug.h>
#include <tremolo_vorbisdec_api.h>

#include "../../include/mm_sound.h"
#include "../../include/mm_ipc.h"
#include "../../include/mm_sound_thread_pool.h"
#include "../../include/mm_sound_plugin_codec.h"
#include "../../../include/mm_sound_private.h"

#define OGG_DEC_BUF_SIZE 4096
#define OGG_FILE_SAMPLE_PLAY_DURATION 290

#define TIME_MSEC (1000)
#define SILENT_SND_MIN_VALUE (90)
#define CODEC_OGG_LOCK(LOCK) do { pthread_mutex_lock(LOCK); } while (0)
#define CODEC_OGG_UNLOCK(LOCK) do { pthread_mutex_unlock(LOCK); } while (0)

enum {
   STATE_NONE = 0,
   STATE_READY,
   STATE_BEGIN,
   STATE_PLAY,
   STATE_ENDOFDECORD,
   STATE_STOP,
};

typedef struct {
	/* thread controls */
	sem_t 			start_wave_signal; /* control start of pcm write thread */
	sem_t 			start_ogg_signal; /* control start of ogg decord thread*/
	pthread_mutex_t		mutex;

     /* Codec infomations */
	void 			*decoder;
	char* pcm_out_buf;
	int			offset;
	int	period;
	int	handle_route;

     /* Audio Infomations */
	int 				transper_size; /* avsysaudioopen return */
	avsys_handle_t	     audio_handle;

     /* control Informations */
	int				repeat_count;
	int				(*stop_cb)(int);
	int				cb_param;
	int				state;
	MMSourceType 	     *source;
	int codec;
	int NumberOfChannels;
	int SamplingFrequency;
	int format;
	int Duration;
	int BitRate;
	int BufferSize;
} ogg_info_t;

static int (*g_thread_pool_func)(void*, void (*)(void*)) = NULL;

void _pcm_out_func(void *data)
{
	ogg_info_t *p = (ogg_info_t*) data;
	int used_size;
	int decoded_len;
	unsigned char* ogg_buf;
	unsigned int ogg_remain;
	int err;

	debug_enter("\n");
	//CODEC_OGG_LOCK(p->codec_mutex);

	sem_wait(&p->start_wave_signal);
	ogg_buf = MMSourceGetPtr(p->source);
	ogg_remain = MMSourceGetCurSize(p->source);

	while (p->state == STATE_PLAY) {
		err = OGGDEC_FrameDecode(p->decoder, ogg_buf+p->offset, &used_size, p->pcm_out_buf, &decoded_len);
		if (decoded_len > 0) {
			avsys_audio_write(p->audio_handle, p->pcm_out_buf, decoded_len);
			ogg_buf += used_size;
			ogg_remain -= used_size;
			if(err != OGGDEC_SUCCESS) {
				p->state = STATE_STOP;
				debug_error("Decode done :: %d\n", err);
				break;
			}
		} else {
			p->state = STATE_STOP;
			break;
		}
	}

	if(AVSYS_FAIL(avsys_audio_drain(p->audio_handle)))
		debug_error("avsys_audio_drain() failed\n");

	if(AVSYS_FAIL(avsys_audio_close(p->audio_handle)))
		debug_error("avsys_audio_close() failed\n");

	//CODEC_OGG_UNLOCK(p->codec_mutex);

	/* Notice */
	/* OggDestory is called by stop_cb func */
	/* INDEED the stop_cb must be called, after end of all progress */
	if(p->stop_cb)
		p->stop_cb(p->cb_param);

	debug_leave("\n");
}

int MMSoundPlugCodecOggSetThreadPool(int (*func)(void*, void (*)(void*)))
{
	debug_enter("(func : %p)\n", func);
	g_thread_pool_func = func;
	debug_leave("\n");
	return MM_ERROR_NONE;
}

int* MMSoundPlugCodecOggGetSupportTypes(void)
{
	debug_enter("\n");
	static int suported[2] = {MM_SOUND_SUPPORTED_CODEC_OGG, 0};
	debug_leave("\n");
	return suported;
}

int MMSoundPlugCodecOggParse(MMSourceType *source, mmsound_codec_info_t *info)
{
	unsigned char *ptr = NULL;
	unsigned int size = 0;
	int err;
	OGG_DEC_INFO ogg_info;

	debug_enter("\n");

	ptr = (unsigned char*)MMSourceGetPtr(source);
	size = MMSourceGetCurSize(source);
	debug_msg("[CODEC OGG] source ptr :[0x%08X] ::: source size :[0x%d]\n", ptr, size);

	err = OGGDEC_PreparseDecode(ptr, size, &ogg_info);
	if (err != OGGDEC_SUCCESS) {
		debug_error("Not valid Ogg data format");
		return MM_ERROR_SOUND_UNSUPPORTED_MEDIA_TYPE;
	}

	info->format = 16;
	info->channels = ogg_info.channels;
	info->samplerate = ogg_info.samplerate;
	info->duration = ogg_info.duration;
	info->codec = MM_AUDIO_CODEC_OGG;
	debug_msg("[CODEC OGG]Channels %d  Samplerate %d\n", ogg_info.channels, ogg_info.samplerate);
	debug_leave("\n");

	return MM_ERROR_NONE;
}

int MMSoundPlugCodecOggCreate(mmsound_codec_param_t *param, mmsound_codec_info_t *info, MMHandleType *handle)
{
	ogg_info_t* p = NULL;
	MMSourceType *source = NULL;
	avsys_audio_param_t audio_param;
	OGG_DEC_INFO ogg_info;
	int err, used_size, skipsize;

	debug_enter("\n");

	if (g_thread_pool_func == NULL) {
		debug_error("Need thread pool!\n");
		return MM_ERROR_SOUND_INTERNAL;
	}

	source = param->source;
	debug_msg("[CODEC OGG]Param source p 0x08%X\n", param->source);

	p = (ogg_info_t *) malloc(sizeof(ogg_info_t));
	if (p == NULL) {
		debug_error("[CODEC OGG]Memory allocation Fail\n");
		return MM_ERROR_OUT_OF_MEMORY;
	}

	memset(p, 0, sizeof(ogg_info_t));
	p->source = param->source;
	p->BufferSize = MMSourceGetCurSize(source);

	pthread_mutex_init(&p->mutex, NULL);

	err = sem_init(&p->start_ogg_signal, 0, 0);
	if (err == -1) {
		debug_error("[CODEC OGG]Semaphore init fail\n");
		free(p);
		return MM_ERROR_SOUND_INTERNAL;
	}

	err = sem_init(&p->start_wave_signal, 0, 0);
	if (err == -1) {
		debug_error("[CODEC OGG]Semaphore init fail\n");
		free(p);
		sem_destroy(&p->start_ogg_signal);
		return MM_ERROR_SOUND_INTERNAL;
	}

	err = OGGDEC_CreateDecode(&p->decoder);
	if (err != OGGDEC_SUCCESS || p->decoder == NULL) {
		debug_error("[CODEC OGG]Fail to Create ogg decoder\n");
		sem_destroy(&p->start_ogg_signal);
		sem_destroy(&p->start_wave_signal);
		free(p);
		return MM_ERROR_SOUND_INTERNAL;
	}
	p->pcm_out_buf = (char *)malloc(sizeof(char)*OGG_DEC_BUF_SIZE);
	if (p->pcm_out_buf == NULL) {
		debug_error("[CODEC OGG]Memory allocation fail\n");
		sem_destroy(&p->start_ogg_signal);
		sem_destroy(&p->start_wave_signal);
		OGGDEC_ResetDecode(p->decoder);
		OGGDEC_DeleteDecode(p->decoder);
		free(p);
		return MM_ERROR_SOUND_NO_FREE_SPACE;
	}
	err = OGGDEC_InitDecode(p->decoder, (unsigned char*)p->source->ptr, p->BufferSize, &skipsize);
	if (err != OGGDEC_SUCCESS || p->decoder == NULL) {
		debug_error("Fail to init ogg decoder\n");
		sem_destroy(&p->start_ogg_signal);
		sem_destroy(&p->start_wave_signal);
		OGGDEC_ResetDecode(p->decoder);
		OGGDEC_DeleteDecode(p->decoder);
		free(p->pcm_out_buf);
		free(p);
		return MM_ERROR_SOUND_INTERNAL;
	}
	p->offset = skipsize;

	err = OGGDEC_InfoDecode(p->decoder, p->source->ptr+p->offset, &used_size, &ogg_info);
	if (err != OGGDEC_SUCCESS || p->decoder == NULL)
	{
		debug_error("[CODEC OGG]Fail to get ogg info\n");
		sem_destroy(&p->start_ogg_signal);
		sem_destroy(&p->start_wave_signal);
		OGGDEC_ResetDecode(p->decoder);
		OGGDEC_DeleteDecode(p->decoder);
		free(p->pcm_out_buf);
		free(p);
		return MM_ERROR_SOUND_INTERNAL;
	}
	p->offset += used_size;
	p->NumberOfChannels = info->channels = ogg_info.channels;
	p->SamplingFrequency = info->samplerate = ogg_info.samplerate;
	p->Duration = info->duration;
	p->format = info->format = 16;

	/* Temporal code for debug */
	/*
	debug_msg("([CODEC OGG]handle %x)\n", p);
	debug_msg("[CODEC OGG]Type %s\n", info->codec == MM_AUDIO_CODEC_OGG ? "OGG" : "Unknown");
	debug_msg("[CODEC OGG]channels   : %d\n", info->channels);
	debug_msg("[CODEC OGG]format     : %d\n", info->format);
	debug_msg("[CODEC OGG]samplerate : %d\n", info->samplerate);
	debug_msg("[CODEC OGG]doffset    : %d\n", info->doffset);
	debug_msg("[CODEC OGG]priority : %d\n", param->priority);
	debug_msg("[CODEC OGG]repeat : %d\n", param->repeat_count);
	debug_msg("[CODEC OGG]volume : %d\n", param->volume);
	debug_msg("[CODEC OGG]callback : %08x\n", param->stop_cb);
	*/
	debug_msg("Audio duration [%d]", info->duration);
	p->repeat_count = param ->repeat_count;
	p->stop_cb = param->stop_cb;
	p->cb_param = param->param;

	/* Set audio param */
	memset (&audio_param, 0, sizeof(avsys_audio_param_t));

	if(info->duration < OGG_FILE_SAMPLE_PLAY_DURATION) {
		audio_param.mode = AVSYS_AUDIO_MODE_OUTPUT_LOW_LATENCY;
	} else {
		audio_param.mode = AVSYS_AUDIO_MODE_OUTPUT;
	}

	audio_param.priority = param->priority;
	audio_param.vol_type = param->volume_config;
	audio_param.channels = info->channels;
	audio_param.samplerate = info->samplerate;

	if(param->handle_route == MM_SOUND_HANDLE_ROUTE_USING_CURRENT) /* normal, solo */
		audio_param.handle_route = AVSYS_AUDIO_HANDLE_ROUTE_FOLLOWING_POLICY;
	else /* loud solo */
		audio_param.handle_route = AVSYS_AUDIO_HANDLE_ROUTE_HANDSET_ONLY;

	p->handle_route = param->handle_route;

	switch(info->format)
	{
	case 8:
		audio_param.format =  AVSYS_AUDIO_FORMAT_8BIT;
		break;
	case 16:
		audio_param.format =  AVSYS_AUDIO_FORMAT_16BIT;
		break;
	default:
		audio_param.format =  AVSYS_AUDIO_FORMAT_16BIT;
		break;
	}

	debug_msg("[CODEC OGG] PARAM mode : [%d]  priority: [%d] channels : [%d] : samplerate : [%d] format : [%d] volume type : [%x]\n",
		audio_param.mode, audio_param.priority, audio_param.channels, audio_param.samplerate, audio_param.format, audio_param.vol_type);

	err = avsys_audio_open(&audio_param, &p->audio_handle, &p->period);
	if (err != MM_ERROR_NONE)
		debug_critical("[CODEC OGG] Can not open audio handle\n");

	if (p->audio_handle == (avsys_handle_t)-1) {
		debug_critical("[CODEC OGG] audio_handle is not created !! \n");
		sem_destroy(&p->start_ogg_signal);
		sem_destroy(&p->start_wave_signal);
		OGGDEC_ResetDecode(p->decoder);
		OGGDEC_DeleteDecode(p->decoder);
		free(p->pcm_out_buf);
		free(p);
		return MM_ERROR_SOUND_INTERNAL;
	}

	pthread_mutex_lock(&p->mutex);
	p->state = STATE_READY;
	pthread_mutex_unlock(&p->mutex);
	*handle = (MMHandleType)p;

	err = g_thread_pool_func(p, _pcm_out_func);
	if (err) {
		sem_destroy(&p->start_ogg_signal);
		sem_destroy(&p->start_wave_signal);
		OGGDEC_ResetDecode(p->decoder);
		OGGDEC_DeleteDecode(p->decoder);
		free(p->pcm_out_buf);
		free(p);
		debug_error("[CODEC OGG]pthread_create() fail in pcm thread\n");
		return MM_ERROR_SOUND_INTERNAL;
	}

	debug_leave("\n");
	return MM_ERROR_NONE;
}

int MMSoundPlugCodecOggPlay(MMHandleType handle)
{
	ogg_info_t *p = (ogg_info_t *) handle;
	debug_enter("(handle %x)\n", handle);

	if (p->BufferSize <= 0) {
	    debug_error("[CODEC OGG]End of file\n");
	    return MM_ERROR_END_OF_FILE;
	}
	pthread_mutex_lock(&p->mutex);
	p->state = STATE_PLAY;
	pthread_mutex_unlock(&p->mutex);
	sem_post(&p->start_wave_signal);

	debug_leave("\n");
	return MM_ERROR_NONE;
}

int MMSoundPlugCodecOggStop(MMHandleType handle)
{
	ogg_info_t *p = (ogg_info_t *) handle;
	debug_enter("(handle %x)\n", handle);
	pthread_mutex_lock(&p->mutex);
	p->state = STATE_STOP;
	pthread_mutex_unlock(&p->mutex);
	debug_leave("\n");

	return MM_ERROR_NONE;
}

int MMSoundPlugCodecOggDestroy(MMHandleType handle)
{
	ogg_info_t *p = (ogg_info_t*) handle;
	int err;

	debug_enter("(handle %x)\n", handle);

	if (!p) {
		debug_critical("Confirm the hadle (is NULL)\n");
		err = MM_ERROR_SOUND_INTERNAL;
	}

	if(p->source) {
		mm_source_close(p->source);
		free(p->source); p->source = NULL;
	}

	err = OGGDEC_ResetDecode(p->decoder);
	if (err != OGGDEC_SUCCESS) {
		debug_error("[CODEC OGG]Codec Reset fail\n");
		err = MM_ERROR_SOUND_INTERNAL;
	}

	err = OGGDEC_DeleteDecode(p->decoder);
	if (err != OGGDEC_SUCCESS) 	{
		debug_error("[CODEC OGG]Delete Decode fail\n");
		err = MM_ERROR_SOUND_INTERNAL;
	}

	sem_destroy(&p->start_wave_signal);
	sem_destroy(&p->start_ogg_signal);

	if(p->pcm_out_buf) {
		free(p->pcm_out_buf);
		p->pcm_out_buf = NULL;
	}

	if (p) {
		free (p);
		p = NULL;
	}

	debug_leave("\n");
	return err;
}

EXPORT_API
int MMSoundGetPluginType(void)
{
	debug_enter("\n");
	debug_leave("\n");
	return MM_SOUND_PLUGIN_TYPE_CODEC;
}

EXPORT_API
int MMSoundPlugCodecGetInterface(mmsound_codec_interface_t *intf)
{
	debug_enter("\n");
	if (!intf) {
		debug_critical("Confirm the codec interface struct (is NULL)\n");
		return MM_ERROR_SOUND_INTERNAL;
	}

	intf->GetSupportTypes   = MMSoundPlugCodecOggGetSupportTypes;
	intf->Parse             = MMSoundPlugCodecOggParse;
	intf->Create            = MMSoundPlugCodecOggCreate;
	intf->Destroy           = MMSoundPlugCodecOggDestroy;
	intf->Play              = MMSoundPlugCodecOggPlay;
	intf->Stop              = MMSoundPlugCodecOggStop;
	intf->SetThreadPool     = MMSoundPlugCodecOggSetThreadPool;

	debug_leave("\n");
	return MM_ERROR_NONE;
}