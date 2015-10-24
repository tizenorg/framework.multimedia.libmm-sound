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
#include "include/mm_sound_pa_client.h"

#include <audio-session-manager.h>
#include <mm_session.h>
#include <mm_session_private.h>


#define _MIN_SYSTEM_SAMPLERATE	8000
#define _MAX_SYSTEM_SAMPLERATE	48000
#define RW_LOG_PERIOD 5 /* period(second) for print log in capture read or play write*/

#define PCM_LOCK_INTERNAL(LOCK) do { pthread_mutex_lock(LOCK); } while (0)
#define PCM_UNLOCK_INTERNAL(LOCK) do { pthread_mutex_unlock(LOCK); } while (0)
#define PCM_LOCK_DESTROY_INTERNAL(LOCK) do { pthread_mutex_destroy(LOCK); } while (0)

int g_capture_h_count = 0;
#define PCM_CAPTURE_H_COUNT_INC() do { g_capture_h_count++; } while (0)
#define PCM_CAPTURE_H_COUNT_DEC() do { g_capture_h_count--; if(g_capture_h_count < 0) debug_error ("g_capture_h_count[%d] is not valid, check application side for proper handle usage\n", g_capture_h_count); } while (0)
#define PCM_CAPTURE_H_COUNT_GET(x_val) do { x_val = g_capture_h_count; } while (0)

typedef enum {
	MMSOUND_SESSION_TYPE_PLAYBACK,
	MMSOUND_SESSION_TYPE_CAPTURE,
}MMSound_session_type_e;

enum {
	MMSOUND_SESSION_REGISTERED_INTERNALLY,
	MMSOUND_SESSION_REGISTERED_BY_OUTSIDE_MEDIA,
};

typedef struct {
	int			handle;
	int			asm_handle;
	ASM_sound_events_t	asm_event;
	int			asm_options;
	int			session_registered_type;

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

	int volume_config;

} mm_sound_pcm_t;

static int _pcm_sound_start (MMSoundPcmHandle_t handle);
static int _pcm_sound_stop_internal (MMSoundPcmHandle_t handle);
static int _pcm_sound_stop(MMSoundPcmHandle_t handle);
static void __sound_pcm_send_message (mm_sound_pcm_t *pcmHandle, int message, int code);
static int _pcm_sound_ignore_session (MMSoundPcmHandle_t handle, MMSound_session_type_e type);

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

static int _get_asm_information(MMSound_session_type_e session_type, ASM_sound_events_t *type, int *options, int *session_registered_type)
{
	int cur_session = MM_SESSION_TYPE_MEDIA;
	int session_options = 0;
	int ret = MM_ERROR_NONE;
	ASM_sound_events_t asm_event;

	if(type == NULL)
		return MM_ERROR_SOUND_INVALID_POINTER;

	/* read session information */
	if(_mm_session_util_read_information(-1, &cur_session, &session_options) < 0) {
		debug_log("Read Session Information failed. Set default \"Media\" type\n");
		if (session_type == MMSOUND_SESSION_TYPE_PLAYBACK) {
			cur_session = MM_SESSION_TYPE_MEDIA;
		} else if (session_type == MMSOUND_SESSION_TYPE_CAPTURE) {
			cur_session = MM_SESSION_TYPE_MEDIA_RECORD;
		}
		ret = _mm_session_util_write_type(-1, cur_session);
		if (ret) {
			debug_error("_mm_session_util_write_type() failed\n");
			return MM_ERROR_SOUND_INTERNAL;
		}
		*session_registered_type = MMSOUND_SESSION_REGISTERED_INTERNALLY;
	} else {
		/* session was already registered */
		if (session_type == MMSOUND_SESSION_TYPE_CAPTURE) {
			if (cur_session > MM_SESSION_TYPE_MEDIA && cur_session < MM_SESSION_TYPE_CALL) {
				debug_error("current session type(%d) does not support capture\n", session_type);
				return MM_ERROR_POLICY_BLOCKED;
			} else if (cur_session == MM_SESSION_TYPE_MEDIA) {
				debug_log("session was already registered to MEDIA, update it to MEDIA_RECORD");
				/* update session information */
				ret = _mm_session_util_write_information(-1, MM_SESSION_TYPE_MEDIA_RECORD, session_options);
				if (ret) {
					debug_error("_mm_session_util_write_type() failed\n");
					return MM_ERROR_SOUND_INTERNAL;
				}
				*session_registered_type = MMSOUND_SESSION_REGISTERED_BY_OUTSIDE_MEDIA;
			}
		}
	}

	/* convert MM_SESSION_TYPE to ASM_EVENT_TYPE */
	switch (cur_session)
	{
	case MM_SESSION_TYPE_MEDIA:
	case MM_SESSION_TYPE_MEDIA_RECORD:
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
		debug_error("Unexpected %d\n", cur_session);
		return MM_ERROR_SOUND_INTERNAL;
	}

	*type = asm_event;
	*options = session_options;
	return MM_ERROR_NONE;
}

