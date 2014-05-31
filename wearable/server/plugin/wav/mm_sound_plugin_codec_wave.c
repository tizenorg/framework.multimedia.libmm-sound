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

#include <mm_error.h>
#include <mm_debug.h>
#include <pthread.h>
#include <avsys-audio.h>

#include "../../include/mm_sound.h"
#include "../../include/mm_ipc.h"
#include "../../include/mm_sound_thread_pool.h"
#include "../../include/mm_sound_plugin_codec.h"
#include "../../../include/mm_sound_private.h"

#ifdef TIZEN_MICRO
#define USE_SILENT_SND
#endif

#ifdef USE_SILENT_SND
/* Per Sample X 1000 msec /Samplerate = Duration per Sample msec */
/* 1024 Samples X 1000 / 44.1K = 23.22 msec [Duration of Zerovalue feeding time */
/* Mono channel & 16bit format = 2byte per sample */
#define TIME_MSEC (1000)
#define SILENT_SND_MIN_VALUE (90)
#endif

#define SAMPLE_COUNT	128
#define WAV_FILE_SAMPLE_PLAY_DURATION		290			/*Unit: ms*/
enum {
	WAVE_CODE_UNKNOWN				= 0,
	WAVE_CODE_PCM					= 1,
	WAVE_CODE_ADPCM				= 2,
	WAVE_CODE_G711					= 3,
	WAVE_CODE_IMA_ADPCM				= 17,
	WAVE_CODE_G723_ADPCM			= 20,
	WAVE_CODE_GSM					= 49,
	WAVE_CODE_G721_ADPCM			= 64,
	WAVE_CODE_MPEG					= 80,
};

#define MAKE_FOURCC(a, b, c, d)		((a) | (b) << 8) | ((c) << 16 | ((d) << 24))
#define RIFF_CHUNK_ID				((unsigned long) MAKE_FOURCC('R', 'I', 'F', 'F'))
#define RIFF_CHUNK_TYPE				((unsigned long) MAKE_FOURCC('W', 'A', 'V', 'E'))
#define FMT_CHUNK_ID				((unsigned long) MAKE_FOURCC('f', 'm', 't', ' '))
#define DATA_CHUNK_ID				((unsigned long) MAKE_FOURCC('d', 'a', 't', 'a'))

#define CODEC_WAVE_LOCK(LOCK) do { pthread_mutex_lock(LOCK); } while (0)
#define CODEC_WAVE_UNLOCK(LOCK) do { pthread_mutex_unlock(LOCK); } while (0)

enum {
   STATE_NONE = 0,
   STATE_READY,
   STATE_BEGIN,
   STATE_PLAY,
   STATE_STOP,
};

typedef struct
{
	char *ptr_current;
	int size;
	int transper_size;
	avsys_handle_t audio_handle;
	int period;
	int tone;
	int keytone;
	int repeat_count;
	int (*stop_cb)(int);
	int cb_param;
	int state;
	pthread_mutex_t mutex;
	pthread_mutex_t *codec_wave_mutex;
	avsys_audio_param_t audio_param;
	MMSourceType *source;
	char buffer[48000 / 1000 * SAMPLE_COUNT * 2 *2];//segmentation fault when above 22.05KHz stereo
	int handle_route;
	int gain, out, in, option;
#ifdef USE_SILENT_SND
	int silent_snd_cnt;
	unsigned char* silent_snd_buffer;
#endif
} wave_info_t;

static void _runing(void *param);

static int (*g_thread_pool_func)(void*, void (*)(void*)) = NULL;

/* remove build warning */
extern int avsys_audio_pa_ctrl_cork_all(bool cork);

int MMSoundPlugCodecWaveSetThreadPool(int (*func)(void*, void (*)(void*)))
{
    debug_enter("(func : %p)\n", func);
    g_thread_pool_func = func;
    debug_leave("\n");
    return MM_ERROR_NONE;
}

