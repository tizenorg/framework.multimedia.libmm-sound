/*
 * libmm-sound
 *
 * Copyright (c) 2000 - 2013 Samsung Electronics Co., Ltd. All rights reserved.
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

#include <stdlib.h>
#include <memory.h>
#include <unistd.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/types.h>
#include <fcntl.h>
#include <vconf.h>

#include <sys/stat.h>
#include <errno.h>

#include <mm_types.h>
#include <mm_error.h>
#include <mm_message.h>
#include <mm_debug.h>
#include "include/mm_sound_private.h"
#include "include/mm_sound.h"
#include "include/mm_sound_utils.h"
#include "include/mm_sound_common.h"
#include "include/mm_sound_pcm_async.h"


#include <audio-session-manager.h>
#include <mm_session.h>
#include <mm_session_private.h>

#include <pulse/simple.h>
#include <pulse/error.h>
#include <pulse/proplist.h>
#include <pulse/channelmap.h>
#include <pulse/pulseaudio.h>
#include <pulse/ext-policy.h>


#define _MIN_SYSTEM_SAMPLERATE	8000
#define _MAX_SYSTEM_SAMPLERATE	48000
#define RW_LOG_PERIOD 5 /* period(second) for print log in capture read or play write*/

#define PCM_LOCK_INTERNAL(LOCK) do { pthread_mutex_lock(LOCK); } while (0)
#define PCM_UNLOCK_INTERNAL(LOCK) do { pthread_mutex_unlock(LOCK); } while (0)
#define PCM_LOCK_DESTROY_INTERNAL(LOCK) do { pthread_mutex_destroy(LOCK); } while (0)

#define MAINLOOP_LOCK(LOCK) do { pa_threaded_mainloop_lock(LOCK); debug_msg("MAINLOOP locked"); } while (0)
#define MAINLOOP_UNLOCK(LOCK) do { pa_threaded_mainloop_unlock(LOCK); debug_msg("MAINLOOP unlocked"); } while (0)

typedef struct {
	int			asm_handle;
	ASM_sound_events_t	asm_event;
	int			asm_options;

	bool 			is_started;
	bool			is_playback;
	bool			skip_session;
	ASM_resource_t resource;
	pthread_mutex_t pcm_mutex_internal;
	MMMessageCallback	msg_cb;
	void *msg_cb_param;

	unsigned int rate;
	MMSoundPcmChannel_t channel;
	MMSoundPcmFormat_t format;
	unsigned int byte_per_sec;

	pa_threaded_mainloop *mainloop;
	pa_context *context;
    pa_stream* s;

    int	operation_success;

} mm_sound_pcm_async_t;

static int _pcm_sound_start (MMSoundPcmHandle_t handle);
static int _pcm_sound_stop_internal (MMSoundPcmHandle_t handle);
static int _pcm_sound_stop(MMSoundPcmHandle_t handle);
static void __sound_pcm_send_message (mm_sound_pcm_async_t *pcmHandle, int message, int code);

static int mm_sound_pa_open(mm_sound_pcm_async_t* handle, int mode, int policy, int priority, int volume_config, pa_sample_spec* ss,
		              pa_channel_map* channel_map, int* size, mm_sound_pcm_stream_cb_t callback, void* userdata);
static int _mm_sound_pa_close(mm_sound_pcm_async_t* handle);
static int _mm_sound_pa_cork(mm_sound_pcm_async_t* handle, int cork);
static int _mm_sound_pa_drain(mm_sound_pcm_async_t* handle);
static int _mm_sound_pa_get_latency(mm_sound_pcm_async_t* handle, int* latency);;
static void __mm_sound_pa_success_cb(pa_context *c, int success, void *userdata);

///////////////////////////////////
////     MMSOUND PCM APIs
///////////////////////////////////


static char* __get_channel_str(MMSoundPcmChannel_t channel)
{
	if (channel == MMSOUND_PCM_MONO)
		return "Mono";
	else if (channel == MMSOUND_PCM_STEREO)
		return "Stereo";
	else
		return "Unknown";
}

static char* __get_format_str(MMSoundPcmFormat_t format)
{
	if (format == MMSOUND_PCM_S16_LE)
		return "S16LE";
	else if (format == MMSOUND_PCM_U8)
		return "U8";
	else
		return "Unknown";
}

static int _get_asm_information(ASM_sound_events_t *type, int *options)
{
	int session_type = MM_SESSION_TYPE_MEDIA;
	int session_options = 0;
	ASM_sound_events_t asm_event;

	if(type == NULL)
		return MM_ERROR_SOUND_INVALID_POINTER;

	/* read session information */
	if(_mm_session_util_read_information(-1, &session_type, &session_options) < 0) {
		debug_log("Read Session Information failed. Set default \"Media\" type\n");
		session_type = MM_SESSION_TYPE_MEDIA;
		if(mm_session_init(session_type) < 0) {
			debug_error("mm_session_init() failed\n");
			return MM_ERROR_SOUND_INTERNAL;
		}
	}

	/* convert MM_SESSION_TYPE to ASM_EVENT_TYPE */
	switch (session_type)
	{
	case MM_SESSION_TYPE_MEDIA:
		asm_event = ASM_EVENT_MEDIA_MMSOUND;
		break;
	case MM_SESSION_TYPE_NOTIFY:
		asm_event = ASM_EVENT_NOTIFY;
		break;
	case MM_SESSION_TYPE_ALARM:
		asm_event = ASM_EVENT_ALARM;
		break;
	case MM_SESSION_TYPE_CALL:
		asm_event = ASM_EVENT_CALL;
		break;
	case MM_SESSION_TYPE_VIDEOCALL:
		asm_event = ASM_EVENT_VIDEOCALL;
		break;
	case MM_SESSION_TYPE_VOIP:
		asm_event = ASM_EVENT_VOIP;
		break;
	case MM_SESSION_TYPE_EMERGENCY:
		asm_event = ASM_EVENT_EMERGENCY;
		break;
	case MM_SESSION_TYPE_VOICE_RECOGNITION:
		asm_event = ASM_EVENT_VOICE_RECOGNITION;
		break;
	case MM_SESSION_TYPE_RECORD_AUDIO:
		asm_event = ASM_EVENT_MMCAMCORDER_AUDIO;
		break;
	case MM_SESSION_TYPE_RECORD_VIDEO:
		asm_event = ASM_EVENT_MMCAMCORDER_VIDEO;
		break;
	default:
		debug_error("Unexpected %d\n", session_type);
		return MM_ERROR_SOUND_INTERNAL;
	}

	*type = asm_event;
	*options = session_options;
	return MM_ERROR_NONE;
}
#if 0
static bool _check_skip_session_type(mm_sound_source_type_e type)
{
	bool ret = false;
	switch (type)
	{
	case SUPPORT_SOURCE_TYPE_DEFAULT:
	case SUPPORT_SOURCE_TYPE_VOICECONTROL:
		ret = false;
		break;
	case SUPPORT_SOURCE_TYPE_MIRRORING:
	case SUPPORT_SOURCE_TYPE_CALL_FORWARDING:
	case SUPPORT_SOURCE_TYPE_LOOPBACK:
		ret = true;
		break;
	default:
		debug_error("Unexpected %d\n", type);
		return false;
	}

	return ret;
}
#endif
static void __sound_pcm_send_message (mm_sound_pcm_async_t *pcmHandle, int message, int code)
{
	int ret = 0;
	if (pcmHandle->msg_cb) {
		MMMessageParamType msg;
		msg.union_type = MM_MSG_UNION_CODE;
		msg.code = code;

		debug_log ("calling msg callback(%p) with message(%d), code(%d), msg callback param(%p)\n",
				pcmHandle->msg_cb, message, msg.code, pcmHandle->msg_cb_param);
		ret = pcmHandle->msg_cb(message, &msg, pcmHandle->msg_cb_param);
		debug_log ("msg callback returned (%d)\n", ret);
	} else {
		debug_log ("No pcm msg callback\n");
	}
}

