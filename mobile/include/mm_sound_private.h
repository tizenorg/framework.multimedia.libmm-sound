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

#ifndef	__MM_SOUND_PRIVATE_H__
#define	__MM_SOUND_PRIVATE_H__

#include <mm_types.h>
#include <mm_error.h>
#include <mm_sound.h>

#ifdef __cplusplus
	extern "C" {
#endif

/**
  	@internal
	@addtogroup SOUND_PRIVATE
	@{

 */

#define MM_SOUND_VOLUME_CONFIG_TYPE(vol) (vol && 0x00FF)
#define MM_SOUND_VOLUME_CONFIG_GAIN(vol) (vol && 0xFF00)

enum MMSoundGainType {
	MM_SOUND_GAIN_KEYTONE = 0,	/**< hw gain configuration for keytone play */
	MM_SOUND_GAIN_RINGTONE,		/**< hw gain configuration for ringtone play */
	MM_SOUND_GAIN_ALARMTONE,	/**< hw gain configuration for alarmtone play */
	MM_SOUND_GAIN_CALLTONE,		/**< hw gain configuration for calltone play */
	MM_SOUND_GAIN_AUDIOPLAYER,	/**< hw gain configuration for music play */
	MM_SOUND_GAIN_VIDEOPLAYER,	/**< hw gain configuration for video play */
	MM_SOUND_GAIN_VOICECALL,	/**< hw gain configuration for voice call */
	MM_SOUND_GAIN_VIDEOCALL,	/**< hw gain configuration for video call */
	MM_SOUND_GAIN_FMRADIO,		/**< hw gain configuration for fm radio play */
	MM_SOUND_GAIN_VOICEREC,		/**< hw gain configuration for voice recording */
	MM_SOUND_GAIN_CAMCORDER,	/**< hw gain configuration for camcording */
	MM_SOUND_GAIN_CAMERA,		/**< hw gain configuration for camera shutter sound */
	MM_SOUND_GAIN_GAME,			/**< hw gain configuration for game play */
	MM_SOUND_GAIN_CNT,			/**< hw gain configuration count */
	MM_SOUND_GAIN_MUSIC = MM_SOUND_GAIN_AUDIOPLAYER,	/**< remained for legacy application */
	MM_SOUND_GAIN_VIDEO,							/**< remained for legacy application */
	MM_SOUND_GAIN_NONE = MM_SOUND_GAIN_KEYTONE,		/**< remained for legacy application */
};

/**
 * Enumerations for sound path
 */
enum MMSoundPathExType {
	MM_SOUND_PATH_NONE = 0,		/**< no route path */
	MM_SOUND_PATH_SPK,			/**< sound route to speaker */
	MM_SOUND_PATH_RECV,			/**< sound route to receiver */
	MM_SOUND_PATH_HEADSET,		/**< sound route to headset */
	MM_SOUND_PATH_BTHEADSET,		/**< sound route to bluetooth headset */
	MM_SOUND_PATH_A2DP,			/**< not used */
	MM_SOUND_PATH_HANDSFREE,		/**< not used */
	MM_SOUND_PATH_HDMI,			/**< not used */
	MM_SOUND_PATH_OUTMAX,		/**< output route count */
	MM_SOUND_PATH_MIC = 1,		/**< sound route from microphone */
	MM_SOUND_PATH_HEADSETMIC,	/**< sound route from headset microphone */
	MM_SOUND_PATH_BTMIC,			/**< sound route from bluetooth microphone */
	MM_SOUND_PATH_FMINPUT,		/**< sound route from FM radio module */
	MM_SOUND_PATH_HANDSFREEMIC,	/**< not used */
	MM_SOUND_PATH_INMAX,			/**< input route count */
};

/**
 * Enumerations for sound path option
 */
enum MMSoundPathOptionType {
	MM_SOUND_PATH_OPTION_NONE					= 0x00000000,	/**< no sound path option */
	MM_SOUND_PATH_OPTION_AUTO_HEADSET_CONTROL	= 0x00000001,	/**< automatic sound path change by earphone event */
	MM_SOUND_PATH_OPTION_SPEAKER_WITH_HEADSET	= 0x00000002,	/**< play sound via speaker and earphone (if inserted) */
	MM_SOUND_PATH_OPTION_VOICECALL_REC			= 0x00000010,	/**< voice call recording option */
	MM_SOUND_PATH_OPTION_USE_SUB_MIC				= 0x00000020,	/**< use sub-mic on call and recording */
};


enum mm_sound_handle_route_t {
	MM_SOUND_HANDLE_ROUTE_USING_CURRENT,
	MM_SOUND_HANDLE_ROUTE_SPEAKER,
	MM_SOUND_HANDLE_ROUTE_SPEAKER_NO_RESTORE
};

typedef struct {
	const char			*filename;		/**< filename to play */
	int					volume;			/**< relative volume level */
	int					loop;			/**< loop count */
	mm_sound_stop_callback_func	callback;		/**< callback function when playing is terminated */
	void				*data;			/**< user data to callback */
	void				*mem_ptr;		/**< memory buffer to play */
	int					mem_size;		/**< size of memory buffer */
	int					handle_route;	/**< 1 for speaker, 0 for current */
	int					volume_config;	/**< volume type & volume gain */
	int					priority;		/**< 0 or 1 */
} MMSoundPlayParam;


/**
 * This function is to play system sound with specified parameters.
 *
 * @param	param		[in] Reference pointer to MMSoundPlayParam structure
 * @param	handle	[out] Handle of sound play.
 *
 * @return	This function returns MM_ERROR_NONE on success, or negative value
 *			with error code.
 * @remark	When the stop callback is set, it will be called when system sound is
 *			terminated. If mm_sound_stop_sound() is called apparently before
 *			system sound is terminated, stop_callback will not be called.
 *			This function can use various sound route path with mm_sound_set_path
 * @see		mm_sound_stop_sound mm_sound_set_path
 * @since		R1, 1.0
 * @limo
 */
int mm_sound_play_sound_ex(MMSoundPlayParam *param, int *handle);


/**
 * This function is to play key sound.
 *
 * @param	filename		[in] keytone filename to play
 * @param	volume_config	[in] Volume type & volume gain
 *
 * @return	This function returns MM_ERROR_NONE on success, or negative value
 *			with error code.
 *
 * @remark	This function provide low latency sound play (such as dialer keytone)
 * 			using fixed spec of wave file (44100Hz, mono channel)
 * @see		volume_type_t volume_gain_t mm_sound_volume_set_value
 */
int mm_sound_play_keysound(const char *filename, int volume_config);


/**
 * This function is to set sound device path.
 *
 * @param	gain		[in] sound path gain
 * @param	output		[in] sound path out device
 * @param	input		[in] sound path in device
 * @param   option      [in] Sound path option
 * 								MM_SOUND_PATH_OPTION_NONE
 * 								MM_SOUND_PATH_OPTION_AUTO_HEADSET_CONTROL
 * 								MM_SOUND_PATH_OPTION_SPEAKER_WITH_HEADSET
 *
 * @return	This function returns MM_ERROR_NONE on success, or negative value
 *			with error code.
 * @remark
 * @see		mm_sound_get_path
 * @since		1.0
 */
int mm_sound_set_path(int gain, int output, int input, int option);


/**
 * This function retreives current sound device path.
 *
 * @param	gain		[out] Sound device path
 * @param	output		[out] Sound output device on/off
 * @param	input		[out] Sound input device on/off
 * @param   option      [out] Sound path option
 * 								MM_SOUND_PATH_OPTION_NONE
 * 								MM_SOUND_PATH_OPTION_AUTO_HEADSET_CONTROL
 * 								MM_SOUND_PATH_OPTION_SPEAKER_WITH_HEADSET
 *
 * @return	This function returns MM_ERROR_NONE on success, or negative value
 *			with error code.
 * @remark
 * @see		mm_sound_set_path
 */
int mm_sound_get_path(int *gain, int *output, int *input, int *option);

int mm_sound_pcm_play_open_ex (MMSoundPcmHandle_t *handle, const unsigned int rate, MMSoundPcmChannel_t channel, MMSoundPcmFormat_t format, int volume_config, int asm_event);

int mm_sound_pcm_play_open_no_session(MMSoundPcmHandle_t *handle, const unsigned int rate, MMSoundPcmChannel_t channel, MMSoundPcmFormat_t format);

/**
	@}
 */

#ifdef __cplusplus
}
#endif

#endif	/* __MM_SOUND_H__ */