int* MMSoundPlugCodecWaveGetSupportTypes(void)
{
    debug_enter("\n");
    static int suported[2] = {MM_SOUND_SUPPORTED_CODEC_WAVE, 0};
    debug_leave("\n");
    return suported;
}

int MMSoundPlugCodecWaveParse(MMSourceType *source, mmsound_codec_info_t *info)
{
	struct __riff_chunk
	{
		long chunkid;
		long chunksize;
		long rifftype;
	};

	struct __wave_chunk
	{
		long chunkid;
		long chunksize;
		unsigned short compression;
		unsigned short channels;
		unsigned long samplerate;
		unsigned long avgbytepersec;
		unsigned short blockkalign;
		unsigned short bitspersample;
	};

	struct __data_chunk
	{
		long chunkid;
		long chunkSize;
	};

	struct __riff_chunk *priff = NULL;
	struct __wave_chunk *pwav = NULL;
	struct __data_chunk *pdata = NULL;
//	struct __fmt_chunk *pfmt = NULL;

	int datalen = -1;
	char *data = NULL;
	unsigned int tSize;

	debug_enter("\n");

	data = MMSourceGetPtr(source);
	debug_msg("[CODEC WAV] source ptr :[%p]\n", data);

	datalen = MMSourceGetCurSize(source);
	debug_msg("[CODEC WAV] source size :[0x%08X]\n", datalen);

	priff = (struct __riff_chunk *) data;

	/* Must be checked, Just for wav or not */
	if (priff->chunkid != RIFF_CHUNK_ID ||priff->rifftype != RIFF_CHUNK_TYPE)
		return MM_ERROR_SOUND_UNSUPPORTED_MEDIA_TYPE;

	if(priff->chunksize != datalen -8)
		priff->chunksize = (datalen-8);

	if (priff->chunkid != RIFF_CHUNK_ID ||priff->chunksize != datalen -8 ||priff->rifftype != RIFF_CHUNK_TYPE) {
		debug_msg("[CODEC WAV] This contents is not RIFF file\n");
		debug_msg("[CODEC WAV] cunkid : %ld, chunksize : %ld, rifftype : 0x%lx\n", priff->chunkid, priff->chunksize, priff->rifftype);
		//debug_msg("[CODEC WAV] cunkid : %ld, chunksize : %d, rifftype : 0x%lx\n", RIFF_CHUNK_ID, datalen-8, RIFF_CHUNK_TYPE);
		return MM_ERROR_SOUND_UNSUPPORTED_MEDIA_TYPE;
	}

	debug_msg("[CODEC WAV] cunkid : %ld, chunksize : %ld, rifftype : 0x%lx\n", priff->chunkid, priff->chunksize, priff->rifftype);
	//debug_msg("[CODEC WAV] cunkid : %ld, chunksize : %d, rifftype : 0x%lx\n", RIFF_CHUNK_ID, datalen-8, RIFF_CHUNK_TYPE);

	tSize = sizeof(struct __riff_chunk);
	pdata = (struct __data_chunk*)(data+tSize);

	while (pdata->chunkid != FMT_CHUNK_ID && tSize < datalen) {
		tSize += (pdata->chunkSize+8);

		if (tSize >= datalen) {
			debug_warning("[CODEC WAV] Parsing finished : unable to find the Wave Format chunk\n");
			return MM_ERROR_SOUND_UNSUPPORTED_MEDIA_TYPE;
		} else {
			pdata = (struct __data_chunk*)(data+tSize);
		}
	}
	pwav = (struct __wave_chunk*)(data+tSize);

	if (pwav->chunkid != FMT_CHUNK_ID ||
	    pwav->compression != WAVE_CODE_PCM ||	/* Only supported PCM */
	    pwav->avgbytepersec != pwav->samplerate * pwav->blockkalign ||
	    pwav->blockkalign != (pwav->bitspersample >> 3)*pwav->channels) {
		debug_msg("[CODEC WAV] This contents is not supported wave file\n");
		debug_msg("[CODEC WAV] chunkid : 0x%lx, comp : 0x%x, av byte/sec : %lu, blockalign : %d\n", pwav->chunkid, pwav->compression, pwav->avgbytepersec, pwav->blockkalign);
		return MM_ERROR_SOUND_UNSUPPORTED_MEDIA_TYPE;
	}

	/* Only One data chunk support */

	tSize += (pwav->chunksize+8);
	pdata = (struct __data_chunk *)(data+tSize);

	while (pdata->chunkid != DATA_CHUNK_ID && tSize < datalen) {
		tSize += (pdata->chunkSize+8);
		if (tSize >= datalen) {
			debug_warning("[CODEC WAV] Parsing finished : unable to find the data chunk\n");
			return MM_ERROR_SOUND_UNSUPPORTED_MEDIA_TYPE;
		} else {
			pdata = (struct __data_chunk*)(data+tSize);
		}
	}

	info->codec = MM_SOUND_SUPPORTED_CODEC_WAVE;
	info->channels = pwav->channels;
	info->format = pwav->bitspersample;
	info->samplerate = pwav->samplerate;
	info->doffset = (tSize+8);
	info->size = pdata->chunkSize;
	info->duration = ((info->size)*1000)/pwav->avgbytepersec;
	debug_msg("info->size:%d info->duration: %d\n", info->size, info->duration);

	debug_leave("\n");
	return MM_ERROR_NONE;
}

