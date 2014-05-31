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
#include <avsystem.h>

#include <mm_types.h>
#include <mm_error.h>
#include <mm_message.h>
#include <mm_debug.h>
#include "include/mm_sound_private.h"
#include "include/mm_sound.h"
#include "include/mm_sound_utils.h"
#include "include/mm_sound_common.h"


#include <audio-session-manager.h>
#include <mm_session.h>
#include <mm_session_private.h>

#define _MIN_SYSTEM_SAMPLERATE	8000
#define _MAX_SYSTEM_SAMPLERATE	48000
#define RW_LOG_PERIOD 5 /* period(second) for print log in capture read or play write*/

#define PCM_LOCK_INTERNAL(LOCK) do { pthread_mutex_lock(LOCK); } while (0)
#define PCM_UNLOCK_INTERNAL(LOCK) do { pthread_mutex_unlock(LOCK); } while (0)
#define PCM_LOCK_DESTROY_INTERNAL(LOCK) do { pthread_mutex_destroy(LOCK); } while (0)

typedef struct {
	avsys_handle_t		audio_handle;
	int			asm_handle;
	ASM_sound_events_t	asm_event;

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

} mm_sound_pcm_t;

static int _pcm_sound_start (MMSoundPcmHandle_t handle);
static int _pcm_sound_stop_internal (MMSoundPcmHandle_t handle);
static int _pcm_sound_stop(MMSoundPcmHandle_t handle);
static void __sound_pcm_send_message (mm_sound_pcm_t *pcmHandle, int message, int code);
static int _pcm_sound_ignore_session (MMSoundPcmHandle_t handle);


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

static int _get_asm_event_type(ASM_sound_events_t *type)
{
	int	sessionType = MM_SESSION_TYPE_SHARE;
	ASM_sound_events_t asm_event;

	if(type == NULL)
		return MM_ERROR_SOUND_INVALID_POINTER;

	/* read session type */
	if(_mm_session_util_read_type(-1, &sessionType) < 0) {
		debug_log("Read Session Type failed. Set default \"Share\" type\n");
		sessionType = MM_SESSION_TYPE_SHARE;
		if(mm_session_init(sessionType) < 0) {
			debug_error("mm_session_init() failed\n");
			return MM_ERROR_SOUND_INTERNAL;
		}
	}

	/* convert MM_SESSION_TYPE to ASM_EVENT_TYPE */
	switch (sessionType)
	{
	case MM_SESSION_TYPE_SHARE:
		asm_event = ASM_EVENT_SHARE_MMSOUND;
		break;
	case MM_SESSION_TYPE_EXCLUSIVE:
		asm_event = ASM_EVENT_EXCLUSIVE_MMSOUND;
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
	case MM_SESSION_TYPE_RICH_CALL:
		asm_event = ASM_EVENT_RICH_CALL;
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
		debug_error("Unexpected %d\n", sessionType);
		return MM_ERROR_SOUND_INTERNAL;
	}

	*type = asm_event;
	return MM_ERROR_NONE;
}

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
		ret = true;
		break;
	default:
		debug_error("Unexpected %d\n", type);
		return false;
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
		break;

	case ASM_COMMAND_PLAY:
	case ASM_COMMAND_NONE:
		debug_error ("Not an expected case!!!!\n");
		break;
	}

	/* execute user callback if callback available */
	__sound_pcm_send_message (pcmHandle, MM_MESSAGE_SOUND_PCM_INTERRUPTED, event_src);

	return cb_res;
}

static int _pcm_sound_ignore_session (MMSoundPcmHandle_t handle)
{
	int result = MM_ERROR_NONE;
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

	PCM_UNLOCK_INTERNAL(&pcmHandle->pcm_mutex_internal);

EXIT:
	debug_fleave();
	return result;
}

