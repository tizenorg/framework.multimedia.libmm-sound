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
#include <sys/time.h>
#include <time.h>
#include <semaphore.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <mm_error.h>
#include <mm_debug.h>
#include <mm_source.h>

#include <pthread.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <semaphore.h>

#include <avsys-audio.h>


#include "../../include/mm_sound_plugin_run.h"
#include "../../include/mm_sound_plugin_codec.h"

#define TIMEOUT_SEC 2
#define MAX_BUFFER_SIZE 1920
#define KEYTONE_PATH "/tmp/keytone"		/* Keytone pipe path */
#define KEYTONE_GROUP	6526			/* Keytone group : assigned by security */
#define FILE_FULL_PATH 1024				/* File path lenth */
#define AUDIO_CHANNEL 1
#define AUDIO_SAMPLERATE 44100


enum {
	WAVE_CODE_UNKNOWN		= 0,
	WAVE_CODE_PCM			= 1,
	WAVE_CODE_ADPCM			= 2,
	WAVE_CODE_G711			= 3,
	WAVE_CODE_IMA_ADPCM		= 17,
	WAVE_CODE_G723_ADPCM		= 20,
	WAVE_CODE_GSM			= 49,
	WAVE_CODE_G721_ADPCM		= 64,
	WAVE_CODE_MPEG			= 80,
};

#define MAKE_FOURCC(a, b, c, d)		((a) | (b) << 8) | ((c) << 16 | ((d) << 24))
#define RIFF_CHUNK_ID				((unsigned long) MAKE_FOURCC('R', 'I', 'F', 'F'))
#define RIFF_CHUNK_TYPE				((unsigned long) MAKE_FOURCC('W', 'A', 'V', 'E'))
#define FMT_CHUNK_ID				((unsigned long) MAKE_FOURCC('f', 'm', 't', ' '))
#define DATA_CHUNK_ID				((unsigned long) MAKE_FOURCC('d', 'a', 't', 'a'))

enum {
	RENDER_READY,
	RENDER_START,
	RENDER_STARTED,
	RENDER_STOP,
	RENDER_STOPED,
	RENDER_STOPED_N_WAIT,
	RENDER_COND_TIMED_WAIT,
};

typedef struct
{
	pthread_mutex_t sw_lock;
	pthread_cond_t sw_cond;
	avsys_handle_t handle;

	int period;
	int volume_config;
	int state;
	void *src;
} keytone_info_t;

typedef struct
{
	char filename[FILE_FULL_PATH];
	int volume_config;
} ipc_type;

typedef struct
{
	mmsound_codec_info_t *info;
	MMSourceType *source;
} buf_param_t;

static int (*g_thread_pool_func)(void*, void (*)(void*)) = NULL;

int CreateAudioHandle();
static int g_CreatedFlag;
static int __MMSoundKeytoneParse(MMSourceType *source, mmsound_codec_info_t *info);
static int _MMSoundKeytoneInit(void);
static int _MMSoundKeytoneFini(void);
static int _MMSoundKeytoneRender(void *param_not_used);
static keytone_info_t g_keytone;
static int stop_flag = 0;