static ASM_cb_result_t sound_pcm_asm_callback(int handle, ASM_event_sources_t event_src, ASM_sound_commands_t command, unsigned int sound_status, void *cb_data)
{
	mm_sound_pcm_async_t *pcmHandle = (mm_sound_pcm_async_t *)cb_data;
	ASM_cb_result_t	cb_res = ASM_CB_RES_IGNORE;
	int message = MM_MESSAGE_SOUND_PCM_INTERRUPTED;

	/* Check input param */
	if(pcmHandle == NULL) {
		debug_error("sound_pcm_asm_callback cb_data is null\n");
		return cb_res;
	}

	debug_log ("command = %d, handle = %p, is_started = %d\n",command, pcmHandle, pcmHandle->is_started);
	switch(command)
	{
	case ASM_COMMAND_PAUSE:
	case ASM_COMMAND_STOP:
		/* Do stop */
		//MAINLOOP_LOCK(pcmHandle->mainloop);
		PCM_LOCK_INTERNAL(&pcmHandle->pcm_mutex_internal);
		_pcm_sound_stop_internal (pcmHandle);
		PCM_UNLOCK_INTERNAL(&pcmHandle->pcm_mutex_internal);
		//MAINLOOP_LOCK(pcmHandle->mainloop);
		cb_res = ASM_CB_RES_PAUSE;
		break;

	case ASM_COMMAND_RESUME:
		cb_res = ASM_CB_RES_IGNORE;
		message = MM_MESSAGE_READY_TO_RESUME;
		break;

	case ASM_COMMAND_PLAY:
	case ASM_COMMAND_NONE:
		debug_error ("Not an expected case!!!!\n");
		break;
	}

	/* execute user callback if callback available */
	__sound_pcm_send_message (pcmHandle, message, event_src);

	return cb_res;
}

EXPORT_API
int mm_sound_pcm_capture_open_async(MMSoundPcmHandle_t *handle, const unsigned int rate, MMSoundPcmChannel_t channel,
									MMSoundPcmFormat_t format, mm_sound_source_type_e source_type,
									mm_sound_pcm_stream_cb_t callback, void* userdata)
{
	mm_sound_pcm_async_t *pcmHandle = NULL;
	int size = 0;
	int result = MM_ERROR_NONE;
	int ret_mutex = 0;

	int volume_config = 0;
	int src_type = 0;
	pa_sample_spec ss;

	debug_warning ("enter : rate=[%d Hz], channel=[%x][%s], format=[%x][%s], source_type=[%x]\n",
				rate, channel, __get_channel_str(channel), format, __get_format_str(format), source_type);

	if (rate < _MIN_SYSTEM_SAMPLERATE || rate > _MAX_SYSTEM_SAMPLERATE) {
		debug_error("unsupported sample rate %u", rate);
		return MM_ERROR_SOUND_DEVICE_INVALID_SAMPLERATE;
	} else {
		ss.rate = rate;
	}

	switch(channel)
	{
	case MMSOUND_PCM_MONO:
		ss.channels = 1;
		break;
	case MMSOUND_PCM_STEREO:
		ss.channels = 2;
		break;

	default:
		debug_error("Unsupported channel type\n");
		return MM_ERROR_SOUND_DEVICE_INVALID_CHANNEL;
	}

	switch(format)
	{
	case MMSOUND_PCM_U8:
		ss.format = PA_SAMPLE_U8;
		break;
	case MMSOUND_PCM_S16_LE:
		ss.format = PA_SAMPLE_S16LE;
		break;
	default:
		debug_error("Unsupported format type\n");
		return MM_ERROR_SOUND_DEVICE_INVALID_FORMAT;
	}

	//pcmHandle = calloc(sizeof(mm_sound_pcm_async_t), 1);
	pcmHandle = malloc (sizeof(mm_sound_pcm_async_t));
	if(pcmHandle == NULL)
		return MM_ERROR_OUT_OF_MEMORY;

	memset (pcmHandle, 0, sizeof(mm_sound_pcm_async_t));

	ret_mutex = pthread_mutex_init(&pcmHandle->pcm_mutex_internal, NULL);
	if(ret_mutex != 0)
	{
		free(pcmHandle);
		return MM_ERROR_OUT_OF_MEMORY;
	}

	/* Register ASM */
	/* get session information */
	if(MM_ERROR_NONE != _get_asm_information(&pcmHandle->asm_event, &pcmHandle->asm_options)) {
		PCM_LOCK_DESTROY_INTERNAL(&pcmHandle->pcm_mutex_internal);
		free(pcmHandle);
		return MM_ERROR_POLICY_INTERNAL;
	}

	switch (source_type)
	{
	case SUPPORT_SOURCE_TYPE_VOICECONTROL:
		pcmHandle->asm_event = ASM_EVENT_EXCLUSIVE_RESOURCE;
		pcmHandle->resource = ASM_RESOURCE_VOICECONTROL;
		break;

	case SUPPORT_SOURCE_TYPE_VOICERECORDING:
		break;

	case SUPPORT_SOURCE_TYPE_MIRRORING:
	case SUPPORT_SOURCE_TYPE_SVR:
	/* Noise reduction is  already present in VoIP source so secrec is no more required. */
	case SUPPORT_SOURCE_TYPE_VOIP:
	default:
		/* Skip any specific asm setting or secrec */
		break;
	}

#if 0
	/* register asm */
	if(pcmHandle->asm_event != ASM_EVENT_CALL &&
		pcmHandle->asm_event != ASM_EVENT_VIDEOCALL &&
		pcmHandle->asm_event != ASM_EVENT_VOIP &&
		pcmHandle->asm_event != ASM_EVENT_VOICE_RECOGNITION &&
		pcmHandle->asm_event != ASM_EVENT_MMCAMCORDER_VIDEO &&
		_check_skip_session_type(source_type) == false) {
		if(!ASM_register_sound(-1, &pcmHandle->asm_handle, pcmHandle->asm_event,
				/* ASM_STATE_PLAYING */ ASM_STATE_NONE, sound_pcm_asm_callback, (void*)pcmHandle, pcmHandle->resource, &errorcode)) 	{
			debug_error("ASM_register_sound() failed 0x%x\n", errorcode);
			PCM_LOCK_DESTROY_INTERNAL(&pcmHandle->pcm_mutex_internal);
			free(pcmHandle);
			return MM_ERROR_POLICY_BLOCKED;
		}
		if(!ASM_set_session_option(pcmHandle->asm_handle, pcmHandle->asm_options, &errorcode)) {
			debug_error("ASM_set_session_option() failed 0x%x\n", errorcode);
		}
	} else {
		pcmHandle->skip_session = true;
	}
#else
	pcmHandle->skip_session = true; /* FIXME */
#endif

	/* For Video Call or VoIP select volume type VOLUME_TYPE_VOIP for sink/source */
	if( (pcmHandle->asm_event == ASM_EVENT_VIDEOCALL) || (pcmHandle->asm_event == ASM_EVENT_VOIP) )
		volume_config = VOLUME_TYPE_VOIP;
	else
		volume_config = VOLUME_TYPE_SYSTEM; //dose not effect at capture mode

	src_type = source_type;
	/* FIXME : remove after quality issue is fixed */

	mm_sound_pa_open(pcmHandle, 1, src_type, 0, volume_config, &ss, NULL, &size, callback, userdata);
	if(!pcmHandle->s) {
		debug_error("Device Open Error 0x%x\n", result);
		PCM_LOCK_DESTROY_INTERNAL(&pcmHandle->pcm_mutex_internal);
		free(pcmHandle);
		return MM_ERROR_SOUND_DEVICE_NOT_OPENED;
	}

	pcmHandle->is_playback = false;
	pcmHandle->rate = rate;
	pcmHandle->channel = channel;
	pcmHandle->format = format;
	pcmHandle->byte_per_sec = rate*(format==MMSOUND_PCM_U8?1:2)*(channel==MMSOUND_PCM_MONO?1:2);

	/* Set handle to return */
	*handle = (MMSoundPcmHandle_t)pcmHandle;

	debug_warning ("success : handle=[%p], handle->s=[%p], size=[%d]\n", handle, pcmHandle->s, size);

	return size;
}