static bool _check_skip_session_type_for_capture(mm_sound_pcm_t *pcmHandle, mm_sound_source_type_e type)
{
	bool ret = false;
	int session_result = MM_ERROR_NONE;
	switch (type)
	{
	case SUPPORT_SOURCE_TYPE_DEFAULT:
	case SUPPORT_SOURCE_TYPE_VOICECONTROL:
		ret = false;
		break;
	case SUPPORT_SOURCE_TYPE_MIRRORING:
		ret = true;
		break;
	default:
		debug_error("Unexpected %d\n", type);
		return false;
	}
	if (ret) {
		int capture_h_count = 0;
		PCM_CAPTURE_H_COUNT_GET(capture_h_count);
		if (capture_h_count == 1) { /* if it is last one */
			/* Recover session information */
			session_result = _mm_session_util_write_information(-1, MM_SESSION_TYPE_MEDIA, pcmHandle->asm_options);
			if (session_result) {
				debug_error("_mm_session_util_write_information() [type %d, options %x] failed[%x]", MM_SESSION_TYPE_MEDIA, pcmHandle->asm_options, session_result);
			}
		}
	}
	return ret;
}

static void __sound_pcm_send_message (mm_sound_pcm_t *pcmHandle, int message, int code)
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
	mm_sound_pcm_t *pcmHandle = (mm_sound_pcm_t *)cb_data;
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
		PCM_LOCK_INTERNAL(&pcmHandle->pcm_mutex_internal);
		_pcm_sound_stop_internal (pcmHandle);
		PCM_UNLOCK_INTERNAL(&pcmHandle->pcm_mutex_internal);
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

static int _pcm_sound_ignore_session (MMSoundPcmHandle_t handle, MMSound_session_type_e type)
{
	int result = MM_ERROR_NONE;
	int session_result = MM_ERROR_NONE;
	mm_sound_pcm_t *pcmHandle = (mm_sound_pcm_t*)handle;
	int errorcode = 0;

	debug_fenter();

	/* Check input param */
	if(pcmHandle == NULL) {
		debug_error ("Handle is null, return Invalid Argument\n");
		result = MM_ERROR_INVALID_ARGUMENT;
		goto EXIT;
	}

	if (pcmHandle->is_started) {
		debug_error ("Operation is not permitted while started\n");
		result = MM_ERROR_SOUND_INVALID_STATE;
		goto EXIT;
	}

	PCM_LOCK_INTERNAL(&pcmHandle->pcm_mutex_internal);

	/* Unregister ASM */
	if (pcmHandle->skip_session == false && pcmHandle->asm_handle) {
		if(!ASM_unregister_sound(pcmHandle->asm_handle, pcmHandle->asm_event, &errorcode)) {
			debug_error("ASM_unregister failed with 0x%x\n", errorcode);
			result = MM_ERROR_SOUND_INTERNAL;
		}
		pcmHandle->skip_session = true;
		pcmHandle->asm_handle = 0;
	}
	if (type == MMSOUND_SESSION_TYPE_CAPTURE){
		int capture_h_count = 0;
		PCM_CAPTURE_H_COUNT_GET(capture_h_count);
		if (capture_h_count == 1) { /* if it is last one */
			/* Recover session information */
			session_result = _mm_session_util_write_information(-1, MM_SESSION_TYPE_MEDIA, pcmHandle->asm_options);
			if (session_result) {
				debug_error("_mm_session_util_write_information() [type %d, options %x] failed[%x]", MM_SESSION_TYPE_MEDIA, pcmHandle->asm_options, session_result);
			}
		}
	}

	PCM_UNLOCK_INTERNAL(&pcmHandle->pcm_mutex_internal);

EXIT:
	debug_fleave();
	return result;
}