static 
int MMSoundPlugRunKeytoneControlRun(void)
{
	int pre_mask;
	int ret = MM_ERROR_NONE;
	int fd = -1;
	ipc_type data;
	int size = 0;
	mmsound_codec_info_t info = {0,};
	MMSourceType source = {0,};
	buf_param_t buf_param = {NULL, NULL};

	debug_enter("\n");

	/* INIT IPC */
	pre_mask = umask(0);
	if (mknod(KEYTONE_PATH,S_IFIFO|0660,0)<0) {
		debug_warning ("mknod failed. errno=[%d][%s]\n", errno, strerror(errno));
	}
	umask(pre_mask);

	fd = open(KEYTONE_PATH, O_RDWR);
	debug_msg("after open file descriptor %d\n", fd);
	if (fd == -1) {
		debug_warning("Check ipc node %s\n", KEYTONE_PATH);
		return MM_ERROR_SOUND_INTERNAL;
	}

	/* change access mode so group can use keytone pipe */
	if (fchmod (fd, 0660) == -1) {
		debug_warning("Changing keytone access mode is failed. errno=[%d][%s]\n", errno, strerror(errno));
	}

	/* change group due to security request */
	if (fchown (fd, -1, KEYTONE_GROUP) == -1) {
		debug_warning("Changing keytone group is failed. errno=[%d][%s]\n", errno, strerror(errno));
	}

	/* Init Audio Handle & internal buffer */
	ret = _MMSoundKeytoneInit();	/* Create two thread and open device */
	if (ret != MM_ERROR_NONE) {
		debug_critical("Cannot create keytone\n");

	}
	stop_flag = 1;
	source.ptr = NULL;

	debug_msg("[%s] Trace\n", __func__);
	size = sizeof(ipc_type);
	int once= MMSOUND_TRUE;
	g_CreatedFlag = MMSOUND_FALSE;

	while (stop_flag) {
		memset(&data, 0, sizeof(ipc_type));
		debug_msg("[%s] The Keytone plugin is running......\n", __func__);
		ret = read(fd, (void *)&data, size);
		if (ret == -1) {
			debug_error("[%s] Fail to read file\n", __func__);
			continue;
		}
		debug_msg("[%s] The Keytone plugin is running......READ returns....\n", __func__);

		pthread_mutex_lock(&g_keytone.sw_lock);

		if (g_keytone.state == RENDER_STARTED) {
			g_keytone.state = RENDER_STOP;
			pthread_cond_wait(&g_keytone.sw_cond, &g_keytone.sw_lock);
		}

		/* Close audio handle if volume_config is changed */
		if ((g_CreatedFlag == MMSOUND_TRUE) && (g_keytone.volume_config != data.volume_config)) {
			debug_msg("[%s] Close audio handle if volume config is changed previous:%x new:%x",
				__func__, g_keytone.volume_config, data.volume_config);
			if(AVSYS_FAIL(avsys_audio_close(g_keytone.handle)))	{
				debug_critical("avsys_audio_close() failed !!!!!!!!\n");
			}
			g_CreatedFlag = MMSOUND_FALSE;
		}
		g_keytone.volume_config = data.volume_config;
		debug_log("[%s] The volume config is [%x]\n", __func__, g_keytone.volume_config);

		ret = mm_source_open_file(data.filename, &source, MM_SOURCE_NOT_DRM_CONTENTS);
		if (ret != MM_ERROR_NONE) {
			debug_critical("Cannot open files\n");
			pthread_mutex_unlock(&g_keytone.sw_lock);
			continue;
		}


		ret = __MMSoundKeytoneParse(&source, &info);
		if (ret != MM_ERROR_NONE) {
			debug_critical("Fail to parse file\n");
			mm_source_close(&source);
			source.ptr = NULL;
			pthread_mutex_unlock(&g_keytone.sw_lock);
			continue;
		}

		if (g_CreatedFlag== MMSOUND_FALSE) {
			if(MM_ERROR_NONE != CreateAudioHandle(info)) {
				debug_critical("Audio handle creation failed. cannot play keytone\n");
				mm_source_close(&source);
				source.ptr = NULL;
				pthread_mutex_unlock(&g_keytone.sw_lock);
				continue;
			}
			g_CreatedFlag = MMSOUND_TRUE;
		}

		buf_param.info = &info;
		buf_param.source = &source;
		g_keytone.src = &buf_param;
		if (once== MMSOUND_TRUE) {
			g_thread_pool_func(NULL,  (void*)_MMSoundKeytoneRender);
			once= MMSOUND_FALSE;
		}

		if (g_keytone.state == RENDER_STOPED_N_WAIT ||
				 g_keytone.state == RENDER_COND_TIMED_WAIT) {
			pthread_cond_signal(&g_keytone.sw_cond);
		}

		debug_log ("[%s] set state to START, unlock \n", __func__);
		g_keytone.state = RENDER_START;
		pthread_mutex_unlock(&g_keytone.sw_lock);

	}

	if (fd > -1)
		close(fd);
	_MMSoundKeytoneFini();
	debug_leave("\n");

	return MM_ERROR_NONE;
}