static int _pcm_sound_start (MMSoundPcmHandle_t handle)
{
	mm_sound_pcm_async_t *pcmHandle = (mm_sound_pcm_async_t*)handle;
	int errorcode = 0;
	int ret = 0;

	debug_fenter();

	/* Check input param */
	if(pcmHandle == NULL) {
		debug_error ("Handle is null, return Invalid Argument\n");
		ret = MM_ERROR_INVALID_ARGUMENT;
		goto NULL_HANDLE;
	}

	PCM_LOCK_INTERNAL(&pcmHandle->pcm_mutex_internal);

	if (pcmHandle->skip_session == false) {
		/* ASM set state to PLAYING */
		if (!ASM_set_sound_state(pcmHandle->asm_handle, pcmHandle->asm_event, ASM_STATE_PLAYING, pcmHandle->resource, &errorcode)) {
			debug_error("ASM_set_sound_state(PLAYING) failed 0x%x\n", errorcode);
			ret = MM_ERROR_POLICY_BLOCKED;
			goto EXIT;
		}
	}

	/* Update State */
	pcmHandle->is_started = true;

	/* Un-Cork */
	ret = _mm_sound_pa_cork(pcmHandle, 0);
EXIT:
	PCM_UNLOCK_INTERNAL(&pcmHandle->pcm_mutex_internal);

NULL_HANDLE:
	debug_fleave();
	return ret;
}

EXPORT_API
int mm_sound_pcm_capture_start_async(MMSoundPcmHandle_t handle)
{
	int ret = MM_ERROR_NONE;

	debug_warning ("enter : handle=[%p]\n", handle);

	ret = _pcm_sound_start (handle);
	if (ret != MM_ERROR_NONE)  {
		debug_error ("_pcm_sound_start() failed (%x)\n", ret);
		goto EXIT;
	}

EXIT:
	debug_warning ("leave : handle=[%p], ret=[0x%X]", handle, ret);

	return ret;
}

static int _pcm_sound_stop_internal (MMSoundPcmHandle_t handle)
{
	mm_sound_pcm_async_t *pcmHandle = (mm_sound_pcm_async_t*)handle;

	/* Check input param */
	if(pcmHandle == NULL)
		return MM_ERROR_INVALID_ARGUMENT;

	/* Check State */
	if (pcmHandle->is_started == false) {
		debug_warning ("Can't stop because not started\n");
		return MM_ERROR_SOUND_INVALID_STATE;
	}

#if 0
	/* Drain if playback mode */
	if (pcmHandle->is_playback) {
		if(MM_ERROR_NONE != _mm_sound_pa_drain(pcmHandle)) {
			debug_error("drain failed\n");
		}
	}
#endif
	/* Update State */
	pcmHandle->is_started = false;

	/* Cork */
	return _mm_sound_pa_cork(pcmHandle, 1);
}

static int _pcm_sound_stop(MMSoundPcmHandle_t handle)
{
	mm_sound_pcm_async_t *pcmHandle = (mm_sound_pcm_async_t*)handle;
	int errorcode = 0;
	int ret = MM_ERROR_NONE;

	debug_fenter();

	/* Check input param */
	if(pcmHandle == NULL) {
		debug_error ("Handle is null, return Invalid Argument\n");
		ret = MM_ERROR_INVALID_ARGUMENT;
		goto NULL_HANDLE;
	}

//	MAINLOOP_LOCK(pcmHandle->mainloop);
	PCM_LOCK_INTERNAL(&pcmHandle->pcm_mutex_internal);

	/* Do stop procedure */
	ret = _pcm_sound_stop_internal(handle);
	if (ret == MM_ERROR_NONE) {
		/* Set ASM State to STOP */
		if (pcmHandle->skip_session == false) {
			if (!ASM_set_sound_state(pcmHandle->asm_handle, pcmHandle->asm_event, ASM_STATE_STOP, pcmHandle->resource, &errorcode)) {
				debug_error("ASM_set_sound_state(STOP) failed 0x%x\n", errorcode);
				ret = MM_ERROR_POLICY_BLOCKED;
				goto EXIT;
			}
		}
	}

EXIT:
	PCM_UNLOCK_INTERNAL(&pcmHandle->pcm_mutex_internal);
//	MAINLOOP_UNLOCK(pcmHandle->mainloop);
NULL_HANDLE:
	debug_fleave();
	return ret;
}

