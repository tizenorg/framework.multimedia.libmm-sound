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

/**
 * @file		mm_sound.h
 * @brief		Application interface library for sound module.
 * @date
 * @version		Release
 *
 * Application interface library for sound module.
 */

#ifndef	__MM_SOUND_H__
#define	__MM_SOUND_H__

#include <mm_types.h>
#include <mm_error.h>
#include <mm_message.h>

#ifdef __cplusplus
	extern "C" {
#endif

/**
	@addtogroup SOUND
	@{
	@par
	This part is describes the sound module of multimedia framework. Sound
	module provides APIs to implement play wav file with simple api, to handle volume information,
	to handle audio route policy.

	@par
	There is six different volume type for normal usage. application should set proper volume type to multimedia playback APIs.
	<div> <table>
	<tr>
	<td><B>Type</B></td>
	<td><B>Description</B></td>
	</tr>
	<tr>
	<td>VOLUME_TYPE_SYSTEM</td>
	<td>volume for normal system sound (e.g. keysound, camera shutter)</td>
	</tr>
	<tr>
	<td>VOLUME_TYPE_NOTIFICATION</td>
	<td>volume for notification (e.g. message, email notification)</td>
	</tr>
	<tr>
	<td>VOLUME_TYPE_RINGTONE</td>
	<td>volume for incoming call ring</td>
	</tr>
	<tr>
	<td>VOLUME_TYPE_MEDIA</td>
	<td>volume for media playback (e.g. music, video playback)</td>
	</tr>
	<tr>
	<td>VOLUME_TYPE_CALL</td>
	<td>volume for call</td>
	</tr>
	</table> </div>

	@par
	application can change audio route policy with mm-sound API.
	Audio route is input and output of audio stream.

	@par
	@image html		audio_device.png	"Figure1. Audio Devices of mobile phone"	width=12cm
	@image latex	audio_device.png	"Figure1. Audio Devices of mobile phone"	width=12cm

	@par
	Default audio route policy is like follows
	@par
	for playback
	<div><table>
	<tr>
	<td><B>Bluetooth headset</B></td>
	<td><B>Wired headset</B></td>
	<td><B>Playback Device</B></td>
	</tr>
	<tr>
	<td>connected</td>
	<td>plugged</td>
	<td>Bluetooth headset</td>
	</tr>
	<tr>
	<td>connected</td>
	<td>unplugged</td>
	<td>Bluetooth headset</td>
	</tr>
	<tr>
	<td>disconnected</td>
	<td>plugged</td>
	<td>Wired headset</td>
	</tr>
	<tr>
	<td>disconnected</td>
	<td>unplugged</td>
	<td>Loud speaker</td>
	</tr>
	</table></div>

	@par
	for capture (bluetooth headset mic used only in call mode)
	<div><table>
	<tr>
	<td><B>Bluetooth headset mic</B></td>
	<td><B>Wired headset mic</B></td>
	<td><B>Capture Device</B></td>
	</tr>
	<tr>
	<td>connected</td>
	<td>plugged</td>
	<td>Wired headset mic</td>
	</tr>
	<tr>
	<td>connected</td>
	<td>unplugged</td>
	<td>microphone</td>
	</tr>
	<tr>
	<td>disconnected</td>
	<td>plugged</td>
	<td>Wired headset mic</td>
	</tr>
	<tr>
	<td>disconnected</td>
	<td>unplugged</td>
	<td>Wired headset mic</td>
	</tr>
	</table></div>

	@par
	If application changes routing policy to SYSTEM_AUDIO_ROUTE_POLICY_IGNORE_A2DP with mm_sound_route_set_system_policy
	audio routing policy has changed to ignore bluetooth headset connection.
	@par
	for playback
	<div><table>
	<tr>
	<td><B>Bluetooth headset</B></td>
	<td><B>Wired headset</B></td>
	<td><B>Playback Device</B></td>
	</tr>
	<tr>
	<td>connected</td>
	<td>plugged</td>
	<td>Wired headset</td>
	</tr>
	<tr>
	<td>connected</td>
	<td>unplugged</td>
	<td>Loud speaker</td>
	</tr>
	<tr>
	<td>disconnected</td>
	<td>plugged</td>
	<td>Wired headset</td>
	</tr>
	<tr>
	<td>disconnected</td>
	<td>unplugged</td>
	<td>Loud speaker</td>
	</tr>
	</table></div>

	@par
	for capture (bluetooth headset mic used only in call mode)
	<div><table>
	<tr>
	<td><B>Bluetooth headset mic</B></td>
	<td><B>Wired headset mic</B></td>
	<td><B>Capture Device</B></td>
	</tr>
	<tr>
	<td>connected</td>
	<td>plugged</td>
	<td>Wired headset mic</td>
	</tr>
	<tr>
	<td>connected</td>
	<td>unplugged</td>
	<td>microphone</td>
	</tr>
	<tr>
	<td>disconnected</td>
	<td>plugged</td>
	<td>Wired headset mic</td>
	</tr>
	<tr>
	<td>disconnected</td>
	<td>unplugged</td>
	<td>Wired headset mic</td>
	</tr>
	</table></div>

	@par
	If application changes routing policy to SYSTEM_AUDIO_ROUTE_POLICY_HANDSET_ONLY with mm_sound_route_set_system_policy
	audio routing policy has changed to use only loud speaker and microphone.
	@par
	for playback
	<div><table>
	<tr>
	<td><B>Bluetooth headset</B></td>
	<td><B>Wired headset</B></td>
	<td><B>Playback Device</B></td>
	</tr>
	<tr>
	<td>connected</td>
	<td>plugged</td>
	<td>Loud speaker</td>
	</tr>
	<tr>
	<td>connected</td>
	<td>unplugged</td>
	<td>Loud speaker</td>
	</tr>
	<tr>
	<td>disconnected</td>
	<td>plugged</td>
	<td>Loud speaker</td>
	</tr>
	<tr>
	<td>disconnected</td>
	<td>unplugged</td>
	<td>Loud speaker</td>
	</tr>
	</table></div>

	@par
	for capture (bluetooth headset mic used only in call mode)
	<div><table>
	<tr>
	<td><B>Bluetooth headset mic</B></td>
	<td><B>Wired headset mic</B></td>
	<td><B>Capture Device</B></td>
	</tr>
	<tr>
	<td>connected</td>
	<td>plugged</td>
	<td>microphone</td>
	</tr>
	<tr>
	<td>connected</td>
	<td>unplugged</td>
	<td>microphone</td>
	</tr>
	<tr>
	<td>disconnected</td>
	<td>plugged</td>
	<td>microphone</td>
	</tr>
	<tr>
	<td>disconnected</td>
	<td>unplugged</td>
	<td>microphone</td>
	</tr>
	</table></div>

 */

/*
 * MMSound Volume APIs
 */

/**
 * Enumerations of Volume type.
 */

typedef enum {
	VOLUME_TYPE_SYSTEM,				/**< System volume type */
	VOLUME_TYPE_NOTIFICATION,		/**< Notification volume type */
	VOLUME_TYPE_ALARM,				/**< Alarm volume type */
	VOLUME_TYPE_RINGTONE,			/**< Ringtone volume type */
	VOLUME_TYPE_MEDIA,				/**< Media volume type */
	VOLUME_TYPE_CALL,				/**< Call volume type */
	VOLUME_TYPE_VOIP,				/**< VOIP volume type */
	VOLUME_TYPE_VOICE,				/**< VOICE volume type */
	VOLUME_TYPE_FIXED,				/**< Volume type for fixed acoustic level */
	VOLUME_TYPE_MAX,				/**< Volume type count */
	VOLUME_TYPE_EXT_ANDROID = VOLUME_TYPE_FIXED,		/**< External system volume for Android */
} volume_type_t;

typedef enum {
	VOLUME_GAIN_DEFAULT		= 0,
	VOLUME_GAIN_DIALER		= 1<<8,
	VOLUME_GAIN_TOUCH		= 2<<8,
	VOLUME_GAIN_AF			= 3<<8,
	VOLUME_GAIN_SHUTTER1	= 4<<8,
	VOLUME_GAIN_SHUTTER2	= 5<<8,
	VOLUME_GAIN_CAMCORDING	= 6<<8,
	VOLUME_GAIN_MIDI		= 7<<8,
	VOLUME_GAIN_BOOTING		= 8<<8,
	VOLUME_GAIN_VIDEO		= 9<<8,
	VOLUME_GAIN_TTS			= 10<<8,
} volume_gain_t;

/**
 * @brief Enumerations of supporting source_type
 */
typedef enum {
    SUPPORT_SOURCE_TYPE_DEFAULT,
    SUPPORT_SOURCE_TYPE_MIRRORING,
    SUPPORT_SOURCE_TYPE_VOICECONTROL,
    SUPPORT_SOURCE_TYPE_SVR,
    SUPPORT_SOURCE_TYPE_VIDEOCALL,
    SUPPORT_SOURCE_TYPE_VOICERECORDING,
    SUPPORT_SOURCE_TYPE_VOIP, /* Supporting VoIP source*/
    SUPPORT_SOURCE_TYPE_CALL_FORWARDING,
    SUPPORT_SOURCE_TYPE_FMRADIO,
    SUPPORT_SOURCE_TYPE_LOOPBACK,
} mm_sound_source_type_e;

/**
 * Volume change callback function type.
 *
 * @param	user_data		[in]	Argument passed when callback has called
 *
 * @return	No return value
 * @remark	None.
 * @see		mm_sound_volume_add_callback mm_sound_volume_remove_callback
 */
typedef void (*volume_callback_fn)(void* user_data);

/**
 * Active volume change callback function type.
 *
 * @param	type			[in]	The sound type of changed volume
 * @param	volume			[in]	The new volume value
 * @param	user_data		[in]	Argument passed when callback has called
 *
 * @return	No return value
 * @remark	None.
 * @see		mm_sound_add_volume_changed_callback mm_sound_remove_volume_changed_callback
 */
typedef void (*mm_sound_volume_changed_cb) (volume_type_t type, unsigned int volume, void *user_data);


/**
 * Muteall state change callback function type.
 *
 * @param	user_data		[in]	Argument passed when callback has called
 *
 * @return	No return value
 * @remark	None.
 * @see		mm_sound_muteall_add_callback mm_sound_muteall_remove_callback
 */
typedef void (*muteall_callback_fn)(void* user_data);

/**
 * This function is to retrieve number of volume level.
 *
 * @param	type			[in]	volume type to query
 * @param	step			[out]	number of volume steps
 *
 * @return 	This function returns MM_ERROR_NONE on success, or negative value
 *			with error code.
 * @remark	step means number of steps. so actual volume step can be 0 ~ step-1
 * @see		volume_type_t
 * @pre		None.
 * @post	None.
 * @par Example
 * @code
int step = 0;
int ret = 0;
int max = 0;

ret = mm_sound_volume_get_step(VOLUME_TYPE_SYSTEM, &step);
if(ret < 0)
{
	printf("Can not get volume step\n");
}
else
{
	max = step - 1;
	//set system volume to max value
	mm_sound_volume_set_value(VOLUME_TYPE_SYSTEM, max);
}
 * @endcode
 */
int mm_sound_volume_get_step(volume_type_t type, int *step);


/**
 * This function is to add volume changed callback.
 *
 * @param	type			[in]	volume type to set change callback function
 * @param	func			[in]	callback function pointer
 * @param	user_data		[in]	user data passing to callback function
 *
 * @return 	This function returns MM_ERROR_NONE on success, or negative value
 *			with error code.
 * @remark	Only one callback function per volume type will be registered.
 * 			if you want to change callback function for certain volume type,
 * 			remove callback first via mm_sound_volume_remove_callback().
 * @see		volume_type_t volume_callback_fn
 * @pre		There should be not be pre-registered callback fuction to given volume type.
 * @post	Callback function will be registered to given volume type
 * @par Example
 * @code
volume_type_t g_vol_type = VOLUME_TYPE_MEDIA;

void _volume_callback(void *data)
{
	unsigned int value = 0;
	int result = 0;
	volume_type_t *type = (volume_type_t*)data;

	result = mm_sound_volume_get_value(*type, &value);
	if(result == MM_ERROR_NONE)
	{
		printf("Current volume value is %d\n", value);
	}
	else
	{
		printf("Can not get volume\n");
	}
}

int volume_control()
{
	int ret = 0;

	ret = mm_sound_volume_add_callback(g_vol_type, _volume_callback, (void*)&g_vol_type);
	if ( MM_ERROR_NONE != ret)
	{
		printf("Can not add callback\n");
	}
	else
	{
		printf("Add callback success\n");
	}

	return 0;
}

 * @endcode
 */
int mm_sound_volume_add_callback(volume_type_t type, volume_callback_fn func, void* user_data);
int mm_sound_add_volume_changed_callback(mm_sound_volume_changed_cb func, void* user_data);


/**
 * This function is to remove volume changed callback.
 *
 * @param	type			[in]	volume type to set change callback function
 *
 * @return 	This function returns MM_ERROR_NONE on success, or negative value
 *			with error code.
 * @remark	None.
 * @pre		Callback function should be registered previously for given volume type.
 * @post	Callback function will not be called anymore.
 * @see		volume_type_t
 * @par Example
 * @code
void _volume_callback(void *data)
{
	printf("Callback function\n");
}

int volume_callback()
{
	int ret = 0;
	int vol_type = VOLUME_TYPE_MEDIA;

	mm_sound_volume_add_callback(vol_type, _volume_callback, NULL);

	ret = mm_sound_volume_remove_callback(vol_type);
	if ( MM_ERROR_NONE == ret)
	{
		printf("Remove callback success\n");
	}
	else
	{
		printf("Remove callback failed\n");
	}

	return ret;
}

 * @endcode
 */
int mm_sound_volume_remove_callback(volume_type_t type);

/**
 * This function is to remove volume change callback.
 *
 * @return 	This function returns MM_ERROR_NONE on success, or negative value
 * 			with error code.
 **/
int mm_sound_remove_volume_changed_callback(void);

/**
 * This function is to add muteall changed callback.
 *
 * @param	func			[in]	callback function pointer
 * @param	user_data		[in]	user data passing to callback function
 *
 * @return 	This function returns MM_ERROR_NONE on success, or negative value
 *			with error code.
 * @see		muteall_callback_fn
 * @code
void _muteall_callback(void *data)
{
	int  muteall;

	mm_sound_get_muteall(&muteall);
	g_print("Muteall Callback Runs :::: muteall value = %d\n", muteall);
}

int muteall_callback()
{
	int ret = 0;

	ret = mm_sound_muteall_add_callback( _muteall_callback);

	if ( MM_ERROR_NONE != ret)
	{
		printf("Can not add callback\n");
	}
	else
	{
		printf("Add callback success\n");
	}

	return 0;
}

 * @endcode
 */
int mm_sound_muteall_add_callback(muteall_callback_fn func);


/**
 * This function is to remove muteall changed callback.
 *
 * @param	func			[in]	callback function pointer
 *
 * @return 	This function returns MM_ERROR_NONE on success, or negative value
 *			with error code.
 * @remark	None.
 * @post	Callback function will not be called anymore.
 * @see		muteall_callback_fn
 * @code
void _muteall_callback(void *data)
{
	printf("Callback function\n");
}

int muteall_callback()
{
	int ret = 0;

	mm_sound_muteall_add_callback( _muteall_callback);

	ret = mm_sound_muteall_remove_callback(_muteall_callback);
	if ( MM_ERROR_NONE == ret)
	{
		printf("Remove callback success\n");
	}
	else
	{
		printf("Remove callback failed\n");
	}

	return ret;
}

 * @endcode
 */
int mm_sound_muteall_remove_callback(muteall_callback_fn func);

/**
 * This function is to set volume level of certain volume type.
 *
 * @param	type			[in]	volume type to set value.
 * @param	value			[in]	volume value.
 *
 * @return 	This function returns MM_ERROR_NONE on success, or negative value
 *			with error code.
 * @remark	value should be 0 ~ mm_sound_volume_get_step() -1
 * @see		mm_sound_volume_get_step, mm_sound_volume_get_value volume_type_t
 * @pre		None.
 * @post	Volume value will be changed to given value for given volume type.
 * @par Example
 * @code
int step = 0;
int ret = 0;
int max = 0;

ret = mm_sound_volume_get_step(VOLUME_TYPE_SYSTEM, &step);
if(ret < 0)
{
	printf("Can not get volume step\n");
}
else
{
	max = step - 1;
	//set system volume to max value
	ret = mm_sound_volume_set_value(VOLUME_TYPE_SYSTEM, max);
	if(ret < 0)
	{
		printf("Can not set volume value\n");
	}
}
 * @endcode
 */
int mm_sound_volume_set_value(volume_type_t type, const unsigned int value);






/**
 * This function is to set all volume type to mute or unmute.
 *
 * @param	muteall			[in]	the switch to control  mute or unmute.
 *
 * @return 	This function returns MM_ERROR_NONE on success, or negative value
 *			with error code.
 * @remark	None.
 * @pre		None.
 * @post	None.
 */
int mm_sound_mute_all(int muteall);



/**
 * This function is to get volume level of certain volume type.
 *
 * @param	type			[in]	volume type to get value.
 * @param	value			[out]	volume value.
 *
 * @return 	This function returns MM_ERROR_NONE on success, or negative value
 *			with error code.
 * @remark	None.
 * @pre		None.
 * @post	None.
 * @see		volume_type_t mm_sound_volume_set_value
 * @par Example
 * @code
int value = 0;
int ret = 0;

ret = mm_sound_volume_get_value(VOLUME_TYPE_SYSTEM, &value);
if(ret < 0)
{
	printf("Can not get volume\n");
}
else
{
	printf("System type volume is %d\n", value);
}
 * @endcode
 * @see		mm_sound_volume_set_value
 */
int mm_sound_volume_get_value(volume_type_t type, unsigned int *value);



/**
 * This function is to set primary volume type.
 *
 * @param	type			[in]	volume type to set as primary volume type.
 *
 * @return 	This function returns MM_ERROR_NONE on success, or negative value
 *			with error code.
 * @remark	Application should use this function during foreground.
 * 			Application should clear primary volume type by mm_sound_volume_primary_type_clear() when it goes background.
 * @pre		None.
 * @post	Volume app. will be update given volume type when H/W volume control key pressed.
 * @see		mm_sound_volume_primary_type_clear volume_type_t
 * @par Example
 * @code
static int _resume(void *data)
{
	int ret = 0;

	ret = mm_sound_volume_primary_type_set(VOLUME_TYPE_MEDIA);
	if(ret < 0)
	{
		printf("Can not set primary volume type\n");
	}
	...
}

static int _pause(void* data)
{
	int ret = 0;

	ret = mm_sound_volume_primary_type_clear();
	if(ret < 0)
	{
		printf("Can not clear primary volume type\n");
	}
	...
}

int main()
{
	...
	struct appcore_ops ops = {
		.create = _create,
		.terminate = _terminate,
		.pause = _pause,
		.resume = _resume,
		.reset = _reset,
	};
	...
	return appcore_efl_main(PACKAGE, ..., &ops);
}
 * @endcode
 */
int mm_sound_volume_primary_type_set(volume_type_t type);



/**
 * This function is to clear primary volume type.
 *
 *
 * @return 	This function returns MM_ERROR_NONE on success, or negative value
 *			with error code.
 * @remark	mm_sound_volume_primary_type_set() and mm_sound_volume_primary_type_clear() should be used as pair
 * @see		mm_sound_volume_primary_type_set
 * @pre		primary volume should be set at same process.
 * @post	primary volume will be cleared.
 * @par Example
 * @code
static int _resume(void *data)
{
	int ret = 0;

	ret = mm_sound_volume_primary_type_set(VOLUME_TYPE_MEDIA);
	if(ret < 0)
	{
		printf("Can not set primary volume type\n");
	}
	...
}

static int _pause(void* data)
{
	int ret = 0;

	ret = mm_sound_volume_primary_type_clear();
	if(ret < 0)
	{
		printf("Can not clear primary volume type\n");
	}
	...
}

int main()
{
	...
	struct appcore_ops ops = {
		.create = _create,
		.terminate = _terminate,
		.pause = _pause,
		.resume = _resume,
		.reset = _reset,
	};
	...
	return appcore_efl_main(PACKAGE, ..., &ops);
}
 * @endcode
 */
int mm_sound_volume_primary_type_clear(void);



/**
 * This function is to get current playing volume type
 *
 * @param	type			[out]	current playing volume type
 *
 * @return 	This function returns MM_ERROR_NONE on success,
 *          or MM_ERROR_SOUND_VOLUME_NO_INSTANCE when there is no existing playing instance,
 *          or MM_ERROR_SOUND_VOLUME_CAPTURE_ONLY when only capture audio instances are exist.
 *          or negative value with error code for other errors.
 * @remark	None.
 * @pre		None.
 * @post	None.
 * @see		mm_sound_volume_get_value, mm_sound_volume_set_value
 * @par Example
 * @code
int ret=0;
volume_type_t type = 0;

ret = mm_sound_volume_get_current_playing_type(&type);
switch(ret)
{
case MM_ERROR_NONE:
	printf("Current playing is %d\n", type);
	break;
case MM_ERROR_SOUND_VOLUME_NO_INSTANCE:
	printf("No sound instance\n");
	break;
case MM_ERROR_SOUND_VOLUME_CAPTURE_ONLY:
	printf("Only sound capture instances are exist\n");
	break;
default:
	printf("Error\n");
	break;
}

 * @endcode
 */
int mm_sound_volume_get_current_playing_type(volume_type_t *type);

int mm_sound_volume_set_balance (float balance);

int mm_sound_volume_get_balance (float *balance);

int mm_sound_set_muteall (int muteall);

int mm_sound_get_muteall (int *muteall);

int mm_sound_set_stereo_to_mono (int ismono);

int mm_sound_get_stereo_to_mono (int *ismono);

int mm_sound_set_call_mute(volume_type_t type, int mute);

int mm_sound_get_call_mute(volume_type_t type, int *mute);

int mm_sound_set_factory_loopback_test(int loopback);

int mm_sound_get_factory_loopback_test(int *loopback);

typedef enum {
	MM_SOUND_FACTORY_MIC_TEST_STATUS_OFF = 0,
	MM_SOUND_FACTORY_MIC_TEST_STATUS_MAIN_MIC,
	MM_SOUND_FACTORY_MIC_TEST_STATUS_SUB_MIC,
	MM_SOUND_FACTORY_MIC_TEST_STATUS_NUM,
} mm_sound_factory_mic_test_status_t;/* device in for factory mic test */

int mm_sound_set_factory_mic_test(mm_sound_factory_mic_test_status_t mic_test);

int mm_sound_get_factory_mic_test(mm_sound_factory_mic_test_status_t *mic_test);

typedef enum {
	MMSOUND_DHA_OFF,
	MMSOUND_DHA_SOFT_SOUND,
	MMSOUND_DHA_CLEAR_SOUND,
	MMSOUND_DHA_PERSNOL_LEFT,
	MMSOUND_DHA_PERSNOL_RIGHT,
	MMSOUND_DHA_INVALID,
} MMSoundDHAMode_t;



/*
 * MMSound PCM APIs
 */
typedef void*	MMSoundPcmHandle_t;	/**< MMsound PCM handle type */

/**
 * Enumerations of Format used in MMSoundPcm operation.
 */
typedef enum {
	MMSOUND_PCM_U8 = 0x70, /**< unsigned 8bit audio */
	MMSOUND_PCM_S16_LE,   /**< signed 16bit audio */
} MMSoundPcmFormat_t;

/**
 * Enumerations of Channel count used in MMSoundPcm operation.
 */
typedef enum {
	MMSOUND_PCM_MONO = 0x80,	/**< Mono channel */
	MMSOUND_PCM_STEREO,			/**< Stereo channel */
}MMSoundPcmChannel_t;

/**
 * Get audio stream latency value.
 *
 * @param	handle			[in]	handle to get latency
 * @param	latency			[out]	Stream latency value(millisecond).
 *
 * @return	This function returns MM_ERROR_NONE on success, or negative value
 *			with error code.
 * @remark
 * @see
 */
int mm_sound_pcm_get_latency(MMSoundPcmHandle_t handle, int *latency);

/**
 * Get started status of pcm stream.
 *
 * @param	handle			[in]	handle to check pcm start
 * @param	is_started			[out]	retrieve started status of pcm stream.
 *
 * @return	This function returns MM_ERROR_NONE on success, or negative value
 *			with error code.
 * @remark
 * @see
 */
int mm_sound_pcm_is_started(MMSoundPcmHandle_t handle, bool *is_started);

int mm_sound_pcm_play_open_no_session(MMSoundPcmHandle_t *handle, const unsigned int rate, MMSoundPcmChannel_t channel, MMSoundPcmFormat_t format, int volume_config);

/**
 * This function is to create handle for PCM playback.
 *
 * @param	handle			[out] handle to play pcm data
 * @param	rate			[in] sample rate (8000Hz ~ 44100Hz)
 * @param	channel			[in] number of channels (mono or stereo)
 * @param	format			[in] S8 or S16LE
 * @param	volume config	[in] Volume type & volume gain
 *
 * @return	This function returns suggested buffer size (in bytes) on success, or negative value
 *			with error code.
 * @remark	use mm_sound_volume_set_value() function to change volume
 * @see		mm_sound_pcm_play_write, mm_sound_pcm_play_close, mm_sound_volume_set_value, MMSoundPcmFormat_t, MMSoundPcmChannel_t volume_type_t
 * @pre		None.
 * @post	PCM play handle will be created.
 * @par Example
 * @code
#include <mm_sound.h>
#include <stdio.h>
#include <alloca.h>

int main(int argc, char* argv[])
{
	FILE *fp = NULL;
	char *buffer = NULL;
	int ret = 0;
	int size = 0;
	int readed = 0;
	MMSoundPcmHandle_t handle;
	char *filename = NULL;

	if(argc !=2 )
	{
		printf("Usage) %s filename\n", argv[0]);
		return -1;
	}
	filename = argv[1];

	fp = fopen(filename,"r");
	if(fp ==NULL)
	{
		printf("Can not open file %s\n", filename);
		return -1;
	}

	size = mm_sound_pcm_play_open(&handle, 44100, MMSOUND_PCM_STEREO, MMSOUND_PCM_S16_LE, VOLUME_TYPE_SYSTEM);
	if(size < 0)
	{
		printf("Can not open playback handle\n");
		return -2;
	}

	buffer = alloca(size);
	while((readed = fread(buffer, size, sizeof(char), fp)) > 0 )
	{
		ret = mm_sound_pcm_play_write(handle, (void*)buffer, readed);
		if(ret < 0)
		{
			printf("write fail\n");
			break;
		}
		memset(buffer, '\0', sizeof(buffer));
	}

	fclose(fp);
	mm_sound_pcm_play_close(handle);
	return 0;
}
 * @endcode
 */
int mm_sound_pcm_play_open(MMSoundPcmHandle_t *handle, const unsigned int rate, MMSoundPcmChannel_t channel, MMSoundPcmFormat_t format, int volume_config);

/**
 * This function start pcm playback
 *
 * @param	handle	[in] handle to start playback
 *
 * @return	This function returns MM_ERROR_NONE on success, or negative value
 *			with error code.
 * @remark
 * @see
 * @pre		PCM playback handle should be allocated.
 * @post	PCM playback is ready to write.
 */
int mm_sound_pcm_play_start(MMSoundPcmHandle_t handle);

/**
 * This function stop pcm playback
 *
 * @param	handle	[in] handle to stop playback
 *
 * @return	This function returns MM_ERROR_NONE on success, or negative value
 *			with error code.
 * @remark
 * @see
 * @pre		PCM playback handle should be allocated.
 * @post	PCM playback data will not be buffered.
 */
int mm_sound_pcm_play_stop(MMSoundPcmHandle_t handle);

/**
 * This function is to play PCM memory buffer.
 *
 * @param	handle	[in] handle to play pcm data
 * @param	ptr		[in] pcm buffer address
 * @param	length_byte	[in] size of pcm buffer (in bytes)
 *
 * @return	This function returns written data size on success, or negative value
 *			with error code.
 * @remark	Make pcm buffer size with returned value of mm_sound_pcm_play_open()
 * @see		mm_sound_pcm_play_open, mm_sound_pcm_play_close
 * @pre		PCM play handle should be created.
 * @post	Sound will be generated with given PCM buffer data.
 * @par Example
 * @code
#include <mm_sound.h>
#include <stdio.h>
#include <alloca.h>

int main(int argc, char* argv[])
{
	FILE *fp = NULL;
	char *buffer = NULL;
	int ret = 0;
	int size = 0;
	int readed = 0;
	MMSoundPcmHandle_t handle;
	char *filename = NULL;

	if(argc !=2 )
	{
		printf("Usage) %s filename\n", argv[0]);
		return -1;
	}
	filename = argv[1];

	fp = fopen(filename,"r");
	if(fp ==NULL)
	{
		printf("Can not open file %s\n", filename);
		return -1;
	}

	size = mm_sound_pcm_play_open(&handle, 44100, MMSOUND_PCM_STEREO, MMSOUND_PCM_S16_LE, VOLUME_TYPE_SYSTEM);
	if(size < 0)
	{
		printf("Can not open playback handle\n");
		return -2;
	}

	buffer = alloca(size);
	while((readed = fread(buffer, size, sizeof(char), fp)) > 0 )
	{
		ret = mm_sound_pcm_play_write(handle, (void*)buffer, readed);
		if(ret < 0)
		{
			printf("write fail\n");
			break;
		}
		memset(buffer, '\0', sizeof(buffer));
	}

	fclose(fp);
	mm_sound_pcm_play_close(handle);
	return 0;
}
 * @endcode
 */
int mm_sound_pcm_play_write(MMSoundPcmHandle_t handle, void* ptr, unsigned int length_byte);



/**
 * This function is to close PCM memory playback handle
 *
 * @param	handle	[in] handle to play pcm data
 *
 * @return	This function returns MM_ERROR_NONE on success, or negative value
 *			with error code.
 * @remark	None
 * @see		mm_sound_pcm_play_open, mm_sound_pcm_play_write
 * @pre		PCM play handle should be created
 * @post	PCM play handle will be terminated.
 * @par Example
 * @code
#include <mm_sound.h>
#include <stdio.h>
#include <alloca.h>

int main(int argc, char* argv[])
{
	FILE *fp = NULL;
	char *buffer = NULL;
	int ret = 0;
	int size = 0;
	int readed = 0;
	MMSoundPcmHandle_t handle;
	char *filename = NULL;

	if(argc !=2 )
	{
		printf("Usage) %s filename\n", argv[0]);
		return -1;
	}
	filename = argv[1];

	fp = fopen(filename,"r");
	if(fp ==NULL)
	{
		printf("Can not open file %s\n", filename);
		return -1;
	}

	size = mm_sound_pcm_play_open(&handle, 44100, MMSOUND_PCM_STEREO, MMSOUND_PCM_S16_LE, VOLUME_TYPE_SYSTEM);
	if(size < 0)
	{
		printf("Can not open playback handle\n");
		return -2;
	}

	buffer = alloca(size);
	while((readed = fread(buffer, size, sizeof(char), fp)) > 0 )
	{
		ret = mm_sound_pcm_play_write(handle, (void*)buffer, readed);
		if(ret < 0)
		{
			printf("write fail\n");
			break;
		}
		memset(buffer, '\0', sizeof(buffer));
	}

	fclose(fp);
	mm_sound_pcm_play_close(handle);
	return 0;
}
 * @endcode
 */
int mm_sound_pcm_play_close(MMSoundPcmHandle_t handle);

/**
 * This function is to ignore session for playback
 *
 * @param	handle	[in] handle to play pcm data
 *
 * @return	This function returns MM_ERROR_NONE on success, or negative value
 *			with error code.
 * @remark	This function only works for not started pcm handle and can't be reversed.
 * @see
 * @pre		PCM play handle should be created and not started.
 * @post	PCM play session will be set to mix.
 */
int mm_sound_pcm_play_ignore_session(MMSoundPcmHandle_t *handle);

/**
 * This function is to create handle for PCM capture.
 *
 * @param	handle	[out] handle to capture pcm data
 * @param	rate	[in] sample rate (8000Hz ~ 44100Hz)
 * @param	channel	[in] number of channels (mono or stereo)
 * @param	format	[in] S8 or S16LE
 *
 * @return	This function returns suggested buffer size (in bytes) on success, or negative value
 *			with error code.
 * @remark	only mono channel is valid for now.
 * @see		mm_sound_pcm_capture_read, mm_sound_pcm_capture_close, MMSoundPcmFormat_t, MMSoundPcmChannel_t
 * @pre		None.
 * @post	PCM capture handle will be allocated.
 * @par Example
 * @code
#include <mm_sound.h>
#include <stdio.h>
#include <alloca.h>

int main(int argc, char* argv[])
{
	FILE *fp = NULL;
	char *buffer = NULL;
	int ret = 0;
	int size = 0;
	int count = 0;
	MMSoundPcmHandle_t handle;
	char *filename = NULL;

	if(argc !=2 )
	{
		printf("Usage) %s filename\n", argv[0]);
		return -1;
	}
	filename = argv[1];

	fp = fopen(filename,"w");
	if(fp ==NULL)
	{
		printf("Can not open file %s\n", filename);
		return -1;
	}

	size = mm_sound_pcm_capture_open(&handle, 44100, MMSOUND_PCM_MONO, MMSOUND_PCM_S16_LE);
	if(size < 0)
	{
		printf("Can not open capture handle\n");
		return -2;
	}

	buffer = alloca(size);
	while(1)
	{
		ret = mm_sound_pcm_capture_read(handle, (void*)buffer, size);
		if(ret < 0)
		{
			printf("read fail\n");
			break;
		}
		fwrite(buffer, ret, sizeof(char), fp);
		if(count++ > 20) {
			break;
		}
	}

	fclose(fp);
	mm_sound_pcm_capture_close(handle);
	return 0;
}

 * @endcode
 */
int mm_sound_pcm_capture_open(MMSoundPcmHandle_t *handle, const unsigned int rate, MMSoundPcmChannel_t channel, MMSoundPcmFormat_t format);

/**
 * This function is to create handle for PCM capture of source_type.
 *
 * @param	handle	[out] handle to capture pcm data
 * @param	rate	[in] sample rate (8000Hz ~ 44100Hz)
 * @param	channel	[in] number of channels (mono or stereo)
 * @param	format	[in] S8 or S16LE
 * @param	source_type		[in]  The source_type,mm_sound_source_type_e
 *
 * @return	This function returns suggested buffer size (in bytes) on success, or negative value
 *			with error code.
 * @remark	only mono channel is valid for now.
 * @see		mm_sound_pcm_capture_read, mm_sound_pcm_capture_close, MMSoundPcmFormat_t, MMSoundPcmChannel_t
 * @pre		None.
 * @post	PCM capture handle will be allocated.
 * @par Example
 * @code
#include <mm_sound.h>
#include <stdio.h>
#include <alloca.h>

int main(int argc, char* argv[])
{
	FILE *fp = NULL;
	char *buffer = NULL;
	int ret = 0;
	int size = 0;
	int count = 0;
	MMSoundPcmHandle_t handle;
	char *filename = NULL;

	if(argc !=2 )
	{
		printf("Usage) %s filename\n", argv[0]);
		return -1;
	}
	filename = argv[1];

	fp = fopen(filename,"w");
	if(fp ==NULL)
	{
		printf("Can not open file %s\n", filename);
		return -1;
	}

	size = mm_sound_pcm_capture_open_ex(&handle, 44100, MMSOUND_PCM_MONO, MMSOUND_PCM_S16_LE,1);
	if(size < 0)
	{
		printf("Can not open capture handle\n");
		return -2;
	}

	buffer = alloca(size);
	while(1)
	{
		ret = mm_sound_pcm_capture_read(handle, (void*)buffer, size);
		if(ret < 0)
		{
			printf("read fail\n");
			break;
		}
		fwrite(buffer, ret, sizeof(char), fp);
		if(count++ > 20) {
			break;
		}
	}

	fclose(fp);
	mm_sound_pcm_capture_close(handle);
	return 0;
}

 * @endcode
 */
int mm_sound_pcm_capture_open_ex(MMSoundPcmHandle_t *handle, const unsigned int rate, MMSoundPcmChannel_t channel, MMSoundPcmFormat_t format, mm_sound_source_type_e source_type);

/**
 * This function start pcm capture
 *
 * @param	handle	[in] handle to start capture
 *
 * @return	This function returns read data size on success, or negative value
 *			with error code.
 * @remark
 * @see
 * @pre		PCM capture handle should be allocated.
 * @post	PCM capture data will be buffered.
 */
int mm_sound_pcm_capture_start(MMSoundPcmHandle_t handle);

/**
 * This function stop pcm capture
 *
 * @param	handle	[in] handle to stop capture
 *
 * @return	This function returns read data size on success, or negative value
 *			with error code.
 * @remark
 * @see
 * @pre		PCM capture handle should be allocated.
 * @post	PCM capture data will not be buffered.
 */
int mm_sound_pcm_capture_stop(MMSoundPcmHandle_t handle);

/**
 * This function captures PCM to memory buffer. (Samsung extension)
 *
 * @param	handle	[in] handle to play pcm data
 * @param	buffer	[in] pcm buffer address
 * @param	length	[in] size of pcm buffer (in bytes)
 *
 * @return	This function returns read data size on success, or negative value
 *			with error code.
 * @remark	Make pcm buffer size with returned value of mm_sound_pcm_capture_open()
 * @see		mm_sound_pcm_capture_open, mm_sound_pcm_capture_close
 * @pre		PCM capture handle should be allcated.
 * @post	PCM data will be filled to given memory pointer.
 * @par Example
 * @code
#include <mm_sound.h>
#include <stdio.h>
#include <alloca.h>

int main(int argc, char* argv[])
{
	FILE *fp = NULL;
	char *buffer = NULL;
	int ret = 0;
	int size = 0;
	int count = 0;
	MMSoundPcmHandle_t handle;
	char *filename = NULL;

	if(argc !=2 )
	{
		printf("Usage) %s filename\n", argv[0]);
		return -1;
	}
	filename = argv[1];

	fp = fopen(filename,"w");
	if(fp ==NULL)
	{
		printf("Can not open file %s\n", filename);
		return -1;
	}

	size = mm_sound_pcm_capture_open(&handle, 44100, MMSOUND_PCM_MONO, MMSOUND_PCM_S16_LE);
	if(size < 0)
	{
		printf("Can not open capture handle\n");
		return -2;
	}

	buffer = alloca(size);
	while(1)
	{
		ret = mm_sound_pcm_capture_read(handle, (void*)buffer, size);
		if(ret < 0)
		{
			printf("read fail\n");
			break;
		}
		fwrite(buffer, ret, sizeof(char), fp);
		if(count++ > 20) {
			break;
		}
	}

	fclose(fp);
	mm_sound_pcm_capture_close(handle);
	return 0;
}

 * @endcode
 */
int mm_sound_pcm_capture_read(MMSoundPcmHandle_t handle, void *buffer, const unsigned int length );

/**
 * This function captures PCM memory to memory buffer (Samsung extension)
 *
 * @param	handle	[in] handle to capture pcm data
 *
 * @return	This function returns MM_ERROR_NONE on success, or negative value
 *			with error code.
 * @remark	None.
 * @see		mm_sound_pcm_capture_open, mm_sound_pcm_capture_read
 * @pre		PCM capture handle should be opend.
 * @post	PCM capture handle will be freed.
 * @par Example
 * @code
#include <mm_sound.h>
#include <stdio.h>
#include <alloca.h>

int main(int argc, char* argv[])
{
	FILE *fp = NULL;
	char *buffer = NULL;
	int ret = 0;
	int size = 0;
	int count = 0;
	MMSoundPcmHandle_t handle;
	char *filename = NULL;

	if(argc !=2 )
	{
		printf("Usage) %s filename\n", argv[0]);
		return -1;
	}
	filename = argv[1];

	fp = fopen(filename,"w");
	if(fp ==NULL)
	{
		printf("Can not open file %s\n", filename);
		return -1;
	}

	size = mm_sound_pcm_capture_open(&handle, 44100, MMSOUND_PCM_MONO, MMSOUND_PCM_S16_LE);
	if(size < 0)
	{
		printf("Can not open capture handle\n");
		return -2;
	}

	buffer = alloca(size);
	while(1)
	{
		ret = mm_sound_pcm_capture_read(handle, (void*)buffer, size);
		if(ret < 0)
		{
			printf("read fail\n");
			break;
		}
		fwrite(buffer, ret, sizeof(char), fp);
		if(count++ > 20) {
			break;
		}
	}

	fclose(fp);
	mm_sound_pcm_capture_close(handle);
	return 0;
}

 * @endcode
 */
int mm_sound_pcm_capture_close(MMSoundPcmHandle_t handle);

/**
 * This function is to ignore session for capture
 *
 * @param	handle	[in] handle to capture pcm data
 *
 * @return	This function returns MM_ERROR_NONE on success, or negative value
 *			with error code.
 * @remark	This function only works for not started pcm handle and can't be reversed.
 * @see
 * @pre		PCM capture handle should be created and not started.
 * @post	PCM capture session will be set to mix.
 */
int mm_sound_pcm_capture_ignore_session(MMSoundPcmHandle_t *handle);

/**
 * This function sets callback function for receiving messages from pcm API.
 *
 * @param	handle		[in]	Handle of pcm.
 * @param	callback	[in]	Message callback function.
 * @param	user_param	[in]	User parameter which is passed to callback function.
 *
 * @return	This function returns MM_ERROR_NONE on success, or negative value with error code.
 * @see		MMMessageCallback
 * @remark	None
 * @par Example
 * @code
int msg_callback(int message, MMMessageParamType *param, void *user_param)
{
	switch (message)
	{
		case MM_MESSAGE_SOUND_PCM_CAPTURE_RESTRICTED:
			//do something
			break;

		case MM_MESSAGE_SOUND_PCM_INTERRUPTED:
			//do something
			break;

		default:
			break;
	}
	return TRUE;
}

mm_sound_pcm_set_message_callback(pcmHandle, msg_callback, (void *)pcmHandle);
 * @endcode
 */
int mm_sound_pcm_set_message_callback (MMSoundPcmHandle_t handle, MMMessageCallback callback, void *user_param);

/**
 * Terminate callback function type.
 *
 * @param	data		[in]	Argument passed when callback was set
 * @param	id	    	[in]	handle which has completed playing
 *
 * @return	No return value
 * @remark	It is not allowed to call MMSound API recursively or do time-consuming
 *			task in this callback because this callback is called synchronously.
 * @see		mm_sound_play_sound
 */
typedef void (*mm_sound_stop_callback_func) (void *data, int id);

/*
 * MMSound Play APIs
 */

/**
 * This function is to play system sound.
 *
 * @param	filename		[in] Sound filename to play
 * @param	volume config	[in] Volume type & volume gain
 * @param	callback		[in] Callback function pointer when playing is terminated.
 * @param	data			[in] Pointer to user data when callback is called.
 * @param	handle			[out] Handle of sound play.
 *
 * @return	This function returns MM_ERROR_NONE on success, or negative value
 *			with error code.
 * @remark	When the stop callback is set, it will be called when system sound is
 *			terminated. If mm_sound_stop_sound() is called apparently before
 *			system sound is terminated, stop_callback will not be called.
 * @see		mm_sound_stop_sound mm_sound_stop_callback_func volume_type_t volume_gain_t
 * @pre		None.
 * @post	Sound will be generated with given filename.
 * @par Example
 * @code
int g_stop=0;
void _stop_callback(void* data)
{
	printf("Stop callback\n");
	g_stop = 1;
}

int play_file()
{
	char filename[] ="/opt/media/Sound/testfile.wav";
	volume_type_t volume = VOLUME_TYPE_SYSTEM;
	int ret = 0;
	int handle = -1;

	ret = mm_sound_play_sound(filename, volume, _stop_callback, NULL, &handle);
	if(ret < 0)
	{
		printf("play file failed\n");
	}
	else
	{
		printf("play file success\n");
	}
	while(g_stop == 0)
	{
		sleep(1);
	}
	printf("play stopped\n");
	return 0;
}
 * @endcode
 */
int mm_sound_play_sound(const char *filename, int volume_config, mm_sound_stop_callback_func callback, void *data, int *handle);

int mm_sound_play_sound_without_session(const char *filename, int volume_config, mm_sound_stop_callback_func callback, void *data, int *handle);

int mm_sound_play_solo_sound(const char *filename, int volume_config, mm_sound_stop_callback_func callback, void *data, int *handle);

/**
 * This function is to play system sound. And other audio stream will be mute during playing time
 *
 * @param	filename		[in] Sound filename to play
 * @param	volume config	[in] Volume type & volume gain
 * @param	callback		[in] Callback function pointer when playing is terminated.
 * @param	data			[in] Pointer to user data when callback is called.
 * @param	handle			[out] Handle of sound play.
 *
 * @return	This function returns MM_ERROR_NONE on success, or negative value
 *			with error code.
 * @remark	This function is almost same with mm_sound_play_sound,
 * 			but this make other audio playback stream to mute during playing time.
 * @see		mm_sound_stop_sound mm_sound_stop_callback_func volume_type_t volume_gain_t
 * @pre		None.
 * @post	Sound will be generated with given filename.
 * @par Example
 * @code
int g_stop=0;
void _stop_callback(void* data)
{
	printf("Stop callback\n");
	g_stop = 1;
}

int play_file()
{
	char filename[] ="/opt/media/Sound/testfile.wav";
	volume_type_t volume = VOLUME_TYPE_SYSTEM;
	int ret = 0;
	int handle = -1;

	ret = mm_sound_play_loud_solo_sound(filename, volume, _stop_callback, NULL, &handle);
	if(ret < 0)
	{
		printf("play file failed\n");
	}
	else
	{
		printf("play file success\n");
	}
	while(g_stop == 0)
	{
		sleep(1);
	}
	printf("play stopped\n");
	return 0;
}
 * @endcode
 */
int mm_sound_play_loud_solo_sound(const char *filename, int volume_config, mm_sound_stop_callback_func callback, void *data, int *handle);

int mm_sound_play_loud_solo_sound_no_restore(const char *filename, int volume_config, mm_sound_stop_callback_func callback, void *data, int *handle);

/**
 * This function is to stop playing system sound.
 *
 * @param	handle	[in] Handle of mm_sound_play_sound
 *
 * @return	This function returns MM_ERROR_NONE on success, or negative value
 *			with error code.
 *
 * @remark	When system sound is terminated with this function call, it does not
 *			call stop callback which was set when start playing system sound.
 * @see		mm_sound_play_sound
 * @pre		An sound play handle should be valid.
 * @post	Playing sound file will be stopped.
 * @par Example
 * @code
int g_stop=0;
void _stop_callback(void* data)
{
	printf("Stop callback\n");
	g_stop = 1;
}

int play_file_one_second()
{
	char filename[] ="/opt/media/Sound/testfile.wav";
	volume_type_t volume = VOLUME_TYPE_SYSTEM;
	int ret = 0;
	int handle = -1;

	ret = mm_sound_play_sound(filename, volume, _stop_callback, NULL, &handle);
	if(ret < 0)
	{
		printf("play file failed\n");
	}
	else
	{
		printf("play file success\n");
	}

	sleep(1); //wait 1 second

	ret = mm_sound_stop_sound(handle);
	if(ret < 0)
	{
		printf("stop failed\n");
	}
	else
	{
		printf("play stopped\n");
	}
	return 0;
}
 * @endcode
 */
int mm_sound_stop_sound(int handle);


/**
 * Enumerations for TONE
 */

typedef enum  {
	MM_SOUND_TONE_DTMF_0 = 0,			/**< Predefined DTMF 0 */
	MM_SOUND_TONE_DTMF_1, 				/**< Predefined DTMF 1 */
	MM_SOUND_TONE_DTMF_2,				/**< Predefined DTMF 2 */
	MM_SOUND_TONE_DTMF_3,				/**< Predefined DTMF 3 */
	MM_SOUND_TONE_DTMF_4,				/**< Predefined DTMF 4 */
	MM_SOUND_TONE_DTMF_5,				/**< Predefined DTMF 5 */
	MM_SOUND_TONE_DTMF_6,				/**< Predefined DTMF 6 */
	MM_SOUND_TONE_DTMF_7,				/**< Predefined DTMF 7 */
	MM_SOUND_TONE_DTMF_8,				/**< Predefined DTMF 8 */
	MM_SOUND_TONE_DTMF_9,				/**< Predefined DTMF 9 */
	MM_SOUND_TONE_DTMF_S,			        /**< Predefined DTMF Star - Asterisk (*) */
	MM_SOUND_TONE_DTMF_P,				/**< Predefined DTMF sharP (#) */
	MM_SOUND_TONE_DTMF_A,				/**< Predefined DTMF A (A) */
	MM_SOUND_TONE_DTMF_B,				/**< Predefined DTMF B (B) */
	MM_SOUND_TONE_DTMF_C,				/**< Predefined DTMF C (C) */
	MM_SOUND_TONE_DTMF_D,				/**< Predefined DTMF D (D) */

	/**< Pre-defined TONE */
	MM_SOUND_TONE_SUP_DIAL, 				/**Call supervisory tone, Dial tone: CEPT: 425Hz, continuous */
	MM_SOUND_TONE_ANSI_DIAL,				/**Call supervisory tone, Dial tone: ANSI (IS-95): 350Hz+440Hz, continuous */
	MM_SOUND_TONE_JAPAN_DIAL,				/**Call supervisory tone, Dial tone: JAPAN: 400Hz, continuous*/
	MM_SOUND_TONE_SUP_BUSY,				/**Call supervisory tone, Busy: CEPT: 425Hz, 500ms ON, 500ms OFF... */
	MM_SOUND_TONE_ANSI_BUSY, 				/**Call supervisory tone, Busy: ANSI (IS-95): 480Hz+620Hz, 500ms ON, 500ms OFF... */
	MM_SOUND_TONE_JAPAN_BUSY, 				/**Call supervisory tone, Busy: JAPAN: 400Hz, 500ms ON, 500ms OFF...*/
	MM_SOUND_TONE_SUP_CONGESTION, 		/**Call supervisory tone, Congestion: CEPT, JAPAN: 425Hz, 200ms ON, 200ms OFF */
	MM_SOUND_TONE_ANSI_CONGESTION,		/**Call supervisory tone, Congestion: ANSI (IS-95): 480Hz+620Hz, 250ms ON, 250ms OFF... */
	MM_SOUND_TONE_SUP_RADIO_ACK,			/**Call supervisory tone, Radio path acknowlegment : CEPT, ANSI: 425Hz, 200ms ON  */
	MM_SOUND_TONE_JAPAN_RADIO_ACK,		/**Call supervisory tone, Radio path acknowlegment : JAPAN: 400Hz, 1s ON, 2s OFF...*/
	MM_SOUND_TONE_SUP_RADIO_NOTAVAIL,		/**Call supervisory tone, Radio path not available: 425Hz, 200ms ON, 200 OFF 3 bursts */
	MM_SOUND_TONE_SUP_ERROR,				/**Call supervisory tone, Error/Special info: 950Hz+1400Hz+1800Hz, 330ms ON, 1s OFF... */
	MM_SOUND_TONE_SUP_CALL_WAITING,		/**Call supervisory tone, Call Waiting: CEPT, JAPAN: 425Hz, 200ms ON, 600ms OFF, 200ms ON, 3s OFF...  */
	MM_SOUND_TONE_ANSI_CALL_WAITING,		/**Call supervisory tone, Call Waiting: ANSI (IS-95): 440 Hz, 300 ms ON, 9.7 s OFF, (100 ms ON, 100 ms OFF, 100 ms ON, 9.7s OFF ...) */
	MM_SOUND_TONE_SUP_RINGTONE,			/**Call supervisory tone, Ring Tone: CEPT, JAPAN: 425Hz, 1s ON, 4s OFF... */
	MM_SOUND_TONE_ANSI_RINGTONE,			/**Call supervisory tone, Ring Tone: ANSI (IS-95): 440Hz + 480Hz, 2s ON, 4s OFF... */
	MM_SOUND_TONE_PROP_BEEP,				/**General beep: 400Hz+1200Hz, 35ms ON */
	MM_SOUND_TONE_PROP_ACK, 				/**Proprietary tone, positive acknowlegement: 1200Hz, 100ms ON, 100ms OFF 2 bursts */
	MM_SOUND_TONE_PROP_NACK, 				/**Proprietary tone, negative acknowlegement: 300Hz+400Hz+500Hz, 400ms ON */
	MM_SOUND_TONE_PROP_PROMPT, 			/**Proprietary tone, prompt tone: 400Hz+1200Hz, 200ms ON	 */
	MM_SOUND_TONE_PROP_BEEP2, 				/**Proprietary tone, general double beep: twice 400Hz+1200Hz, 35ms ON, 200ms OFF, 35ms ON */
	MM_SOUND_TONE_SUP_INTERCEPT, 						/**Call supervisory tone (IS-95), intercept tone: alternating 440 Hz and 620 Hz tones, each on for 250 ms */
	MM_SOUND_TONE_SUP_INTERCEPT_ABBREV,				/**Call supervisory tone (IS-95), abbreviated intercept: intercept tone limited to 4 seconds */
	MM_SOUND_TONE_SUP_CONGESTION_ABBREV, 				/**Call supervisory tone (IS-95), abbreviated congestion: congestion tone limited to 4 seconds */
	MM_SOUND_TONE_SUP_CONFIRM, 						/**Call supervisory tone (IS-95), confirm tone: a 350 Hz tone added to a 440 Hz tone repeated 3 times in a 100 ms on, 100 ms off cycle */
	MM_SOUND_TONE_SUP_PIP, 							/**Call supervisory tone (IS-95), pip tone: four bursts of 480 Hz tone (0.1 s on, 0.1 s off). */
	MM_SOUND_TONE_CDMA_DIAL_TONE_LITE, 				/**425Hz continuous */
	MM_SOUND_TONE_CDMA_NETWORK_USA_RINGBACK, 		/**CDMA USA Ringback: 440Hz+480Hz 2s ON, 4000 OFF ...*/
	MM_SOUND_TONE_CDMA_INTERCEPT, 					/**CDMA Intercept tone: 440Hz 250ms ON, 620Hz 250ms ON ...*/
	MM_SOUND_TONE_CDMA_ABBR_INTERCEPT, 				/**CDMA Abbr Intercept tone: 440Hz 250ms ON, 620Hz 250ms ON */
	MM_SOUND_TONE_CDMA_REORDER, 						/**CDMA Reorder tone: 480Hz+620Hz 250ms ON, 250ms OFF... */
	MM_SOUND_TONE_CDMA_ABBR_REORDER, 				/**CDMA Abbr Reorder tone: 480Hz+620Hz 250ms ON, 250ms OFF repeated for 8 times */
	MM_SOUND_TONE_CDMA_NETWORK_BUSY, 				/**CDMA Network Busy tone: 480Hz+620Hz 500ms ON, 500ms OFF continuous */
	MM_SOUND_TONE_CDMA_CONFIRM, 						/**CDMA Confirm tone: 350Hz+440Hz 100ms ON, 100ms OFF repeated for 3 times */
	MM_SOUND_TONE_CDMA_ANSWER, 						/**CDMA answer tone: silent tone - defintion Frequency 0, 0ms ON, 0ms OFF */
	MM_SOUND_TONE_CDMA_NETWORK_CALLWAITING, 			/**CDMA Network Callwaiting tone: 440Hz 300ms ON */
	MM_SOUND_TONE_CDMA_PIP, 							/**CDMA PIP tone: 480Hz 100ms ON, 100ms OFF repeated for 4 times */
	MM_SOUND_TONE_CDMA_CALL_SIGNAL_ISDN_NORMAL, 		/**ISDN Call Signal Normal tone: {2091Hz 32ms ON, 2556 64ms ON} 20 times, 2091 32ms ON, 2556 48ms ON, 4s OFF */
	MM_SOUND_TONE_CDMA_CALL_SIGNAL_ISDN_INTERGROUP, 	/**ISDN Call Signal Intergroup tone: {2091Hz 32ms ON, 2556 64ms ON} 8 times, 2091Hz 32ms ON, 400ms OFF, {2091Hz 32ms ON, 2556Hz 64ms ON} 8times, 2091Hz 32ms ON, 4s OFF.*/
	MM_SOUND_TONE_CDMA_CALL_SIGNAL_ISDN_SP_PRI, 		/**ISDN Call Signal SP PRI tone:{2091Hz 32ms ON, 2556 64ms ON} 4 times 2091Hz 16ms ON, 200ms OFF, {2091Hz 32ms ON, 2556Hz 64ms ON} 4 times, 2091Hz 16ms ON, 200ms OFF */
	MM_SOUND_TONE_CDMA_CALL_SIGNAL_ISDN_PAT3, 		/**SDN Call sign PAT3 tone: silent tone */
	MM_SOUND_TONE_CDMA_CALL_SIGNAL_ISDN_PING_RING, 	/**ISDN Ping Ring tone: {2091Hz 32ms ON, 2556Hz 64ms ON} 5 times 2091Hz 20ms ON */
	MM_SOUND_TONE_CDMA_CALL_SIGNAL_ISDN_PAT5, 		/**ISDN Pat5 tone: silent tone */
	MM_SOUND_TONE_CDMA_CALL_SIGNAL_ISDN_PAT6, 		/**ISDN Pat6 tone: silent tone */
	MM_SOUND_TONE_CDMA_CALL_SIGNAL_ISDN_PAT7, 		/**ISDN Pat7 tone: silent tone */
	MM_SOUND_TONE_CDMA_HIGH_L, 						/**TONE_CDMA_HIGH_L tone: {3700Hz 25ms, 4000Hz 25ms} 40 times 4000ms OFF, Repeat .... */
	MM_SOUND_TONE_CDMA_MED_L, 						/**TONE_CDMA_MED_L tone: {2600Hz 25ms, 2900Hz 25ms} 40 times 4000ms OFF, Repeat .... */
	MM_SOUND_TONE_CDMA_LOW_L, 						/**TONE_CDMA_LOW_L tone: {1300Hz 25ms, 1450Hz 25ms} 40 times, 4000ms OFF, Repeat .... */
	MM_SOUND_TONE_CDMA_HIGH_SS, 						/**CDMA HIGH SS tone: {3700Hz 25ms, 4000Hz 25ms} repeat 16 times, 400ms OFF, repeat .... */
	MM_SOUND_TONE_CDMA_MED_SS, 						/**CDMA MED SS tone: {2600Hz 25ms, 2900Hz 25ms} repeat 16 times, 400ms OFF, repeat .... */
	MM_SOUND_TONE_CDMA_LOW_SS, 						/**CDMA LOW SS tone: {1300z 25ms, 1450Hz 25ms} repeat 16 times, 400ms OFF, repeat .... */
	MM_SOUND_TONE_CDMA_HIGH_SSL, 						/**CDMA HIGH SSL tone: {3700Hz 25ms, 4000Hz 25ms} 8 times, 200ms OFF, {3700Hz 25ms, 4000Hz 25ms} repeat 8 times, 200ms OFF, {3700Hz 25ms, 4000Hz 25ms} repeat 16 times, 4000ms OFF, repeat ... */
	MM_SOUND_TONE_CDMA_MED_SSL, 						/**CDMA MED SSL tone: {2600Hz 25ms, 2900Hz 25ms} 8 times, 200ms OFF, {2600Hz 25ms, 2900Hz 25ms} repeat 8 times, 200ms OFF, {2600Hz 25ms, 2900Hz 25ms} repeat 16 times, 4000ms OFF, repeat ... */
	MM_SOUND_TONE_CDMA_LOW_SSL, 						/**CDMA LOW SSL tone: {1300Hz 25ms, 1450Hz 25ms} 8 times, 200ms OFF, {1300Hz 25ms, 1450Hz 25ms} repeat 8 times, 200ms OFF, {1300Hz 25ms, 1450Hz 25ms} repeat 16 times, 4000ms OFF, repeat ... */
	MM_SOUND_TONE_CDMA_HIGH_SS_2, 					/**CDMA HIGH SS2 tone: {3700Hz 25ms, 4000Hz 25ms} 20 times, 1000ms OFF, {3700Hz 25ms, 4000Hz 25ms} 20 times, 3000ms OFF, repeat .... */
	MM_SOUND_TONE_CDMA_MED_SS_2, 						/**CDMA MED SS2 tone: {2600Hz 25ms, 2900Hz 25ms} 20 times, 1000ms OFF, {2600Hz 25ms, 2900Hz 25ms} 20 times, 3000ms OFF, repeat .... */
	MM_SOUND_TONE_CDMA_LOW_SS_2, 						/**CDMA LOW SS2 tone: {1300Hz 25ms, 1450Hz 25ms} 20 times, 1000ms OFF, {1300Hz 25ms, 1450Hz 25ms} 20 times, 3000ms OFF, repeat .... */
	MM_SOUND_TONE_CDMA_HIGH_SLS, 						/**CDMA HIGH SLS tone: {3700Hz 25ms, 4000Hz 25ms} 10 times, 500ms OFF, {3700Hz 25ms, 4000Hz 25ms} 20 times, 500ms OFF, {3700Hz 25ms, 4000Hz 25ms} 10 times, 3000ms OFF, REPEAT */
	MM_SOUND_TONE_CDMA_MED_SLS, 						/**CDMA MED SLS tone: {2600Hz 25ms, 2900Hz 25ms} 10 times, 500ms OFF, {2600Hz 25ms, 2900Hz 25ms} 20 times, 500ms OFF, {2600Hz 25ms, 2900Hz 25ms} 10 times, 3000ms OFF, REPEAT */
	MM_SOUND_TONE_CDMA_LOW_SLS, 						/**CDMA LOW SLS tone: {1300Hz 25ms, 1450Hz 25ms} 10 times, 500ms OFF, {1300Hz 25ms, 1450Hz 25ms} 20 times, 500ms OFF, {1300Hz 25ms, 1450Hz 25ms} 10 times, 3000ms OFF, REPEAT */
	MM_SOUND_TONE_CDMA_HIGH_S_X4, 					/**CDMA HIGH S X4 tone: {3700Hz 25ms, 4000Hz 25ms} 10 times, 500ms OFF, {3700Hz 25ms, 4000Hz 25ms} 10 times, 500ms OFF, {3700Hz 25ms, 4000Hz 25ms} 10 times, 500ms OFF, {3700Hz 25ms, 4000Hz 25ms} 10 times, 2500ms OFF, REPEAT.... */
	MM_SOUND_TONE_CDMA_MED_S_X4, 						/**CDMA MED S X4 tone: {2600Hz 25ms, 2900Hz 25ms} 10 times, 500ms OFF, {2600Hz 25ms, 2900Hz 25ms} 10 times, 500ms OFF, {2600Hz 25ms, 2900Hz 25ms} 10 times, 500ms OFF, {2600Hz 25ms, 2900Hz 25ms} 10 times, 2500ms OFF, REPEAT.... */
	MM_SOUND_TONE_CDMA_LOW_S_X4, 						/**CDMA LOW S X4 tone: {2600Hz 25ms, 2900Hz 25ms} 10 times, 500ms OFF, {2600Hz 25ms, 2900Hz 25ms} 10 times, 500ms OFF, {2600Hz 25ms, 2900Hz 25ms} 10 times, 500ms OFF, {2600Hz 25ms, 2900Hz 25ms} 10 times, 2500ms OFF, REPEAT....*/
	MM_SOUND_TONE_CDMA_HIGH_PBX_L, 					/**CDMA HIGH PBX L: {3700Hz 25ms, 4000Hz 25ms}20 times, 2000ms OFF, REPEAT.... */
	MM_SOUND_TONE_CDMA_MED_PBX_L, 					/**CDMA MED PBX L: {2600Hz 25ms, 2900Hz 25ms}20 times, 2000ms OFF, REPEAT.... */
	MM_SOUND_TONE_CDMA_LOW_PBX_L, 					/**CDMA LOW PBX L: {1300Hz 25ms,1450Hz 25ms}20 times, 2000ms OFF, REPEAT.... */
	MM_SOUND_TONE_CDMA_HIGH_PBX_SS, 					/**CDMA HIGH PBX SS tone: {3700Hz 25ms, 4000Hz 25ms} 8 times 200 ms OFF, {3700Hz 25ms 4000Hz 25ms}8 times, 2000ms OFF, REPEAT.... */
	MM_SOUND_TONE_CDMA_MED_PBX_SS, 					/**CDMA MED PBX SS tone: {2600Hz 25ms, 2900Hz 25ms} 8 times 200 ms OFF, {2600Hz 25ms 2900Hz 25ms}8 times, 2000ms OFF, REPEAT.... */
	MM_SOUND_TONE_CDMA_LOW_PBX_SS, 					/**CDMA LOW PBX SS tone: {1300Hz 25ms, 1450Hz 25ms} 8 times 200 ms OFF, {1300Hz 25ms 1450Hz 25ms}8 times, 2000ms OFF, REPEAT.... */
	MM_SOUND_TONE_CDMA_HIGH_PBX_SSL, 					/**CDMA HIGH PBX SSL tone:{3700Hz 25ms, 4000Hz 25ms} 8 times 200ms OFF, {3700Hz 25ms, 4000Hz 25ms} 8 times, 200ms OFF, {3700Hz 25ms, 4000Hz 25ms} 16 times, 1000ms OFF, REPEAT.... */
	MM_SOUND_TONE_CDMA_MED_PBX_SSL, 					/**CDMA MED PBX SSL tone:{2600Hz 25ms, 2900Hz 25ms} 8 times 200ms OFF, {2600Hz 25ms, 2900Hz 25ms} 8 times, 200ms OFF, {2600Hz 25ms, 2900Hz 25ms} 16 times, 1000ms OFF, REPEAT.... */
	MM_SOUND_TONE_CDMA_LOW_PBX_SSL, 					/**CDMA LOW PBX SSL tone:{1300Hz 25ms, 1450Hz 25ms} 8 times 200ms OFF, {1300Hz 25ms, 1450Hz 25ms} 8 times, 200ms OFF, {1300Hz 25ms, 1450Hz 25ms} 16 times, 1000ms OFF, REPEAT.... */
	MM_SOUND_TONE_CDMA_HIGH_PBX_SLS, 					/**CDMA HIGH PBX SLS tone:{3700Hz 25ms, 4000Hz 25ms} 8 times 200ms OFF, {3700Hz 25ms, 4000Hz 25ms} 16 times, 200ms OFF, {3700Hz 25ms, 4000Hz 25ms} 8 times, 1000ms OFF, REPEAT....  */
	MM_SOUND_TONE_CDMA_MED_PBX_SLS, 					/**CDMA MED PBX SLS tone:{2600Hz 25ms, 2900Hz 25ms} 8 times 200ms OFF, {2600Hz 25ms, 2900Hz 25ms} 16 times, 200ms OFF, {2600Hz 25ms, 2900Hz 25ms} 8 times, 1000ms OFF, REPEAT.... */
	MM_SOUND_TONE_CDMA_LOW_PBX_SLS, 					/**CDMA LOW PBX SLS tone:{1300Hz 25ms, 1450Hz 25ms} 8 times 200ms OFF, {1300Hz 25ms, 1450Hz 25ms} 16 times, 200ms OFF, {1300Hz 25ms, 1450Hz 25ms} 8 times, 1000ms OFF, REPEAT.... */
	MM_SOUND_TONE_CDMA_HIGH_PBX_S_X4, 				/**CDMA HIGH PBX X S4 tone: {3700Hz 25ms 4000Hz 25ms} 8 times, 200ms OFF, {3700Hz 25ms 4000Hz 25ms} 8 times, 200ms OFF, {3700Hz 25ms 4000Hz 25ms} 8 times, 200ms OFF, {3700Hz 25ms 4000Hz 25ms} 8 times, 800ms OFF, REPEAT... */
	MM_SOUND_TONE_CDMA_MED_PBX_S_X4, 					/**CDMA MED PBX X S4 tone: {2600Hz 25ms 2900Hz 25ms} 8 times, 200ms OFF, {2600Hz 25ms 2900Hz 25ms} 8 times, 200ms OFF, {2600Hz 25ms 2900Hz 25ms} 8 times, 200ms OFF, {2600Hz 25ms 2900Hz 25ms} 8 times, 800ms OFF, REPEAT... */
	MM_SOUND_TONE_CDMA_LOW_PBX_S_X4, 					/**CDMA LOW PBX X S4 tone: {1300Hz 25ms 1450Hz 25ms} 8 times, 200ms OFF, {1300Hz 25ms 1450Hz 25ms} 8 times, 200ms OFF, {1300Hz 25ms 1450Hz 25ms} 8 times, 200ms OFF, {1300Hz 25ms 1450Hz 25ms} 8 times, 800ms OFF, REPEAT... */
	MM_SOUND_TONE_CDMA_ALERT_NETWORK_LITE, 			/**CDMA Alert Network Lite tone: 1109Hz 62ms ON, 784Hz 62ms ON, 740Hz 62ms ON 622Hz 62ms ON, 1109Hz 62ms ON */
	MM_SOUND_TONE_CDMA_ALERT_AUTOREDIAL_LITE, 		/**CDMA Alert Auto Redial tone: {1245Hz 62ms ON, 659Hz 62ms ON} 3 times, 1245 62ms ON */
	MM_SOUND_TONE_CDMA_ONE_MIN_BEEP, 					/**CDMA One Min Beep tone: 1150Hz+770Hz 400ms ON */
	MM_SOUND_TONE_CDMA_KEYPAD_VOLUME_KEY_LITE, 		/**CDMA KEYPAD Volume key lite tone: 941Hz+1477Hz 120ms ON */
	MM_SOUND_TONE_CDMA_PRESSHOLDKEY_LITE, 			/**CDMA PRESSHOLDKEY LITE tone: 587Hz 375ms ON, 1175Hz 125ms ON */
	MM_SOUND_TONE_CDMA_ALERT_INCALL_LITE, 				/**CDMA ALERT INCALL LITE tone: 587Hz 62ms, 784 62ms, 831Hz 62ms, 784Hz 62ms, 1109 62ms, 784Hz 62ms, 831Hz 62ms, 784Hz 62ms*/
	MM_SOUND_TONE_CDMA_EMERGENCY_RINGBACK, 			/**CDMA EMERGENCY RINGBACK tone: {941Hz 125ms ON, 10ms OFF} 3times 4990ms OFF, REPEAT... */
	MM_SOUND_TONE_CDMA_ALERT_CALL_GUARD, 			/**CDMA ALERT CALL GUARD tone: {1319Hz 125ms ON, 125ms OFF} 3 times */
	MM_SOUND_TONE_CDMA_SOFT_ERROR_LITE, 				/**CDMA SOFT ERROR LITE tone: 1047Hz 125ms ON, 370Hz 125ms */
	MM_SOUND_TONE_CDMA_CALLDROP_LITE, 				/**CDMA CALLDROP LITE tone: 1480Hz 125ms, 1397Hz 125ms, 784Hz 125ms */
	MM_SOUND_TONE_CDMA_NETWORK_BUSY_ONE_SHOT, 		/**CDMA_NETWORK_BUSY_ONE_SHOT tone: 425Hz 500ms ON, 500ms OFF. */
	MM_SOUND_TONE_CDMA_ABBR_ALERT, 					/**CDMA_ABBR_ALERT tone: 1150Hz+770Hz 400ms ON */
	MM_SOUND_TONE_CDMA_SIGNAL_OFF,					/**CDMA_SIGNAL_OFF - silent tone */
	MM_SOUND_TONE_LOW_FRE,					/**100Hz continuous */
	MM_SOUND_TONE_MED_FRE,					/**200Hz continuous */
	MM_SOUND_TONE_HIGH_FRE,					/**300Hz continuous */
	MM_SOUND_TONE_NUM,
}MMSoundTone_t;

typedef unsigned long sound_time_msec_t;		/**< millisecond unit */

/**
 * This function is to play tone sound.
 *
 * @param	num				[in] predefined tone type (MMSoundTone_t)
 * 			volume config	[in] volume type & volume gain
 *			volume			[in] volume ratio (0.0 ~1.0)
 * 			duration		[in] millisecond (-1 for infinite)
 *			handle			[in] Handle of mm_sound_play_tone
 *			enable_session	[in] set enable/unable session
 *
 * @return	This function returns MM_ERROR_NONE on success, or negative value
 *			with error code.
 *
 * @remark	It doesn't provide stop
 * @see	volume_type_t volume_gain_t MMSoundTone_t
 * @pre		None.
 * @post	TONE sound will be played.
 * @par Example
 * @code
int ret = 0;

ret = mm_sound_play_tone_ex(MM_SOUND_TONE_DTMF_9, VOLUME_TYPE_SYSTEM, 1.0, 1000, &handle, TRUE); //play 1 second with volume ratio 1.0
if(ret < 0)
{
	printf("play tone failed\n");
}
else
{
	printf("play tone success\n");
}
 * @endcode
 */
int mm_sound_play_tone_ex (MMSoundTone_t num, int volume_config, const double volume, const int duration, int *handle, bool enable_session);

/**
 * This function is to play tone sound.
 *
 * @param	num				[in] predefined tone type (MMSoundTone_t)
 * 			volume config	[in] volume type & volume gain
 *			volume			[in] volume ratio (0.0 ~1.0)
 * 			duration		[in] millisecond (-1 for infinite)
 *			handle			[in] Handle of mm_sound_play_tone
 *
 * @return	This function returns MM_ERROR_NONE on success, or negative value
 *			with error code.
 *
 * @remark	It doesn't provide stop
 * @see	volume_type_t volume_gain_t MMSoundTone_t
 * @pre		None.
 * @post	TONE sound will be played.
 * @par Example
 * @code
int ret = 0;

ret = mm_sound_play_tone(MM_SOUND_TONE_DTMF_9, VOLUME_TYPE_SYSTEM, 1.0, 1000, &handle); //play 1 second with volume ratio 1.0
if(ret < 0)
{
	printf("play tone failed\n");
}
else
{
	printf("play tone success\n");
}
 * @endcode
 */
int mm_sound_play_tone (MMSoundTone_t num, int volume_config, const double volume, const int duration, int *handle);

/*
 * Enumerations of System audio route policy
 */
typedef enum {
	SYSTEM_AUDIO_ROUTE_POLICY_DEFAULT,			/**< Play via a2dp headset if connected. or play via headset if connected. or play via speaker.
													And capture via 4pole headset-mic if connected. or capture via mic */
	SYSTEM_AUDIO_ROUTE_POLICY_IGNORE_A2DP,		/**< Play via headset if connected. or play via speaker
													 And capture via 4pole headset-mic if connected. or capture via mic */
	SYSTEM_AUDIO_ROUTE_POLICY_HANDSET_ONLY,		/**< Play via speaker. and capture via mic  */
	SYSTEM_AUDIO_ROUTE_POLICY_MAX
}system_audio_route_t;

typedef enum {
	SYSTEM_AUDIO_ROUTE_PLAYBACK_DEVICE_NONE,			/**< Abnormal case */
	SYSTEM_AUDIO_ROUTE_PLAYBACK_DEVICE_HANDSET,		/**< Speaker or Headset or Earpiece */
	SYSTEM_AUDIO_ROUTE_PLAYBACK_DEVICE_BLUETOOTH,	/**< Bluetooth */
	SYSTEM_AUDIO_ROUTE_PLAYBACK_DEVICE_EARPHONE,	/**< Earphone */
	SYSTEM_AUDIO_ROUTE_PLAYBACK_DEVICE_MAX,
}system_audio_route_device_t;

typedef enum {
	SYSTEM_AUDIO_CAPTURE_NONE,	/**< Capture device is not in use */
	SYSTEM_AUDIO_CAPTURE_ACTIVE,	/**< Capture device is in use */
	SYSTEM_AUDIO_CAPTURE_MAX,
}system_audio_capture_status_t;


/**
 * This function set system route policy.
 *
 * @param	route		[in] audio route type
 *
 * @return	This function returns MM_ERROR_NONE on success, or negative value
 *			with error code.
 * @remark	If bluetooth has disconnected during SYSTEM_AUDIO_ROUTE_POLICY_IGNORE_A2DP policy,
 * 			The audio route policy will be changed to SYSTEM_AUDIO_ROUTE_POLICY_DEFAULT.
 * @see		mm_sound_route_get_system_policy system_audio_route_t mm_sound_route_is_a2dp_on
 * @pre		None.
 * @post	Audio routing policy will be changed with given type. And route change callback function will be called if registered.
 * @par Example
 * @code
int g_stop = 0;
void _stop_callback()
{
	g_stop = 1;
}

int play_file_via_speaker()
{
	int ret = 0;
	system_audio_route_t route, original_route;

	//get backup current policy
	ret = mm_sound_route_get_system_policy(&original_route);
	if(ret < 0)
	{
		printf("Can not get system audio route policy\n");
	}
	else
	{
		//set route policy to ignore a2dp
		route = SYSTEM_AUDIO_ROUTE_POLICY_IGNORE_A2DP;
		ret = mm_sound_route_set_system_policy(route);
		if(ret < 0)
		{
			printf("Ca not set route policy\n");
		}
		else
		{
			int handle;
			//play wav file
			ret = mm_sound_play_sound("/opt/media/Sound/alert.wav", VOLUME_TYPE_SYSTEM, NULL, NULL, &handle);
			if(ret < 0)
			{
				printf("Can not play wav file\n");
			}
			else
			{
				while(g_stop == 0)
				{
					sleep(1);
				}
				//restore original policy
				mm_sound_route_set_system_policy(original_route);
			}
		}
	}
	return 0;
}
 * @endcode
 */
int mm_sound_route_set_system_policy (system_audio_route_t route);

/**
 * This function get sysytem route policy.
 *
 * @param	route		[out] audio route type
 *
 * @return	This function returns MM_ERROR_NONE on success, or negative value
 *			with error code.
 * @remark	None.
 * @see		mm_sound_route_set_system_policy system_audio_route_t mm_sound_route_is_a2dp_on
 * @pre		None.
 * @post	None.
 * @par Example
 * @code
int g_stop = 0;
void _stop_callback()
{
	g_stop = 1;
}

int play_file_via_speaker()
{
	int ret = 0;
	system_audio_route_t route, original_route;

	//get backup current policy
	ret = mm_sound_route_get_system_policy(&original_route);
	if(ret < 0)
	{
		printf("Can not get system audio route policy\n");
	}
	else
	{
		//set route policy to ignore a2dp
		route = SYSTEM_AUDIO_ROUTE_POLICY_IGNORE_A2DP;
		ret = mm_sound_route_set_system_policy(route);
		if(ret < 0)
		{
			printf("Can not set route policy\n");
		}
		else
		{
			int handle;
			//play wav file
			ret = mm_sound_play_sound("/opt/media/Sound/alert.wav", VOLUME_TYPE_SYSTEM, NULL, NULL, &handle);
			if(ret < 0)
			{
				printf("Can not play wav file\n");
			}
			else
			{
				while(g_stop == 0)
				{
					sleep(1);
				}
				//restore original policy
				mm_sound_route_set_system_policy(original_route);
			}
		}
	}
	return 0;
}
 * @endcode
 */
int mm_sound_route_get_system_policy (system_audio_route_t *route);



/**
 * This function get a2dp activation information.
 *
 * @param	 connected		    [out] is Bluetooth A2DP connected (1:connected, 0:not connected)
 *              bt_name		[out] Bluetooth A2DP connected device name (allocated by internal when connected=1 otherwise set to null)
 *
 *
 * @return	This function returns MM_ERROR_NONE on success, or negative value
 *			with error code.
 * @remark	This function allocation memory to given bt_name pointer internally.
 *          So application should free given memory pointer later after use.
 *          bt_name will be null if there's no a2dp device is connected (connected is 0)
 * @see		mm_sound_route_set_system_policy mm_sound_route_get_system_policy
 * @pre		None.
 * @post	memory buffer will be allocated and fill with bluetooth device name.
 * @par Example
 * @code
int ret;
int connected = 0;
char* bt_name = NULL;
ret = mm_sound_route_get_a2dp_status (&connected, &bt_name);

if (ret == MM_ERROR_NONE) {
	g_print ("### Is Bluetooth A2DP On Success : connected=[%d] name=[%s]\n", connected, bt_name);
	if (bt_name)
		free (bt_name);
} else {
	g_print ("### Is Bluetooth A2DP On Error : errno [%d]\n", ret);

 * @endcode
 */
int mm_sound_route_get_a2dp_status (bool* connected, char** bt_name);


/**
 * This function get current playing device.
 *
 * @param	dev		[out] current playing device information
 *
 * @return	This function returns MM_ERROR_NONE on success, or negative value
 *			with error code.
 * @remark	if there is no running instance in system,
 * 			output parameter dev can be SYSTEM_AUDIO_ROUTE_PLAYING_DEVICE_NONE.
 *
 * @see		system_audio_route_device_t
 * @pre		None.
 * @post	None.
 * @par Example
 * @code
int ret = 0;
system_audio_route_device_t dev;

ret = mm_sound_route_get_playing_device (&dev);
if(ret == MM_ERROR_NONE)
{
	switch(dev)
	{
	case SYSTEM_AUDIO_ROUTE_PLAYBACK_DEVICE_HANDSET:
		printf("Handset is playing\n");
		break;
	case SYSTEM_AUDIO_ROUTE_PLAYBACK_DEVICE_BLUETOOTH:
		printf("Bluetooth is playing\n");
		break;
	case SYSTEM_AUDIO_ROUTE_PLAYBACK_DEVICE_NONE:
	default:
		printf("Unexptected\n");
		break;
	}
}
else
{
	printf ("Can not get current running device\n");
}

 * @endcode
 */
int mm_sound_route_get_playing_device(system_audio_route_device_t *dev);

/**
 * Audio route policy change callback function type.
 *
 * @param	user_data		[in]	Argument passed when callback has called
 * @param	policy			[in]	changed policy type
 *
 * @return	No return value
 * @remark
 * @see		mm_sound_volume_add_callback mm_sound_volume_remove_callback
 */
typedef void (*audio_route_policy_changed_callback_fn)(void* user_data, system_audio_route_t policy);

/**
 * This function set system audio policy changed callback function.
 *
 * @param	func		[in] callback function pointer
 * @param	user_data	[in] user data will be called with func
 *
 * @return	This function returns MM_ERROR_NONE on success, or negative value
 *			with error code.
 * @remark	None.
 * @see		mm_sound_route_remove_change_callback mm_sound_route_set_system_policy mm_sound_route_get_system_policy system_audio_route_t
 * @pre		None.
 * @post	None.
 * @par Example
 * @code
void audio_route_policy_changed_callback(void* data, system_audio_route_t policy)
{
	int value = (int) data;
	system_audio_route_t lv_policy;
	char *str_route[SYSTEM_AUDIO_ROUTE_POLICY_MAX] = {
			"DEFAULT","IGN_A2DP","HANDSET"
		};
	printf("Audio Route Policy has changed to [%s]\n", str_route[policy]);
	printf("user data : %d\n", value);
	if(0 > mm_sound_route_get_system_policy(&lv_policy)) {
		printf("Can not get policy...in callback function\n");
	}
	else {
		printf("readed policy [%s]\n", str_route[lv_policy]);
	}
}
int make_callback()
{
	int ret = 0;
	ret = mm_sound_route_add_change_callback(audio_route_policy_changed_callback, (void*)111);
	if(ret < 0)
	{
		printf("Can not add callback\n");
	}
	return 0;
}
 * @endcode
 */
int mm_sound_route_add_change_callback(audio_route_policy_changed_callback_fn func, void* user_data);

/**
 * This function remove system audio policy changed callback function.
 *
 * @return	This function returns MM_ERROR_NONE on success, or negative value
 *			with error code.
 * @remark	None.
 * @see		mm_sound_route_add_change_callback mm_sound_route_set_system_policy mm_sound_route_get_system_policy system_audio_route_t
 * @pre		Sound route change callback should be registered.
 * @post	Sound route change callback deregistered and does not be called anymore.
 * @par Example
 * @code
void audio_route_policy_changed_callback(void* data, system_audio_route_t policy)
{
	int value = (int) data;
	system_audio_route_t lv_policy;
	char *str_route[SYSTEM_AUDIO_ROUTE_POLICY_MAX] = {
			"DEFAULT","IGN_A2DP","HANDSET"
		};
	printf("Audio Route Policy has changed to [%s]\n", str_route[policy]);
	printf("user data : %d\n", value);
	if(0 > mm_sound_route_get_system_policy(&lv_policy)) {
		printf("Can not get policy...in callback function\n");
	}
	else {
		printf("readed policy [%s]\n", str_route[lv_policy]);
	}
}
int make_callback()
{
	int ret = 0;
	ret = mm_sound_route_add_change_callback(audio_route_policy_changed_callback, (void*)111);
	if(ret < 0)
	{
		printf("Can not add callback\n");
	}
	else
	{
		ret = mm_sound_route_remove_change_callback();
		if(ret < 0)
		{
			printf("Can not remove callback\n");
		}
	}
	return 0;
}
 * @endcode
 */
int mm_sound_route_remove_change_callback(void);

/*
 * Enumerations of device & route
 */

typedef enum{
	MM_SOUND_DIRECTION_NONE,
	MM_SOUND_DIRECTION_IN,							/**< Capture */
	MM_SOUND_DIRECTION_OUT,							/**< Playback */
} mm_sound_direction;

typedef enum{
	MM_SOUND_DEVICE_IN_NONE				= 0x00,
	MM_SOUND_DEVICE_IN_MIC				= 0x01,		/**< Device builtin mic. */
	MM_SOUND_DEVICE_IN_WIRED_ACCESSORY	= 0x02,		/**< Wired input devices */
	MM_SOUND_DEVICE_IN_BT_SCO	= 0x08,		/**< Bluetooth SCO device */
} mm_sound_device_in;

typedef enum{
	MM_SOUND_DEVICE_OUT_NONE			= 0x000,
	MM_SOUND_DEVICE_OUT_SPEAKER		= 0x001<<8,	/**< Device builtin speaker */
	MM_SOUND_DEVICE_OUT_RECEIVER		= 0x002<<8,	/**< Device builtin receiver */
	MM_SOUND_DEVICE_OUT_WIRED_ACCESSORY	= 0x004<<8,	/**< Wired output devices such as headphone, headset, and so on. */
	MM_SOUND_DEVICE_OUT_BT_SCO			= 0x008<<8,	/**< Bluetooth SCO device */
	MM_SOUND_DEVICE_OUT_BT_A2DP		= 0x010<<8,	/**< Bluetooth A2DP device */
	MM_SOUND_DEVICE_OUT_DOCK			= 0x020<<8,	/**< DOCK device */
	MM_SOUND_DEVICE_OUT_HDMI			= 0x040<<8,	/**< HDMI device */
	MM_SOUND_DEVICE_OUT_MIRRORING 		= 0x080<<8, /**< MIRRORING device */
	MM_SOUND_DEVICE_OUT_USB_AUDIO		= 0x100<<8,	/**< USB Audio device */
	MM_SOUND_DEVICE_OUT_MULTIMEDIA_DOCK	= 0x200<<8,	/**< Multimedia DOCK device */
} mm_sound_device_out;

typedef enum {
	MM_SOUND_VOLUME_DEVICE_OUT_SPEAKER,				/**< Device builtin speaker */
	MM_SOUND_VOLUME_DEVICE_OUT_RECEIVER,			/**< Device builtin receiver */
	MM_SOUND_VOLUME_DEVICE_OUT_WIRED_ACCESSORY,		/**< Wired output devices such as headphone, headset, and so on. */
	MM_SOUND_VOLUME_DEVICE_OUT_BT_SCO,				/**< Bluetooth SCO device */
	MM_SOUND_VOLUME_DEVICE_OUT_BT_A2DP,				/**< Bluetooth A2DP device */
	MM_SOUND_VOLUME_DEVICE_OUT_DOCK,				/**< DOCK device */
	MM_SOUND_VOLUME_DEVICE_OUT_HDMI,				/**< HDMI device */
	MM_SOUND_VOLUME_DEVICE_OUT_MIRRORING,			/**< MIRRORING device */
	MM_SOUND_VOLUME_DEVICE_OUT_USB_AUDIO,			/**< USB Audio device */
	MM_SOUND_VOLUME_DEVICE_OUT_MULTIMEDIA_DOCK,		/**< Multimedia DOCK device */
} mm_sound_volume_device_out_t;

#define MM_SOUND_ROUTE_NUM 16
#define MM_SOUND_NAME_NUM 32

typedef enum{
	MM_SOUND_ROUTE_OUT_SPEAKER = MM_SOUND_DEVICE_OUT_SPEAKER, /**< Routing audio output to builtin device such as internal speaker. */
	MM_SOUND_ROUTE_OUT_RECEIVER = MM_SOUND_DEVICE_OUT_RECEIVER, /**< Routing audio output to builtin device such as internal receiver. */
	MM_SOUND_ROUTE_OUT_WIRED_ACCESSORY = MM_SOUND_DEVICE_OUT_WIRED_ACCESSORY,/**< Routing audio output to wired accessory such as headphone, headset, and so on. */
	MM_SOUND_ROUTE_OUT_BLUETOOTH_SCO = MM_SOUND_DEVICE_OUT_BT_SCO, /**< Routing audio output to bluetooth SCO. */
	MM_SOUND_ROUTE_OUT_BLUETOOTH_A2DP = MM_SOUND_DEVICE_OUT_BT_A2DP, /**< Routing audio output to bluetooth A2DP. */
	MM_SOUND_ROUTE_OUT_DOCK = MM_SOUND_DEVICE_OUT_DOCK, /**< Routing audio output to DOCK */
	MM_SOUND_ROUTE_OUT_HDMI = MM_SOUND_DEVICE_OUT_HDMI, /**< Routing audio output to HDMI */
	MM_SOUND_ROUTE_OUT_MIRRORING = MM_SOUND_DEVICE_OUT_MIRRORING, /**< Routing audio output to MIRRORING */
	MM_SOUND_ROUTE_OUT_USB_AUDIO = MM_SOUND_DEVICE_OUT_USB_AUDIO, /**< Routing audio output to USB Audio */
	MM_SOUND_ROUTE_OUT_MULTIMEDIA_DOCK = MM_SOUND_DEVICE_OUT_MULTIMEDIA_DOCK, /**< Routing audio output to Multimedia DOCK */
	MM_SOUND_ROUTE_IN_MIC = MM_SOUND_DEVICE_IN_MIC, /**< Routing audio input to device builtin mic. */
	MM_SOUND_ROUTE_IN_WIRED_ACCESSORY = MM_SOUND_DEVICE_IN_WIRED_ACCESSORY, /**< Routing audio input to wired accessory. */
	MM_SOUND_ROUTE_IN_MIC_OUT_RECEIVER = MM_SOUND_DEVICE_IN_MIC | MM_SOUND_DEVICE_OUT_RECEIVER, /**< Routing audio input to device builtin mic and routing audio output to builtin receiver*/
	MM_SOUND_ROUTE_IN_MIC_OUT_SPEAKER = MM_SOUND_DEVICE_IN_MIC | MM_SOUND_DEVICE_OUT_SPEAKER , /**< Routing audio input to device builtin mic and routing audio output to builtin speaker */
	MM_SOUND_ROUTE_IN_MIC_OUT_HEADPHONE = MM_SOUND_DEVICE_IN_MIC | MM_SOUND_DEVICE_OUT_WIRED_ACCESSORY,/**< Routing audio input to device builtin mic and routing audio output to headphone */
	MM_SOUND_ROUTE_INOUT_HEADSET = MM_SOUND_DEVICE_IN_WIRED_ACCESSORY | MM_SOUND_DEVICE_OUT_WIRED_ACCESSORY,	/**< Routing audio input and output to headset*/
	MM_SOUND_ROUTE_INOUT_BLUETOOTH = MM_SOUND_DEVICE_IN_BT_SCO | MM_SOUND_DEVICE_OUT_BT_SCO /**< Routing audio input and output to bluetooth SCO */
} mm_sound_route;

/*
 * MMSound Device APIs
 */

typedef enum {
	MM_SOUND_DEVICE_IO_DIRECTION_IN_FLAG      = 0x0001,  /**< Flag for input devices */
	MM_SOUND_DEVICE_IO_DIRECTION_OUT_FLAG     = 0x0002,  /**< Flag for output devices */
	MM_SOUND_DEVICE_IO_DIRECTION_BOTH_FLAG    = 0x0004,  /**< Flag for input/output devices (both directions are available) */
	MM_SOUND_DEVICE_TYPE_INTERNAL_FLAG        = 0x0010,  /**< Flag for built-in devices */
	MM_SOUND_DEVICE_TYPE_EXTERNAL_FLAG        = 0x0020,  /**< Flag for external devices */
	MM_SOUND_DEVICE_STATE_DEACTIVATED_FLAG    = 0x1000,  /**< Flag for deactivated devices */
	MM_SOUND_DEVICE_STATE_ACTIVATED_FLAG      = 0x2000,  /**< Flag for activated devices */
	MM_SOUND_DEVICE_ALL_FLAG                  = 0xFFFF,  /**< Flag for all devices */
} mm_sound_device_flags_e;

typedef enum {
	MM_SOUND_DEVICE_IO_DIRECTION_IN,
	MM_SOUND_DEVICE_IO_DIRECTION_OUT,
	MM_SOUND_DEVICE_IO_DIRECTION_BOTH,
} mm_sound_device_io_direction_e;

typedef enum {
	MM_SOUND_DEVICE_STATE_DEACTIVATED,
	MM_SOUND_DEVICE_STATE_ACTIVATED,
} mm_sound_device_state_e;

typedef enum
{
	MM_SOUND_DEVICE_TYPE_BUILTIN_SPEAKER,   /**< Built-in speaker. */
	MM_SOUND_DEVICE_TYPE_BUILTIN_RECEIVER,  /**< Built-in receiver. */
	MM_SOUND_DEVICE_TYPE_BUILTIN_MIC,       /**< Built-in mic. */
	MM_SOUND_DEVICE_TYPE_AUDIOJACK,         /**< Audio jack such as headphone, headset, and so on. */
	MM_SOUND_DEVICE_TYPE_BLUETOOTH,         /**< Bluetooth */
	MM_SOUND_DEVICE_TYPE_HDMI,              /**< HDMI. */
	MM_SOUND_DEVICE_TYPE_MIRRORING,         /**< MIRRORING. */
	MM_SOUND_DEVICE_TYPE_USB_AUDIO,         /**< USB Audio. */
} mm_sound_device_type_e;

typedef void *MMSoundDevice_t;          /**< MMsound Device handle */
typedef void *MMSoundDeviceList_t;      /**< MMsound Device list handle */
typedef void (*mm_sound_device_connected_cb) (MMSoundDevice_t device_h, bool is_connected, void *user_data);
typedef void (*mm_sound_device_info_changed_cb) (MMSoundDevice_t device_h, int changed_info_type, void *user_data);

int mm_sound_add_device_connected_callback(mm_sound_device_flags_e flags, mm_sound_device_connected_cb func, void *user_data);
int mm_sound_remove_device_connected_callback(void);
int mm_sound_add_device_information_changed_callback(mm_sound_device_flags_e flags, mm_sound_device_info_changed_cb func, void *user_data);
int mm_sound_remove_device_information_changed_callback(void);

int mm_sound_get_current_device_list(mm_sound_device_flags_e device_mask, MMSoundDeviceList_t *device_list);
int mm_sound_get_next_device (MMSoundDeviceList_t device_list, MMSoundDevice_t *device);
int mm_sound_get_prev_device (MMSoundDeviceList_t device_list, MMSoundDevice_t *device);
int mm_sound_get_device_type(MMSoundDevice_t device_h, mm_sound_device_type_e *type);
int mm_sound_get_device_io_direction(MMSoundDevice_t device_h, mm_sound_device_io_direction_e *io_direction);
int mm_sound_get_device_id(MMSoundDevice_t device_h, int *id);
int mm_sound_get_device_state(MMSoundDevice_t device_h, mm_sound_device_state_e *state);
int mm_sound_get_device_name(MMSoundDevice_t device_h, char **name);

/* below APIs are for product */
typedef int (*mm_sound_available_route_cb)(mm_sound_route route, void *user_data);
int mm_sound_is_route_available(mm_sound_route route, bool *is_available);
int mm_sound_foreach_available_route_cb(mm_sound_available_route_cb, void *user_data);
int mm_sound_set_active_route(mm_sound_route route);
int mm_sound_set_active_route_auto(void);


/**
 * This function is to set active route without callback to client.
 *
 * @param	route			[IN]	route
 * @return 	This function returns MM_ERROR_NONE on success, or negative value
 *			with error code.
 * @remark	None.
 * @pre		None.
 * @post	None.
 * @see		mm_sound_set_active_route_without_broadcast mm_sound_route
 */
int mm_sound_set_active_route_without_broadcast(mm_sound_route route);

/**
 * This function is to get active playback device and capture device.
 *
 * @param	playback_device			[out]	playback device.
 * @param	capture_device			[out]	capture device.
 *
 * @return 	This function returns MM_ERROR_NONE on success, or negative value
 *			with error code.
 * @remark	None.
 * @pre		None.
 * @post	None.
 * @see		mm_sound_set_active_route mm_sound_device_in mm_sound_device_out
 */
int mm_sound_get_active_device(mm_sound_device_in *device_in, mm_sound_device_out *device_out);

/**
 * Active device changed callback function type.
 *
 * @param	user_data		[in]	Argument passed when callback has called
 *
 * @return	No return value
 * @remark	None.
 * @see		mm_sound_add_active_device_changed_callback mm_sound_remove_active_device_changed_callback
 */
typedef void (*mm_sound_active_device_changed_cb) (mm_sound_device_in device_in, mm_sound_device_out device_out, void *user_data);

/**
 * This function is to add active device callback.
 *
 * @param	name			[in]	plugin name (name max size : MM_SOUND_NAME_NUM 32)
 * @param	func			[in]	callback function pointer
 * @param	user_data		[in]	user data passing to callback function
 *
 * @return 	This function returns MM_ERROR_NONE on success, or negative value
 * 			with error code.
 * @remark	None.
 * @see		mm_sound_remove_active_device_changed_callback mm_sound_active_device_changed_cb
 * @pre		None.
 * @post	None.
 * @par Example
 * @code

void __active_device_callback(void *user_data)
{
	printf("Callback function\n");
}

int active_device_control()
{
	int ret = 0;

	ret = mm_sound_add_active_device_changed_callback(__active_device_callback, NULL);
	if ( MM_ERROR_NONE != ret)
	{
		printf("Can not add callback\n");
	}
	else
	{
		printf("Add callback success\n");
	}

	return ret;
}

 * @endcode
 */
int mm_sound_add_active_device_changed_callback(const char *name,mm_sound_active_device_changed_cb func, void *user_data);
/**
 * This function is to remove active device callback.
 *
 * @param	name			[in]	plugin name (name max size : MM_SOUND_NAME_NUM 32)
 * @return 	This function returns MM_ERROR_NONE on success, or negative value
 * 			with error code.
 * @remark	None.
 * @pre		Active device callback should be registered.
 * @post	Active device callback deregistered and does not be called anymore.
 * @see		mm_sound_add_active_device_changed_callback mm_sound_active_device_changed_cb
 * @par Example
 * @code
void __active_device_callback(void *data)
{
	printf("Callback function\n");
}

int active_device_control()
{
	int ret = 0;

	mm_sound_add_active_device_changed_callback(__active_device_callback, NULL);

	ret = mm_sound_remove_active_device_changed_callback();
	if ( MM_ERROR_NONE == ret)
	{
		printf("Remove callback success\n");
	}
	else
	{
		printf("Remove callback failed\n");
	}

	return ret;
}

 * @endcode
 */
int mm_sound_remove_active_device_changed_callback(const char *name);
/**
 * Available route changed callback function type.
 *
 * @param	user_data		[in]	Argument passed when callback has called
 *
 * @return	No return value
 * @remark	None.
 * @see		mm_sound_add_active_device_changed_callback mm_sound_remove_active_device_changed_callback
 */
typedef void (*mm_sound_available_route_changed_cb) (mm_sound_route route, bool available, void *user_data);

/**
 * This function is to add available device callback.
 *
 * @param	func			[in]	callback function pointer
 * @param	user_data		[in]	user data passing to callback function
 *
 * @return 	This function returns MM_ERROR_NONE on success, or negative value
 * 			with error code.
 * @remark	None.
 * @see		mm_sound_remove_available_route_changed_callback mm_sound_active_device_changed_cb
 * @pre		None.
 * @post	None.
 * @par Example
 * @code

void __available_device_callback(void *user_data)
{
	printf("Callback function\n");
}

int available_device_control()
{
	int ret = 0;

	ret = mm_sound_add_available_route_changed_callback(__available_device_callback, NULL);
	if ( MM_ERROR_NONE != ret)
	{
		printf("Can not add callback\n");
	}
	else
	{
		printf("Add callback success\n");
	}

	return ret;
}

 * @endcode
 */
int mm_sound_add_available_route_changed_callback(mm_sound_available_route_changed_cb func, void *user_data);

/**
 * This function is to remove available device callback.
 *
 * @return 	This function returns MM_ERROR_NONE on success, or negative value
 * 			with error code.
 * @remark	None.
 * @pre		available device callback should be registered.
 * @post	available device callback deregistered and does not be called anymore.
 * @see		mm_sound_add_available_route_changed_callback mm_sound_active_device_changed_cb
 * @par Example
 * @code
void __available_device_callback(void *data)
{
	printf("Callback function\n");
}

int available_device_control()
{
	int ret = 0;

	mm_sound_add_available_route_changed_callback(__available_device_callback, NULL);

	ret = mm_sound_remove_available_route_changed_callback();
	if ( MM_ERROR_NONE == ret)
	{
		printf("Remove callback success\n");
	}
	else
	{
		printf("Remove callback failed\n");
	}

	return ret;
}

 * @endcode
 */
int mm_sound_remove_available_route_changed_callback(void);

 /**
 * This function is to set path for active device.
 *
 * @param	device_out		[in]	active playback device
 * @param	device_in		[in]	active capture device
 *
 * @return 	This function returns MM_ERROR_NONE on success, or negative value
 * 			with error code.
 * @remark	None.
 * @see		None
 * @pre		None.
 * @post	None.
 * @par Example
 * @*/
int mm_sound_set_sound_path_for_active_device(mm_sound_device_out device_out, mm_sound_device_in device_in);

/**
* This function is to get current audio path.
*
* @param   device_out	   [in]    active playback device
* @param   device_in	   [in]    active capture device
*
* @return  This function returns MM_ERROR_NONE on success, or negative value
*		   with error code.
* @remark  None.
* @see	   None
* @pre	   None.
* @post    None.
* @par Example
* @*/

int mm_sound_get_audio_path(mm_sound_device_in *device_in, mm_sound_device_out *device_out);

/**
	@}
 */

#ifdef __cplusplus
}
#endif

#endif	/* __MM_SOUND_H__ */