#ifdef  USE_SILENT_SND
static int 	_get_silent_snd_buffer_cnt(int sample_rate, int channel, int format)
{
	int cnt;
	int buffer_cnt;
	debug_msg("s %d, ch %d, f %d", sample_rate, channel, format);

	/* Need to control cnt value */
	switch(sample_rate)
	{
	case 48000:
	case 44100:
		cnt = 2;
		break;
	case 32000:
	case 24000:
	case 22050:
	case 16000:
	case 12000:
	case 11025:
	case 8000:
		cnt = 1;
		break;
	default:
		debug_error("Invalid sample rate %d", sample_rate);
		return MM_ERROR_SOUND_UNSUPPORTED_FORMAT;
	}
	/* Sample count 1024 based on 44K */
	buffer_cnt = 1024 * channel * (format >> 3) * cnt;
	debug_msg("Buffer count :: %d\n", buffer_cnt);

	return buffer_cnt;
}
#endif

int MMSoundPlugCodecWaveCreate(mmsound_codec_param_t *param, mmsound_codec_info_t *info, MMHandleType *handle)
{
	wave_info_t* p = NULL;

	static avsys_handle_t keytone_handle = (avsys_handle_t)-1;
	MMSourceType *source;

	static int keytone_period = 0;
#ifdef USE_SILENT_SND
	int duration, buf_size;
	int samplerate, channel, format, offset;
#endif
#ifdef DEBUG_DETAIL
	debug_enter("\n");
#endif
	debug_msg("period[%d] handle[%d] type[%s] ch[%d] format[%d] rate[%d] doffset[%d] priority[%d] repeat[%d] volume[%d] callback[%p] keytone[%08x] route[%d]\n",
			keytone_period, (int)keytone_handle, (info->codec == MM_SOUND_SUPPORTED_CODEC_WAVE) ? "Wave" : "Unknown",
			info->channels, info->format, info->samplerate, info->doffset, param->priority, param->repeat_count,
			param->volume, param->stop_cb, param->keytone, param->handle_route);

	source = param->source;

	if (g_thread_pool_func == NULL) {
		debug_error("[CODEC WAV] Need thread pool!\n");
		return MM_ERROR_SOUND_INTERNAL;
	}

	p = (wave_info_t *) malloc(sizeof(wave_info_t));

	if (p == NULL) {
		debug_error("[CODEC WAV] memory allocation failed\n");
		return MM_ERROR_OUT_OF_MEMORY;
	}

	memset(p, 0, sizeof(wave_info_t));
	p->audio_handle = (avsys_handle_t)-1;

	p->ptr_current = MMSourceGetPtr(source) + info->doffset;

	p->size = info->size;
	p->transper_size = info->samplerate / 1000 * SAMPLE_COUNT * (info->format >> 3) * info->channels;

	p->tone = param->tone;
	p->repeat_count = param ->repeat_count;
	p->stop_cb = param->stop_cb;
	p->cb_param = param->param;
	p->source = source;
	p->codec_wave_mutex = param->codec_wave_mutex;
	//	pthread_mutex_init(&p->mutex, NULL);

	debug_msg("[CODEC WAV] transper_size : %d\n", p->transper_size);
	debug_msg("[CODEC WAV] size : %d\n", p->size);


	/* audio param setting */
	memset (&p->audio_param, 0, sizeof(avsys_audio_param_t));
	p->audio_param.priority = param->priority;
	p->audio_param.vol_type = param->volume_config;
	p->audio_param.channels = info->channels;
	p->audio_param.samplerate = info->samplerate;

	if(param->handle_route == MM_SOUND_HANDLE_ROUTE_USING_CURRENT) /* normal, solo */
		p->audio_param.handle_route = AVSYS_AUDIO_HANDLE_ROUTE_FOLLOWING_POLICY;
	else /* loud solo */
		p->audio_param.handle_route = AVSYS_AUDIO_HANDLE_ROUTE_HANDSET_ONLY;
	p->handle_route = param->handle_route;

	switch(info->format)
	{
	case 8:
		p->audio_param.format =  AVSYS_AUDIO_FORMAT_8BIT;
		break;
	case 16:
		p->audio_param.format =  AVSYS_AUDIO_FORMAT_16BIT;
		break;
	default:
		p->audio_param.format =  AVSYS_AUDIO_FORMAT_16BIT;
		break;
	}

	debug_msg("[CODEC WAV] PARAM mode : [%d]\n", p->audio_param.mode);
	debug_msg("[CODEC WAV] PARAM priority: [%d]\n", p->audio_param.priority);
	debug_msg("[CODEC WAV] PARAM channels : [%d]\n", p->audio_param.channels);
	debug_msg("[CODEC WAV] PARAM samplerate : [%d]\n", p->audio_param.samplerate);
	debug_msg("[CODEC WAV] PARAM format : [%d]\n", p->audio_param.format);
	debug_msg("[CODEC WAV] PARAM volume type : [%x]\n", p->audio_param.vol_type);

#ifdef USE_SILENT_SND
	if((info->format >> 3) == 0) {
		debug_msg("Invalid format parameter %d", info->format);
	}

	samplerate = p->audio_param.samplerate;
	channel = p->audio_param.channels;
	format = info->format;
	offset = info->doffset;
	buf_size = info->size - info->doffset;

	/* Duration per msec */
	duration = (buf_size - offset)/(channel*format>>3)*TIME_MSEC/samplerate;
	debug_msg("Duration %d", duration);
	if(duration < SILENT_SND_MIN_VALUE) {
		p->silent_snd_cnt = _get_silent_snd_buffer_cnt(samplerate, channel, format);
		p->silent_snd_buffer = (unsigned char*)malloc(sizeof(unsigned char)*p->silent_snd_cnt);
		memset(p->silent_snd_buffer, 0, p->silent_snd_cnt);
	}
	else {
		p->silent_snd_cnt = 0;
		p->silent_snd_buffer = NULL;
	}

	if(p->silent_snd_cnt)
		p->audio_param.mode = AVSYS_AUDIO_MODE_OUTPUT_LOW_LATENCY;
	else
		p->audio_param.mode = AVSYS_AUDIO_MODE_OUTPUT;
#else	/* USE_SILENT_SND */
		/* Will be fix with duration info */
		p->audio_param.mode = AVSYS_AUDIO_MODE_OUTPUT_LOW_LATENCY;
#endif	/* NOT USE_SILENT_SND */

	p->state = STATE_READY;

	g_thread_pool_func(p, _runing);
	debug_msg("[CODEC WAV] Thread pool start\n");
	*handle = (MMHandleType)p;

#ifdef DEBUG_DETAIL
	debug_leave("\n");
#endif

	return MM_ERROR_NONE;
}