EXPORT_API
int mm_sound_pcm_capture_stop_async(MMSoundPcmHandle_t handle)
{
	int ret = 0;

	debug_warning ("enter : handle=[%p]\n", handle);
	ret = _pcm_sound_stop(handle);

	debug_warning ("leave : handle=[%p], ret=[0x%X]\n", handle, ret);

	return ret;
}

EXPORT_API
int mm_sound_pcm_capture_peek(MMSoundPcmHandle_t handle, const void **buffer, const unsigned int *length)
{
	int ret = 0;
	mm_sound_pcm_async_t *pcmHandle = (mm_sound_pcm_async_t*)handle;

	/* Check input param */
	if(pcmHandle == NULL) {
		debug_error ("Handle is null, return Invalid Argument\n");
		ret =  MM_ERROR_INVALID_ARGUMENT;
		goto NULL_HANDLE;
	}
//	PCM_LOCK_INTERNAL(&pcmHandle->pcm_mutex_internal);

	if(buffer == NULL) {
		debug_error("Invalid buffer pointer\n");
		ret = MM_ERROR_SOUND_INVALID_POINTER;
		goto EXIT;
	}
	if(length == 0 ) {
		debug_error ("length is 0, return 0\n");
		ret = 0;
		goto EXIT;
	}

#if 0 /* FIXME */
	/* Check State : return fail if not started */
	if (!pcmHandle->is_started) {
		/*  not started, return fail */
		debug_error ("Not started yet, return Invalid State \n");
		ret = MM_ERROR_SOUND_INVALID_STATE;
		goto EXIT;
	}
#endif
	ret = pa_stream_peek (pcmHandle->s, buffer, (unsigned int *)length);

EXIT:
//	PCM_UNLOCK_INTERNAL(&pcmHandle->pcm_mutex_internal);
NULL_HANDLE:
	return ret;
}

EXPORT_API
int mm_sound_pcm_capture_drop(MMSoundPcmHandle_t handle)
{
	int ret = 0;
	mm_sound_pcm_async_t *pcmHandle = (mm_sound_pcm_async_t*)handle;

	/* Check input param */
	if(pcmHandle == NULL) {
		debug_error ("Handle is null, return Invalid Argument\n");
		ret =  MM_ERROR_INVALID_ARGUMENT;
		goto NULL_HANDLE;
	}
//	PCM_LOCK_INTERNAL(&pcmHandle->pcm_mutex_internal);

	/* Check State : return fail if not started */
	if (!pcmHandle->is_started) {
		/*  not started, return fail */
		debug_error ("Not started yet, return Invalid State \n");
		ret = MM_ERROR_SOUND_INVALID_STATE;
		goto EXIT;
	}

	if (pa_stream_drop(pcmHandle->s) != 0)
		ret = MM_ERROR_SOUND_INTERNAL;
EXIT:
//	PCM_UNLOCK_INTERNAL(&pcmHandle->pcm_mutex_internal);
NULL_HANDLE:
	return ret;
}


EXPORT_API
int mm_sound_pcm_capture_close_async(MMSoundPcmHandle_t handle)
{
	int result = MM_ERROR_NONE;
	mm_sound_pcm_async_t *pcmHandle = (mm_sound_pcm_async_t*)handle;
	int errorcode = 0;

	debug_warning ("enter : handle=[%p]\n", handle);

	/* Check input param */
	if(pcmHandle == NULL) {
		debug_error ("Handle is null, return Invalid Argument\n");
		result = MM_ERROR_INVALID_ARGUMENT;
		goto NULL_HDL;
	}
	PCM_LOCK_INTERNAL(&pcmHandle->pcm_mutex_internal);
	/* Close */


	result = _mm_sound_pa_close(pcmHandle);
	if (result != 0) {
		debug_error("pa_stream_disconnect failed 0x%X", result);
		result = MM_ERROR_SOUND_INTERNAL;
		goto EXIT;
	}

	/* Unregister ASM */
	if(pcmHandle->asm_event != ASM_EVENT_CALL &&
		pcmHandle->asm_event != ASM_EVENT_VIDEOCALL &&
		pcmHandle->asm_event != ASM_EVENT_VOIP &&
		pcmHandle->asm_event != ASM_EVENT_VOICE_RECOGNITION) {
		if (pcmHandle->skip_session == false && pcmHandle->asm_handle) {
			if(!ASM_unregister_sound(pcmHandle->asm_handle, pcmHandle->asm_event, &errorcode)) {
				debug_error("ASM_unregister failed with 0x%x\n", errorcode);
				result = MM_ERROR_SOUND_INTERNAL;
				goto EXIT;
			}
		}
	}

EXIT:
	PCM_UNLOCK_INTERNAL(&pcmHandle->pcm_mutex_internal);
NULL_HDL:
	debug_warning ("leave : handle=[%p], ret=[0x%X]\n", handle, result);
	/* Free handle */
	if (pcmHandle) {
		PCM_LOCK_DESTROY_INTERNAL(&pcmHandle->pcm_mutex_internal);
		free(pcmHandle);
		pcmHandle = NULL;
	}

	return result;
}

EXPORT_API
int mm_sound_pcm_set_message_callback_async (MMSoundPcmHandle_t handle, MMMessageCallback callback, void *user_param)
{
	mm_sound_pcm_async_t *pcmHandle =  (mm_sound_pcm_async_t*)handle;

	if(pcmHandle == NULL || callback == NULL)
		return MM_ERROR_INVALID_ARGUMENT;

	pcmHandle->msg_cb = callback;
	pcmHandle->msg_cb_param = user_param;

	debug_log ("set pcm message callback (%p,%p)\n", callback, user_param);

	return MM_ERROR_NONE;
}