static
int MMSoundPlugRunKeytoneControlStop(void)
{
	stop_flag = 0; /* No impl. Don`t stop */
	return MM_ERROR_NONE;
}

static
int MMSoundPlugRunKeytoneSetThreadPool(int (*func)(void*, void (*)(void*)))
{
	debug_enter("(func : %p)\n", func);
	g_thread_pool_func = func;
	debug_leave("\n");
	return MM_ERROR_NONE;
}

EXPORT_API
int MMSoundPlugRunGetInterface(mmsound_run_interface_t *intf)
{
	debug_enter("\n");
	intf->run = MMSoundPlugRunKeytoneControlRun;
	intf->stop = MMSoundPlugRunKeytoneControlStop;
	intf->SetThreadPool = MMSoundPlugRunKeytoneSetThreadPool;
	debug_leave("\n");

	return MM_ERROR_NONE;
}

EXPORT_API
int MMSoundGetPluginType(void)
{
	debug_enter("\n");
	debug_leave("\n");
	return MM_SOUND_PLUGIN_TYPE_RUN;
}

static int _MMSoundKeytoneInit(void)
{

	debug_enter("\n");

	//g_keytone.vol_type = AVSYS_AUDIO_VOLUME_TYPE_SYSTEM; //default value.

	/* Set audio FIXED param */

	g_keytone.state = RENDER_READY;
	if (pthread_mutex_init(&(g_keytone.sw_lock), NULL)) {
		debug_error("pthread_mutex_init() failed [%s][%d]\n", __func__, __LINE__);
		return MM_ERROR_SOUND_INTERNAL;
	}
	if (pthread_cond_init(&g_keytone.sw_cond,NULL)) {
		debug_error("pthread_cond_init() failed [%s][%d]\n", __func__, __LINE__);
		return MM_ERROR_SOUND_INTERNAL;
	}

	debug_msg("init\n");
	return MM_ERROR_NONE;
}

static int _MMSoundKeytoneFini(void)
{
	g_keytone.handle = (avsys_handle_t)-1;

	if (pthread_mutex_destroy(&(g_keytone.sw_lock))) {
		debug_error("Fail to destroy mutex\n");
		return MM_ERROR_SOUND_INTERNAL;
	}
	debug_msg("destroy\n");

	if (pthread_cond_destroy(&g_keytone.sw_cond)) {
		debug_error("Fail to destroy cond\n");
		return MM_ERROR_SOUND_INTERNAL;
	}

	return MM_ERROR_NONE;
}

int CreateAudioHandle(mmsound_codec_info_t info)
{
	int err = MM_ERROR_NONE;
	avsys_audio_param_t audio_param;
	
	memset(&audio_param, 0, sizeof(avsys_audio_param_t));

	audio_param.mode = AVSYS_AUDIO_MODE_OUTPUT_LOW_LATENCY;
	audio_param.channels = info.channels;//AUDIO_CHANNEL;
	audio_param.samplerate = info.samplerate;//AUDIO_SAMPLERATE;
	audio_param.format =  AVSYS_AUDIO_FORMAT_16BIT;
	audio_param.vol_type = g_keytone.volume_config;
	audio_param.priority = AVSYS_AUDIO_PRIORITY_0;


	err = avsys_audio_open(&audio_param, &g_keytone.handle, &g_keytone.period);
	if (AVSYS_FAIL(err)) {
		debug_error("Fail to audio open 0x%08X\n", err);
		return MM_ERROR_SOUND_INTERNAL;
	}
	debug_log("Period size is %d bytes\n", g_keytone.period);


	//FIXME :: remove dasf buffer size
	if (g_keytone.period > MAX_BUFFER_SIZE) {
		g_keytone.period = MAX_BUFFER_SIZE;
	}

	return err;

}