EXPORT_API
int mm_sound_pcm_capture_open(MMSoundPcmHandle_t *handle, const unsigned int rate, MMSoundPcmChannel_t channel, MMSoundPcmFormat_t format)
{
	mm_sound_pcm_t *pcmHandle = NULL;
	int size = 0;
	int result = MM_ERROR_NONE;
	int errorcode = 0;
	int ret_mutex = 0;
	int ret = MM_ERROR_NONE;

	int volume_config = 0;
	pa_sample_spec ss;

	mm_sound_handle_route_info route_info;
	route_info.policy = HANDLE_ROUTE_POLICY_DEFAULT;

	debug_warning ("enter : rate=[%d], channel=[%x], format=[%x]\n", rate, channel, format);

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

	pcmHandle = calloc(sizeof(mm_sound_pcm_t), 1);
	if(pcmHandle == NULL)
		return MM_ERROR_OUT_OF_MEMORY;

	ret_mutex = pthread_mutex_init(&pcmHandle->pcm_mutex_internal, NULL);
	if(ret_mutex != 0)
	{
		free(pcmHandle);
		return MM_ERROR_OUT_OF_MEMORY;
	}

	/* Register ASM */
	/* get session information */
	ret = _get_asm_information(MMSOUND_SESSION_TYPE_CAPTURE, &pcmHandle->asm_event, &pcmHandle->asm_options, &pcmHandle->session_registered_type);
	if(ret) {
		PCM_LOCK_DESTROY_INTERNAL(&pcmHandle->pcm_mutex_internal);
		free(pcmHandle);
		return ret;
	}
	PCM_CAPTURE_H_COUNT_INC();

	/* register asm */
	if(pcmHandle->asm_event != ASM_EVENT_CALL &&
		pcmHandle->asm_event != ASM_EVENT_VIDEOCALL &&
		pcmHandle->asm_event != ASM_EVENT_VOIP &&
		pcmHandle->asm_event != ASM_EVENT_VOICE_RECOGNITION &&
		pcmHandle->asm_event != ASM_EVENT_MMCAMCORDER_AUDIO &&
		pcmHandle->asm_event != ASM_EVENT_MMCAMCORDER_VIDEO &&
		pcmHandle->skip_session == false) {
		if(!ASM_register_sound(-1, &pcmHandle->asm_handle, pcmHandle->asm_event,
				/* ASM_STATE_PLAYING */ ASM_STATE_NONE, sound_pcm_asm_callback, (void*)pcmHandle, pcmHandle->resource, &errorcode))
		{
			debug_error("ASM_register_sound() failed 0x%x\n", errorcode);
			PCM_LOCK_DESTROY_INTERNAL(&pcmHandle->pcm_mutex_internal);
			free(pcmHandle);
			PCM_CAPTURE_H_COUNT_DEC();
			return MM_ERROR_POLICY_BLOCKED;
		}
		if(!ASM_set_session_option(pcmHandle->asm_handle, pcmHandle->asm_options, &errorcode)) {
			debug_error("ASM_set_session_option() failed 0x%x\n", errorcode);
		}
	} else {
		pcmHandle->skip_session = true;
	}

	/* Open */
	if(pcmHandle->asm_event == ASM_EVENT_VOIP)
		volume_config = VOLUME_TYPE_VOIP;
	else
		volume_config = VOLUME_TYPE_SYSTEM; //dose not effect at capture mode

	pcmHandle->handle = mm_sound_pa_open(HANDLE_MODE_INPUT, &route_info, 0, volume_config, &ss, NULL, &size);
	if(pcmHandle->handle<0) {
		result = pcmHandle->handle;
		debug_error("Device Open Error 0x%x\n", result);
		PCM_LOCK_DESTROY_INTERNAL(&pcmHandle->pcm_mutex_internal);
		free(pcmHandle);
		PCM_CAPTURE_H_COUNT_DEC();
		return result;
	}

	pcmHandle->is_playback = false;
	pcmHandle->rate = rate;
	pcmHandle->channel = channel;
	pcmHandle->format = format;
	pcmHandle->byte_per_sec = rate*(format==MMSOUND_PCM_U8?1:2)*(channel==MMSOUND_PCM_MONO?1:2);

	/* Set handle to return */
	*handle = (MMSoundPcmHandle_t)pcmHandle;

	debug_warning ("success : handle=[%p], size=[%d]\n", pcmHandle, size);

	return size;
}