EXPORT_API
int mm_sound_pcm_play_open_async (MMSoundPcmHandle_t *handle, const unsigned int rate, MMSoundPcmChannel_t channel,
							MMSoundPcmFormat_t format, int volume_config,
							mm_sound_pcm_stream_cb_t callback, void* userdata)
{
	mm_sound_pcm_async_t *pcmHandle = NULL;
	int size = 0;
	int result = MM_ERROR_NONE;
	int errorcode = 0;
	int volume_type = MM_SOUND_VOLUME_CONFIG_TYPE(volume_config);
	int ret_mutex = 0;

	pa_sample_spec ss;

	debug_warning ("enter : rate=[%d], channel=[%x][%s], format=[%x][%s], volconf=[%d]",
			rate, channel, __get_channel_str(channel), format, __get_format_str(format), volume_config);

	/* Check input param */
	if (volume_type < 0 || volume_type >= VOLUME_TYPE_MAX) {
		debug_error("Volume type is invalid %d\n", volume_type);
		return MM_ERROR_INVALID_ARGUMENT;
	}
	if (rate < _MIN_SYSTEM_SAMPLERATE || rate > _MAX_SYSTEM_SAMPLERATE) {
		debug_error("unsupported sample rate %u", rate);
		return MM_ERROR_SOUND_DEVICE_INVALID_SAMPLERATE;
	} else {
		ss.rate = rate;
	}

	switch(channel)
	{
	case MMSOUND_PCM_MONO:
		ss.channels = 1;
		break;
	case MMSOUND_PCM_STEREO:
		ss.channels = 2;
		break;
	default:
		debug_error("Unsupported channel type\n");
		return MM_ERROR_SOUND_DEVICE_INVALID_CHANNEL;
	}

	switch(format)
	{
	case MMSOUND_PCM_U8:
		ss.format = PA_SAMPLE_U8;
		break;
	case MMSOUND_PCM_S16_LE:
		ss.format = PA_SAMPLE_S16LE;
		break;
	default:
		debug_error("Unsupported format type\n");
		return MM_ERROR_SOUND_DEVICE_INVALID_FORMAT;
	}

	pcmHandle = calloc(sizeof(mm_sound_pcm_async_t),1);
	if(pcmHandle == NULL)
		return MM_ERROR_OUT_OF_MEMORY;

	ret_mutex = pthread_mutex_init(&pcmHandle->pcm_mutex_internal, NULL);
	if(ret_mutex != 0) {
		debug_error ("error mutex init....%d");
		result = MM_ERROR_OUT_OF_MEMORY;
		goto ERROR;
	}

	/* Register ASM */
	debug_log ("session start");

	/* get session information */
	if(MM_ERROR_NONE != _get_asm_information(&pcmHandle->asm_event, &pcmHandle->asm_options)) {
		debug_error ("_get_asm_information failed....\n");
		result = MM_ERROR_POLICY_INTERNAL;
		goto ERROR;
	}

	// should be fixed. call forwarding engine(voip) use call volume type.
	if(volume_type == VOLUME_TYPE_CALL) {
		pcmHandle->skip_session = true;
	}

	pcmHandle->skip_session = true;

	if(pcmHandle->asm_event != ASM_EVENT_CALL &&
		pcmHandle->asm_event != ASM_EVENT_VIDEOCALL &&
		pcmHandle->asm_event != ASM_EVENT_VOIP &&
		pcmHandle->asm_event != ASM_EVENT_VOICE_RECOGNITION &&
		pcmHandle->skip_session == false) {

		/* register asm */
		if(!ASM_register_sound(-1, &pcmHandle->asm_handle, pcmHandle->asm_event,
				ASM_STATE_NONE, sound_pcm_asm_callback, (void*)pcmHandle, pcmHandle->resource, &errorcode)) {
			debug_error("ASM_register_sound() failed 0x%x\n", errorcode);
			result = MM_ERROR_POLICY_BLOCKED;
			goto ERROR;
		}
		if(!ASM_set_session_option(pcmHandle->asm_handle, pcmHandle->asm_options, &errorcode)) {
			debug_error("ASM_set_session_option() failed 0x%x\n", errorcode);
		}
	} else {
		pcmHandle->skip_session = true;
	}

	pcmHandle->is_playback = true;
	pcmHandle->rate = rate;
	pcmHandle->channel = channel;
	pcmHandle->format = format;
	pcmHandle->byte_per_sec = rate*(format==MMSOUND_PCM_U8?1:2)*(channel==MMSOUND_PCM_MONO?1:2);

	/* Set handle to return */
	*handle = (MMSoundPcmHandle_t)pcmHandle;

	/* Open */
	mm_sound_pa_open(pcmHandle, 0, 0, 0,
			volume_config, &ss, NULL, &size, callback, userdata);
	if(!pcmHandle->s) {
		debug_error("Device Open Error 0x%x\n", result);
		result = MM_ERROR_SOUND_DEVICE_NOT_OPENED;
		goto ERROR;
	}

	debug_warning ("success : handle=[%p], handle->s=[%p], size=[%d]\n", handle, pcmHandle->s, size);
	return size;

ERROR:
	if (pcmHandle) {
		if(ret_mutex == 0) {
			PCM_LOCK_DESTROY_INTERNAL(&pcmHandle->pcm_mutex_internal);
		}
		free(pcmHandle);
	}
	return result;



}

EXPORT_API
int mm_sound_pcm_play_start_async(MMSoundPcmHandle_t handle)
{
	int ret = 0;

	debug_warning ("enter : handle=[%p]\n", handle);
	ret = _pcm_sound_start (handle);
	debug_warning ("leave : handle=[%p], ret=[0x%X]\n", handle, ret);

	return ret;
}

EXPORT_API
int mm_sound_pcm_play_stop_async(MMSoundPcmHandle_t handle)
{
	int ret = 0;

	debug_warning ("enter : handle=[%p]\n", handle);
	ret = _pcm_sound_stop(handle);
	debug_warning ("leave : handle=[%p], ret=[0x%X]\n", handle, ret);

	return ret;
}

EXPORT_API
int mm_sound_pcm_play_write_async(MMSoundPcmHandle_t handle, void* ptr, unsigned int length_byte)
{
	int ret = 0;
	static int written_byte = 0;
	mm_sound_pcm_async_t *pcmHandle = (mm_sound_pcm_async_t*)handle;
	int vr_state = 0;

	/* Check input param */
	if(pcmHandle == NULL) {
		debug_error ("Handle is null, return Invalid Argument\n");
		ret = MM_ERROR_INVALID_ARGUMENT;
		goto NULL_HANDLE;
	}

//	PCM_LOCK_INTERNAL(&pcmHandle->pcm_mutex_internal);

	if(ptr == NULL) {
		debug_error("Invalid buffer pointer\n");
		ret = MM_ERROR_SOUND_INVALID_POINTER;
		goto EXIT;
	}
	if(length_byte == 0 ) {
		debug_error ("length is 0, return 0\n");
		ret = 0;
		goto EXIT;
	}

#if 0
	/* Check State : return fail if not started */
	if (!pcmHandle->is_started) {
		/* not started, return fail */
		debug_error ("Not started yet, return Invalid State \n");
		ret = MM_ERROR_SOUND_INVALID_STATE;
		goto EXIT;
	}
#endif
	/* Write */

	/*  Check whether voicerecorder is running */
	vconf_get_int(VCONFKEY_VOICERECORDER_STATE, &vr_state);

	if (vr_state == VCONFKEY_VOICERECORDER_RECORDING) {
		debug_log ("During VoiceRecording....MUTE!!!");
	} else {
		ret = pa_stream_write(pcmHandle->s, ptr, length_byte, NULL, 0LL, PA_SEEK_RELATIVE);
		if (ret == 0) {
			ret = length_byte;
		}
	}

EXIT:
//	PCM_UNLOCK_INTERNAL(&pcmHandle->pcm_mutex_internal);
NULL_HANDLE:
	written_byte += length_byte;
	if(ret > 0 && written_byte>pcmHandle->byte_per_sec*RW_LOG_PERIOD){
		debug_log ("(%d)/write-once, (%d)/%dsec bytes written\n", length_byte, written_byte, RW_LOG_PERIOD);
		written_byte = 0;
	}

	return ret;
}