EXPORT_API
int mm_sound_pcm_capture_open(MMSoundPcmHandle_t *handle, const unsigned int rate, MMSoundPcmChannel_t channel, MMSoundPcmFormat_t format)
{
	avsys_audio_param_t param;
	mm_sound_pcm_t *pcmHandle = NULL;
	int size = 0;
	int result = AVSYS_STATE_SUCCESS;
	int errorcode = 0;
	int ret_mutex = 0;

	debug_warning ("enter : rate=[%d], channel=[%x], format=[%x]\n", rate, channel, format);

	memset(&param, 0, sizeof(avsys_audio_param_t));


	if (rate < _MIN_SYSTEM_SAMPLERATE || rate > _MAX_SYSTEM_SAMPLERATE) {
		debug_error("unsupported sample rate %u", rate);
		return MM_ERROR_SOUND_DEVICE_INVALID_SAMPLERATE;
	} else {
		param.samplerate = rate;
	}

	switch(channel)
	{
	case MMSOUND_PCM_MONO:
		param.channels = 1;
		break;
	case MMSOUND_PCM_STEREO:
		param.channels = 2;
		break;

	default:
		debug_error("Unsupported channel type\n");
		return MM_ERROR_SOUND_DEVICE_INVALID_CHANNEL;
	}

	switch(format)
	{
	case MMSOUND_PCM_U8:
		param.format = AVSYS_AUDIO_FORMAT_8BIT;
		break;
	case MMSOUND_PCM_S16_LE:
		param.format = AVSYS_AUDIO_FORMAT_16BIT;
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
	/* get session type */
	if(MM_ERROR_NONE != _get_asm_event_type(&pcmHandle->asm_event)) {
		PCM_LOCK_DESTROY_INTERNAL(&pcmHandle->pcm_mutex_internal);
		free(pcmHandle);
		return MM_ERROR_POLICY_INTERNAL;
	}
	/* register asm */
	if(pcmHandle->asm_event != ASM_EVENT_CALL &&
		pcmHandle->asm_event != ASM_EVENT_VIDEOCALL &&
		pcmHandle->asm_event != ASM_EVENT_VOIP &&
		pcmHandle->asm_event != ASM_EVENT_VOICE_RECOGNITION &&
		pcmHandle->asm_event != ASM_EVENT_MMCAMCORDER_VIDEO) {
		if(!ASM_register_sound(-1, &pcmHandle->asm_handle, pcmHandle->asm_event,
				/* ASM_STATE_PLAYING */ ASM_STATE_NONE, sound_pcm_asm_callback, (void*)pcmHandle, pcmHandle->resource, &errorcode))
		{
			debug_error("ASM_register_sound() failed 0x%x\n", errorcode);
			PCM_LOCK_DESTROY_INTERNAL(&pcmHandle->pcm_mutex_internal);
			free(pcmHandle);
			return MM_ERROR_POLICY_BLOCKED;
		}
	} else {
		pcmHandle->skip_session = true;
	}

	/* Open */
	param.mode = AVSYS_AUDIO_MODE_INPUT;
	if(pcmHandle->asm_event == ASM_EVENT_VOIP)
		param.vol_type = AVSYS_AUDIO_VOLUME_TYPE_VOIP;
	else
		param.vol_type = AVSYS_AUDIO_VOLUME_TYPE_SYSTEM; //dose not effect at capture mode
	param.priority = AVSYS_AUDIO_PRIORITY_0;		//This does not affect anymore.
	result = avsys_audio_open(&param, &pcmHandle->audio_handle, &size);
	if(AVSYS_FAIL(result)) {
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

	debug_warning ("success : handle=[%p], size=[%d]\n", pcmHandle, size);

	return size;
}

EXPORT_API
int mm_sound_pcm_capture_open_ex(MMSoundPcmHandle_t *handle, const unsigned int rate, MMSoundPcmChannel_t channel, MMSoundPcmFormat_t format, mm_sound_source_type_e source_type)
{
	avsys_audio_param_t param;
	mm_sound_pcm_t *pcmHandle = NULL;
	int size = 0;
	int result = AVSYS_STATE_SUCCESS;
	int errorcode = 0;
	int ret_mutex = 0;

	debug_warning ("enter : rate=[%d Hz], channel=[%x][%s], format=[%x][%s], source_type=[%x]\n",
				rate, channel, __get_channel_str(channel), format, __get_format_str(format), source_type);

	memset(&param, 0, sizeof(avsys_audio_param_t));


	if (rate < _MIN_SYSTEM_SAMPLERATE || rate > _MAX_SYSTEM_SAMPLERATE) {
		debug_error("unsupported sample rate %u", rate);
		return MM_ERROR_SOUND_DEVICE_INVALID_SAMPLERATE;
	} else {
		param.samplerate = rate;
	}

	switch(channel)
	{
	case MMSOUND_PCM_MONO:
		param.channels = 1;
		break;
	case MMSOUND_PCM_STEREO:
		param.channels = 2;
		break;

	default:
		debug_error("Unsupported channel type\n");
		return MM_ERROR_SOUND_DEVICE_INVALID_CHANNEL;
	}

	switch(format)
	{
	case MMSOUND_PCM_U8:
		param.format = AVSYS_AUDIO_FORMAT_8BIT;
		break;
	case MMSOUND_PCM_S16_LE:
		param.format = AVSYS_AUDIO_FORMAT_16BIT;
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
	/* get session type */
	if(MM_ERROR_NONE != _get_asm_event_type(&pcmHandle->asm_event)) {
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
	case SUPPORT_SOURCE_TYPE_VOIP:
	default:
		/* Skip any specific asm setting */
		break;
	}

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
	} else {
		pcmHandle->skip_session = true;
	}

	/* Open */
	param.mode = AVSYS_AUDIO_MODE_INPUT;

	/* For Video Call or VoIP select volume type AVSYS_AUDIO_VOLUME_TYPE_VOIP for sink/source */
	if( (pcmHandle->asm_event == ASM_EVENT_VIDEOCALL) || (pcmHandle->asm_event == ASM_EVENT_VOIP) )
		param.vol_type = AVSYS_AUDIO_VOLUME_TYPE_VOIP;
	else
		param.vol_type = AVSYS_AUDIO_VOLUME_TYPE_SYSTEM; //dose not effect at capture mode
	param.priority = AVSYS_AUDIO_PRIORITY_0;		//This does not affect anymore.
	param.source_type = source_type;
	/* FIXME : remove after quality issue is fixed */
	if (param.source_type == SUPPORT_SOURCE_TYPE_VOICECONTROL) {
		debug_warning("param.source_type = %d \n",param.source_type);
		param.source_type = SUPPORT_SOURCE_TYPE_DEFAULT;
	}
	result = avsys_audio_open(&param, &pcmHandle->audio_handle, &size);
	if(AVSYS_FAIL(result)) {
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

	debug_warning ("success : handle=[%p], size=[%d]\n", handle, size);

	return size;
}

EXPORT_API
int mm_sound_pcm_capture_ignore_session(MMSoundPcmHandle_t *handle)
{
	return _pcm_sound_ignore_session(handle);
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
	ret = avsys_audio_cork(pcmHandle->audio_handle, 0);

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
		if(AVSYS_FAIL(avsys_audio_drain(pcmHandle->audio_handle))) {
			debug_error("drain failed\n");
		}
	}

	/* Update State */
	pcmHandle->is_started = false;

	/* Cork */
	return avsys_audio_cork(pcmHandle->audio_handle, 1);
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
	ret = avsys_audio_read(pcmHandle->audio_handle, buffer, length);

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
	result = avsys_audio_close(pcmHandle->audio_handle);
	if(AVSYS_FAIL(result)) {
		debug_error("handle close failed 0x%X", result);
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
	/* Free handle */
	if (pcmHandle) {
		PCM_LOCK_DESTROY_INTERNAL(&pcmHandle->pcm_mutex_internal);
		free(pcmHandle);
		pcmHandle = NULL;
	}
	debug_warning ("leave : handle=[%p], ret=[0x%X]\n", handle, result);

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
	avsys_audio_param_t param;
	mm_sound_pcm_t *pcmHandle = NULL;
	int size = 0;
	int result = AVSYS_STATE_SUCCESS;
	int errorcode = 0;
	int volume_type = MM_SOUND_VOLUME_CONFIG_TYPE(volume_config);
	int ret_mutex = 0;

	debug_warning ("enter : rate=[%d], channel=[%x][%s], format=[%x][%s], volconf=[%d], event=[%d]\n",
			rate, channel, __get_channel_str(channel), format, __get_format_str(format), volume_config, asm_event);

	memset(&param, 0, sizeof(avsys_audio_param_t));

	/* Check input param */
	if (volume_type < 0 || volume_type >= VOLUME_TYPE_MAX) {
		debug_error("Volume type is invalid %d\n", volume_type);
		return MM_ERROR_INVALID_ARGUMENT;
	}
	if (rate < _MIN_SYSTEM_SAMPLERATE || rate > _MAX_SYSTEM_SAMPLERATE) {
		debug_error("unsupported sample rate %u", rate);
		return MM_ERROR_SOUND_DEVICE_INVALID_SAMPLERATE;
	} else {
		param.samplerate = rate;
	}

	switch(channel)
	{
	case MMSOUND_PCM_MONO:
		param.channels = 1;
		break;
	case MMSOUND_PCM_STEREO:
		param.channels = 2;
		break;
	default:
		debug_error("Unsupported channel type\n");
		return MM_ERROR_SOUND_DEVICE_INVALID_CHANNEL;
	}

	switch(format)
	{
	case MMSOUND_PCM_U8:
		param.format = AVSYS_AUDIO_FORMAT_8BIT;
		break;
	case MMSOUND_PCM_S16_LE:
		param.format = AVSYS_AUDIO_FORMAT_16BIT;
		break;
	default:
		debug_error("Unsupported format type\n");
		return MM_ERROR_SOUND_DEVICE_INVALID_FORMAT;
	}

	pcmHandle = calloc(sizeof(mm_sound_pcm_t),1);
	if(pcmHandle == NULL)
		return MM_ERROR_OUT_OF_MEMORY;

	ret_mutex = pthread_mutex_init(&pcmHandle->pcm_mutex_internal, NULL);
	if(ret_mutex != 0)
	{
		free(pcmHandle);
		return MM_ERROR_OUT_OF_MEMORY;
	}

	/* Register ASM */
	debug_log ("session start : input asm_event = %d-------------\n", asm_event);
	if (asm_event == ASM_EVENT_MONITOR) {
		debug_log ("Skip SESSION for event (%d)\n", asm_event);
		pcmHandle->skip_session = true;
	} else if (asm_event == ASM_EVENT_NONE) {
		/* get session type */
		if(MM_ERROR_NONE != _get_asm_event_type(&pcmHandle->asm_event)) {
			debug_error ("_get_asm_event_type failed....\n");
			PCM_LOCK_DESTROY_INTERNAL(&pcmHandle->pcm_mutex_internal);
			free(pcmHandle);
			return MM_ERROR_POLICY_INTERNAL;
		}

		if(pcmHandle->asm_event != ASM_EVENT_CALL &&
			pcmHandle->asm_event != ASM_EVENT_VIDEOCALL &&
			pcmHandle->asm_event != ASM_EVENT_VOIP &&
			pcmHandle->asm_event != ASM_EVENT_VOICE_RECOGNITION) {
			/* register asm */
			if(!ASM_register_sound(-1, &pcmHandle->asm_handle, pcmHandle->asm_event,
					ASM_STATE_NONE, sound_pcm_asm_callback, (void*)pcmHandle, pcmHandle->resource, &errorcode)) {
				debug_error("ASM_register_sound() failed 0x%x\n", errorcode);
				PCM_LOCK_DESTROY_INTERNAL(&pcmHandle->pcm_mutex_internal);
				free(pcmHandle);
				return MM_ERROR_POLICY_BLOCKED;
			}
		} else {
			pcmHandle->skip_session = true;
		}
	} else {
		/* register asm using asm_event input */
		if(!ASM_register_sound(-1, &pcmHandle->asm_handle, asm_event,
				ASM_STATE_NONE, NULL, (void*)pcmHandle, pcmHandle->resource, &errorcode)) {
			debug_error("ASM_register_sound() failed 0x%x\n", errorcode);
			PCM_LOCK_DESTROY_INTERNAL(&pcmHandle->pcm_mutex_internal);
			free(pcmHandle);
			return MM_ERROR_POLICY_BLOCKED;
		}
	}

	param.mode = AVSYS_AUDIO_MODE_OUTPUT;
	param.vol_type = volume_config;
	param.priority = AVSYS_AUDIO_PRIORITY_0;

//	avsys_audio_ampon();

	/* Open */
	result = avsys_audio_open(&param, &pcmHandle->audio_handle, &size);
	if(AVSYS_FAIL(result)) {
		debug_error("Device Open Error 0x%x\n", result);
		PCM_LOCK_DESTROY_INTERNAL(&pcmHandle->pcm_mutex_internal);
		free(pcmHandle);
		return MM_ERROR_SOUND_DEVICE_NOT_OPENED;
	}

	pcmHandle->is_playback = true;
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

	/* Write */

	/*  Check whether voicerecorder is running */
	vconf_get_int(VCONFKEY_VOICERECORDER_STATE, &vr_state);

	if (vr_state == VCONFKEY_VOICERECORDER_RECORDING) {
		debug_log ("During VoiceRecording....MUTE!!!");
	} else {
		ret = avsys_audio_write(pcmHandle->audio_handle, ptr, length_byte);
	}

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
		if(AVSYS_FAIL(avsys_audio_drain(pcmHandle->audio_handle))) {
			debug_error("drain failed\n");
			result = MM_ERROR_SOUND_INTERNAL;
			goto EXIT;
		}
	}
	pcmHandle->is_started = false;
	/* Close */
	result = avsys_audio_close(pcmHandle->audio_handle);
	if(AVSYS_FAIL(result)) {
		debug_error("handle close failed 0x%X", result);
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
	if (pcmHandle) {
		/* Free handle */
		PCM_LOCK_DESTROY_INTERNAL(&pcmHandle->pcm_mutex_internal);
		free(pcmHandle);
		pcmHandle= NULL;
	}
	debug_warning ("leave : handle=[%p], result[0x%X]\n", handle, result);

	return result;
}

EXPORT_API
int mm_sound_pcm_play_ignore_session(MMSoundPcmHandle_t *handle)
{
	return _pcm_sound_ignore_session(handle);
}

EXPORT_API
int mm_sound_pcm_get_latency(MMSoundPcmHandle_t handle, int *latency)
{
	mm_sound_pcm_t *pcmHandle = (mm_sound_pcm_t*)handle;
	int result = AVSYS_STATE_SUCCESS;
	int mlatency = 0;

	/* Check input param */
	if (latency == NULL)
		return MM_ERROR_INVALID_ARGUMENT;

	result = avsys_audio_get_latency(pcmHandle->audio_handle, &mlatency);
	if(AVSYS_FAIL(result)) {
		debug_error("Get Latency Error 0x%x\n", result);
		return MM_ERROR_SOUND_DEVICE_NOT_OPENED;
	}

	*latency = mlatency;

	return MM_ERROR_NONE;
}
