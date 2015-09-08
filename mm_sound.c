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

#include <stdlib.h>
#include <memory.h>
#include <unistd.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/types.h>
#include <fcntl.h>

#include <sys/stat.h>
#include <errno.h>

#include <vconf.h>
#include <mm_types.h>
#include <mm_error.h>
#include <mm_message.h>
#include <mm_debug.h>
#include "include/mm_sound_private.h"
#include "include/mm_sound.h"
#include "include/mm_sound_utils.h"
#include "include/mm_sound_client.h"
#include "include/mm_sound_pa_client.h"
#include "include/mm_ipc.h"
#include "include/mm_sound_common.h"


#define VOLUME_MAX_MULTIMEDIA	16
#define VOLUME_MAX_BASIC		8
#define VOLUME_MAX_SINGLE		1


#define MASTER_VOLUME_MAX 100
#define MASTER_VOLUME_MIN 0


typedef struct {
	volume_callback_fn	func;
	void*				data;
	volume_type_t		type;
}volume_cb_param;

volume_cb_param g_volume_param[VOLUME_TYPE_MAX];

static pthread_mutex_t _volume_mutex = PTHREAD_MUTEX_INITIALIZER;

static char *volume_type_str[VOLUME_TYPE_MAX] = { "SYSTEM", "NOTIFICATION", "ALARM", "RINGTONE", "MEDIA", "CALL", "VOIP", "VOICE", "FIXED"};

static char* __get_volume_str (volume_type_t type)
{
	return (type >= VOLUME_TYPE_SYSTEM && type < VOLUME_TYPE_MAX)? volume_type_str[type] : "Unknown";
}

static int __validate_volume(volume_type_t type, int value)
{
	if (value < 0)
		return -1;

	switch (type)
	{
	case VOLUME_TYPE_CALL:
	case VOLUME_TYPE_VOIP:
		if (value >= VOLUME_MAX_BASIC) {
			return -1;
		}
		break;
	case VOLUME_TYPE_SYSTEM:
	case VOLUME_TYPE_MEDIA:
	case VOLUME_TYPE_ALARM:
	case VOLUME_TYPE_NOTIFICATION:
	case VOLUME_TYPE_RINGTONE:
	case VOLUME_TYPE_VOICE:
		if (value >= VOLUME_MAX_MULTIMEDIA) {
			return -1;
		}
		break;
	case VOLUME_TYPE_EXT_ANDROID:
		if (value >= VOLUME_MAX_SINGLE) {
			return -1;
		}
		break;
	default:
		return -1;
		break;
	}
	return 0;
}

static void volume_changed_cb(keynode_t* node, void* data)
{
	volume_cb_param* param = (volume_cb_param*) data;

	debug_msg("%s changed callback called\n",vconf_keynode_get_name(node));

	MMSOUND_ENTER_CRITICAL_SECTION( &_volume_mutex )

	if(param && (param->func != NULL)) {
		debug_log("function 0x%x\n", param->func);
		((volume_callback_fn)param->func)(param->data);
	}

	MMSOUND_LEAVE_CRITICAL_SECTION( &_volume_mutex )
}

EXPORT_API
int mm_sound_volume_add_callback(volume_type_t type, volume_callback_fn func, void* user_data)
{
	debug_msg("type = (%d)%15s, func = %p, user_data = %p", type, __get_volume_str(type), func, user_data);

	/* Check input param */
	if (type < 0 || type >= VOLUME_TYPE_MAX) {
		debug_error("invalid argument\n");
		return MM_ERROR_INVALID_ARGUMENT;
	}
	if (!func) {
		debug_warning("callback function is null\n");
		return MM_ERROR_INVALID_ARGUMENT;
	}

	MMSOUND_ENTER_CRITICAL_SECTION_WITH_RETURN( &_volume_mutex, MM_ERROR_SOUND_INTERNAL );

	g_volume_param[type].func = func;
	g_volume_param[type].data = user_data;
	g_volume_param[type].type = type;

	MMSOUND_LEAVE_CRITICAL_SECTION( &_volume_mutex );

	return _mm_sound_volume_add_callback(type, volume_changed_cb, (void*)&g_volume_param[type]);
}

