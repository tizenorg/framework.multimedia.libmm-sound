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
#include <mm_sound.h>

#include <pthread.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <semaphore.h>

#include <pulse/sample.h>

#include "../../include/mm_sound_plugin_run.h"
#include "../../include/mm_sound_plugin_codec.h"
#include "../../../include/mm_sound_utils.h"
#include "../../../include/mm_sound_common.h"
#include "../../../include/mm_sound_pa_client.h"

#ifdef OGG_SUPPORT
#include <tremolo_vorbisdec_api.h>
#define OGG_DEC_BUF_SIZE 4096
#endif

#define OGG_FILE_SAMPLE_PLAY_DURATION (290)
#define DEFAULT_TIMEOUT_MSEC_IN_USEC (600*1000)
#define ENV_KEYTONE_TIMEOUT "KEYTONE_TIMEOUT"

#define MAX_BUFFER_SIZE 1920
#define KEYTONE_PATH "/tmp/keytone"		/* Keytone pipe path */
#define KEYTONE_PATH_TMP "/tmp/keytone_tmp"		/* Keytone pipe path (it will be deprecated)*/
#define KEYTONE_GROUP	6526			/* Keytone group : assigned by security */
#define FILE_FULL_PATH 1024				/* File path lenth */
#define AUDIO_CHANNEL 1
#define AUDIO_SAMPLERATE 44100
#define DURATION_CRITERIA 11000          /* write once or not       */

#define SUPPORT_DBUS_KEYTONE
#ifdef SUPPORT_DBUS_KEYTONE
#include <gio/gio.h>

#include <vconf.h>

#define BUS_NAME       "org.tizen.system.deviced"
#define OBJECT_PATH    "/Org/Tizen/System/DeviceD/Key"
#define INTERFACE_NAME "org.tizen.system.deviced.Key"
#define SIGNAL_NAME    "ChangeHardkey"

#define DBUS_HW_KEYTONE "/usr/share/sounds/sound-server/Tizen_HW_Touch.ogg"

#endif /* SUPPORT_DBUS_KEYTONE */

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
	int handle;

	int period;
	int volume_config;
	int state;
	void *src;
#ifdef OGG_SUPPORT
	void *ogg_dec;
	unsigned char *ogg_dec_buf;
	int ogg_offset;
#endif
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
static int g_CreatedFlag = 0;
static int __MMSoundKeytoneParse(MMSourceType *source, mmsound_codec_info_t *info);
static int _MMSoundKeytoneInit(void);
static int _MMSoundKeytoneFini(void);
static int _MMSoundKeytoneRender(void *param_not_used);
static keytone_info_t g_keytone;
static int stop_flag = 0;

//#define USE_SILENT_SND
#ifdef USE_SILENT_SND
/* Per Sample X 1000 msec /Samplerate = Duration per Sample msec */
/* 1024 Samples X 1000 / 44.1K = 23.22 msec [Duration of Zerovalue feeding time */
/* Mono channel & 16bit format = 2byte per sample */
#define SILENT_SND 4096
static unsigned char g_silent_sound[SILENT_SND];
#endif

#define CONTROL_LATENCY
#ifdef CONTROL_LATENCY
#define LATENCY_BUF (8192*2)
#define LATENCY_CONTROL (5292*2)
static unsigned char g_latency_buf[LATENCY_BUF];
#endif



#ifdef SUPPORT_DBUS_KEYTONE
typedef struct {
	char filename[FILE_FULL_PATH];
	int volume_config;
} ipc_t;

GDBusConnection *conn;
guint sig_id;

static int _play_keytone(const char *filename, int volume_config)
{
	int err = -1;
	int fd = -1;
	ipc_t data = {{0,},};
	int ret = MM_ERROR_NONE;

	debug_msg("filepath=[%s], volume_config=[0x%x]\n", filename, volume_config);

	if (!filename)
		return MM_ERROR_SOUND_INVALID_FILE;

	/* Open PIPE */
	if ((fd = open(KEYTONE_PATH, O_WRONLY | O_NONBLOCK)) != -1) {
		/* Set send info. */
		data.volume_config = volume_config;
		MMSOUND_STRNCPY(data.filename, filename, FILE_FULL_PATH);
		/* Write to PIPE */
		if ((err = write(fd, &data, sizeof(ipc_t))) < 0) {
			debug_error("Fail to write data: %s\n", strerror(errno));
			ret = MM_ERROR_SOUND_INTERNAL;
		}
	} else {
		debug_error("Fail to open pipe\n");
		ret = MM_ERROR_SOUND_FILE_NOT_FOUND;
	}

	/* Close PIPE */
	if (fd != -1)
		close(fd);

	return ret;
}