int MMSoundPlugCodecWavePlay(MMHandleType handle)
{
	wave_info_t *p = (wave_info_t *) handle;

	debug_enter("(handle %x)\n", handle);

	if (p->size <= 0) {
		debug_error("[CODEC WAV] end of file\n");
		return MM_ERROR_END_OF_FILE;
	}
	debug_msg("[CODEC WAV] send start signal\n");
	p->state = STATE_BEGIN;

	debug_leave("\n");

	return MM_ERROR_NONE;
 }

bool is_previous_route_available(int out ,int in)
{
	mm_sound_route route;
	bool in_available = false;
	bool out_available = false;
	int ret = MM_ERROR_NONE;

	switch(out) {
		case AVSYS_AUDIO_PATH_EX_SPK:
			route = MM_SOUND_ROUTE_OUT_SPEAKER;
			break;

		case AVSYS_AUDIO_PATH_EX_RECV:
			route = MM_SOUND_ROUTE_OUT_RECEIVER;
			break;

		case AVSYS_AUDIO_PATH_EX_HEADSET:
			route = MM_SOUND_ROUTE_OUT_WIRED_ACCESSORY;
			break;

		case AVSYS_AUDIO_PATH_EX_BTHEADSET:
			route = MM_SOUND_ROUTE_INOUT_BLUETOOTH;
			break;

		case AVSYS_AUDIO_PATH_EX_A2DP:
			route = MM_SOUND_ROUTE_OUT_BLUETOOTH;
			break;

		case AVSYS_AUDIO_PATH_EX_DOCK:
			route = MM_SOUND_ROUTE_OUT_DOCK;
			break;

		case AVSYS_AUDIO_PATH_EX_HDMI:
			route = MM_SOUND_ROUTE_OUT_HDMI;
			break;

		case AVSYS_AUDIO_PATH_EX_USBAUDIO:
			route = MM_SOUND_ROUTE_OUT_USB_AUDIO;
			break;

		case AVSYS_AUDIO_PATH_EX_WFD:
			route = MM_SOUND_ROUTE_OUT_MIRRORING;
			break;

		default :
			debug_msg("[CODEC WAV] unknow path");
			return false;
	}

	ret =  mm_sound_is_route_available(route, &out_available);
	if (ret != MM_ERROR_NONE) {
		debug_error("mm_sound_is_route_available() failed\n");
		return false;
	}

	if (false == out_available) {
		return false;
	}

	switch (in) {
		case MM_SOUND_ROUTE_IN_MIC:
			route = MM_SOUND_ROUTE_IN_MIC;
			break;

		case AVSYS_AUDIO_PATH_EX_HEADSETMIC:
			route = MM_SOUND_ROUTE_IN_WIRED_ACCESSORY;
			break;

		case AVSYS_AUDIO_PATH_EX_BTMIC:
			route = MM_SOUND_ROUTE_INOUT_BLUETOOTH;
			break;

		default :
			debug_msg("[CODEC WAV] unknow path");
			return false;
	}
	ret =  mm_sound_is_route_available(route, &in_available);
	if (ret != MM_ERROR_NONE) {
		debug_error("mm_sound_is_route_available() failed\n");
		return false;
	}

	return  in_available & out_available;
}