EXPORT_API
int mm_sound_volume_remove_callback(volume_type_t type)
{
	debug_msg("type = (%d)%s", type, __get_volume_str(type));

	if(type < 0 || type >=VOLUME_TYPE_MAX) {
		debug_error("invalid argument\n");
		return MM_ERROR_INVALID_ARGUMENT;
	}

	MMSOUND_ENTER_CRITICAL_SECTION_WITH_RETURN( &_volume_mutex, MM_ERROR_SOUND_INTERNAL );

	g_volume_param[type].func = NULL;
	g_volume_param[type].data = NULL;
	g_volume_param[type].type = type;

	MMSOUND_LEAVE_CRITICAL_SECTION( &_volume_mutex );

	return _mm_sound_volume_remove_callback(type, volume_changed_cb);
}

EXPORT_API
int mm_sound_add_volume_changed_callback(mm_sound_volume_changed_cb func, void* user_data)
{
	int ret = MM_ERROR_NONE;

	if (func == NULL) {
		debug_error("argument is not valid\n");
		return MM_ERROR_INVALID_ARGUMENT;
	}

	ret = _mm_sound_client_add_volume_changed_callback(func, user_data);
	if (ret < 0) {
		debug_error("Can not add volume changed callback, ret = %x\n", ret);
	}

	return ret;
}

EXPORT_API
int mm_sound_remove_volume_changed_callback(void)
{
	int ret = MM_ERROR_NONE;

	ret = _mm_sound_client_remove_volume_changed_callback();
	if (ret < 0) {
		debug_error("Can not remove volume changed callback, ret = %x\n", ret);
	}

	return ret;
}

EXPORT_API
int mm_sound_muteall_add_callback(muteall_callback_fn func)
{
	debug_msg("func = %p", func);

	if (!func) {
		debug_warning("callback function is null\n");
		return MM_ERROR_INVALID_ARGUMENT;
	}

	return _mm_sound_muteall_add_callback(func);
}

EXPORT_API
int mm_sound_muteall_remove_callback(muteall_callback_fn func)
{
	debug_msg("func = %p", func);

	if (!func) {
		debug_warning("callback function is null\n");
		return MM_ERROR_INVALID_ARGUMENT;
	}

	return _mm_sound_muteall_remove_callback(func);
}

EXPORT_API
int mm_sound_get_volume_step(volume_type_t type, int *step)
{
	debug_error("\n**********\n\nTHIS FUNCTION HAS DEFPRECATED\n\n \
			use mm_sound_volume_get_step() instead\n\n**********\n");
	return mm_sound_volume_get_step(type, step);
}

EXPORT_API
int mm_sound_volume_get_step(volume_type_t type, int *step)
{
	int err;

	/* Check input param */
	if (step == NULL) {
		debug_error("second parameter is null\n");
		return MM_ERROR_INVALID_ARGUMENT;
	}
	if (type < 0 || type >= VOLUME_TYPE_MAX) {
		debug_error("Invalid type value %d\n", (int)type);
		return MM_ERROR_INVALID_ARGUMENT;
	}

	if(MM_ERROR_NONE != mm_sound_pa_get_volume_max(type, step)) {
		err = MM_ERROR_INVALID_ARGUMENT;
	}

	debug_msg("type = (%d)%15s, step = %d", type, __get_volume_str(type), *step);

	return MM_ERROR_NONE;
}

EXPORT_API
int mm_sound_volume_set_value(volume_type_t type, const unsigned int value)
{
	int ret = MM_ERROR_NONE;

	debug_msg("type = (%d)%s, value = %d", type, __get_volume_str(type), value);

	/* Check input param */
	if (0 > __validate_volume(type, (int)value)) {
		debug_error("invalid volume type %d, value %u\n", type, value);
		return MM_ERROR_INVALID_ARGUMENT;
	}

	ret = _mm_sound_volume_set_value_by_type(type, value);
	if (ret == MM_ERROR_NONE) {
		/* update shared memory value */
		int muteall;
		_mm_sound_get_muteall(&muteall);
		if(!muteall) {
			if(MM_ERROR_NONE != mm_sound_pa_set_volume_by_type(type, (int)value)) {
				debug_error("Can not set volume to shared memory 0x%x\n", ret);
			}
		}
	}

	return ret;
}