static int _MMSoundKeytoneRender(void *param_not_used)
{
	//static int IsAmpON = MMSOUND_FALSE; //FIXME :: this should be removed
	MMSourceType source = {0,};
	mmsound_codec_info_t info = {0,};
	unsigned char *buf = NULL, Outbuf[MAX_BUFFER_SIZE];
	unsigned int size=0;
	buf_param_t *param=NULL;
	struct timespec timeout;
	struct timeval tv;
	int stat;

	while(stop_flag) {
		pthread_mutex_lock(&g_keytone.sw_lock);
		if(g_keytone.state == RENDER_STOPED) {
			debug_log ("[%s] set state to STOPPED_N_WAIT and do cond wait\n", __func__);
			g_keytone.state = RENDER_STOPED_N_WAIT;
			pthread_cond_wait(&g_keytone.sw_cond, &g_keytone.sw_lock);
		}

		if(g_keytone.state == RENDER_START) {
			//IsAmpON = MMSOUND_TRUE;

			debug_log ("[%s] state is START\n", __func__);
			param = (buf_param_t *)g_keytone.src;

			source = *param->source; /* Copy source */
			info = *param->info;
			buf = source.ptr+info.doffset;

			size = info.size;
			if(buf==NULL) {
				size=0;
				debug_critical("Ooops.... Not Expected!!!!!!!!\n");
			}

			g_keytone.state = RENDER_STARTED;
		}

		pthread_mutex_unlock(&g_keytone.sw_lock);




		while(size && stop_flag) {
			pthread_mutex_lock(&g_keytone.sw_lock);
			if (g_keytone.state == RENDER_STOP) {
				debug_log ("[%s] state is STOP\n", __func__);
				pthread_mutex_unlock(&g_keytone.sw_lock);
				break;
			}
			pthread_mutex_unlock(&g_keytone.sw_lock);				

			if(size<g_keytone.period) {
#if defined(_DEBUG_VERBOS_)
				debug_msg("[%s][Keysound] Last Buffer :: size=%d, period=%d\n", __func__, size, g_keytone.period);
#endif
				memset(Outbuf, 0, g_keytone.period);
				memcpy(Outbuf, buf, size);
				avsys_audio_write(g_keytone.handle, (void *)Outbuf, g_keytone.period);
								
				memset(Outbuf, 0, g_keytone.period);
				avsys_audio_write(g_keytone.handle, (void *)Outbuf, g_keytone.period);
				avsys_audio_write(g_keytone.handle, (void *)Outbuf, g_keytone.period);				
				
				size = 0;
			} else {
#if defined(_DEBUG_VERBOS_)
				debug_msg("[%s][Keysound] size=%d, period=%d\n", __func__, size, g_keytone.period);
#endif
				memcpy(Outbuf, buf, g_keytone.period);

				avsys_audio_write(g_keytone.handle, (void *)Outbuf, g_keytone.period);
				size -= g_keytone.period;
				buf += g_keytone.period;

			}
		}

		mm_source_close(&source);
		source.ptr = NULL;

		pthread_mutex_lock(&g_keytone.sw_lock);
		if(g_keytone.state == RENDER_STOP ) {
			debug_msg("[%s] state is STOP, do cond signal \n", __func__);
			g_keytone.state = RENDER_STOPED;
			pthread_cond_signal(&g_keytone.sw_cond);
		} else {
			debug_msg("[%s] state is not STOP\n", __func__);
			g_keytone.state = RENDER_COND_TIMED_WAIT;
			gettimeofday(&tv, NULL);
			timeout.tv_sec = tv.tv_sec + TIMEOUT_SEC;
			timeout.tv_nsec = tv.tv_usec;
			stat = pthread_cond_timedwait(&g_keytone.sw_cond, &g_keytone.sw_lock, &timeout);
			if(stat == ETIMEDOUT && g_keytone.state != RENDER_START) {
				debug_msg("[%s] Do audio handle close and set state to STOPPED\n", __func__);
				if(AVSYS_FAIL(avsys_audio_close(g_keytone.handle)))	{
					debug_critical("avsys_audio_close() failed !!!!!!!!\n");
				}

				g_CreatedFlag = MMSOUND_FALSE;
				g_keytone.state = RENDER_STOPED;
			}
		} /* while(stop_flag) */

		pthread_mutex_unlock(&g_keytone.sw_lock);
	}
	return MMSOUND_FALSE;
}