EXPORT_API
int mm_sound_pcm_capture_open_ex(MMSoundPcmHandle_t *handle, const unsigned int rate, MMSoundPcmChannel_t channel, MMSoundPcmFormat_t format, mm_sound_source_type_e source_type)
{
	mm_sound_pcm_t *pcmHandle = NULL;
	int size = 0;
	int result = MM_ERROR_NONE;
	int errorcode = 0;
	int ret_mutex = 0;

	int volume_config = 0;
	pa_sample_spec ss;
	mm_sound_handle_route_info route_info;
	route_info.policy = HANDLE_ROUTE_POLICY_DEFAULT;

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

	pcmHandle = calloc(sizeof(mm_sound_pcm_t), 1);
	if(pcmHandle == NULL)
		return MM_ERROR_OUT_OF_MEMORY;

	ret_mutex = pthread_mutex_init(&pcmHandle->pcm_mutex_internal, NULL);
	if(ret_mutex != 0)
	{
		free(pcmHandle);
		return MM_ERROR_OUT_OF_MEMORY;
	}

	/* Register ASM */
	/* get session information */
	if(MM_ERROR_NONE != _get_asm_information(MMSOUND_SESSION_TYPE_CAPTURE, &pcmHandle->asm_event, &pcmHandle->asm_options, &pcmHandle->session_registered_type)) {
		PCM_LOCK_DESTROY_INTERNAL(&pcmHandle->pcm_mutex_internal);
		free(pcmHandle);
		return MM_ERROR_POLICY_INTERNAL;
	}
	PCM_CAPTURE_H_COUNT_INC();

	switch (source_type) {
	case SUPPORT_SOURCE_TYPE_VOIP:
		route_info.policy = HANDLE_ROUTE_POLICY_IN_VOIP;
		break;
	case SUPPORT_SOURCE_TYPE_MIRRORING:
		route_info.policy = HANDLE_ROUTE_POLICY_IN_MIRRORING;
		break;
	case SUPPORT_SOURCE_TYPE_DEFAULT:
	case SUPPORT_SOURCE_TYPE_VIDEOCALL:
	case SUPPORT_SOURCE_TYPE_VOICERECORDING:
		break;
	case SUPPORT_SOURCE_TYPE_VOICECONTROL:
		pcmHandle->asm_event = ASM_EVENT_EXCLUSIVE_RESOURCE;
		pcmHandle->resource = ASM_RESOURCE_VOICECONTROL;
		break;
	default:
		break;
	}

	/* register asm */
	if(pcmHandle->asm_event != ASM_EVENT_CALL &&
		pcmHandle->asm_event != ASM_EVENT_VIDEOCALL &&
		pcmHandle->asm_event != ASM_EVENT_VOIP &&
		pcmHandle->asm_event != ASM_EVENT_VOICE_RECOGNITION &&
		pcmHandle->asm_event != ASM_EVENT_MMCAMCORDER_AUDIO &&
		pcmHandle->asm_event != ASM_EVENT_MMCAMCORDER_VIDEO &&
		pcmHandle->skip_session == false &&
		_check_skip_session_type_for_capture(pcmHandle, source_type) == false) {
		if(!ASM_register_sound(-1, &pcmHandle->asm_handle, pcmHandle->asm_event,
				/* ASM_STATE_PLAYING */ ASM_STATE_NONE, sound_pcm_asm_callback, (void*)pcmHandle, pcmHandle->resource, &errorcode)) 	{
			debug_error("ASM_register_sound() failed 0x%x\n", errorcode);
			PCM_LOCK_DESTROY_INTERNAL(&pcmHandle->pcm_mutex_internal);
			free(pcmHandle);
			PCM_CAPTURE_H_COUNT_DEC();
			return MM_ERROR_POLICY_BLOCKED;
		}
		if(!ASM_set_session_option(pcmHandle->asm_handle, pcmHandle->asm_options, &errorcode)) {
			debug_error("ASM_set_session_option() failed 0x%x\n", errorcode);
		}
	} else {
		pcmHandle->skip_session = true;
	}

	/* For Video Call or VoIP select volume type VOLUME_TYPE_VOIP for sink/source */
	if( (pcmHandle->asm_event == ASM_EVENT_VIDEOCALL) || (pcmHandle->asm_event == ASM_EVENT_VOIP) )
		volume_config = VOLUME_TYPE_VOIP;
	else
		volume_config = VOLUME_TYPE_SYSTEM; //dose not effect at capture mode

	pcmHandle->handle = mm_sound_pa_open(HANDLE_MODE_INPUT, &route_info, 0, volume_config, &ss, NULL, &size);
	if(pcmHandle->handle<0) {
		result = pcmHandle->handle;
		debug_error("Device Open Error 0x%x\n", result);
		PCM_LOCK_DESTROY_INTERNAL(&pcmHandle->pcm_mutex_internal);
		free(pcmHandle);
		PCM_CAPTURE_H_COUNT_DEC();
		return result;
	}

	pcmHandle->is_playback = false;
	pcmHandle->rate = rate;
	pcmHandle->channel = channel;
	pcmHandle->format = format;
	pcmHandle->byte_per_sec = rate*(format==MMSOUND_PCM_U8?1:2)*(channel==MMSOUND_PCM_MONO?1:2);

	/* Set handle to return */
	*handle = (MMSoundPcmHandle_t)pcmHandle;

	debug_warning ("success : handle=[%p], size=[%d]\n", handle, size);

	return size;
}

EXPORT_API
int mm_sound_pcm_capture_ignore_session(MMSoundPcmHandle_t *handle)
{
	return _pcm_sound_ignore_session(handle, MMSOUND_SESSION_TYPE_CAPTURE);
}