EXPORT_API
int mm_sound_mute_all(int muteall)
{
	int ret = MM_ERROR_NONE;

	debug_msg("** deprecated API ** muteall = %d", muteall);

	return ret;
}


EXPORT_API
int mm_sound_set_call_mute(volume_type_t type, int mute)
{
	int ret = MM_ERROR_NONE;

	debug_error("Unsupported API\n");

	return ret;
}


EXPORT_API
int mm_sound_get_call_mute(volume_type_t type, int *mute)
{
	int ret = MM_ERROR_NONE;

	if(!mute)
		return MM_ERROR_INVALID_ARGUMENT;

	debug_error("Unsupported API\n");

	return ret;
}

EXPORT_API
int mm_sound_volume_get_value(volume_type_t type, unsigned int *value)
{
	int ret = MM_ERROR_NONE;

	/* Check input param */
	if (value == NULL) {
		debug_error("invalid argument\n");
		return MM_ERROR_INVALID_ARGUMENT;
	}
	if (type < 0 || type >= VOLUME_TYPE_MAX) {
		debug_error("invalid volume type value %d\n", type);
		return MM_ERROR_INVALID_ARGUMENT;
	}

	ret = _mm_sound_volume_get_value_by_type(type, value);

	debug_msg("returned %s = %d", __get_volume_str(type), *value);
	return ret;
}

EXPORT_API
int mm_sound_volume_primary_type_set(volume_type_t type)
{
	pid_t mypid;
	int ret = MM_ERROR_NONE;

	/* Check input param */
	if(type < 0 || type >= VOLUME_TYPE_MAX) {
		debug_error("invalid argument\n");
		return MM_ERROR_INVALID_ARGUMENT;
	}

	if (vconf_set_int(VCONFKEY_SOUND_PRIMARY_VOLUME_TYPE_FORCE, type)) {
		debug_error("could not set vconf for RIMARY_VOLUME_TYPE_FORCE\n");
		ret = MM_ERROR_SOUND_INTERNAL;
	} else {
		debug_msg("set primary volume type forcibly %d(%s)", type, __get_volume_str(type));
	}

	return ret;
}

EXPORT_API
int mm_sound_volume_primary_type_clear(void)
{
	pid_t mypid;
	int ret = MM_ERROR_NONE;

	if (vconf_set_int(VCONFKEY_SOUND_PRIMARY_VOLUME_TYPE_FORCE, -1)) {
		debug_error("could not reset vconf for RIMARY_VOLUME_TYPE_FORCE\n");
		ret = MM_ERROR_SOUND_INTERNAL;
	} else {
		debug_msg("clear primary volume type forcibly %d(%s)", -1, "none");
	}

	return ret;
}

EXPORT_API
int mm_sound_volume_get_current_playing_type(volume_type_t *type)
{
	int ret = MM_ERROR_NONE;
	int voltype = VOLUME_TYPE_RINGTONE;
	int fvoltype = -1;

	/* Check input param */
	if(type == NULL) {
		debug_error("invalid argument\n");
		return MM_ERROR_INVALID_ARGUMENT;
	}

	/* check force set */
	if (vconf_get_int(VCONFKEY_SOUND_PRIMARY_VOLUME_TYPE_FORCE, &fvoltype)) {
		debug_error("could not get vconf for RIMARY_VOLUME_TYPE_FORCE\n");
		ret = MM_ERROR_SOUND_INTERNAL;
	} else {
		if(fvoltype >= 0) {
			*type = fvoltype;
			return MM_ERROR_NONE;
		}
	}

	/* If primary volume is not set by user, get current playing volume */
	if(vconf_get_int(VCONFKEY_SOUND_PRIMARY_VOLUME_TYPE, &voltype)) {
		debug_error("get vconf(VCONFKEY_SOUND_PRIMARY_VOLUME_TYPE) failed voltype(%d)\n", voltype);
	} else {
		debug_error("get vconf(VCONFKEY_SOUND_PRIMARY_VOLUME_TYPE) voltype(%d)\n", voltype);
	}

	if(voltype >= 0) {
		*type = voltype;
		ret = MM_ERROR_NONE;
	}
	else if(voltype == -1)
		ret = MM_ERROR_SOUND_VOLUME_NO_INSTANCE;
	else if(voltype == -2)
		ret = MM_ERROR_SOUND_VOLUME_CAPTURE_ONLY;
	else
		ret = MM_ERROR_SOUND_INTERNAL;

	debug_msg("returned type = (%d)%15s, ret = 0x%x", *type, __get_volume_str(*type), ret);

	return ret;
}