EXPORT_API
int mm_sound_pcm_play_close_async(MMSoundPcmHandle_t handle)
{
	int result = MM_ERROR_NONE;
	mm_sound_pcm_async_t *pcmHandle = (mm_sound_pcm_async_t*)handle;
	int errorcode = 0;

	debug_warning ("enter : handle=[%p]\n", handle);

	/* Check input param */
	if(pcmHandle == NULL) {
		debug_error ("Handle is null, return Invalid Argument\n");
		result = MM_ERROR_INVALID_ARGUMENT;
		goto NULL_HANDLE;
	}
	PCM_LOCK_INTERNAL(&pcmHandle->pcm_mutex_internal);
	/* Drain if needed */
	if (pcmHandle->is_started) {
		/* stop() is not called before close(), drain is needed */
		if(MM_ERROR_NONE != _mm_sound_pa_drain(pcmHandle)) {
			debug_error("drain failed\n");
			result = MM_ERROR_SOUND_INTERNAL;
			goto EXIT;
		}
	}
	pcmHandle->is_started = false;
	/* Close */
	result = _mm_sound_pa_close(pcmHandle);
	if (result != 0) {
		debug_error("pa_stream_disconnect failed 0x%X", result);
		result = MM_ERROR_SOUND_INTERNAL;
		goto EXIT;
	}

	if (pcmHandle->skip_session == false) {
		/* Unregister ASM */
		if(pcmHandle->asm_event != ASM_EVENT_CALL &&
			pcmHandle->asm_event != ASM_EVENT_VIDEOCALL &&
			pcmHandle->asm_event != ASM_EVENT_VOIP &&
			pcmHandle->asm_event != ASM_EVENT_VOICE_RECOGNITION) {
			if(!ASM_unregister_sound(pcmHandle->asm_handle, pcmHandle->asm_event, &errorcode)) {
				debug_error("ASM_unregister failed with 0x%x\n", errorcode);
				result = MM_ERROR_SOUND_INTERNAL;
				goto EXIT;
			}
		}
	}

EXIT:
	PCM_UNLOCK_INTERNAL(&pcmHandle->pcm_mutex_internal);
NULL_HANDLE:
	debug_warning ("leave : handle=[%p], result[0x%X]\n", handle, result);
	if (pcmHandle) {
		/* Free handle */
		PCM_LOCK_DESTROY_INTERNAL(&pcmHandle->pcm_mutex_internal);
		free(pcmHandle);
		pcmHandle= NULL;
	}

	return result;
}

EXPORT_API
int mm_sound_pcm_get_latency_async(MMSoundPcmHandle_t handle, int *latency)
{
	mm_sound_pcm_async_t *pcmHandle = (mm_sound_pcm_async_t*)handle;
	int mlatency = 0;

	/* Check input param */
	if (latency == NULL)
		return MM_ERROR_INVALID_ARGUMENT;

	if(MM_ERROR_NONE != _mm_sound_pa_get_latency(pcmHandle, &mlatency)) {
		debug_error("Get Latency Error");
		return MM_ERROR_SOUND_DEVICE_NOT_OPENED;
	}

	*latency = mlatency;

	return MM_ERROR_NONE;
}

EXPORT_API
int mm_sound_pcm_is_started_async(MMSoundPcmHandle_t handle, bool *is_started)
{
	mm_sound_pcm_async_t *pcmHandle = (mm_sound_pcm_async_t*)handle;

	/* Check input param */
	if (is_started == NULL)
		return MM_ERROR_INVALID_ARGUMENT;

	*is_started = pcmHandle->is_started;

	return MM_ERROR_NONE;
}

/* --------------------------------- pa client ----------------------------------*/

#define IS_INPUT_HANDLE(x) \
    if( x == HANDLE_MODE_INPUT || x == HANDLE_MODE_INPUT_HIGH_LATENCY || \
        x == HANDLE_MODE_INPUT_LOW_LATENCY || x == HANDLE_MODE_INPUT_AP_CALL )

#define MEDIA_POLICY_AUTO                   "auto"
#define MEDIA_POLICY_PHONE                  "phone"
#define MEDIA_POLICY_ALL                    "all"
#define MEDIA_POLICY_VOIP                   "voip"
#define MEDIA_POLICY_MIRRORING              "mirroring"
#define MEDIA_POLICY_HIGH_LATENCY           "high-latency"

enum mm_sound_handle_mode_t {
	HANDLE_MODE_OUTPUT,				/**< Output mode of handle */
	HANDLE_MODE_OUTPUT_CLOCK,			/**< Output mode of gst audio only mode */
	HANDLE_MODE_OUTPUT_VIDEO,			/**< Output mode of gst video mode */
	HANDLE_MODE_OUTPUT_LOW_LATENCY,	/**< Output mode for low latency play mode. typically for game */
	HANDLE_MODE_INPUT,					/**< Input mode of handle */
	HANDLE_MODE_INPUT_HIGH_LATENCY,	/**< Input mode for high latency capture mode. */
	HANDLE_MODE_INPUT_LOW_LATENCY,		/**< Input mode for low latency capture mode. typically for VoIP */
	HANDLE_MODE_CALL_OUT,				/**< for voice call establish */
	HANDLE_MODE_CALL_IN,				/**< for voice call establish */
	HANDLE_MODE_OUTPUT_AP_CALL,		/**< for VT call on thin modem */
	HANDLE_MODE_INPUT_AP_CALL,			/**< for VT call on thin modem */
	HANDLE_MODE_NUM,					/**< Number of mode */
};

static void __mm_sound_pa_success_cb(pa_context *c, int success, void *userdata)
{
	mm_sound_pcm_async_t* handle = (mm_sound_pcm_async_t*) userdata;

	if (!success) {
		debug_error("pa control failed: %s\n", pa_strerror(pa_context_errno(c)));
	} else {
		debug_msg("pa control success\n");
	}
    handle->operation_success = success;
	pa_threaded_mainloop_signal(handle->mainloop, 0);
}