static void _runing(void *param)
{
	wave_info_t *p = (wave_info_t*) param;
	int nread = 0;
	char *org_cur = NULL;
	int org_size = 0;
//	int stop_size;
	char *dummy = NULL;
	int ret;

	int gain_after, out_after, in_after, option_after;

	debug_enter("[CODEC WAV] (Slot ID %d)\n", p->cb_param);
	CODEC_WAVE_LOCK(p->codec_wave_mutex);

	/*
	 * set path here
	 */
	switch(p->handle_route)
	{
		case MM_SOUND_HANDLE_ROUTE_SPEAKER:
		case MM_SOUND_HANDLE_ROUTE_SPEAKER_NO_RESTORE:
			debug_msg("[CODEC WAV] Save backup path\n");
			avsys_audio_get_path_ex(&p->gain, &p->out, &p->in, &p->option);

			/* if current out is not speaker, then force set path to speaker */
			if (p->out != AVSYS_AUDIO_PATH_EX_SPK) {
				debug_msg("[CODEC WAV] current out is not SPEAKER, set path to SPEAKER now!!!\n");

				avsys_audio_pa_ctrl_cork_all(1);
				avsys_audio_set_path_ex(AVSYS_AUDIO_GAIN_EX_AUDIOPLAYER, AVSYS_AUDIO_PATH_EX_SPK, AVSYS_AUDIO_PATH_EX_NONE, AVSYS_AUDIO_PATH_OPTION_NONE);
			}
			break;
		case MM_SOUND_HANDLE_ROUTE_USING_CURRENT:
		default:
			break;
	}

	ret = avsys_audio_open(&p->audio_param, &p->audio_handle, &p->period);
	if (ret != MM_ERROR_NONE) {
		debug_critical("[CODEC WAV] Can not open audio handle\n");
		return;
	}

	if (p->audio_handle == (avsys_handle_t)-1) {
		debug_critical("[CODEC WAV] audio_handle is not created !! \n");
		if (p) {
			free(p);
			p = NULL;
		}
		return;
	}

	/* Set the thread schedule */
	org_cur = p->ptr_current;
	org_size = p->size;

	dummy = malloc(p->period);
	if(!dummy) {
		debug_error("[CODEC WAV] not enough memory");
		return;
	}
	memset(dummy, 0, p->period);
	p->transper_size = p->period;
//	stop_size = org_size > p->period ? org_size : p->period;

	debug_msg("[CODEC WAV] Wait start signal\n");

	while(p->state == STATE_READY)
		usleep(4);

	debug_msg("[CODEC WAV] Recv start signal\n");
	debug_msg("[CODEC WAV] repeat : %d\n", p->repeat_count);
	debug_msg("[CODEC WAV] transper_size : %d\n", p->transper_size);

	if (p->state != STATE_STOP) {
		debug_msg("[CODEC WAV] Play start\n");
		p->state = STATE_PLAY;
	} else {
		debug_warning ("[CODEC WAV] state is already STATE_STOP\n");
	}

#ifdef USE_SILENT_SND
		if(p->silent_snd_cnt) {
			/* Silent SND playback */
			avsys_audio_write(p->audio_handle, p->silent_snd_buffer, p->silent_snd_cnt);
			debug_msg("Silence sound played %d\n",p->silent_snd_cnt);
		}
#endif

	while (((p->repeat_count == -1)?(1):(p->repeat_count--)) && p->state == STATE_PLAY) {
		while (p->state == STATE_PLAY && p->size > 0) {
			if (p->size >= p->transper_size) {
				nread = p->transper_size;
				memcpy(p->buffer, p->ptr_current, nread);
				avsys_audio_write(p->audio_handle, p->buffer, nread);
				p->ptr_current += nread;
				p->size -= nread;
				debug_msg("[CODEC WAV] Playing, nRead_data : %d Size : %d \n", nread, p->size);
			} else {
				/* Write remain size */
				nread = p->size;
				memcpy(p->buffer, p->ptr_current, nread);
				avsys_audio_write(p->audio_handle, p->buffer, nread);
				avsys_audio_write(p->audio_handle, dummy, (p->transper_size-nread));
				//avsys_audio_drain(p->audio_handle);
				//debug_error("Drain is called\n");
				p->ptr_current += nread;
				p->size = 0;
			}
		}
		p->ptr_current = org_cur;
		p->size = org_size;
	}

	debug_msg("[CODEC WAV] End playing\n");
	p->state = STATE_STOP;

	if (p->audio_handle == (avsys_handle_t)-1) {
		usleep(200000);
		debug_warning("[CODEC WAV] audio already unrealize !!\n");
	} else {
		//usleep(75000);
		if(AVSYS_FAIL(avsys_audio_drain(p->audio_handle)))
		{
			debug_error("avsys_audio_drain() failed\n");
		}

		ret = avsys_audio_close(p->audio_handle);
		if (AVSYS_FAIL(ret)) {
			debug_critical("[CODEC WAV] Can not close audio handle\n");
		}
		/*
		 * Restore path here
		 */
		if (p->handle_route == MM_SOUND_HANDLE_ROUTE_SPEAKER) {
			avsys_audio_get_path_ex(&gain_after, &out_after, &in_after, &option_after);

			/* If current path is not same as before playing sound, restore the sound path */
			if (gain_after != p->gain || out_after != p->out || in_after != p->in || option_after != p->option) {
				if (is_previous_route_available(p->out, p->in)) {
					debug_msg("[CODEC WAV] previous route is available");
					debug_msg("[CODEC WAV] Restore path to previous one\n");
					ret = avsys_audio_set_path_ex(p->gain, p->out, p->in, p->option);
					if(AVSYS_FAIL(ret)) {
						debug_error("[CODEC WAV] Can not restore sound path\n");
					}
				}
				avsys_audio_pa_ctrl_cork_all(0);
			}
		}
	}
	CODEC_WAVE_UNLOCK(p->codec_wave_mutex);

	p->audio_handle = (avsys_handle_t)-1;

	p->state = STATE_NONE;

	free(dummy); dummy = NULL;
	if (p->stop_cb)
	{
		debug_msg("[CODEC WAV] Play is finished, Now start callback\n");
		p->stop_cb(p->cb_param);
	}
	debug_leave("\n");
}