EXPORT_API
int mm_sound_volume_set_balance (float balance)
{
	debug_msg("balance = %f", balance);

	/* Check input param */
	if (balance < -1.0 || balance > 1.0) {
		debug_error("invalid balance value [%f]\n", balance);
		return MM_ERROR_INVALID_ARGUMENT;
	}

	return _mm_sound_volume_set_balance(balance);
}

EXPORT_API
int mm_sound_volume_get_balance (float *balance)
{
	int ret = MM_ERROR_NONE;

	/* Check input param */
	if (balance == NULL) {
		debug_error("invalid argument\n");
		return MM_ERROR_INVALID_ARGUMENT;
	}

	ret = _mm_sound_volume_get_balance(balance);
	debug_msg("returned balance = %f", *balance);

	return ret;
}

EXPORT_API
int mm_sound_set_muteall (int muteall)
{
	debug_msg("muteall = %d", muteall);

	/* Check input param */
	if (muteall < 0 || muteall > 1) {
		debug_error("invalid muteall value [%f]\n", muteall);
		return MM_ERROR_INVALID_ARGUMENT;
	}

	return _mm_sound_set_muteall(muteall);
}

EXPORT_API
int mm_sound_get_muteall (int *muteall)
{
	int ret = MM_ERROR_NONE;

	/* Check input param */
	if (muteall == NULL) {
		debug_error("invalid argument\n");
		return MM_ERROR_INVALID_ARGUMENT;
	}

	ret = _mm_sound_get_muteall(muteall);
	debug_msg("returned muteall = %d", *muteall);

	return ret;
}

EXPORT_API
int mm_sound_set_stereo_to_mono (int ismono)
{
	debug_msg("ismono = %d", ismono);

	/* Check input param */
	if (ismono < 0 || ismono > 1) {
		debug_error("invalid ismono value [%f]\n", ismono);
		return MM_ERROR_INVALID_ARGUMENT;
	}

	return __mm_sound_set_stereo_to_mono(ismono);
}

EXPORT_API
int mm_sound_get_stereo_to_mono (int *ismono)
{
	int ret = MM_ERROR_NONE;

	/* Check input param */
	if (ismono == NULL) {
		debug_error("invalid argument\n");
		return MM_ERROR_INVALID_ARGUMENT;
	}

	ret = __mm_sound_get_stereo_to_mono(ismono);
	debug_msg("returned ismono = %d", *ismono);

	return ret;
}


///////////////////////////////////
////     MMSOUND PLAY APIs
///////////////////////////////////
static inline void _mm_sound_fill_play_param(MMSoundPlayParam *param, const char *filename, int volume_config, mm_sound_stop_callback_func callback, void *data, int priority, int handle_route)
{
	param->filename = filename;
	param->volume = 0; //volume value dose not effect anymore
	param->callback = callback;
	param->data = data;
	param->loop = 1;
	param->volume_config = volume_config;
	param->priority = priority;
	param->handle_route = handle_route;
}

EXPORT_API
int mm_sound_play_loud_solo_sound_no_restore(const char *filename, int volume_config, mm_sound_stop_callback_func callback, void *data, int *handle)
{
	MMSoundPlayParam param = { 0, };

	_mm_sound_fill_play_param(&param, filename, volume_config, callback, data, HANDLE_PRIORITY_SOLO, MM_SOUND_HANDLE_ROUTE_SPEAKER_NO_RESTORE);
	return mm_sound_play_sound_ex(&param, handle);
}