static void _context_state_cb(pa_context *c, void *userdata)
{
	pa_context_state_t state;
	mm_sound_pcm_async_t* handle = (mm_sound_pcm_async_t*) userdata;
    assert(c);

    state = pa_context_get_state(c);
    debug_msg ("[%p] context state = [%d]", handle, state);
    switch (state) {
        case PA_CONTEXT_READY:
        case PA_CONTEXT_TERMINATED:
        case PA_CONTEXT_FAILED:
            debug_msg ("signaling mainloop!!!!");
            pa_threaded_mainloop_signal(handle->mainloop, 0);
            break;

        case PA_CONTEXT_UNCONNECTED:
        case PA_CONTEXT_CONNECTING:
        case PA_CONTEXT_AUTHORIZING:
        case PA_CONTEXT_SETTING_NAME:
            break;
    }
}

static void _stream_state_cb(pa_stream *s, void * userdata)
{
	pa_stream_state_t state;
	mm_sound_pcm_async_t* handle = (mm_sound_pcm_async_t*) userdata;
	assert(s);

	state = pa_stream_get_state(s);
	debug_msg ("[%p] stream [%d] state = [%d]", handle, pa_stream_get_index(s), state);

    switch (state) {
        case PA_STREAM_READY:
        case PA_STREAM_FAILED:
        case PA_STREAM_TERMINATED:
            debug_msg ("signaling mainloop!!!!");
            pa_threaded_mainloop_signal(handle->mainloop, 0);
            break;

        case PA_STREAM_UNCONNECTED:
        case PA_STREAM_CREATING:
            break;
    }
}


static int _pa_stream_create (mm_sound_pcm_async_t* handle, int is_capture, pa_sample_spec* ss,
		pa_channel_map* channel_map, pa_proplist *proplist, mm_sound_pcm_stream_cb_t callback, void* userdata)
{
	pa_buffer_attr attr;
    pa_stream_flags_t flags = PA_STREAM_AUTO_TIMING_UPDATE | PA_STREAM_START_CORKED;// | PA_STREAM_ADJUST_LATENCY;

    int error = PA_OK, r;
    int latency = 0;

    memset (&attr, 0, sizeof(pa_buffer_attr));

    if (!is_capture) {
        latency = 100000;
    }

	attr.maxlength = (uint32_t) -1; /* common */
	attr.tlength = latency > 0 ? (uint32_t) pa_usec_to_bytes(latency, ss) : (uint32_t) -1;  /* for playback only */
	attr.prebuf = (uint32_t) 0; /* for playback only */
	attr.minreq = (uint32_t) -1;  /* for playback only */
	attr.fragsize = (uint32_t) 512; /* for recording only */


	/* PULSE STREAM CREATION  */
    if (!(handle->mainloop = pa_threaded_mainloop_new()))
        goto fail;

    if (!(handle->context = pa_context_new(pa_threaded_mainloop_get_api(handle->mainloop), NULL)))
        goto fail;

    pa_context_set_state_callback(handle->context, _context_state_cb, handle);

    if (pa_context_connect(handle->context, NULL, 0, NULL) < 0) {
        error = pa_context_errno(handle->context);
        goto fail;
    }

    MAINLOOP_LOCK(handle->mainloop);

    if (pa_threaded_mainloop_start(handle->mainloop) < 0)
        goto unlock_and_fail;

    for (;;) {
        pa_context_state_t state;

        state = pa_context_get_state(handle->context);

        if (state == PA_CONTEXT_READY)
            break;

        if (!PA_CONTEXT_IS_GOOD(state)) {
            error = pa_context_errno(handle->context);
            goto unlock_and_fail;
        }

        /* Wait until the context is ready */
        pa_threaded_mainloop_wait(handle->mainloop);
    }


    if (is_capture) {
       handle->s = pa_stream_new_with_proplist(handle->context, "capture", ss, channel_map, proplist);
       pa_stream_set_state_callback(handle->s, _stream_state_cb, handle);
       pa_stream_set_read_callback(handle->s, (pa_stream_request_cb_t)callback, userdata);
    } else {
       handle->s = pa_stream_new_with_proplist(handle->context, "playback", ss, channel_map, proplist);
       pa_stream_set_state_callback(handle->s, _stream_state_cb, handle);
       pa_stream_set_write_callback(handle->s, (pa_stream_request_cb_t)callback, userdata);
    }
//  pa_stream_set_latency_update_callback(p->stream, stream_latency_update_cb, p);

    if (is_capture) {
        r = pa_stream_connect_record(handle->s, NULL, &attr, flags);
    } else {
        r = pa_stream_connect_playback(handle->s, NULL, &attr, flags, NULL, NULL);
    }

    if (r < 0) {
        error = pa_context_errno(handle->context);
        goto unlock_and_fail;
    }

    for (;;) {
        pa_stream_state_t state;

        state = pa_stream_get_state(handle->s);

        if (state == PA_STREAM_READY)
            break;

        if (!PA_STREAM_IS_GOOD(state)) {
               error = pa_context_errno(handle->context);
               assert(0);
        }

        /* Wait until the stream is ready */
        pa_threaded_mainloop_wait(handle->mainloop);
    }

    MAINLOOP_UNLOCK(handle->mainloop);

    return error;

unlock_and_fail:
	debug_error ("error!!!!");
	MAINLOOP_UNLOCK(handle->mainloop);

fail:
//    if (rerror)
//        *rerror = error;

    return error;
}

static const pa_tizen_volume_type_t mm_sound_volume_type_to_pa[VOLUME_TYPE_MAX] = {
	[VOLUME_TYPE_SYSTEM] = PA_TIZEN_VOLUME_TYPE_SYSTEM,
	[VOLUME_TYPE_NOTIFICATION] = PA_TIZEN_VOLUME_TYPE_NOTIFICATION,
	[VOLUME_TYPE_ALARM] = PA_TIZEN_VOLUME_TYPE_ALARM,
	[VOLUME_TYPE_RINGTONE] = PA_TIZEN_VOLUME_TYPE_RINGTONE,
	[VOLUME_TYPE_MEDIA] = PA_TIZEN_VOLUME_TYPE_MEDIA,
	[VOLUME_TYPE_CALL] = PA_TIZEN_VOLUME_TYPE_CALL,
	[VOLUME_TYPE_VOIP] = PA_TIZEN_VOLUME_TYPE_VOIP,
	[VOLUME_TYPE_FIXED] = PA_TIZEN_VOLUME_TYPE_FIXED,
//	[VOLUME_TYPE_EXT_SYSTEM_JAVA] = PA_TIZEN_VOLUME_TYPE_EXT_JAVA,
};