static int __MMSoundKeytoneParse(MMSourceType *source, mmsound_codec_info_t *info)
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
//	struct __fmt_chunk *pfmt  = NULL;

	int datalen = -1;
	char *data = NULL;
	unsigned int tSize;

	debug_enter("\n");

	data = MMSourceGetPtr(source);
	datalen = MMSourceGetCurSize(source);

#if defined(_DEBUG_VERBOS_)
	debug_msg("source ptr :[%p]\n", data);
	debug_msg("source size :[%d]\n", datalen);
#endif
	priff = (struct __riff_chunk *) data;

	/* Must be checked, Just for wav or not */
	if (priff->chunkid != RIFF_CHUNK_ID ||priff->rifftype != RIFF_CHUNK_TYPE)
		return MM_ERROR_SOUND_UNSUPPORTED_MEDIA_TYPE;

	if(priff->chunksize != datalen -8)
		priff->chunksize = (datalen-8);

	if (priff->chunkid != RIFF_CHUNK_ID ||priff->chunksize != datalen -8 ||priff->rifftype != RIFF_CHUNK_TYPE) {
		debug_msg("This contents is not RIFF file\n");
#if defined(_DEBUG_VERBOS_)
		debug_msg("cunkid : %ld, chunksize : %ld, rifftype : 0x%lx\n", priff->chunkid, priff->chunksize, priff->rifftype);
		debug_msg("cunkid : %ld, chunksize : %d, rifftype : 0x%lx\n", RIFF_CHUNK_ID, datalen-8, RIFF_CHUNK_TYPE);
#endif
		return MM_ERROR_SOUND_UNSUPPORTED_MEDIA_TYPE;
	}
#if defined(_DEBUG_VERBOS_)
	debug_msg("cunkid : %ld, chunksize : %ld, rifftype : %lx\n", priff->chunkid, priff->chunksize, priff->rifftype);
	debug_msg("cunkid : %ld, chunksize : %d, rifftype : %lx\n", RIFF_CHUNK_ID, datalen-8, RIFF_CHUNK_TYPE);
#endif
	tSize = sizeof(struct __riff_chunk);
	pdata = (struct __data_chunk*)(data+tSize);

	while (pdata->chunkid != FMT_CHUNK_ID && tSize < datalen) {
		tSize += (pdata->chunkSize+8);

		if (tSize >= datalen) {
			debug_warning("Wave Parsing is Finished : unable to find the Wave Format chunk\n");
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
		debug_msg("This contents is not supported wave file\n");
#if defined(_DEBUG_VERBOS_)
		debug_msg("chunkid : 0x%lx, comp : 0x%x, av byte/sec : %lu, blockalign : %d\n", pwav->chunkid, pwav->compression, pwav->avgbytepersec, pwav->blockkalign);
#endif
		return MM_ERROR_SOUND_UNSUPPORTED_MEDIA_TYPE;
	}

	/* Only One data chunk support */

	tSize += (pwav->chunksize+8);
	pdata = (struct __data_chunk *)(data+tSize);

	while (pdata->chunkid != DATA_CHUNK_ID && tSize < datalen) {
		tSize += (pdata->chunkSize+8);
		if (tSize >= datalen) {
			debug_warning("Wave Parsing is Finished : unable to find the data chunk\n");
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
	debug_msg("info->size:%d\n", info->size);
	debug_leave("\n");
	return MM_ERROR_NONE;
}