EXPORT_API
int mm_sound_play_loud_solo_sound(const char *filename, int volume_config, mm_sound_stop_callback_func callback, void *data, int *handle)
{
	MMSoundPlayParam param = { 0, };

	_mm_sound_fill_play_param(&param, filename, volume_config, callback, data, HANDLE_PRIORITY_SOLO, MM_SOUND_HANDLE_ROUTE_SPEAKER);
	return mm_sound_play_sound_ex(&param, handle);
}

EXPORT_API
int mm_sound_play_solo_sound(const char *filename, int volume_config, mm_sound_stop_callback_func callback, void *data, int *handle)
{
	MMSoundPlayParam param = { 0, };

	_mm_sound_fill_play_param(&param, filename, volume_config, callback, data, HANDLE_PRIORITY_SOLO, MM_SOUND_HANDLE_ROUTE_USING_CURRENT);
	return mm_sound_play_sound_ex(&param, handle);
}

EXPORT_API
int mm_sound_play_sound_without_session(const char *filename, int volume_config, mm_sound_stop_callback_func callback, void *data, int *handle)
{
	MMSoundPlayParam param = { 0, };

	_mm_sound_fill_play_param(&param, filename, volume_config, callback, data, HANDLE_PRIORITY_SOLO, MM_SOUND_HANDLE_ROUTE_USING_CURRENT);
	param.skip_session = true;
	return mm_sound_play_sound_ex(&param, handle);
}

EXPORT_API
int mm_sound_play_sound(const char *filename, int volume_config, mm_sound_stop_callback_func callback, void *data, int *handle)
{
	MMSoundPlayParam param = { 0, };

	_mm_sound_fill_play_param(&param, filename, volume_config, callback, data, HANDLE_PRIORITY_NORMAL, MM_SOUND_HANDLE_ROUTE_USING_CURRENT);
	return mm_sound_play_sound_ex(&param, handle);
}

EXPORT_API
int mm_sound_play_sound_ex(MMSoundPlayParam *param, int *handle)
{
	int err;
	int lhandle = -1;
	int volume_type = 0;
	/* Check input param */
	if (param == NULL) {
		debug_error("param is null\n");
		return MM_ERROR_INVALID_ARGUMENT;
	}

	volume_type = MM_SOUND_VOLUME_CONFIG_TYPE(param->volume_config);

	if (param->filename == NULL) {
		debug_error("filename is NULL\n");
		return MM_ERROR_SOUND_FILE_NOT_FOUND;
	}
	if (volume_type < 0 || volume_type >= VOLUME_TYPE_MAX) {
		debug_error("Volume type is invalid %d\n", volume_type);
		return MM_ERROR_INVALID_ARGUMENT;
	}

	debug_warning ("play sound : priority=[%d], handle_route=[%d]\n", param->priority, param->handle_route);

	/* Play sound */
	err = MMSoundClientPlaySound(param, 0, 0, &lhandle);
	if (err < 0) {
		debug_error("Failed to play sound\n");
		return err;
	}

	/* Set handle to return */
	if (handle) {
		*handle = lhandle;
	} else {
		debug_critical("The sound hadle cannot be get [%d]\n", lhandle);
	}

	debug_warning ("success : handle=[%p]\n", handle);

	return MM_ERROR_NONE;
}


EXPORT_API
int mm_sound_stop_sound(int handle)
{
	int err;

	debug_warning ("enter : handle=[%p]\n", handle);
	/* Stop sound */
	err = MMSoundClientStopSound(handle);
	if (err < 0) {
		debug_error("Fail to stop sound\n");
		return err;
	}
	debug_warning ("success : handle=[%p]\n", handle);

	return MM_ERROR_NONE;
}