int MMSoundPlugCodecWaveStop(MMHandleType handle)
{
	wave_info_t *p = (wave_info_t*) handle;

	if (!p) {
		debug_error("The handle is NULL\n");
		return MM_ERROR_SOUND_INTERNAL;
	}
	debug_msg("[CODEC WAV] Current state is state %d\n", p->state);
	debug_msg("[CODEC WAV] Handle 0x%08X stop requested\n", handle);

	p->state = STATE_STOP;

    return MM_ERROR_NONE;
}

int MMSoundPlugCodecWaveDestroy(MMHandleType handle)
{
	wave_info_t *p = (wave_info_t*) handle;

	if(!p) {
		debug_warning("Can not destroy handle :: handle is null\n");
		return MM_ERROR_SOUND_INVALID_POINTER;
	}

	if(p->source) {
		mm_source_close(p->source);
		free(p->source); p->source = NULL;
	}

	free(p); p = NULL;

	return MM_ERROR_NONE;
}

EXPORT_API
int MMSoundGetPluginType(void)
{
    return MM_SOUND_PLUGIN_TYPE_CODEC;
}

EXPORT_API
int MMSoundPlugCodecGetInterface(mmsound_codec_interface_t *intf)
{
    intf->GetSupportTypes   = MMSoundPlugCodecWaveGetSupportTypes;
    intf->Parse             = MMSoundPlugCodecWaveParse;
    intf->Create            = MMSoundPlugCodecWaveCreate;
    intf->Destroy           = MMSoundPlugCodecWaveDestroy;
    intf->Play              = MMSoundPlugCodecWavePlay;
    intf->Stop              = MMSoundPlugCodecWaveStop;
    intf->SetThreadPool     = MMSoundPlugCodecWaveSetThreadPool;

    return MM_ERROR_NONE;
}