static int _pcm_sound_start (MMSoundPcmHandle_t handle)
{
	mm_sound_pcm_t *pcmHandle = (mm_sound_pcm_t*)handle;
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
	mm_sound_pa_cork(pcmHandle->handle, 0);

EXIT:
	PCM_UNLOCK_INTERNAL(&pcmHandle->pcm_mutex_internal);

NULL_HANDLE:
	debug_fleave();
	return ret;
}

EXPORT_API
int mm_sound_pcm_capture_start(MMSoundPcmHandle_t handle)
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
	mm_sound_pcm_t *pcmHandle = (mm_sound_pcm_t*)handle;

	/* Check input param */
	if(pcmHandle == NULL)
		return MM_ERROR_INVALID_ARGUMENT;

	/* Check State */
	if (pcmHandle->is_started == false) {
		debug_warning ("Can't stop because not started\n");
		return MM_ERROR_SOUND_INVALID_STATE;
	}

	/* Drain if playback mode */
	if (pcmHandle->is_playback) {
		if(MM_ERROR_NONE != mm_sound_pa_drain(pcmHandle->handle)) {
			debug_error("drain failed\n");
		}
	}

	/* Update State */
	pcmHandle->is_started = false;

	/* Cork */
	return mm_sound_pa_cork(pcmHandle->handle, 1);
}

static int _pcm_sound_stop(MMSoundPcmHandle_t handle)
{
	mm_sound_pcm_t *pcmHandle = (mm_sound_pcm_t*)handle;
	int errorcode = 0;
	int ret = MM_ERROR_NONE;

	debug_fenter();

	/* Check input param */
	if(pcmHandle == NULL) {
		debug_error ("Handle is null, return Invalid Argument\n");
		ret = MM_ERROR_INVALID_ARGUMENT;
		goto NULL_HANDLE;
	}

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
NULL_HANDLE:
	debug_fleave();
	return ret;
}

EXPORT_API
int mm_sound_pcm_capture_stop(MMSoundPcmHandle_t handle)
{
	int ret = 0;

	debug_warning ("enter : handle=[%p]\n", handle);
	ret = _pcm_sound_stop(handle);
	debug_warning ("leave : handle=[%p], ret=[0x%X]\n", handle, ret);

	return ret;
}

EXPORT_API
int mm_sound_pcm_capture_flush(MMSoundPcmHandle_t handle)
{
	int ret = 0;
	mm_sound_pcm_t *pcmHandle = (mm_sound_pcm_t*)handle;

	/* Check input param */
	if(pcmHandle == NULL)
		return MM_ERROR_INVALID_ARGUMENT;

	debug_warning ("enter : handle=[%p]\n", handle);
	ret = mm_sound_pa_flush(pcmHandle->handle);
	debug_warning ("leave : handle=[%p], ret=[0x%X]\n", handle, ret);

	return ret;
}

EXPORT_API
int mm_sound_pcm_capture_read(MMSoundPcmHandle_t handle, void *buffer, const unsigned int length )
{
	int ret = 0;
	static int read_byte = 0;
	mm_sound_pcm_t *pcmHandle = (mm_sound_pcm_t*)handle;

	/* Check input param */
	if(pcmHandle == NULL) {
		debug_error ("Handle is null, return Invalid Argument\n");
		ret =  MM_ERROR_INVALID_ARGUMENT;
		goto NULL_HANDLE;
	}
	PCM_LOCK_INTERNAL(&pcmHandle->pcm_mutex_internal);

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

	/* Check State : return fail if not started */
	if (!pcmHandle->is_started) {
		/*  not started, return fail */
		debug_error ("Not started yet, return Invalid State \n");
		ret = MM_ERROR_SOUND_INVALID_STATE;
		goto EXIT;
	}

	/* Read */
	ret = mm_sound_pa_read(pcmHandle->handle, buffer, length);

EXIT:
	PCM_UNLOCK_INTERNAL(&pcmHandle->pcm_mutex_internal);
NULL_HANDLE:
	read_byte += length;

	if(ret > 0 && read_byte>pcmHandle->byte_per_sec*RW_LOG_PERIOD){
		debug_log ("(%d)/read-once, (%d)/%dsec bytes read \n", length, read_byte, RW_LOG_PERIOD);
		read_byte = 0;
	}
	return ret;
}