///////////////////////////////////
////     MMSOUND TONE APIs
///////////////////////////////////
EXPORT_API
int mm_sound_play_tone_ex (MMSoundTone_t num, int volume_config, const double volume, const int duration, int *handle, bool enable_session)
{
	int lhandle = -1;
	int err = MM_ERROR_NONE;
	int volume_type = MM_SOUND_VOLUME_CONFIG_TYPE(volume_config);

	debug_fenter();

	/* Check input param */
	if (duration < -1) {
		debug_error("number is invalid %d\n", duration);
		return MM_ERROR_INVALID_ARGUMENT;
	}
	if (num < MM_SOUND_TONE_DTMF_0 || num >= MM_SOUND_TONE_NUM) {
		debug_error("TONE Value is invalid %d\n", num);
		return MM_ERROR_INVALID_ARGUMENT;
	}
	if (volume_type < 0 || volume_type >= VOLUME_TYPE_MAX) {
		debug_error("Volume type is invalid %d\n", volume_type);
		return MM_ERROR_INVALID_ARGUMENT;
	}
	if (volume < 0.0 || volume > 1.0) {
		debug_error("Volume Value is invalid %d\n", volume);
		return MM_ERROR_INVALID_ARGUMENT;
	}

	/* Play tone */
	debug_msg("Call MMSoundClientPlayTone\n");
	err = MMSoundClientPlayTone(num, volume_config, volume, duration, &lhandle, enable_session);
	if (err < 0) {
		debug_error("Failed to play sound\n");
		return err;
	}

	/* Set handle to return */
	if (handle)
		*handle = lhandle;
	else
		debug_critical("The sound handle cannot be get [%d]\n", lhandle);

	debug_fleave();
	return MM_ERROR_NONE;
}

EXPORT_API
int mm_sound_play_tone (MMSoundTone_t num, int volume_config, const double volume, const int duration, int *handle)
{
	return mm_sound_play_tone_ex (num, volume_config, volume, duration, handle, true);
}

///////////////////////////////////
////     MMSOUND ROUTING APIs
///////////////////////////////////
#ifdef PULSE_CLIENT
enum {
	USE_PA_SINK_ALSA = 0,
	USE_PA_SINK_A2DP,
};
EXPORT_API
int mm_sound_route_set_system_policy (system_audio_route_t route)
{
	return MM_ERROR_NONE;
}


EXPORT_API
int mm_sound_route_get_system_policy (system_audio_route_t *route)
{
	return MM_ERROR_NONE;
}


EXPORT_API
int mm_sound_route_get_a2dp_status (bool *connected, char **bt_name)
{
	int ret = MM_ERROR_NONE;

	if (connected == NULL || bt_name == NULL) {
		debug_error ("argument is not valid\n");
		return MM_ERROR_INVALID_ARGUMENT;
	}

	ret = MMSoundClientIsBtA2dpOn (connected, bt_name);
	debug_msg ("connected=[%d] bt_name[%s]\n", *connected, *bt_name);
	if (ret < 0) {
		debug_error("MMSoundClientIsBtA2dpOn() Failed\n");
		return ret;
	}

	return ret;
}

EXPORT_API
int mm_sound_route_get_playing_device(system_audio_route_device_t *dev)
{
	mm_sound_device_in device_in = MM_SOUND_DEVICE_IN_NONE;
	mm_sound_device_out device_out = MM_SOUND_DEVICE_OUT_NONE;

	if(!dev)
		return MM_ERROR_INVALID_ARGUMENT;

	if(MM_ERROR_NONE != mm_sound_get_active_device(&device_in, &device_out)) {
		debug_error("Can not get active device info\n");
		return MM_ERROR_SOUND_INTERNAL;
	}

	switch(device_out)
	{
	case MM_SOUND_DEVICE_OUT_SPEAKER:
		*dev = SYSTEM_AUDIO_ROUTE_PLAYBACK_DEVICE_HANDSET;
		break;
	case MM_SOUND_DEVICE_OUT_BT_A2DP:
		*dev = SYSTEM_AUDIO_ROUTE_PLAYBACK_DEVICE_BLUETOOTH;
		break;
	case MM_SOUND_DEVICE_OUT_WIRED_ACCESSORY:
		*dev = SYSTEM_AUDIO_ROUTE_PLAYBACK_DEVICE_EARPHONE;
		break;
	default:
		*dev = SYSTEM_AUDIO_ROUTE_PLAYBACK_DEVICE_NONE;
		break;
	}

	return MM_ERROR_NONE;
}