static bool __is_mute_sound ()
{
	int setting_sound_status = true;
	int setting_touch_sound = true;

	/* 1. Check if recording is in progress */
	if (_mm_sound_is_recording()) {
		debug_log ("During Recording....MUTE!!!");
		return true;
	}

	/* 2. Check both SoundStatus & TouchSound vconf key for mute case */
	vconf_get_bool(VCONFKEY_SETAPPL_SOUND_STATUS_BOOL, &setting_sound_status);
	vconf_get_bool(VCONFKEY_SETAPPL_TOUCH_SOUNDS_BOOL, &setting_touch_sound);

	return !(setting_sound_status & setting_touch_sound);
}

static void on_changed_receive(GDBusConnection *conn,
							   const gchar *sender_name,
							   const gchar *object_path,
							   const gchar *interface_name,
							   const gchar *signal_name,
							   GVariant *parameters,
							   gpointer user_data)
{
	debug_msg ("sender : %s, object : %s, interface : %s, signal : %s",
			sender_name, object_path, interface_name, signal_name);

	if (__is_mute_sound ()) {
		debug_log ("Skip playing keytone due to mute sound mode");
	} else {
		_play_keytone (DBUS_HW_KEYTONE, VOLUME_TYPE_SYSTEM | VOLUME_GAIN_TOUCH);
	}
}

static int _init_dbus_keytone ()
{
	GError *err = NULL;

	debug_fenter ();

	g_type_init();

	conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &err);
	if (!conn && err) {
		debug_error ("g_bus_get_sync() error (%s) ", err->message);
		g_error_free (err);
		goto error;
	}

	sig_id = g_dbus_connection_signal_subscribe(conn,
			NULL, INTERFACE_NAME, SIGNAL_NAME, OBJECT_PATH, NULL, 0,
			on_changed_receive, NULL, NULL);
	if (sig_id == 0) {
		debug_error ("g_dbus_connection_signal_subscribe() error (%d)", sig_id);
		goto sig_error;
	}

	debug_fleave ();
	return 0;

sig_error:
	g_dbus_connection_signal_unsubscribe(conn, sig_id);
	if (conn) {
		g_object_unref(conn);
	}

error:
	return -1;
}

static void _deinit_dbus_keytone ()
{
	debug_fenter ();
	g_dbus_connection_signal_unsubscribe(conn, sig_id);
	g_object_unref(conn);
	debug_fleave ();
}
#endif /* SUPPORT_DBUS_KEYTONE */


static MMSourceType * _mm_source_dup(MMSourceType *source)
{
	MMSourceType *dup;
	if(!(dup = (MMSourceType *) malloc (sizeof (MMSourceType))))
		goto fail;
	memcpy (dup, source, sizeof (MMSourceType));

	if(!(dup->ptr = (char*)malloc (source->tot_size)))
		goto fail;
	memcpy (dup->ptr, source->ptr, source->tot_size);
	dup->type = MM_SOURCE_MEMORY;
	dup->fd = -1;

	return dup;
fail:
	if (dup) {
		if (dup->ptr)
			free(dup->ptr);
		free(dup);
	}
	return MM_ERROR_SOUND_INTERNAL;
}



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

	mmsound_codec_info_t info_cached = {0,};
	MMSourceType* source_cached = NULL;
	int is_new_keytone = 0;
	char previous_keytone[FILE_FULL_PATH] = { 0, };
	buf_param_t buf_param = {NULL, NULL};

#ifdef OGG_SUPPORT
	int skipsize;
	OGG_DEC_INFO ogg_info;