EXPORT_API
int mm_sound_pcm_capture_close(MMSoundPcmHandle_t handle)
{
	int result = MM_ERROR_NONE;
	mm_sound_pcm_t *pcmHandle = (mm_sound_pcm_t*)handle;
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
	if(MM_ERROR_NONE != mm_sound_pa_close(pcmHandle->handle)) {
		debug_error("handle close failed 0x%X", result);
		result = MM_ERROR_SOUND_INTERNAL;
		goto EXIT;
	}

	/* Unregister ASM */
	if (pcmHandle->skip_session == false) {
		if(pcmHandle->asm_event != ASM_EVENT_CALL &&
			pcmHandle->asm_event != ASM_EVENT_VIDEOCALL &&
			pcmHandle->asm_event != ASM_EVENT_VOIP &&
			pcmHandle->asm_event != ASM_EVENT_VOICE_RECOGNITION &&
			pcmHandle->asm_event != ASM_EVENT_MMCAMCORDER_AUDIO &&
			pcmHandle->asm_event != ASM_EVENT_MMCAMCORDER_VIDEO) {
			if (pcmHandle->asm_handle) {
				if(!ASM_unregister_sound(pcmHandle->asm_handle, pcmHandle->asm_event, &errorcode)) {
					debug_error("ASM_unregister failed with 0x%x\n", errorcode);
					result = MM_ERROR_SOUND_INTERNAL;
					goto EXIT;
				}
			}
		}
		int capture_h_count = 0;
		PCM_CAPTURE_H_COUNT_GET(capture_h_count);
		if (capture_h_count == 1) { /* if it is last one */
			/* Recover session information */
			result = _mm_session_util_write_information(-1, MM_SESSION_TYPE_MEDIA, pcmHandle->asm_options);
			if (result) {
				debug_error("_mm_session_util_write_information() [type %d, options %x] failed[%x]", MM_SESSION_TYPE_MEDIA, pcmHandle->asm_options, result);
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
	PCM_CAPTURE_H_COUNT_DEC();

	return result;
}

EXPORT_API
int mm_sound_pcm_set_message_callback (MMSoundPcmHandle_t handle, MMMessageCallback callback, void *user_param)
{
	mm_sound_pcm_t *pcmHandle =  (mm_sound_pcm_t*)handle;

	if(pcmHandle == NULL || callback == NULL)
		return MM_ERROR_INVALID_ARGUMENT;

	pcmHandle->msg_cb = callback;
	pcmHandle->msg_cb_param = user_param;

	debug_log ("set pcm message callback (%p,%p)\n", callback, user_param);

	return MM_ERROR_NONE;
}

EXPORT_API
int mm_sound_pcm_play_open_ex (MMSoundPcmHandle_t *handle, const unsigned int rate, MMSoundPcmChannel_t channel, MMSoundPcmFormat_t format, int volume_config, ASM_sound_events_t asm_event)
{
	mm_sound_pcm_t *pcmHandle = NULL;
	int size = 0;
	int result = MM_ERROR_NONE;
	int errorcode = 0;
	int volume_type = MM_SOUND_VOLUME_CONFIG_TYPE(volume_config);
	int ret_mutex = 0;
	mm_sound_handle_route_info route_info;
	route_info.policy = HANDLE_ROUTE_POLICY_OUT_AUTO;

	pa_sample_spec ss;

	debug_warning ("enter : rate=[%d], channel=[%x][%s], format=[%x][%s], volconf=[%d], event=[%d]\n",
			rate, channel, __get_channel_str(channel), format, __get_format_str(format), volume_config, asm_event);

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

	pcmHandle = calloc(sizeof(mm_sound_pcm_t),1);
	if(pcmHandle == NULL)
		return MM_ERROR_OUT_OF_MEMORY;

	ret_mutex = pthread_mutex_init(&pcmHandle->pcm_mutex_internal, NULL);
	if(ret_mutex != 0) {
		debug_error ("error mutex init....%d");
		result = MM_ERROR_OUT_OF_MEMORY;
		goto ERROR;
	}

	/* Register ASM */
	debug_log ("session start : input asm_event = %d-------------\n", asm_event);
	if (asm_event == ASM_EVENT_MONITOR) {
		debug_log ("Skip SESSION for event (%d)\n", asm_event);
		pcmHandle->skip_session = true;
	} else if (asm_event == ASM_EVENT_NONE) {
		/* get session information */
		if(MM_ERROR_NONE != _get_asm_information(MMSOUND_SESSION_TYPE_PLAYBACK, &pcmHandle->asm_event, &pcmHandle->asm_options, &pcmHandle->session_registered_type)) {
			debug_error ("_get_asm_information failed....\n");
			result = MM_ERROR_POLICY_INTERNAL;
			goto ERROR;
		}

		// should be fixed. call forwarding engine(voip) use call volume type.
		if(volume_type == VOLUME_TYPE_CALL) {
			pcmHandle->skip_session = true;
		}

		if(pcmHandle->asm_event != ASM_EVENT_CALL &&
			pcmHandle->asm_event != ASM_EVENT_VIDEOCALL &&
			pcmHandle->asm_event != ASM_EVENT_VOIP &&
			pcmHandle->asm_event != ASM_EVENT_VOICE_RECOGNITION &&
			pcmHandle->asm_event != ASM_EVENT_MMCAMCORDER_AUDIO &&
			pcmHandle->asm_event != ASM_EVENT_MMCAMCORDER_VIDEO &&
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
	} else {
		/* register asm using asm_event input */
		if(!ASM_register_sound(-1, &pcmHandle->asm_handle, asm_event,
				ASM_STATE_NONE, NULL, (void*)pcmHandle, pcmHandle->resource, &errorcode)) {
			debug_error("ASM_register_sound() failed 0x%x\n", errorcode);
			result = MM_ERROR_POLICY_BLOCKED;
			goto ERROR;
		}
	}


	/* Open */
	pcmHandle->handle = mm_sound_pa_open(HANDLE_MODE_OUTPUT, &route_info, 0, volume_config, &ss, NULL, &size);
	if(!pcmHandle->handle) {
		debug_error("Device Open Error 0x%x\n");
		result = MM_ERROR_SOUND_DEVICE_NOT_OPENED;
		goto ERROR;
	}

	/* Set corked state, uncork will be done at prepare()
	   FIXME: we should consider audio_open() return with corked state */
	result = mm_sound_pa_cork(pcmHandle->handle, 1);
	if(result) {
		debug_error("Cork Error 0x%x\n", result);
		result = MM_ERROR_SOUND_INTERNAL;
		goto ERROR;
	}

	pcmHandle->is_playback = true;
	pcmHandle->rate = rate;
	pcmHandle->channel = channel;
	pcmHandle->format = format;
	pcmHandle->byte_per_sec = rate*(format==MMSOUND_PCM_U8?1:2)*(channel==MMSOUND_PCM_MONO?1:2);
	pcmHandle->volume_config = volume_config;

	/* Set handle to return */
	*handle = (MMSoundPcmHandle_t)pcmHandle;

	debug_warning ("success : handle=[%p], size=[%d]\n", pcmHandle, size);
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
int mm_sound_pcm_play_open_no_session(MMSoundPcmHandle_t *handle, const unsigned int rate, MMSoundPcmChannel_t channel, MMSoundPcmFormat_t format, int volume_config)
{
	return mm_sound_pcm_play_open_ex (handle, rate, channel, format, volume_config, ASM_EVENT_MONITOR);
}

EXPORT_API
int mm_sound_pcm_play_open(MMSoundPcmHandle_t *handle, const unsigned int rate, MMSoundPcmChannel_t channel, MMSoundPcmFormat_t format, int volume_config)
{
	return mm_sound_pcm_play_open_ex (handle, rate, channel, format, volume_config, ASM_EVENT_NONE);
}

EXPORT_API
int mm_sound_pcm_play_start(MMSoundPcmHandle_t handle)
{
	int ret = 0;

	debug_warning ("enter : handle=[%p]\n", handle);
	ret = _pcm_sound_start (handle);
	debug_warning ("leave : handle=[%p], ret=[0x%X]\n", handle, ret);

	return ret;
}

EXPORT_API
int mm_sound_pcm_play_stop(MMSoundPcmHandle_t handle)
{
	int ret = 0;

	debug_warning ("enter : handle=[%p]\n", handle);
	ret = _pcm_sound_stop(handle);
	debug_warning ("leave : handle=[%p], ret=[0x%X]\n", handle, ret);

	return ret;
}

EXPORT_API
int mm_sound_pcm_play_drain(MMSoundPcmHandle_t handle)
{
	int ret = 0;
	mm_sound_pcm_t *pcmHandle = (mm_sound_pcm_t*)handle;

	/* Check input param */
	if(pcmHandle == NULL)
		return MM_ERROR_INVALID_ARGUMENT;

	debug_warning ("enter : handle=[%p]\n", handle);
	ret = mm_sound_pa_drain(pcmHandle->handle);
	debug_warning ("leave : handle=[%p], ret=[0x%X]\n", handle, ret);

	return ret;
}

EXPORT_API
int mm_sound_pcm_play_flush(MMSoundPcmHandle_t handle)
{
	int ret = 0;
	mm_sound_pcm_t *pcmHandle = (mm_sound_pcm_t*)handle;

	/* Check input param */
	if(pcmHandle == NULL)
		return MM_ERROR_INVALID_ARGUMENT;

	debug_warning ("enter : handle=[%p]\n", handle);
	ret = mm_sound_pa_flush(pcmHandle->handle);
	debug_warning ("leave : handle=[%p], ret=[0x%X]\n", handle, ret);

	return ret;
}

EXPORT_API
int mm_sound_pcm_play_write(MMSoundPcmHandle_t handle, void* ptr, unsigned int length_byte)
{
	int ret = 0;
	static int written_byte = 0;
	mm_sound_pcm_t *pcmHandle = (mm_sound_pcm_t*)handle;
	int vr_state = 0;

	/* Check input param */
	if(pcmHandle == NULL) {
		debug_error ("Handle is null, return Invalid Argument\n");
		ret = MM_ERROR_INVALID_ARGUMENT;
		goto NULL_HANDLE;
	}

	PCM_LOCK_INTERNAL(&pcmHandle->pcm_mutex_internal);

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

	/* Check State : return fail if not started */
	if (!pcmHandle->is_started) {
		/* not started, return fail */
		debug_error ("Not started yet, return Invalid State \n");
		ret = MM_ERROR_SOUND_INVALID_STATE;
		goto EXIT;
	}

	/* check skip condition.
	 * if accessibility screen reader (VOICE type with NoSession), no need to check, always do write */
	if (!pcmHandle->skip_session || MM_SOUND_VOLUME_CONFIG_TYPE(pcmHandle->volume_config) != VOLUME_TYPE_VOICE) {
		/*  Check whether voicerecorder is running */
		vconf_get_int(VCONFKEY_RECORDER_STATE, &vr_state);

		if (vr_state == VCONFKEY_RECORDER_STATE_RECORDING) {
			debug_log ("During VoiceRecording....MUTE!!!");
			goto EXIT;
		}
	}

	/* Write */
	ret = mm_sound_pa_write(pcmHandle->handle, ptr, length_byte);


EXIT:
	PCM_UNLOCK_INTERNAL(&pcmHandle->pcm_mutex_internal);
NULL_HANDLE:
	written_byte += length_byte;
	if(ret > 0 && written_byte>pcmHandle->byte_per_sec*RW_LOG_PERIOD){
		debug_log ("(%d)/write-once, (%d)/%dsec bytes written\n", length_byte, written_byte, RW_LOG_PERIOD);
		written_byte = 0;
	}

	return ret;
}

EXPORT_API
int mm_sound_pcm_play_close(MMSoundPcmHandle_t handle)
{
	int result = MM_ERROR_NONE;
	mm_sound_pcm_t *pcmHandle = (mm_sound_pcm_t*)handle;
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
		if(MM_ERROR_NONE != mm_sound_pa_drain(pcmHandle->handle)) {
			debug_error("drain failed\n");
			result = MM_ERROR_SOUND_INTERNAL;
			goto EXIT;
		}
	}
	pcmHandle->is_started = false;
	/* Close */
	if(MM_ERROR_NONE != mm_sound_pa_close(pcmHandle->handle)) {
		debug_error("handle close failed. handle(%d)", pcmHandle->handle);
		result = MM_ERROR_SOUND_INTERNAL;
		goto EXIT;
	}

	if (pcmHandle->skip_session == false) {
		/* Unregister ASM */
		if(pcmHandle->asm_event != ASM_EVENT_CALL &&
			pcmHandle->asm_event != ASM_EVENT_VIDEOCALL &&
			pcmHandle->asm_event != ASM_EVENT_VOIP &&
			pcmHandle->asm_event != ASM_EVENT_VOICE_RECOGNITION &&
			pcmHandle->asm_event != ASM_EVENT_MMCAMCORDER_AUDIO &&
			pcmHandle->asm_event != ASM_EVENT_MMCAMCORDER_VIDEO) {
			if(!ASM_unregister_sound(pcmHandle->asm_handle, pcmHandle->asm_event, &errorcode)) {
				debug_error("ASM_unregister failed with 0x%x\n", errorcode);
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
int mm_sound_pcm_play_ignore_session(MMSoundPcmHandle_t *handle)
{
	return _pcm_sound_ignore_session(handle, MMSOUND_SESSION_TYPE_PLAYBACK);
}

EXPORT_API
int mm_sound_pcm_get_latency(MMSoundPcmHandle_t handle, int *latency)
{
	mm_sound_pcm_t *pcmHandle = (mm_sound_pcm_t*)handle;
	int result = MM_ERROR_NONE;
	int mlatency = 0;

	/* Check input param */
	if (latency == NULL)
		return MM_ERROR_INVALID_ARGUMENT;

	if(MM_ERROR_NONE != mm_sound_pa_get_latency(pcmHandle->handle, &mlatency)) {
		debug_error("Get Latency Error");
		return MM_ERROR_SOUND_DEVICE_NOT_OPENED;
	}

	*latency = mlatency;

	return MM_ERROR_NONE;
}

EXPORT_API
int mm_sound_pcm_is_started(MMSoundPcmHandle_t handle, bool *is_started)
{
	mm_sound_pcm_t *pcmHandle = (mm_sound_pcm_t*)handle;

	/* Check input param */
	if (is_started == NULL)
		return MM_ERROR_INVALID_ARGUMENT;

	*is_started = pcmHandle->is_started;

	return MM_ERROR_NONE;
}