EXPORT_API
int mm_sound_route_add_change_callback(audio_route_policy_changed_callback_fn func, void *user_data)
{
	/* Deprecated */
	return MM_ERROR_NONE;
}

EXPORT_API
int mm_sound_route_remove_change_callback(void)
{
	/* Deprecated */
	return MM_ERROR_NONE;
}

#endif /* PULSE_CLIENT */

EXPORT_API
int mm_sound_system_get_capture_status(system_audio_capture_status_t *status)
{
	int err = MM_ERROR_NONE;
	int on_capture = 0;

	if(!status) {
		debug_error("invalid argument\n");
		return MM_ERROR_INVALID_ARGUMENT;
	}

	/*  Check whether sound is capturing */
	vconf_get_int(VCONFKEY_SOUND_CAPTURE_STATUS, &on_capture); // need to check where it is set

	if(on_capture)
		*status = SYSTEM_AUDIO_CAPTURE_ACTIVE;
	else
		*status = SYSTEM_AUDIO_CAPTURE_NONE;

	return MM_ERROR_NONE;
}

EXPORT_API
int mm_sound_is_route_available(mm_sound_route route, bool *is_available)
{
	int ret = MM_ERROR_NONE;

	debug_warning ("enter : route=[%x], is_available=[%p]\n", route, is_available);

	if (!_mm_sound_is_route_valid(route)) {
		debug_error("route is invalid %d\n", route);
		return MM_ERROR_INVALID_ARGUMENT;
	}
	if (!is_available) {
		debug_error("is_available is invalid\n");
		return MM_ERROR_INVALID_ARGUMENT;
	}

	ret = _mm_sound_client_is_route_available(route, is_available);
	if (ret < 0) {
		debug_error("Can not check given route is available, ret = %x\n", ret);
	} else {
		debug_warning ("success : route=[%x], available=[%d]\n", route, *is_available);
	}

	return ret;
}

EXPORT_API
int mm_sound_foreach_available_route_cb(mm_sound_available_route_cb available_route_cb, void *user_data)
{
	int ret = MM_ERROR_NONE;

	if (!available_route_cb) {
		debug_error("available_route_cb is invalid\n");
		return MM_ERROR_INVALID_ARGUMENT;
	}

	ret = _mm_sound_client_foreach_available_route_cb(available_route_cb, user_data);
	if (ret < 0) {
		debug_error("Can not set foreach available route callback, ret = %x\n", ret);
	}

	return ret;
}

EXPORT_API
int mm_sound_set_active_route(mm_sound_route route)
{
	int ret = MM_ERROR_NONE;

	debug_warning ("enter : route=[%x]\n", route);
	if (!_mm_sound_is_route_valid(route)) {
		debug_error("route is invalid %d\n", route);
		return MM_ERROR_INVALID_ARGUMENT;
	}

	ret = _mm_sound_client_set_active_route(route, true);
	if (ret < 0) {
		debug_error("Can not set active route, ret = %x\n", ret);
	} else {
		debug_warning ("success : route=[%x]\n", route);
	}

	return ret;
}

EXPORT_API
int mm_sound_set_active_route_auto(void)
{
	int ret = MM_ERROR_NONE;

	ret = _mm_sound_client_set_active_route_auto();
	if (ret < 0) {
		debug_error("fail to set active route auto, ret = %x\n", ret);
	} else {
		debug_msg ("success !!\n");
	}

	return ret;
}