#endif

	debug_enter("\n");

	/* INIT IPC */
	pre_mask = umask(0);
	if (mknod(KEYTONE_PATH_TMP,S_IFIFO|0660,0)<0) {
		debug_warning ("mknod failed. errno=[%d][%s]\n", errno, strerror(errno));
	}
	umask(pre_mask);

	fd = open(KEYTONE_PATH_TMP, O_RDWR);
	if (fd == -1) {
		debug_warning("Check ipc node %s\n", KEYTONE_PATH);
		return MM_ERROR_SOUND_INTERNAL;
	}

	/* change access mode so group can use keytone pipe */
	if (fchmod (fd, 0666) == -1) {
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
	/* While loop is always on */
	stop_flag = MMSOUND_TRUE;
	source.ptr = NULL;

	debug_msg("Start IPC with pipe\n");
	size = sizeof(ipc_type);
	int once= MMSOUND_TRUE;

#ifdef SUPPORT_DBUS_KEYTONE
	_init_dbus_keytone();
#endif /* SUPPORT_DBUS_KEYTONE */

	while (stop_flag) {
		memset(&data, 0, sizeof(ipc_type));
#if defined(_DEBUG_VERBOS_)
		debug_log("Start to read from pipe\n");
#endif
		ret = read(fd, (void *)&data, size);
		if (ret == -1) {
			debug_error("Fail to read file\n");
			continue;
		}
#if defined(_DEBUG_VERBOS_)
		debug_log("Read returns\n");
#endif

		pthread_mutex_lock(&g_keytone.sw_lock);

		if (g_keytone.state == RENDER_STARTED) {
			g_keytone.state = RENDER_STOP;
			pthread_cond_wait(&g_keytone.sw_cond, &g_keytone.sw_lock);
		}

		/* Close audio handle if volume_config is changed */
		if ((g_CreatedFlag == MMSOUND_TRUE) && (g_keytone.volume_config != data.volume_config)) {
			debug_msg("Close audio handle if volume config is changed previous:%x new:%x",
				g_keytone.volume_config, data.volume_config);

			if(mm_sound_pa_close(g_keytone.handle) < 0)
				debug_critical("mm_sound_pa_close() failed !!!!!!!!\n");

			g_CreatedFlag = MMSOUND_FALSE;
		}
		g_keytone.volume_config = data.volume_config;
#if defined(_DEBUG_VERBOS_)
		debug_log("The volume config is [%x]\n", g_keytone.volume_config);
#endif

		is_new_keytone = strcmp (data.filename, previous_keytone);
		if (is_new_keytone) {
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

			/* Close audio handle if audio spec is changed */
			if ((g_CreatedFlag == MMSOUND_TRUE)
				&& ((info.channels != info_cached.channels)
					|| (info.samplerate!= info_cached.samplerate)
					|| (info.format != info_cached.format))) {
				debug_msg("Close audio handle if audio channel is changed with previous:%d new:%d", info_cached.channels, info.channels);

				if(mm_sound_pa_close(g_keytone.handle) < 0)
					debug_critical("mm_sound_pa_close() failed !!!!!!!!\n");

				g_CreatedFlag = MMSOUND_FALSE;
			}

			/* Cache this */
			strcpy (previous_keytone, data.filename);
			memcpy (&info_cached, &info, sizeof(mmsound_codec_info_t));
			/* Free previous buffer */
			if (source_cached) {
				mm_source_close(source_cached);
				free (source_cached);
				source_cached = NULL;
			}
			/* Cache again in case of new keytone path */
			source_cached = _mm_source_dup(&source);

			/* Close opened file after copy sound */
			ret = mm_source_close(&source);
			if (ret != MM_ERROR_NONE)
			{
				debug_critical("Fail to close file\n");
				source.ptr = NULL;
				pthread_mutex_unlock(&g_keytone.sw_lock);
				continue;
			}
		} else {
#if defined(_DEBUG_VERBOS_)
			debug_msg ("CONTROL :: Use Cached data : skip source open & parse!!!!!\n");
#endif
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
		/* Use cached value always */
		buf_param.info = &info_cached;
		buf_param.source = source_cached;

#ifdef OGG_SUPPORT
		if(!is_new_keytone) {
			if (buf_param.info->codec == MM_SOUND_SUPPORTED_CODEC_OGG) {
				debug_msg("In case OGG codec type = %d :: SrcPtr = %x :: SrcSize =%d \n ", buf_param.info->codec,buf_param.source->ptr, buf_param.source->cur_size);
				ret = OGGDEC_ResetDecode(g_keytone.ogg_dec);
				if(ret != OGGDEC_SUCCESS) {
					debug_warning("Fail to reset, if in case of first time, ignore it");
				}
				ret = OGGDEC_InitDecode(g_keytone.ogg_dec, (unsigned char*)buf_param.source->ptr, buf_param.source->cur_size, &skipsize);
				if(ret != OGGDEC_SUCCESS) {
					debug_error("Fail to init ogg decoder\n");
					pthread_mutex_unlock(&g_keytone.sw_lock);
					if (fd > -1) close(fd);
					return MM_ERROR_SOUND_UNSUPPORTED_MEDIA_TYPE;
				}
				ret = OGGDEC_InfoDecode(g_keytone.ogg_dec, (unsigned char*)buf_param.source->ptr, &skipsize, &ogg_info);
				if(ret != OGGDEC_SUCCESS) {
					debug_error("Fail to get ogg info\n");
					pthread_mutex_unlock(&g_keytone.sw_lock);
					if (fd > -1) close(fd);
					return MM_ERROR_SOUND_UNSUPPORTED_MEDIA_TYPE;
				}
			}
		}
#endif
		g_keytone.src = &buf_param;
		if (once== MMSOUND_TRUE) {
			g_thread_pool_func(NULL,  (void*)_MMSoundKeytoneRender);
			once= MMSOUND_FALSE;
		}

		if (g_keytone.state == RENDER_STOPED_N_WAIT ||
				 g_keytone.state == RENDER_COND_TIMED_WAIT) {
			pthread_cond_signal(&g_keytone.sw_cond);
		}

#if defined(_DEBUG_VERBOS_)
		debug_log ("set state to START, unlock \n");
#endif
		g_keytone.state = RENDER_START;
		pthread_mutex_unlock(&g_keytone.sw_lock);

	}

	if (fd > -1)
		close(fd);

	if (source_cached) {
		mm_source_close(source_cached);
		free(source_cached);
		source_cached = NULL;
	}

	_MMSoundKeytoneFini();
	debug_leave("\n");

	return MM_ERROR_NONE;
}

static
int MMSoundPlugRunKeytoneControlStop(void)
{
	stop_flag = MMSOUND_FALSE; /* No impl. Don`t stop */

#ifdef SUPPORT_DBUS_KEYTONE
	_deinit_dbus_keytone();
#endif /* SUPPORT_DBUS_KEYTONE */

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

	/* Set audio FIXED param */
	memset(&g_keytone, 0x00, sizeof(g_keytone));
	if (pthread_mutex_init(&(g_keytone.sw_lock), NULL)) {
		debug_error("pthread_mutex_init() failed\n");
		return MM_ERROR_SOUND_INTERNAL;
	}
	if (pthread_cond_init(&g_keytone.sw_cond,NULL)) {
		debug_error("pthread_cond_init() failed\n");
		return MM_ERROR_SOUND_INTERNAL;
	}
#ifdef OGG_SUPPORT
	/* Ogg decoder create */
	g_keytone.ogg_dec_buf = (unsigned char*) malloc(sizeof(unsigned char)*OGG_DEC_BUF_SIZE);
	if (g_keytone.ogg_dec_buf == NULL) {
		debug_error("Fail to malloc\n");
		return MM_ERROR_SOUND_NO_FREE_SPACE;
	}
	memset(g_keytone.ogg_dec_buf, 0, sizeof(unsigned char)*OGG_DEC_BUF_SIZE);
	if(!OGGDEC_CreateDecode(&g_keytone.ogg_dec)) {
		free(g_keytone.ogg_dec_buf);
		debug_error("Fail to create OGG decoder\n");
		return MM_ERROR_SOUND_INTERNAL;
	}
#endif
#ifdef USE_SILENT_SND
	memset(g_silent_sound, 0, SILENT_SND);
#endif

#ifdef CONTROL_LATENCY
	memset(g_latency_buf, 0, LATENCY_BUF);
#endif
	return MM_ERROR_NONE;
}

static int _MMSoundKeytoneFini(void)
{
	g_keytone.handle = -1;

	if (pthread_mutex_destroy(&(g_keytone.sw_lock))) {
		debug_error("Fail to destroy mutex\n");
		return MM_ERROR_SOUND_INTERNAL;
	}
	debug_msg("destroy\n");

	if (pthread_cond_destroy(&g_keytone.sw_cond)) {
		debug_error("Fail to destroy cond\n");
		return MM_ERROR_SOUND_INTERNAL;
	}
#ifdef OGG_SUPPORT
	if(!OGGDEC_ResetDecode(g_keytone.ogg_dec)) {
		debug_error("Fail to Reset decode\n");
		return MM_ERROR_SOUND_INTERNAL;
	}

	if(!OGGDEC_DeleteDecode(g_keytone.ogg_dec)) {
		debug_error("Fail to delete decoder\n");
		return MM_ERROR_SOUND_INTERNAL;
	}
	if (g_keytone.ogg_dec_buf)
		free(g_keytone.ogg_dec_buf);
#endif
	return MM_ERROR_NONE;
}

int CreateAudioHandle(mmsound_codec_info_t info)
{
	int err = MM_ERROR_NONE;
	int size;
	int mode;
	pa_sample_spec ss;

	if(info.duration < OGG_FILE_SAMPLE_PLAY_DURATION) {
		mode = HANDLE_MODE_OUTPUT_LOW_LATENCY;
	} else {
		mode = HANDLE_MODE_OUTPUT;
	}

	ss.rate = info.samplerate;
	ss.channels = info.channels;
	ss.format = PA_SAMPLE_S16LE;

	mm_sound_handle_route_info route_info;
	route_info.policy = HANDLE_ROUTE_POLICY_OUT_AUTO;

	g_keytone.handle = mm_sound_pa_open(mode, &route_info, HANDLE_PRIORITY_NORMAL, g_keytone.volume_config, &ss, NULL, &size);
	if(!g_keytone.handle) {
		debug_error("Fail to audio open \n");
		return MM_ERROR_SOUND_INTERNAL;
	}

#if defined(_DEBUG_VERBOS_)
	debug_log("Period size is %d bytes\n", g_keytone.period);
#endif

	//FIXME :: remove dasf buffer size
	if (g_keytone.period > MAX_BUFFER_SIZE) {
		g_keytone.period = MAX_BUFFER_SIZE;
	}

	return err;

}

static void _init_timeout_val (struct timeval* ptv)
{
	int timeout_us = DEFAULT_TIMEOUT_MSEC_IN_USEC;
	char* str_timeout = NULL;

	if (ptv == NULL)
		return;

	str_timeout = getenv (ENV_KEYTONE_TIMEOUT);
	if (str_timeout) {
		debug_log ("[%s] detected = [%s]\n", ENV_KEYTONE_TIMEOUT, str_timeout);
		timeout_us = atoi (str_timeout);
	}
	debug_log ("Set keytone timeout as [%d] us\n", timeout_us);

	ptv->tv_sec = 0;
	ptv->tv_usec = timeout_us;
}

static int _MMSoundKeytoneRender(void *param_not_used)
{
	//static int IsAmpON = MMSOUND_FALSE; //FIXME :: this should be removed
	MMSourceType source = {0,};
	mmsound_codec_info_t info = {0,};
	unsigned char *buf = NULL;
	unsigned int size=0;
	buf_param_t *param=NULL;
//	unsigned int period_buffer, period_time = 0;

#ifdef LATENCY_CONTROL
	static int lt_cnt = 0;
#endif

	struct timespec timeout;
	struct timeval tv;
	struct timeval tv_to_add;
	struct timeval tv_result;
	int stat;
#ifdef OGG_SUPPORT
	int err, decoded_len, used_size;
#endif
	/* Initialize timeout value */
	_init_timeout_val (&tv_to_add);

	/* Loop */
	while(stop_flag) {
		pthread_mutex_lock(&g_keytone.sw_lock);
		if(g_keytone.state == RENDER_STOPED) {
#if defined(_DEBUG_VERBOS_)
			debug_log ("set state to STOPPED_N_WAIT and do cond wait\n");
#endif
			g_keytone.state = RENDER_STOPED_N_WAIT;
			pthread_cond_wait(&g_keytone.sw_cond, &g_keytone.sw_lock);
		}

		if(g_keytone.state == RENDER_START) {
			param = (buf_param_t *)g_keytone.src;
			source = *param->source; /* Copy source */
			info = *param->info;
			buf = source.ptr+info.doffset;
			size = info.size;
			if(buf[0]=='\0') {
				size=0;
				debug_critical("Ooops.... Not Expected!!!!!!!!\n");
			}
			g_keytone.state = RENDER_STARTED;
		}
		pthread_mutex_unlock(&g_keytone.sw_lock);

#ifdef USE_SILENT_SND
{
		static int use_silent_snd = 0;
		use_silent_snd = 1;
		if(use_silent_snd) {
			/* Silent SND playback */
			mm_sound_pa_write(g_keytone.handle, g_silent_sound, SILENT_SND);
			debug_msg("[Keysound] Silence sound played %d\n",SILENT_SND);
			use_silent_snd = 0;
		}
}
#endif

		while(size && stop_flag) {
			pthread_mutex_lock(&g_keytone.sw_lock);
			if (g_keytone.state == RENDER_STOP) {
#ifdef CONTROL_LATENCY
#if defined(_DEBUG_VERBOS_)
				debug_log ("[CONTROL_LATENCY] state is STOP %d", lt_cnt);
#endif
				if(lt_cnt > 0) {
					mm_sound_pa_write(g_keytone.handle, g_latency_buf, lt_cnt);
					mm_sound_pa_drain(g_keytone.handle);
					lt_cnt = 0;
				}
#endif	/* CONTROL_LATENCY */

				pthread_mutex_unlock(&g_keytone.sw_lock);
				break;
			}
			pthread_mutex_unlock(&g_keytone.sw_lock);
			if(info.codec == MM_SOUND_SUPPORTED_CODEC_OGG) {
#ifdef OGG_SUPPORT
				err = OGGDEC_FrameDecode(g_keytone.ogg_dec, (unsigned char*)buf, &used_size, (char*)g_keytone.ogg_dec_buf, &decoded_len);
				if (decoded_len == 0) {
					/* EOF */
					/* Rare case */
					break;
				}
				if (decoded_len > 0) {
#ifdef CONTROL_LATENCY
					memcpy(g_latency_buf+lt_cnt, g_keytone.ogg_dec_buf, decoded_len);
					lt_cnt += decoded_len;
					buf += used_size;
#if defined(_DEBUG_VERBOS_)
					debug_log("[CONTROL_LATENCY] DATA Ring count %d", lt_cnt);
#endif
					if(lt_cnt > LATENCY_CONTROL) {
						mm_sound_pa_write(g_keytone.handle, g_latency_buf, lt_cnt);
#if defined(_DEBUG_VERBOS_)
						debug_log("[CONTROL_LATENCY] Write count %d", lt_cnt);
#endif
						lt_cnt = 0;
					}
#else
					mm_sound_pa_write(g_keytone.handle, g_keytone.ogg_dec_buf, decoded_len);
					buf += used_size;
#endif	/* CONTROL_LATENCY */
				} else {
					size = 0;
					break;
				}
				if (err != OGGDEC_SUCCESS) {
					size = 0;
					debug_log("Decoding End %d", err);
#ifdef CONTROL_LATENCY
					if(lt_cnt > 0) {
						mm_sound_pa_write(g_keytone.handle, g_latency_buf, lt_cnt);
#if defined(_DEBUG_VERBOS_)
						debug_log("[CONTROL_LATENCY] Last frame is out %d", lt_cnt);
#endif
						mm_sound_pa_drain(g_keytone.handle);
						lt_cnt = 0;
					}
#endif	/* CONTROL_LATENCY */
					break;
				}
#endif	/* OGG_SUPPORT */
			} else if (size < DURATION_CRITERIA) {
#if defined(_DEBUG_VERBOS_)
				debug_msg("[Keysound] Last Buffer :: size=%d, period=%d\n", size, DURATION_CRITERIA);
#endif
				mm_sound_pa_write(g_keytone.handle,  buf, size);
				size = 0;
			} else {
#if defined(_DEBUG_VERBOS_)
				debug_msg("[Keysound] size=%d, period=%d\n", size, DURATION_CRITERIA);
#endif
				mm_sound_pa_write(g_keytone.handle, (void *)buf, DURATION_CRITERIA);
				size -= DURATION_CRITERIA;
				buf += DURATION_CRITERIA;
			}
		}
		pthread_mutex_lock(&g_keytone.sw_lock);
		if(g_keytone.state == RENDER_STOP ) {
#if defined(_DEBUG_VERBOS_)
			debug_log("state is STOP, do cond signal \n");
#endif
			g_keytone.state = RENDER_STOPED;
			pthread_cond_signal(&g_keytone.sw_cond);
		} else {
#if defined(_DEBUG_VERBOS_)
			debug_log("state is not STOP\n");
#endif
			g_keytone.state = RENDER_COND_TIMED_WAIT;

			/* Set timeout : GetCurrent + Timeout => convert to timespec */
			gettimeofday(&tv, NULL);
			timeradd(&tv, &tv_to_add, &tv_result);
			timeout.tv_sec = tv_result.tv_sec;
			timeout.tv_nsec = tv_result.tv_usec * 1000;

			stat = pthread_cond_timedwait(&g_keytone.sw_cond, &g_keytone.sw_lock, &timeout);
			if(stat == ETIMEDOUT && g_keytone.state != RENDER_START) {
				debug_log("TIMEOUT in Not START state, close Audio, set state to STOPPED\n");

				if(mm_sound_pa_close(g_keytone.handle) < 0)
					debug_critical("mm_sound_pa_close() failed !!!!!!!!\n");

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
	int ret;
#ifdef OGG_SUPPORT
	OGG_DEC_INFO ogg_info;
	int skipsize;
#endif

	data = MMSourceGetPtr(source);
	datalen = MMSourceGetCurSize(source);

#if defined(_DEBUG_VERBOS_)
	debug_msg("source ptr :[%p]\n", data);
	debug_msg("source size :[%d]\n", datalen);
#endif
	priff = (struct __riff_chunk *) data;

	/* Must be checked, Just for wav or not */
	if (priff->chunkid != RIFF_CHUNK_ID ||priff->rifftype != RIFF_CHUNK_TYPE) {
#ifdef OGG_SUPPORT
		ret = OGGDEC_ResetDecode(g_keytone.ogg_dec);
		if(ret != OGGDEC_SUCCESS) {
			debug_warning("Fail to reset, if in case of first time, ignore it");
		}
		/* Ogg media type case */
		ret = OGGDEC_InitDecode(g_keytone.ogg_dec, (unsigned char*)data, datalen, &skipsize);
		if(ret != OGGDEC_SUCCESS ) {
			debug_error("Fail to init ogg decoder\n");
			return MM_ERROR_SOUND_UNSUPPORTED_MEDIA_TYPE;
		}
		info->doffset = skipsize;

		ret = OGGDEC_InfoDecode(g_keytone.ogg_dec, (unsigned char*)data, &skipsize, &ogg_info);
		if(ret != OGGDEC_SUCCESS ) {
			debug_error("Fail to get ogg info\n");
			return MM_ERROR_SOUND_UNSUPPORTED_MEDIA_TYPE;
		}
		ret = OGGDEC_PreparseDecode((unsigned char*)data, datalen, &ogg_info);
		if (ret != OGGDEC_SUCCESS) {
			debug_error("Not valid Ogg data format");
			return MM_ERROR_SOUND_UNSUPPORTED_MEDIA_TYPE;
		}

		info->codec = MM_SOUND_SUPPORTED_CODEC_OGG;
		info->channels = ogg_info.channels;
		info->format = 16;
		info->samplerate = ogg_info.samplerate;
		info->doffset += skipsize;
		info->size = datalen;
		info->duration = ogg_info.duration;
		debug_msg("Duration %d :: Channels %d :: Samplerate %d :: File size %d :: offset %d \n", ogg_info.duration, ogg_info.channels, ogg_info.samplerate, info->size, info->doffset);
		//debug_leave("\n");
		return MM_ERROR_NONE;
#else
		debug_error("Not WAVE File\n");
		return MM_ERROR_SOUND_UNSUPPORTED_MEDIA_TYPE;
#endif
	}

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
#if defined(_DEBUG_VERBOS_)
	debug_msg("info->size:%d\n", info->size);
#endif

	return MM_ERROR_NONE;
}