static int mm_sound_pa_open(mm_sound_pcm_async_t* handle, int is_capture, int policy, int priority, int volume_config, pa_sample_spec* ss,
		              pa_channel_map* channel_map, int* size, mm_sound_pcm_stream_cb_t callback, void* userdata)
{
    int vol_conf_type;
    int prop_vol_type, prop_gain_type;
    int err = MM_ERROR_NONE;
    const char *prop_policy = NULL;
    pa_proplist *proplist = NULL;
    pa_channel_map maps;

    /* ---------- prepare ChannelMap ------------ */
    if(channel_map == NULL) {
        pa_channel_map_init_auto(&maps, ss->channels, PA_CHANNEL_MAP_ALSA);
        channel_map = &maps;
    }

    /* ---------- prepare PROPLIST ------------ */
    proplist = pa_proplist_new();

    /* Set volume type of stream */
    vol_conf_type = volume_config & 0x000000FF;
    prop_vol_type = mm_sound_volume_type_to_pa[vol_conf_type];
    pa_proplist_setf(proplist, PA_PROP_MEDIA_TIZEN_VOLUME_TYPE, "%d", prop_vol_type);

    /* Set gain type of stream */
    prop_gain_type = (volume_config >> 8) & 0x000000FF;
    pa_proplist_setf(proplist, PA_PROP_MEDIA_TIZEN_GAIN_TYPE, "%d", prop_gain_type);

    /* If not set any yet, set based on volume type */
    if (prop_policy == NULL) {
        /* check stream type (vol type) */
        switch (vol_conf_type)
        {
        case VOLUME_TYPE_NOTIFICATION:
        case VOLUME_TYPE_ALARM:
            prop_policy = MEDIA_POLICY_ALL;
            break;

        case VOLUME_TYPE_MEDIA:
            prop_policy = MEDIA_POLICY_AUTO;
            break;

        case VOLUME_TYPE_CALL:
        case VOLUME_TYPE_RINGTONE:
        case VOLUME_TYPE_FIXED: /* Used for Emergency */
            prop_policy = MEDIA_POLICY_PHONE;
            break;

        case VOLUME_TYPE_VOIP:
            prop_policy = MEDIA_POLICY_VOIP;
            break;

        default:
            prop_policy = MEDIA_POLICY_AUTO;
            break;
        }
    }
    pa_proplist_sets(proplist, PA_PROP_MEDIA_POLICY, prop_policy);

    if (priority) {
        debug_msg("Set HIGH priority [%d]", priority);
        pa_proplist_sets(proplist, PA_PROP_MEDIA_ROLE, "solo");
    }

    /* ---------- create Stream ------------ */
    _pa_stream_create (handle, is_capture, ss, channel_map, proplist, callback, userdata);
    if (!handle->s) {
        debug_error("Open pulseaudio handle has failed - %s\n", pa_strerror(err));
        if (!strncmp(pa_strerror(err), "Access denied by security check",strlen(pa_strerror(err)))) {
            err = MM_ERROR_SOUND_PERMISSION_DENIED;
        } else {
            err = MM_ERROR_SOUND_INTERNAL;
        }
        goto fail;
    }

    //mm_sound_pa_set_mute(handle->handle, prop_vol_type, handle_inout, 0);

fail:
    if (proplist)
        pa_proplist_free(proplist);

    return err;
}

static int _mm_sound_pa_close(mm_sound_pcm_async_t* handle)
{
    //int err = MM_ERROR_NONE;

    if (handle->mainloop)
        pa_threaded_mainloop_stop(handle->mainloop);

    if (handle->s) {
        pa_stream_disconnect(handle->s);
        pa_stream_unref(handle->s);
    }

    if (handle->context) {
        pa_context_disconnect(handle->context);
        pa_context_unref(handle->context);
    }

    if (handle->mainloop)
        pa_threaded_mainloop_free(handle->mainloop);

    return 0;
}

static int _mm_sound_pa_cork(mm_sound_pcm_async_t* handle, int cork)
{
    pa_operation *o = NULL;

    MAINLOOP_LOCK(handle->mainloop);

    o = pa_stream_cork(handle->s, cork, (pa_stream_success_cb_t)__mm_sound_pa_success_cb, handle);
    if (!(o)) {
        goto unlock_and_fail;
	}
    handle->operation_success = 0;
    while (pa_operation_get_state(o) == PA_OPERATION_RUNNING) {
        pa_threaded_mainloop_wait(handle->mainloop);
    }
    if (!(handle->operation_success)) {
        goto unlock_and_fail;
    }
    pa_operation_unref(o);

    MAINLOOP_UNLOCK(handle->mainloop);

    return 0;

unlock_and_fail:
	debug_error ("error!!!!");
    if (o) {
        pa_operation_cancel(o);
        pa_operation_unref(o);
    }

    MAINLOOP_UNLOCK(handle->mainloop);
    return -1;
}

static int _mm_sound_pa_drain(mm_sound_pcm_async_t* handle)
{
    pa_operation *o = NULL;

    MAINLOOP_LOCK(handle->mainloop);

    o = pa_stream_drain(handle->s, (pa_stream_success_cb_t)__mm_sound_pa_success_cb, handle);
    if (!(o)) {
        goto unlock_and_fail;
    }
    handle->operation_success = 0;
    while (pa_operation_get_state(o) == PA_OPERATION_RUNNING) {
        pa_threaded_mainloop_wait(handle->mainloop);
    }
    if (!(handle->operation_success)) {
        goto unlock_and_fail;
    }
    pa_operation_unref(o);

    MAINLOOP_UNLOCK(handle->mainloop);

    return 0;

unlock_and_fail:
	debug_error ("error!!!!");
    if (o) {
        pa_operation_cancel(o);
        pa_operation_unref(o);
    }

    MAINLOOP_UNLOCK(handle->mainloop);
    return -1;
}


static int _mm_sound_pa_get_latency(mm_sound_pcm_async_t* handle, int* latency)
{
    int err = MM_ERROR_NONE;
    pa_usec_t latency_time = 0;

#if 0 /* FIXME */
    /* code from pa_simple_get_final_latency() */
    if (phandle->s->direction == PA_STREAM_PLAYBACK) {
        latency_time = (pa_bytes_to_usec(phandle->s->buffer_attr.tlength, &phandle->s->sample_spec) + phandle->s->timing_info.configured_sink_usec);
    } else if (phandle->s->direction == PA_STREAM_RECORD) {
        latency_time = (pa_bytes_to_usec(phandle->s->buffer_attr.fragsize, &phandle->s->sample_spec) + phandle->s->timing_info.configured_source_usec);
    }
#endif

    *latency = latency_time / 1000; // usec to msec

    return err;
}