EXPORT_API
int mm_sound_set_active_route_without_broadcast(mm_sound_route route)
{
	int ret = MM_ERROR_NONE;

	debug_warning ("enter : route=[%x]\n", route);
	if (!_mm_sound_is_route_valid(route)) {
		debug_error("route is invalid %d\n", route);
		return MM_ERROR_INVALID_ARGUMENT;
	}

	ret = _mm_sound_client_set_active_route(route, false);
	if (ret < 0) {
		debug_error("Can not set active route, ret = %x\n", ret);
	} else {
		debug_warning ("success : route=[%x]\n", route);
	}

	return ret;
}

EXPORT_API
int mm_sound_get_active_device(mm_sound_device_in *device_in, mm_sound_device_out *device_out)
{
	int ret = MM_ERROR_NONE;

	if (device_in == NULL || device_out == NULL) {
		debug_error("argument is not valid\n");
		return MM_ERROR_INVALID_ARGUMENT;
	}

	ret = _mm_sound_client_get_active_device(device_in, device_out);
	if (ret < 0) {
		debug_error("Can not add active device callback, ret = %x\n", ret);
	} else {
		debug_msg ("success : in=[%x], out=[%x]\n", *device_in, *device_out);
	}

	return ret;
}

EXPORT_API
int mm_sound_get_audio_path(mm_sound_device_in *device_in, mm_sound_device_out *device_out)
{
	int ret = MM_ERROR_NONE;

	if (device_in == NULL || device_out == NULL) {
		debug_error("argument is not valid\n");
		return MM_ERROR_INVALID_ARGUMENT;
	}

	ret = _mm_sound_client_get_audio_path(device_in, device_out);
	if (ret < 0) {
		debug_error("Can not add active device callback, ret = %x\n", ret);
	} else {
		debug_msg ("success : in=[%x], out=[%x]\n", *device_in, *device_out);
	}

	return ret;
}

EXPORT_API
int mm_sound_add_active_device_changed_callback(const char *name, mm_sound_active_device_changed_cb func, void *user_data)
{
	int ret = MM_ERROR_NONE;

	debug_warning ("enter %s\n", name);
	if (func == NULL) {
		debug_error("argument is not valid\n");
		return MM_ERROR_INVALID_ARGUMENT;
	}

	ret = _mm_sound_client_add_active_device_changed_callback(name, func, user_data);
	if (ret < 0) {
		debug_error("Can not add active device changed callback, ret = %x\n", ret);
	}

	return ret;
}

EXPORT_API
int mm_sound_remove_active_device_changed_callback(const char *name)
{
	int ret = MM_ERROR_NONE;

	debug_warning ("enter name %s \n", name);
	ret = _mm_sound_client_remove_active_device_changed_callback(name);
	if (ret < 0) {
		debug_error("Can not remove active device changed callback, ret = %x\n", ret);
	}

	return ret;
}

EXPORT_API
int mm_sound_add_available_route_changed_callback(mm_sound_available_route_changed_cb func, void *user_data)
{
	int ret = MM_ERROR_NONE;

	if (func == NULL) {
		debug_error("argument is not valid\n");
		return MM_ERROR_INVALID_ARGUMENT;
	}

	ret = _mm_sound_client_add_available_route_changed_callback(func, user_data);
	if (ret < 0) {
		debug_error("Can not add available route changed callback, ret = %x\n", ret);
	}

	return ret;
}

EXPORT_API
int mm_sound_remove_available_route_changed_callback(void)
{
	int ret = MM_ERROR_NONE;

	ret = _mm_sound_client_remove_available_route_changed_callback();
	if (ret < 0) {
		debug_error("Can not remove available route changed callback, ret = %x\n", ret);
	}

	return ret;
}

EXPORT_API
int mm_sound_set_sound_path_for_active_device(mm_sound_device_out device_out, mm_sound_device_in device_in)
{
	int ret = MM_ERROR_NONE;

	ret = _mm_sound_client_set_sound_path_for_active_device(device_out, device_in);
	if (ret < 0) {
		debug_error("Can not mm sound set sound path for active device, ret = %x\n", ret);
	}

	return ret;
}

__attribute__ ((destructor))
void __mmfsnd_finalize(void)
{
	MMSoundClientCallbackFini();
}

__attribute__ ((constructor))
void __mmfsnd_initialize(void)
{
	/* Will be Fixed */
}
