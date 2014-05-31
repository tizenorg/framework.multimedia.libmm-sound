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
#include <string.h>
#include <pthread.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdbool.h>
#include <errno.h>

#include <vconf.h>
#include <mm_error.h>
#include <mm_debug.h>
#include <audio-session-manager.h>
#include <avsys-audio.h>

#include "../include/mm_sound_common.h"
#include "../include/mm_sound_utils.h"
#include "include/mm_sound_mgr_common.h"
#include "include/mm_sound_mgr_session.h"
#include "include/mm_sound_mgr_device.h"
#include "include/mm_sound_mgr_headset.h"
#include "include/mm_sound_mgr_dock.h"
#include "include/mm_sound_mgr_pulse.h"
#include "include/mm_sound_mgr_asm.h"

#define EARJACK_UNPLUGGED	0
#define EARJACK_WITH_MIC	3

#define MAX_STRING_LEN	256

#define DEVICE_API_BLUETOOTH	"bluez"
#define DEVICE_API_ALSA	"alsa"
#define DEVICE_BUS_BLUETOOTH "bluetooth"
#define DEVICE_BUS_USB "usb"
#define DEVICE_BUS_BUILTIN "builtin"

#define MIRRORING_MONITOR_SOURCE	"alsa_output.0.analog-stereo.monitor"
#define ALSA_SINK_HDMI	"alsa_output.1.analog-stereo"

#define	MM_SOUND_DEVICE_OUT_ANY 0x000FFF00
#define MM_SOUND_DEVICE_IN_ANY	 0x000000FF

#define	MM_SOUND_DEVICE_OUT_FILTER 0x000000FF
#define MM_SOUND_DEVICE_IN_FILTER	 0x000FFF00

pthread_mutex_t g_mutex_session = PTHREAD_MUTEX_INITIALIZER;

#define LOCK_SESSION()  /* do { debug_log("(*)LOCKING\n"); pthread_mutex_lock(&g_mutex_session); debug_log("(+)LOCKED\n"); }while(0) */
#define UNLOCK_SESSION()  /* do { pthread_mutex_unlock(&g_mutex_session); debug_log("(-)UNLOCKED\n"); }while(0) */

pthread_mutex_t g_mutex_path = PTHREAD_MUTEX_INITIALIZER;

#define LOCK_PATH()  do { debug_log("(*)LOCKING\n"); pthread_mutex_lock(&g_mutex_path); debug_log("(+)LOCKED\n"); }while(0)
#define UNLOCK_PATH()  do {  pthread_mutex_unlock(&g_mutex_path); debug_log("(-)UNLOCKED\n"); }while(0)

#define RESET_ACTIVE(x)    (g_info.device_active &= x)
#define RESET_AVAILABLE(x)    (g_info.device_available &= x)

#define SET_ACTIVE(x)    (g_info.device_active |= x)
#define SET_AVAILABLE(x)    (g_info.device_available |= x)

#define SET_PLAYBACK_ONLY_ACTIVE(x)  do { RESET_ACTIVE(MM_SOUND_DEVICE_OUT_FILTER); SET_ACTIVE(x); }while(0)
#define SET_CAPTURE_ONLY_ACTIVE(x)  do {  RESET_ACTIVE(MM_SOUND_DEVICE_IN_FILTER); SET_ACTIVE(x); }while(0)


#define UNSET_ACTIVE(x)    (g_info.device_active &= (~x))
#define UNSET_AVAILABLE(x)    (g_info.device_available &= (~x))

#define TOGGLE_ACTIVE(x)    (g_info.device_active ^= x)
#define TOGGLE_AVAILABLE(x)    (g_info.device_available ^= x)

#define IS_ACTIVE(x)    (g_info.device_active & x)
#define IS_AVAILABLE(x)    (g_info.device_available & x)

#define GET_AVAILABLE_PLAYBACK()	IS_AVAILABLE(MM_SOUND_DEVICE_OUT_ANY)
#define GET_AVAILABLE_CAPTURE()	IS_AVAILABLE(MM_SOUND_DEVICE_IN_ANY)

#define GET_ACTIVE_PLAYBACK()	IS_ACTIVE(MM_SOUND_DEVICE_OUT_ANY)
#define GET_ACTIVE_CAPTURE()	IS_ACTIVE(MM_SOUND_DEVICE_IN_ANY)

#define IS_CALL_SESSION() ((g_info.session == SESSION_VOICECALL) || (g_info.session == SESSION_VIDEOCALL) || (g_info.session == SESSION_VOIP))
#define IS_ALARM_SESSION() (g_info.session == SESSION_ALARM)
#define IS_NOTIFICATION_SESSION() (g_info.session == SESSION_NOTIFICATION)
#define IS_EMERGENCY_SESSION() (g_info.session == SESSION_EMERGENCY)
#define IS_MEDIA_SESSION() (g_info.session == SESSION_MEDIA)

typedef enum {
    ROUTE_PARAM_NONE = 0x00000000,
    ROUTE_PARAM_BROADCASTING = 0x00000001,
    ROUTE_PARAM_CORK_DEVICE = 0x00000010,
} mm_sound_route_param_t;

static int __set_route(mm_sound_route_param_t param);
static int __set_sound_path_for_current_active (mm_sound_route_param_t param);
static int __set_sound_path_to_dual ();
static int __set_sound_path_to_earphone_only (void);
static int __set_sound_path_to_speaker ();
static int __set_sound_path_for_voicecontrol (void);
static void __select_playback_active_device (void);
static void __select_capture_active_device (void);
#ifndef _TIZEN_PUBLIC_
#ifndef TIZEN_MICRO
static bool __is_noise_reduction_on (void);
static bool __is_extra_volume_on (void);
#endif
static bool __is_upscaling_needed (void);
static bool __is_bt_nrec_on (void);
static bool __is_bt_wb_on (void);
#endif

#define ENABLE_CALLBACK
#ifndef ENABLE_CALLBACK
#define _mm_sound_mgr_device_available_device_callback(a,b,c)	MM_ERROR_NONE
#define _mm_sound_mgr_device_active_device_callback(a,b)	MM_ERROR_NONE
#endif

typedef struct _bt_info_struct
{
	bool is_nrec;
	bool is_wb;
	char name[MAX_STRING_LEN];
} BT_INFO_STRUCT;

typedef struct _session_info_struct
{
	int asm_handle;
	int device_available;
	int device_active;
	int headset_type;
#ifndef TIZEN_MICRO
	bool is_noise_reduction;
	bool is_extra_volume;
#endif
	bool is_upscaling_needed;
	bool is_voicecontrol;

	session_t session;
	subsession_t subsession;
	mm_subsession_option_t option;

	device_type_t previous_playback_device;
	device_type_t previous_capture_device;
	int previous_device_available;

	BT_INFO_STRUCT bt_info;
	char default_sink_name[MAX_STRING_LEN];

} SESSION_INFO_STRUCT;


SESSION_INFO_STRUCT g_info;

static void dump_info ()
{
	int i = 0;

	const char *playback_device_str[] = { "SPEAKER ", "RECEIVER ", "HEADSET ", "BTSCO ", "BTA2DP ", "DOCK ", "HDMI ", "MIRRORING ", "USB " };
	const char *capture_device_str[] = { "MAINMIC ", "HEADSET ", "BTMIC "  };

	int playback_max = sizeof (playback_device_str) / sizeof (char*);
	int capture_max = sizeof (capture_device_str) / sizeof (char*);

	static char tmp_str[128];
	static char tmp_str2[128];

	debug_log ("<----------------------------------------------------->\n");


	strcpy (tmp_str, "PLAYBACK = [ ");
	for (i=0; i<playback_max; i++) {
		if (((g_info.device_available & MM_SOUND_DEVICE_OUT_ANY) >> 8) & (0x01 << i)) {
			strcat (tmp_str, playback_device_str[i]);
		}
	}
	strcat (tmp_str, "]");

	strcpy (tmp_str2, "CAPTURE = [ ");
		for (i=0; i<capture_max; i++) {
			if ((g_info.device_available & MM_SOUND_DEVICE_IN_ANY) & (0x01 << i)) {
				strcat (tmp_str2, capture_device_str[i]);
			}
	}
	strcat (tmp_str2, "]");
	debug_warning ("*** Available = [0x%08x], %s %s", g_info.device_available, tmp_str, tmp_str2);

	strcpy (tmp_str, "PLAYBACK = [ ");
	for (i=0; i<playback_max; i++) {
		if (((g_info.device_active & MM_SOUND_DEVICE_OUT_ANY) >> 8) & (0x01 << i)) {
			strcat (tmp_str, playback_device_str[i]);
		}
	}
	strcat (tmp_str, "]");

	strcpy (tmp_str2, "CAPTURE = [ ");
		for (i=0; i<capture_max; i++) {
			if ((g_info.device_active & MM_SOUND_DEVICE_IN_ANY) & (0x01 << i)) {
				strcat (tmp_str2, capture_device_str[i]);
			}
	}
	strcat (tmp_str2, "]");
	debug_warning ("***    Active = [0x%08x], %s %s", g_info.device_active, tmp_str, tmp_str2);


	debug_warning ("*** Headset type = [%d], BT = [%s], default sink = [%s]\n", g_info.headset_type, g_info.bt_info.name, g_info.default_sink_name);
	debug_warning ("*** Session = [%d], SubSession = [%d]\n", g_info.session, g_info.subsession);
	debug_log ("<----------------------------------------------------->\n");
}

/* ------------------------- ASM ------------------------------------*/
static pthread_mutex_t _asm_mutex = PTHREAD_MUTEX_INITIALIZER;

static bool _asm_register_for_headset (int * handle)
{
	int asm_error = 0;

	if (handle == NULL) {
		debug_error ("Handle is not valid!!!\n");
		return false;
	}

	if (!ASM_register_sound_ex (-1, handle, ASM_EVENT_EARJACK_UNPLUG, ASM_STATE_NONE, NULL, NULL, ASM_RESOURCE_NONE, &asm_error, __asm_process_message)) {
		debug_warning("earjack event register failed with 0x%x\n", asm_error);
		return false;
	}

	return true;
}

static void _asm_pause_process(int handle)
{
	int asm_error = 0;

	MMSOUND_ENTER_CRITICAL_SECTION( &_asm_mutex )

	/* If no asm handle register here */
	if (g_info.asm_handle ==  -1) {
		debug_msg ("ASM handle is not valid, try to register once more\n");

		/* This register should be success */
		if (_asm_register_for_headset (&g_info.asm_handle)) {
			debug_msg("_asm_register_for_headset() success\n");
		} else {
			debug_error("_asm_register_for_headset() failed\n");
		}
	}

	//do pause
	debug_warning("Send earphone unplug event to Audio Session Manager Server\n");

	if (!ASM_set_sound_state_ex(handle, ASM_EVENT_EARJACK_UNPLUG, ASM_STATE_PLAYING, ASM_RESOURCE_NONE, &asm_error, __asm_process_message)) {
		debug_error("earjack event set sound state to playing failed with 0x%x\n", asm_error);
	}

	if (!ASM_set_sound_state_ex(handle, ASM_EVENT_EARJACK_UNPLUG, ASM_STATE_STOP, ASM_RESOURCE_NONE, &asm_error, __asm_process_message)) {
		debug_error("earjack event set sound state to stop failed with 0x%x\n", asm_error);
	}

	MMSOUND_LEAVE_CRITICAL_SECTION( &_asm_mutex )
}

static bool _asm_unregister_for_headset (int *handle)
{
	int asm_error = 0;

	if (handle == NULL) {
		debug_error ("Handle is not valid!!!\n");
		return false;
	}

	if (!ASM_unregister_sound_ex(*handle, ASM_EVENT_EARJACK_UNPLUG, &asm_error, __asm_process_message)) {
		debug_error("earjack event unregister failed with 0x%x\n", asm_error);
		return false;
	}

	return true;
}

/* ------------------------- INTERNAL FUNCTIONS ------------------------------------*/

static void __backup_current_active_device()
{
	g_info.previous_playback_device = GET_ACTIVE_PLAYBACK();
	g_info.previous_capture_device = GET_ACTIVE_CAPTURE();
	g_info.previous_device_available = g_info.device_available;

}

static void __restore_previous_active_device()
{
	RESET_ACTIVE(0);

	debug_msg ("available device (0x%x => 0x%x)", g_info.previous_device_available, g_info.device_available);
	if (g_info.previous_device_available == g_info.device_available) {
		/* No Changes */
		g_info.device_active |= g_info.previous_playback_device;
		g_info.device_active |= g_info.previous_capture_device;
	} else {
		/* Changes happens */
		__select_playback_active_device();
		__select_capture_active_device();
	}
}


static int __set_route(mm_sound_route_param_t param)
{
	int ret = MM_ERROR_NONE;

	debug_msg ("param 0x%x\n", param);

	LOCK_PATH();

	if (param & ROUTE_PARAM_BROADCASTING) {
		/* Notify current active device */
		ret = _mm_sound_mgr_device_active_device_callback(GET_ACTIVE_CAPTURE(), GET_ACTIVE_PLAYBACK());
		if (ret != MM_ERROR_NONE) {
			debug_error ("_mm_sound_mgr_device_active_device_callback() failed [%x]\n", ret);
		}
	}

	/* Set path based on current active device */
	ret = __set_sound_path_for_current_active(param);
	if (ret != MM_ERROR_NONE) {
		debug_error ("__set_sound_path_for_current_active() failed [%x]\n", ret);
		UNLOCK_PATH();
		return ret;
	}


	UNLOCK_PATH();
	return ret;
}

static int __set_route_nolock(mm_sound_route_param_t param)
{
	int ret = MM_ERROR_NONE;

	debug_msg ("param 0x%x\n", param);

	/* Set path based on current active device */
	ret = __set_sound_path_for_current_active(param);
	if (ret != MM_ERROR_NONE) {
		debug_error ("__set_sound_path_for_current_active() failed [%x]\n", ret);
		UNLOCK_PATH();
		return ret;
	}

	if (param & ROUTE_PARAM_BROADCASTING) {
		/* Notify current active device */
		ret = _mm_sound_mgr_device_active_device_callback(GET_ACTIVE_CAPTURE(), GET_ACTIVE_PLAYBACK());
		if (ret != MM_ERROR_NONE) {
			debug_error ("_mm_sound_mgr_device_active_device_callback() failed [%x]\n", ret);
		}
	}
	return ret;
}

static int __set_playback_route_media (session_state_t state)
{
	int ret = MM_ERROR_NONE;

	debug_fenter();

	if (state == SESSION_START) {
		dump_info();
	} else { /* SESSION_END */
		__set_route(ROUTE_PARAM_NONE);
		dump_info();
	}

	debug_fleave();
	return ret;
}

static int __set_playback_route_voip (session_state_t state)
{
	int ret = MM_ERROR_NONE;

	debug_fenter();
	if (state == SESSION_START) {

		/* Load AEC Module */
#if 0 /* FIXME : enable after failure is fixed */
		MMSoundMgrPulseHandleAECLoadModule();
#endif
		/* Enable Receiver Device */
		debug_log ("voip call session started...");

		/* Backup current active for future restore */
		__backup_current_active_device();

		/* Set default subsession as VOICE */
		g_info.subsession = SUBSESSION_MEDIA;

		/* OUT */
#if 0
		if (IS_ACTIVE(MM_SOUND_DEVICE_OUT_SPEAKER)) {
			debug_log ("active out was SPEAKER => activate receiver!!\n");
			SET_PLAYBACK_ONLY_ACTIVE(MM_SOUND_DEVICE_OUT_RECEIVER);
		} else if (IS_ACTIVE(MM_SOUND_DEVICE_OUT_BT_A2DP)) {
			debug_log ("active out was BT A2DP => activate BT SCO!!\n");
			SET_PLAYBACK_ONLY_ACTIVE(MM_SOUND_DEVICE_OUT_BT_SCO);
			SET_CAPTURE_ONLY_ACTIVE(MM_SOUND_DEVICE_IN_BT_SCO);
		}
		__set_route(true, false);
#else
		SET_PLAYBACK_ONLY_ACTIVE(MM_SOUND_DEVICE_OUT_SPEAKER);
		SET_CAPTURE_ONLY_ACTIVE(MM_SOUND_DEVICE_IN_MIC);
#endif

		dump_info();

	} else { /* SESSION_END */

		/* UnLoad AEC Module */
#if 0 /* FIXME : enable after failure is fixed */
		MMSoundMgrPulseHandleAECUnLoadModule();
#endif

		/* RESET */
		if (g_info.session != SESSION_VOIP) {
			debug_warning ("Must be VOIP session but current session is [%d]\n", g_info.session);
		}
		if (AVSYS_FAIL(avsys_audio_set_path_ex( AVSYS_AUDIO_GAIN_EX_VOIP,
				AVSYS_AUDIO_PATH_EX_NONE, AVSYS_AUDIO_PATH_EX_NONE,
				AVSYS_AUDIO_PATH_OPTION_NONE ))) {
			debug_error ("avsys_audio_set_path_ex() failed [%x]\n", ret);
			ret = MM_ERROR_SOUND_INTERNAL;
			goto ROUTE_VOIP_EXIT;
		}

		debug_log ("Reset ACTIVE, activate previous active device if still available, if not, set based on priority");
		__restore_previous_active_device();

		debug_log ("voip call session stopped...set path based on current active device");
		__set_route(ROUTE_PARAM_BROADCASTING);

		dump_info();
	}

ROUTE_VOIP_EXIT:

	debug_fleave();
	return ret;
}

static int __set_playback_route_call (session_state_t state)
{
	int ret = MM_ERROR_NONE;
#if 0
	int gain;
	int is_loopback=0;
#endif

	debug_fenter();

	if (state == SESSION_START) {
		debug_log ("voicecall session started...");

		/* Backup current active for future restore */
		__backup_current_active_device();

		/* Set default subsession as MEDIA */
		g_info.subsession = SUBSESSION_MEDIA;

#if 0
		mm_sound_get_factory_loopback_test(&is_loopback);

		/* (speaker = receiver, headset = headset, bt a2dp = bt sco) */
		/* OUT */
		if (IS_ACTIVE(MM_SOUND_DEVICE_OUT_SPEAKER) && !is_loopback) {
			debug_log ("active out was SPEAKER => activate receiver!!\n");
			SET_PLAYBACK_ONLY_ACTIVE(MM_SOUND_DEVICE_OUT_RECEIVER);

		} else if (IS_ACTIVE(MM_SOUND_DEVICE_OUT_BT_A2DP)) {
			debug_log ("active out was BT A2DP => activate BT SCO!!\n");
			SET_PLAYBACK_ONLY_ACTIVE(MM_SOUND_DEVICE_OUT_BT_SCO);
			SET_CAPTURE_ONLY_ACTIVE(MM_SOUND_DEVICE_IN_BT_SCO);
		}
		/* FIXME : Do we have to set IN device ??? */
#else
		SET_PLAYBACK_ONLY_ACTIVE(MM_SOUND_DEVICE_OUT_SPEAKER);
		SET_CAPTURE_ONLY_ACTIVE(MM_SOUND_DEVICE_IN_MIC);
#endif

//		__set_path_with_notification(DO_NOTI);

		dump_info();

	} else { /* SESSION_END */
		if (AVSYS_FAIL(avsys_audio_set_path_ex( AVSYS_AUDIO_GAIN_EX_VOICECALL,
				AVSYS_AUDIO_PATH_EX_NONE, AVSYS_AUDIO_PATH_EX_NONE,
				AVSYS_AUDIO_PATH_OPTION_NONE ))) {
			debug_error ("avsys_audio_set_path_ex() failed [%x]\n", ret);
			ret = MM_ERROR_SOUND_INTERNAL;
			goto ROUTE_CALL_EXIT;
		}

		debug_log ("Reset ACTIVE, activate previous active device if still available, if not, set based on priority");
		__restore_previous_active_device();

		debug_log ("voicecall session stopped...set path based on current active device");
		__set_route(ROUTE_PARAM_BROADCASTING);

		dump_info();
	}

ROUTE_CALL_EXIT:

	debug_fleave();

	return ret;
}

static int __set_playback_route_fmradio (session_state_t state)
{
	int ret = MM_ERROR_NONE;
	int out = AVSYS_AUDIO_PATH_EX_NONE;

	debug_fenter();

	if (state == SESSION_START) {

		if (IS_ACTIVE(MM_SOUND_DEVICE_OUT_SPEAKER))
			out = AVSYS_AUDIO_PATH_EX_SPK;
		else if (IS_ACTIVE(MM_SOUND_DEVICE_OUT_WIRED_ACCESSORY))
			out = AVSYS_AUDIO_PATH_EX_HEADSET;
		else if (IS_ACTIVE(MM_SOUND_DEVICE_OUT_BT_A2DP))
			out = AVSYS_AUDIO_PATH_EX_A2DP;

		/* PATH SET */
		if (AVSYS_FAIL(avsys_audio_set_path_ex( AVSYS_AUDIO_GAIN_EX_FMRADIO,
										out, AVSYS_AUDIO_PATH_EX_FMINPUT,
										AVSYS_AUDIO_PATH_OPTION_NONE))) {
			debug_error ("avsys_audio_set_path_ex() failed\n");
			ret = MM_ERROR_SOUND_INTERNAL;
			goto ROUTE_FMRADIO_EXIT;
		}


	} else { /* SESSION_END */
		/* PATH RELEASE */
		if (AVSYS_FAIL(avsys_audio_set_path_ex( AVSYS_AUDIO_GAIN_EX_FMRADIO,
										AVSYS_AUDIO_PATH_EX_NONE, AVSYS_AUDIO_PATH_EX_NONE,
										AVSYS_AUDIO_PATH_OPTION_NONE ))) {
			debug_error ("avsys_audio_set_path_ex() failed\n");
			ret = MM_ERROR_SOUND_INTERNAL;
			goto ROUTE_FMRADIO_EXIT;
		}

		/* Set as current active status */
		__set_route(ROUTE_PARAM_NONE);
	}

	if (AVSYS_FAIL(avsys_audio_set_ext_device_status(AVSYS_AUDIO_EXT_DEVICE_FMRADIO, state))) {
		debug_error ("avsys_audio_set_ext_device_status() failed\n");
		ret = MM_ERROR_SOUND_INTERNAL;
	}

ROUTE_FMRADIO_EXIT:

	debug_fleave();

	return ret;
}

static int __set_playback_route_notification (session_state_t state)
{
	int ret = MM_ERROR_NONE;

	debug_fenter();

	if (state == SESSION_START) {
		if (_mm_sound_is_recording() || _mm_sound_is_mute_policy()) {
			/* Force earphone path for mute case */
			if ((ret = __set_sound_path_to_earphone_only ()) != MM_ERROR_NONE) {
				debug_error ("__set_sound_path_to_earphone_only() failed [%x]\n", ret);
			}
		} else {
			/* normal dual path */
			if ((ret = __set_sound_path_to_dual ()) != MM_ERROR_NONE) {
				debug_error ("__set_sound_path_to_dual() failed [%x]\n", ret);
			}
		}
	} else { /* SESSION_END */
		__set_route(ROUTE_PARAM_NONE);
	}

	debug_fleave();

	return ret;
}

static int __set_playback_route_alarm (session_state_t state)
{
	int ret = MM_ERROR_NONE;

	debug_fenter();

	if (state == SESSION_START) {
		if ((ret = __set_sound_path_to_dual ()) != MM_ERROR_NONE) {
			debug_error ("__set_sound_path_to_dual() failed [%x]\n", ret);
		}
	} else { /* SESSION_END */
		__set_route(ROUTE_PARAM_NONE);
	}

	debug_fleave();

	return ret;
}

static int __set_playback_route_emergency (session_state_t state)
{
	int ret = MM_ERROR_NONE;

	debug_fenter();

	if (state == SESSION_START) {
		ret = __set_sound_path_to_speaker ();
		if (ret != MM_ERROR_NONE) {
			debug_error ("__set_sound_path_to_speaker() failed [%x]\n", ret);
		}

	} else { /* SESSION_END */
		__set_route(ROUTE_PARAM_NONE);
	}

	debug_fleave();

	return ret;
}

static int __set_playback_route_voicerecognition (session_state_t state)
{
	int ret = MM_ERROR_NONE;

	debug_fenter();

	if (state == SESSION_START) {
		g_info.subsession = SUBSESSION_INIT;
		/* audio path is changed when subsession is set */

	} else { /* SESSION_END */

#ifdef TIZEN_MICRO
		/* Sound path for ALSA */
		debug_log ("Set path to RESET VOICECALL\n");
		if (AVSYS_FAIL(avsys_audio_set_path_ex(AVSYS_AUDIO_GAIN_EX_VOICERECOGNITION,
						AVSYS_AUDIO_PATH_EX_NONE, AVSYS_AUDIO_PATH_EX_NONE, 0))) {
			debug_error ("avsys_audio_set_path_ex failed in case BT SCO\n");
		}
#endif
		__set_route(ROUTE_PARAM_BROADCASTING);
#if 0 //def TIZEN_MICRO
		MMSoundMgrSessionSetSCO (0);
		MMSoundMgrSessionSetDeviceAvailable (DEVICE_BT_SCO, 0, 0, NULL);


#endif
	}

	debug_fleave();

	return ret;
}

static bool __is_forced_session ()
{
	return (IS_ALARM_SESSION() || IS_NOTIFICATION_SESSION() || IS_EMERGENCY_SESSION())? true : false;
}

static bool __is_recording_subsession (int *recording_option)
{
	bool is_recording = true;
	int option = AVSYS_AUDIO_PATH_OPTION_NONE;

	switch (g_info.subsession) {
		case SUBSESSION_RECORD_STEREO:
			option = AVSYS_AUDIO_PATH_OPTION_USE_STEREOMIC;
			break;
		case SUBSESSION_RECORD_STEREO_FOR_INTERVIEW:
			option = AVSYS_AUDIO_PATH_OPTION_USE_STEREOMIC_DIRECTNL_INTV;
			break;
		case SUBSESSION_RECORD_STEREO_FOR_CONVERSATION:
			option = AVSYS_AUDIO_PATH_OPTION_USE_STEREOMIC_DIRECTNL_CONVS;
			break;
		case SUBSESSION_RECORD_MONO:
			option = AVSYS_AUDIO_PATH_OPTION_USE_MAINMIC;
			break;
		default:
			is_recording = false;
			break;
	}

	if (recording_option)
		*recording_option = option;

	return is_recording;
}



static int __set_sound_path_for_current_active (mm_sound_route_param_t param)
{
	int ret = MM_ERROR_NONE;
	int option = AVSYS_AUDIO_PATH_OPTION_NONE;
	int recording_option = AVSYS_AUDIO_PATH_OPTION_NONE;
	int in = 0, out = 0, gain = 0;

	debug_fenter();

	debug_msg ("session:%d, subsession:%d, option:%d, active in:0x%x, out:0x%x, param:0x%x",
			g_info.session, g_info.subsession, g_info.option, GET_ACTIVE_CAPTURE(), GET_ACTIVE_PLAYBACK(), param);

	if (__is_forced_session()) {
		debug_warning ("Current session is ALARM/NOTI/EMER, pending path setting. path set will be done after session ends");
		MMSoundMgrPulseSetActiveDevice(GET_ACTIVE_CAPTURE(), GET_ACTIVE_PLAYBACK());
		return MM_ERROR_NONE;
	}

	if (param & ROUTE_PARAM_CORK_DEVICE)
		MMSoundMgrPulseSetCorkAll (true);

	/* prepare IN */
	if (IS_ACTIVE(MM_SOUND_DEVICE_IN_MIC)) {
		in = AVSYS_AUDIO_PATH_EX_MIC;
	} else if (IS_ACTIVE(MM_SOUND_DEVICE_IN_WIRED_ACCESSORY)) {
		in = AVSYS_AUDIO_PATH_EX_HEADSETMIC;
	} else if (IS_ACTIVE(MM_SOUND_DEVICE_IN_BT_SCO)) {
		in = AVSYS_AUDIO_PATH_EX_BTMIC;
	}

	/* prepare OUT */
	if (IS_ACTIVE(MM_SOUND_DEVICE_OUT_SPEAKER)) {
		out = AVSYS_AUDIO_PATH_EX_SPK;
	} else if (IS_ACTIVE(MM_SOUND_DEVICE_OUT_BT_A2DP)) {
		out = AVSYS_AUDIO_PATH_EX_SPK;
	} else if (IS_ACTIVE(MM_SOUND_DEVICE_OUT_USB_AUDIO)) {
		out = AVSYS_AUDIO_PATH_EX_SPK;
	} else if (IS_ACTIVE(MM_SOUND_DEVICE_OUT_MULTIMEDIA_DOCK)) {
		out = AVSYS_AUDIO_PATH_EX_SPK;
	} else if (IS_ACTIVE(MM_SOUND_DEVICE_OUT_RECEIVER)) {
		out = AVSYS_AUDIO_PATH_EX_RECV;
	} else if (IS_ACTIVE(MM_SOUND_DEVICE_OUT_WIRED_ACCESSORY)) {
		out = AVSYS_AUDIO_PATH_EX_HEADSET;
	} else if (IS_ACTIVE(MM_SOUND_DEVICE_OUT_BT_SCO)) {
		out = AVSYS_AUDIO_PATH_EX_BTHEADSET;
	} else if (IS_ACTIVE(MM_SOUND_DEVICE_OUT_DOCK)) {
		out = AVSYS_AUDIO_PATH_EX_DOCK;
	} else if (IS_ACTIVE(MM_SOUND_DEVICE_OUT_HDMI)) {
		out = AVSYS_AUDIO_PATH_EX_HDMI;
	} else if (IS_ACTIVE(MM_SOUND_DEVICE_OUT_MIRRORING)) {
		out = AVSYS_AUDIO_PATH_EX_WFD;
	}

	if (in == AVSYS_AUDIO_PATH_EX_BTMIC && out == AVSYS_AUDIO_PATH_EX_BTHEADSET) {
		if (__is_bt_nrec_on())
			option |= AVSYS_AUDIO_PATH_OPTION_BT_NREC;
		if (__is_bt_wb_on())
			option |= AVSYS_AUDIO_PATH_OPTION_BT_WB;
	}

	/* prepare GAIN */
	switch (g_info.session) {
	case SESSION_MEDIA:
	case SESSION_NOTIFICATION:
	case SESSION_ALARM:
	case SESSION_EMERGENCY:
		if (IS_MEDIA_SESSION() && __is_recording_subsession(&recording_option)) {
			gain = AVSYS_AUDIO_GAIN_EX_VOICEREC;
			option |= recording_option;
		} else {
			gain = AVSYS_AUDIO_GAIN_EX_KEYTONE;
			if (g_info.is_voicecontrol) {
				debug_warning ("VoiceControl");
				option |= AVSYS_AUDIO_PATH_OPTION_BARGEIN;
			}
		}
#if 0//def TIZEN_MICRO
		if (out == AVSYS_AUDIO_PATH_EX_BTHEADSET && in == AVSYS_AUDIO_PATH_EX_BTMIC) {
			gain = AVSYS_AUDIO_GAIN_EX_VOICECALL;
		}
#endif
		break;

	case SESSION_VOIP:
		if (g_info.subsession == SUBSESSION_RINGTONE) {
			gain = AVSYS_AUDIO_GAIN_EX_RINGTONE;
			in = AVSYS_AUDIO_PATH_EX_NONE;

			/* If active device was WFD(mirroring), set option */
			if (out == AVSYS_AUDIO_PATH_EX_WFD) {
				option |= AVSYS_AUDIO_PATH_OPTION_MIRRORING;
			}

			if (_mm_sound_is_mute_policy ()) {
				/* Mute Ringtone */
				out = AVSYS_AUDIO_PATH_EX_HEADSET;
			} else {
				/* Normal Ringtone */
				out = AVSYS_AUDIO_PATH_EX_SPK;
				option |= AVSYS_AUDIO_PATH_OPTION_DUAL_OUT;
			}
		} else if (g_info.subsession == SUBSESSION_VOICE) {
			gain = AVSYS_AUDIO_GAIN_EX_VOIP;
		} else {
			debug_warning ("Unexpected SUBSESSION [%d]\n", g_info.subsession);
			/* FIXME: remove following gain set after MEDIA is deprecated */
			gain = AVSYS_AUDIO_GAIN_EX_VOIP;
		}
		break;

	case SESSION_VOICECALL:
	case SESSION_VIDEOCALL:
		if (g_info.subsession == SUBSESSION_RINGTONE) {
#ifndef _TIZEN_PUBLIC_
#ifndef TIZEN_MICRO
			int vr_enabled = 0;
#endif
			int vr_ringtone_enabled = 0;
#endif

			gain = AVSYS_AUDIO_GAIN_EX_RINGTONE;
			in = AVSYS_AUDIO_PATH_EX_NONE;

			/* If active device was WFD(mirroring), set option */
			if (out == AVSYS_AUDIO_PATH_EX_WFD) {
				option |= AVSYS_AUDIO_PATH_OPTION_MIRRORING;
			}

#ifndef _TIZEN_PUBLIC_
#ifdef TIZEN_MICRO
			MMSoundMgrPulseHandleAecSetVolume(VOLUME_TYPE_RINGTONE);

			if (vconf_get_bool(VCONF_KEY_VR_RINGTONE_ENABLED, &vr_ringtone_enabled)) {
				debug_warning("vconf_get_bool for %s failed\n", VCONF_KEY_VR_RINGTONE_ENABLED);
			}

			if (vr_ringtone_enabled) {
				option |= AVSYS_AUDIO_PATH_OPTION_BARGEIN;
			}
#else
			/* If voice control for incoming call is enabled, set capture path here for avoiding ringtone breaks */
			if (vconf_get_bool(VCONF_KEY_VR_ENABLED, &vr_enabled)) {
				debug_warning("vconf_get_bool for %s failed\n", VCONF_KEY_VR_ENABLED);
			} else if (vconf_get_bool(VCONF_KEY_VR_RINGTONE_ENABLED, &vr_ringtone_enabled)) {
				debug_warning("vconf_get_bool for %s failed\n", VCONF_KEY_VR_RINGTONE_ENABLED);
			}

			if (vr_enabled && vr_ringtone_enabled) {
				option |= AVSYS_AUDIO_PATH_OPTION_BARGEIN;
			}
#endif /* TIZEN_MICRO */
#endif /* #ifndef _TIZEN_PUBLIC_ */

			if (_mm_sound_is_mute_policy ()) {
				/* Mute Ringtone */
				out = AVSYS_AUDIO_PATH_EX_HEADSET;
			} else {
				/* Normal Ringtone */
				out = AVSYS_AUDIO_PATH_EX_SPK;
				option |= AVSYS_AUDIO_PATH_OPTION_DUAL_OUT;
			}
		} else if (g_info.subsession == SUBSESSION_MEDIA) {
			gain = AVSYS_AUDIO_GAIN_EX_CALLTONE;
			in = AVSYS_AUDIO_PATH_EX_NONE;
		} else if (g_info.subsession == SUBSESSION_VOICE) {
			gain = (g_info.session == SESSION_VOICECALL)?
					AVSYS_AUDIO_GAIN_EX_VOICECALL : AVSYS_AUDIO_GAIN_EX_VIDEOCALL;
#ifdef _TIZEN_PUBLIC_
			if (out == AVSYS_AUDIO_PATH_EX_HEADSET) {
				debug_log ("Fix in path to headsetmic when out path is headset\n");
				in = AVSYS_AUDIO_PATH_EX_HEADSETMIC;
			}
#else
#ifndef TIZEN_MICRO
			if (g_info.is_noise_reduction)
				option |= AVSYS_AUDIO_PATH_OPTION_USE_2MIC;
			if (g_info.is_extra_volume)
				option |= AVSYS_AUDIO_PATH_OPTION_CALL_EXTRA_VOL;
			if (g_info.is_upscaling_needed)
				option |= AVSYS_AUDIO_PATH_OPTION_CALL_NB;
#else
			if (__is_upscaling_needed())
				option |= AVSYS_AUDIO_PATH_OPTION_CALL_NB;
			if (__is_bt_wb_on())
				option |= AVSYS_AUDIO_PATH_OPTION_BT_WB;
#endif /* TIZEN_MICRO */

#endif /* _TIZEN_PUBLIC_*/
		} else if (g_info.subsession == SUBSESSION_VOICE_ANSWER_PLAY) {
			gain = AVSYS_AUDIO_GAIN_EX_VOICECALL;
			option |= AVSYS_AUDIO_PATH_OPTION_VOICECALL_PLAY_AM;
		} else if (g_info.subsession == SUBSESSION_VOICE_ANSWER_REC) {
			gain = AVSYS_AUDIO_GAIN_EX_VOICECALL;
			option |= AVSYS_AUDIO_PATH_OPTION_VOICECALL_REC_AM;
		} else {
			debug_warning ("Unexpected SUBSESSION [%d]\n", g_info.subsession);
		}
		break;

	case SESSION_FMRADIO:
		gain = AVSYS_AUDIO_GAIN_EX_FMRADIO;
		in = AVSYS_AUDIO_PATH_EX_FMINPUT;
		break;

	case SESSION_VOICE_RECOGNITION:
		gain = AVSYS_AUDIO_GAIN_EX_VOICERECOGNITION;
		if (g_info.option & MM_SUBSESSION_OPTION_SVOICE) {
			if (g_info.subsession == SUBSESSION_VR_NORMAL) {
				/* NORMAL mode */
				if (g_info.option & MM_SUBSESSION_OPTION_PRIV_SVOICE_COMMAND) {
					option |= AVSYS_AUDIO_PATH_OPTION_VR_NORMAL_CMD;
				} else if (g_info.option & MM_SUBSESSION_OPTION_PRIV_SVOICE_WAKEUP) {
					option |= AVSYS_AUDIO_PATH_OPTION_VR_NORMAL_WAKE;
				} else {
					debug_warning ("Unexpected option[%d] of SUBSESSION [%d]\n", g_info.option, g_info.subsession);
				}
			} else if (g_info.subsession == SUBSESSION_VR_DRIVE) {
				/* DRIVE mode */
				if (g_info.option & MM_SUBSESSION_OPTION_PRIV_SVOICE_COMMAND) {
					option |= AVSYS_AUDIO_PATH_OPTION_VR_DRIVE_CMD;
				} else if (g_info.option & MM_SUBSESSION_OPTION_PRIV_SVOICE_WAKEUP) {
					option |= AVSYS_AUDIO_PATH_OPTION_VR_DRIVE_WAKE;
				} else {
					debug_warning ("Unexpected option[%d] of SUBSESSION [%d]\n", g_info.option, g_info.subsession);
				}
			} else {
				debug_warning ("Unexpected SUBSESSION [%d]\n", g_info.subsession);
			}
		}
		break;

	default:
		debug_warning ("session [%d] is not handled...\n", g_info.session);
		break;
	}

	debug_warning ("Trying to set avsys set path gain[%d], out[%d], in[%d], option[%d]\n", gain, out, in, option);

	/* Update Pulseaudio Active Device */
	if (IS_ACTIVE(MM_SOUND_DEVICE_OUT_BT_A2DP)) {
		MMSoundMgrPulseSetActiveDevice(GET_ACTIVE_CAPTURE(), GET_ACTIVE_PLAYBACK());
	} else if (IS_ACTIVE(MM_SOUND_DEVICE_OUT_USB_AUDIO)) {
		MMSoundMgrPulseSetActiveDevice(GET_ACTIVE_CAPTURE(), GET_ACTIVE_PLAYBACK());
	} else if (IS_ACTIVE(MM_SOUND_DEVICE_OUT_MULTIMEDIA_DOCK)) {
		MMSoundMgrPulseSetActiveDevice(GET_ACTIVE_CAPTURE(), GET_ACTIVE_PLAYBACK());
	} else {
		/* ALSA route */
		MMSoundMgrPulseSetActiveDevice(_mm_sound_get_device_in_from_path(in), _mm_sound_get_device_out_from_path(out));
	}

	/* AVSYS Set Path (GAIN, OUT, IN) */
	if (AVSYS_FAIL(avsys_audio_set_path_ex(gain, out, in, option))) {
		debug_error ("avsys_audio_set_path_ex failed\n");
		ret = MM_ERROR_SOUND_INTERNAL;
	}

	/* Pulseaudio Default Sink route */
	if (IS_ACTIVE(MM_SOUND_DEVICE_OUT_BT_A2DP)) {
		MMSoundMgrPulseSetDefaultSink (DEVICE_API_BLUETOOTH, DEVICE_BUS_BLUETOOTH);
	} else if (IS_ACTIVE(MM_SOUND_DEVICE_OUT_USB_AUDIO)) {
		MMSoundMgrPulseSetUSBDefaultSink (MM_SOUND_DEVICE_OUT_USB_AUDIO);
	} else if (IS_ACTIVE(MM_SOUND_DEVICE_OUT_MULTIMEDIA_DOCK)) {
		MMSoundMgrPulseSetUSBDefaultSink (MM_SOUND_DEVICE_OUT_MULTIMEDIA_DOCK);
	} else if (IS_ACTIVE(MM_SOUND_DEVICE_OUT_HDMI)) {
		MMSoundMgrPulseSetDefaultSinkByName (ALSA_SINK_HDMI);
	} else {
		MMSoundMgrPulseSetDefaultSink (DEVICE_API_ALSA, DEVICE_BUS_BUILTIN);
	}

	/* Set Source Mute */
	MMSoundMgrPulseSetSourcemutebyname(MIRRORING_MONITOR_SOURCE,
			IS_ACTIVE(MM_SOUND_DEVICE_OUT_MIRRORING)? MM_SOUND_AUDIO_UNMUTE : MM_SOUND_AUDIO_MUTE);

	if (param & ROUTE_PARAM_CORK_DEVICE)
		MMSoundMgrPulseSetCorkAll (false);
	MMSoundMgrPulseUpdateVolume();

	/* clean up */
	debug_fleave();
	return ret;
}

static int __set_sound_path_for_voicecontrol (void)
{
	int ret = MM_ERROR_NONE;
	int option = AVSYS_AUDIO_PATH_OPTION_NONE;
	int in = 0, out = AVSYS_AUDIO_PATH_EX_NONE, gain = 0;

	debug_fenter();

	/* prepare IN */
	if (IS_ACTIVE(MM_SOUND_DEVICE_IN_MIC)) {
		in = AVSYS_AUDIO_PATH_EX_MIC;
	} else if (IS_ACTIVE(MM_SOUND_DEVICE_IN_WIRED_ACCESSORY)) {
		in = AVSYS_AUDIO_PATH_EX_HEADSETMIC;
	} else if (IS_ACTIVE(MM_SOUND_DEVICE_IN_BT_SCO)) {
		debug_warning ("[NOT EXPECTED CASE] BT SCO");
	}

	debug_warning ("g_info.session = %d ",g_info.session);
	/* prepare GAIN */
	switch (g_info.session) {
	case SESSION_MEDIA:
	case SESSION_NOTIFICATION:
	case SESSION_ALARM:
	case SESSION_EMERGENCY:
	case SESSION_VOICECALL:
	case SESSION_VIDEOCALL:
	case SESSION_VOIP:
		if (IS_MEDIA_SESSION() && __is_recording_subsession(NULL)) {
			debug_warning ("[NOT EXPECTED CASE] already RECORDING....return");
			return MM_ERROR_NONE;
		}
		gain = AVSYS_AUDIO_GAIN_EX_KEYTONE;
		if (g_info.is_voicecontrol) {
			debug_warning ("VoiceControl\n");
			option |= AVSYS_AUDIO_PATH_OPTION_BARGEIN;
		}
		break;

	case SESSION_FMRADIO:
	case SESSION_VOICE_RECOGNITION:
		debug_warning ("[NOT EXPECTED CASE] ");
		break;

	default:
		debug_warning ("session [%d] is not handled...\n", g_info.session);
		break;
	}

	debug_warning ("Trying to set avsys set path gain[%d], out[%d], in[%d], option[%d]\n", gain, out, in, option);

	/* Set Path (GAIN, OUT, IN) */
	MMSoundMgrPulseSetActiveDevice(_mm_sound_get_device_in_from_path(in), GET_ACTIVE_PLAYBACK());
	if (AVSYS_FAIL(avsys_audio_set_path_ex(gain, out, in, option))) {
		debug_error ("avsys_audio_set_path_ex failed\n");
		ret = MM_ERROR_SOUND_INTERNAL;
	}

	debug_fleave();
	return ret;

}


static int __set_sound_path_to_dual (void)
{
	int ret = MM_ERROR_NONE;
	int in = AVSYS_AUDIO_PATH_EX_NONE;
	int option = AVSYS_AUDIO_PATH_OPTION_DUAL_OUT;

	debug_fenter();

	in = IS_ACTIVE(MM_SOUND_DEVICE_IN_WIRED_ACCESSORY)? AVSYS_AUDIO_PATH_EX_HEADSETMIC : AVSYS_AUDIO_PATH_EX_MIC;

	/* If active device was WFD(mirroring), set option */
	if (IS_ACTIVE(MM_SOUND_DEVICE_OUT_MIRRORING)) {
		option |= AVSYS_AUDIO_PATH_OPTION_MIRRORING;
	}

	if (g_info.is_voicecontrol) {
		debug_msg ("VoiceControl\n");
		option |= AVSYS_AUDIO_PATH_OPTION_BARGEIN;
	}

	/* Sound path for ALSA */
	debug_msg ("Set path to DUAL.\n");
	if (AVSYS_FAIL(avsys_audio_set_path_ex(AVSYS_AUDIO_GAIN_EX_RINGTONE,
						AVSYS_AUDIO_PATH_EX_SPK, in,
						option))) {
		debug_error ("avsys_audio_set_path_ex failed\n");
		ret = MM_ERROR_SOUND_INTERNAL;
	}

	/* clean up */
	debug_fleave();
	return ret;
}

static int __set_sound_path_to_earphone_only (void)
{
	int ret = MM_ERROR_NONE;
	int in = AVSYS_AUDIO_PATH_EX_NONE;
	int option = AVSYS_AUDIO_PATH_OPTION_NONE;

	debug_fenter();

	in = IS_ACTIVE(MM_SOUND_DEVICE_IN_WIRED_ACCESSORY)? AVSYS_AUDIO_PATH_EX_HEADSETMIC : AVSYS_AUDIO_PATH_EX_MIC;

	if (g_info.is_voicecontrol) {
		debug_msg ("VoiceControl\n");
		option |= AVSYS_AUDIO_PATH_OPTION_BARGEIN;
	}

	/* Sound path for ALSA */
	debug_msg ("Set path to EARPHONE only.\n");
	MMSoundMgrPulseSetActiveDevice(_mm_sound_get_device_in_from_path(in), _mm_sound_get_device_out_from_path(AVSYS_AUDIO_PATH_EX_HEADSET));
	if (AVSYS_FAIL(avsys_audio_set_path_ex(AVSYS_AUDIO_GAIN_EX_RINGTONE,
					AVSYS_AUDIO_PATH_EX_HEADSET, in, option))) {
		debug_error ("avsys_audio_set_path_ex failed\n");
		ret = MM_ERROR_SOUND_INTERNAL;
	}

	/* clean up */
	debug_fleave();
	return ret;
}

static int __set_sound_path_to_speaker (void)
{
	int ret = MM_ERROR_NONE;

	debug_fenter();

	/* Sound path for ALSA */
	debug_log ("Set path to SPEAKER.\n");
	MMSoundMgrPulseSetActiveDevice(GET_ACTIVE_CAPTURE(), _mm_sound_get_device_out_from_path(AVSYS_AUDIO_PATH_EX_SPK));
	if(AVSYS_FAIL(avsys_audio_set_path_ex(AVSYS_AUDIO_GAIN_EX_KEYTONE,
						AVSYS_AUDIO_PATH_EX_SPK, AVSYS_AUDIO_PATH_EX_NONE,
						AVSYS_AUDIO_PATH_OPTION_NONE))) {
		debug_error ("avsys_audio_set_path_ex failed\n");
		ret = MM_ERROR_SOUND_INTERNAL;
	}

	/* clean up */
	debug_fleave();
	return ret;
}

static void __select_playback_active_device (void)
{
	if (IS_ACTIVE(MM_SOUND_DEVICE_OUT_ANY)) {
		debug_log ("Active device exists. Nothing needed...\n");
		return;
	}

	debug_warning ("No playback active device, set active based on priority!!\n");

	/* set active device based on device priority (bt>ear>spk) */
	if (IS_AVAILABLE(MM_SOUND_DEVICE_OUT_BT_A2DP)) {
		debug_log ("BT A2DP available, set as active!!\n");
		SET_ACTIVE(MM_SOUND_DEVICE_OUT_BT_A2DP);
	} else if (IS_AVAILABLE(MM_SOUND_DEVICE_OUT_MIRRORING)) {
		debug_log ("MIRRORING available, set as active!!\n");
		SET_ACTIVE(MM_SOUND_DEVICE_OUT_MIRRORING);
	} else if (IS_AVAILABLE(MM_SOUND_DEVICE_OUT_DOCK)) {
		debug_log ("DOCK available, set as active!!\n");
		SET_ACTIVE(MM_SOUND_DEVICE_OUT_DOCK);
	} else if (IS_AVAILABLE(MM_SOUND_DEVICE_OUT_HDMI)) {
		debug_log ("HDMI available, set as active!!\n");
		SET_ACTIVE(MM_SOUND_DEVICE_OUT_HDMI);
	} else if (IS_AVAILABLE(MM_SOUND_DEVICE_OUT_USB_AUDIO)) {
		debug_log ("USB Audio available, set as active!!\n");
		SET_ACTIVE(MM_SOUND_DEVICE_OUT_USB_AUDIO);
	} else if (IS_AVAILABLE(MM_SOUND_DEVICE_OUT_MULTIMEDIA_DOCK)) {
		debug_log ("Multimedia Dock available, set as active!!\n");
		SET_ACTIVE(MM_SOUND_DEVICE_OUT_MULTIMEDIA_DOCK);
	} else if (IS_AVAILABLE(MM_SOUND_DEVICE_OUT_WIRED_ACCESSORY)) {
		debug_log ("WIRED available, set as active!!\n");
		SET_ACTIVE(MM_SOUND_DEVICE_OUT_WIRED_ACCESSORY);
	} else {
		debug_log ("SPEAKER or RECEIVER available, set SPEAKER as active!!\n");
		SET_ACTIVE(MM_SOUND_DEVICE_OUT_SPEAKER);
	}
}

static void __select_capture_active_device (void)
{
	if (IS_ACTIVE(MM_SOUND_DEVICE_IN_ANY)) {
		debug_log ("Active device exists. Nothing needed...\n");
		return;
	}

	debug_warning ("No capture active device, set active based on priority!!\n");

	/* set active device based on device priority (bt>ear>spk) */
	if (IS_AVAILABLE(MM_SOUND_DEVICE_IN_BT_SCO) && IS_CALL_SESSION()) {
		debug_log ("BT SCO available, set as active!!\n");
		SET_ACTIVE(MM_SOUND_DEVICE_IN_BT_SCO);
	} else if (IS_AVAILABLE(MM_SOUND_DEVICE_IN_WIRED_ACCESSORY)) {
		debug_log ("WIRED available, set as active!!\n");
		SET_ACTIVE(MM_SOUND_DEVICE_IN_WIRED_ACCESSORY);
	} else {
		debug_log ("MIC available, set as active!!\n");
		SET_ACTIVE(MM_SOUND_DEVICE_IN_MIC);
	}
}

static void __set_initial_active_device (void)
{
	int dock_type = 0;
	bool a2dp = 0, sco = 0;

	/* Set SPK/RECIEVER(for OUT) & MIC(for IN) as default available device */
	/* FIXME : spk & mic can be always on??? */
#ifdef TIZEN_MICRO
	SET_AVAILABLE(MM_SOUND_DEVICE_OUT_SPEAKER);
#else
	SET_AVAILABLE(MM_SOUND_DEVICE_OUT_SPEAKER|MM_SOUND_DEVICE_OUT_RECEIVER);
#endif
	SET_AVAILABLE(MM_SOUND_DEVICE_IN_MIC);

	/* Get wired status and set available status */
	_mm_sound_get_earjack_type (&g_info.headset_type);
	if (g_info.headset_type > EARJACK_UNPLUGGED) {
		SET_AVAILABLE(MM_SOUND_DEVICE_OUT_WIRED_ACCESSORY);
		if (g_info.headset_type == EARJACK_WITH_MIC) {
			SET_AVAILABLE(MM_SOUND_DEVICE_IN_WIRED_ACCESSORY);
		}
	}

	/* Get Dock status and set available status */
	_mm_sound_get_dock_type (&dock_type);
	if (dock_type == DOCK_DESKDOCK) {
		SET_AVAILABLE(MM_SOUND_DEVICE_OUT_DOCK);
	}

	/* Get BT status and set available status */
	MMSoundMgrPulseGetInitialBTStatus (&a2dp, &sco);
	if (a2dp) {
		SET_AVAILABLE(MM_SOUND_DEVICE_OUT_BT_A2DP);
	}
	if (sco) {
		SET_AVAILABLE(MM_SOUND_DEVICE_OUT_BT_SCO);
		SET_AVAILABLE(MM_SOUND_DEVICE_IN_BT_SCO);
	}

	/* Set Active device based on priority */
	__select_playback_active_device ();
	__select_capture_active_device ();

	__set_route(ROUTE_PARAM_BROADCASTING);
	dump_info();
}

static void __handle_bt_a2dp_on (void)
{
	int ret = MM_ERROR_NONE;

	/* at this time, pulseaudio default sink is bt sink */
	if (IS_CALL_SESSION()) {
		debug_warning ("Current session is VOICECALL or VIDEOCALL or VOIP, no auto-activation!!!\n");
		return;
	}

	debug_log ("Activate BT_A2DP device\n");
	SET_PLAYBACK_ONLY_ACTIVE(MM_SOUND_DEVICE_OUT_BT_A2DP);

	/* For sharing device information with PulseAudio */
	MMSoundMgrPulseSetActiveDevice(MM_SOUND_DEVICE_IN_NONE, GET_ACTIVE_PLAYBACK());

	ret = _mm_sound_mgr_device_active_device_callback(GET_ACTIVE_CAPTURE(), GET_ACTIVE_PLAYBACK());
	if (ret != MM_ERROR_NONE) {
		debug_error ("_mm_sound_mgr_device_active_device_callback() failed [%x]\n", ret);
	}

	dump_info ();
}

static void __handle_bt_a2dp_off (void)
{
	if (!IS_ACTIVE(MM_SOUND_DEVICE_OUT_BT_A2DP)) {
		debug_warning("MM_SOUND_DEVICE_OUT_BT_A2DP was not active. nothing to do here.");
		dump_info ();
		return;
	}

	/* if bt was active, then do asm pause */
	debug_msg("Do pause here");
	_asm_pause_process (g_info.asm_handle);

	/* set BT A2DP device to none */
	debug_msg("Deactivate BT_A2DP device\n");
	UNSET_ACTIVE(MM_SOUND_DEVICE_OUT_BT_A2DP);

	/* activate current available device based on priority */
	__select_playback_active_device();

	/* Do set path and notify result */
	__set_route_nolock(ROUTE_PARAM_BROADCASTING | ROUTE_PARAM_CORK_DEVICE);

	dump_info ();
}

static void __handle_bt_sco_on ()
{
	/* if fmradio session, do nothing */

	/* Skip when noti session */

	/* ToDo : alarm/notification session ???? */
	if (IS_CALL_SESSION()) {
		debug_warning ("Current session is VOICECALL or VIDEOCALL or VOIP, no auto-activation!!!\n");
		return;
	}

	debug_log ("Activate BT SCO IN/OUT device\n");
	SET_PLAYBACK_ONLY_ACTIVE(MM_SOUND_DEVICE_OUT_BT_SCO);
	SET_CAPTURE_ONLY_ACTIVE(MM_SOUND_DEVICE_IN_BT_SCO);

	/* Do set path and notify result */
#if 0//def TIZEN_MICRO
	/* Sound path for ALSA */
	debug_log ("Set path to BT SCO\n");
	if (AVSYS_FAIL(avsys_audio_set_path_ex(AVSYS_AUDIO_GAIN_EX_VOICECALL,
					AVSYS_AUDIO_PATH_EX_BTHEADSET, AVSYS_AUDIO_PATH_EX_BTMIC, 0))) {
		debug_error ("avsys_audio_set_path_ex failed in BT ON\n");
	}
#else
	__set_route_nolock(ROUTE_PARAM_BROADCASTING | ROUTE_PARAM_CORK_DEVICE);
#endif

	dump_info ();
}

static void __handle_bt_sco_off (void)
{
	/* If sco is not activated, just return */
	if (!IS_ACTIVE(MM_SOUND_DEVICE_OUT_BT_SCO) && !IS_ACTIVE(MM_SOUND_DEVICE_IN_BT_SCO)) {
		debug_warning("BT SCO was not active. nothing to do here.");
		dump_info ();
		return;
	}

#if 0 /* SCO is not for media */
	/* if bt was active, then do asm pause */
	debug_msg("Do pause here");
	_asm_pause_process (g_info.asm_handle);
#endif

	/* set bt device to none */
	debug_msg("Deactivate BT_SCO device\n");
	UNSET_ACTIVE(MM_SOUND_DEVICE_OUT_BT_SCO);
	UNSET_ACTIVE(MM_SOUND_DEVICE_IN_BT_SCO);

	if (IS_CALL_SESSION()) {
		debug_warning ("Current session is VOICECALL or VIDEOCALL or VOIP, no auto-activation!!!\n");
		return;
	}

	/* activate current available device based on priority */
	__select_playback_active_device();
	__select_capture_active_device();
#if 0//def TIZEN_MICRO
	/* Sound path for ALSA */
	debug_log ("Set path to RESET VOICECALL\n");
	if (AVSYS_FAIL(avsys_audio_set_path_ex(AVSYS_AUDIO_GAIN_EX_VOICECALL,
					AVSYS_AUDIO_PATH_EX_NONE, AVSYS_AUDIO_PATH_EX_NONE, 0))) {
		debug_error ("avsys_audio_set_path_ex failed in case BT SCO\n");
	}
#endif

	/* Do set path and notify result */
	__set_route_nolock(ROUTE_PARAM_BROADCASTING | ROUTE_PARAM_CORK_DEVICE);

	dump_info ();
}

static void __handle_headset_on (int type)
{
	/* at this time, pulseaudio default sink is bt sink */
	/* if fmradio session, do nothing */

	/* Skip when noti session */

	/* ToDo : alarm/notification session ???? */
	if (IS_CALL_SESSION()) {
		debug_warning ("Current session is VOICECALL or VIDEOCALL or VOIP, no auto-activation!!!\n");
		return;
	}

	debug_log ("Activate WIRED OUT device\n");
	SET_PLAYBACK_ONLY_ACTIVE(MM_SOUND_DEVICE_OUT_WIRED_ACCESSORY);
	if (type == EARJACK_WITH_MIC) {
		debug_log ("Activate WIRED IN device\n");
		SET_CAPTURE_ONLY_ACTIVE(MM_SOUND_DEVICE_IN_WIRED_ACCESSORY);
	}

	/* Do set path and notify result */
	__set_route(ROUTE_PARAM_BROADCASTING | ROUTE_PARAM_CORK_DEVICE);

	dump_info ();
}

static void __handle_headset_off (void)
{
	if (!IS_ACTIVE(MM_SOUND_DEVICE_OUT_WIRED_ACCESSORY)) {
		debug_warning("MM_SOUND_DEVICE_OUT_WIRED_ACCESSORY was not active. nothing to do here.");
		return;
	}

	/* if Headset was active, then do asm pause */
	debug_msg("Do pause here");
	_asm_pause_process (g_info.asm_handle);

	/* set Headset device to none */
	debug_msg("Deactivate WIRED IN/OUT device\n");
	UNSET_ACTIVE(MM_SOUND_DEVICE_OUT_WIRED_ACCESSORY);
	UNSET_ACTIVE(MM_SOUND_DEVICE_IN_WIRED_ACCESSORY);

	/* For call or voip session, activation device is up-to application policy */
	if (IS_CALL_SESSION()) {
		debug_warning ("Current session is VOICECALL or VIDEOCALL or VOIP, no auto-activation!!!\n");
		return;
	}

	/* activate current available device based on priority */
	__select_playback_active_device();
	__select_capture_active_device();

	/* Do set path and notify result */
	__set_route(ROUTE_PARAM_BROADCASTING | ROUTE_PARAM_CORK_DEVICE);

	dump_info ();
}

static void __handle_dock_on (void)
{
	/* ToDo : alarm/notification session ???? */
	if (IS_CALL_SESSION()) {
		debug_warning ("Current session is VOICECALL or VIDEOCALL or VOIP, no auto-activation!!!\n");
		return;
	}

	debug_log ("Activate DOCK device\n");
	SET_PLAYBACK_ONLY_ACTIVE(MM_SOUND_DEVICE_OUT_DOCK);

	/* Do set path and notify result */
	__set_route(ROUTE_PARAM_BROADCASTING | ROUTE_PARAM_CORK_DEVICE);

	dump_info ();
}

static void __handle_dock_off (void)
{
	if (!IS_ACTIVE(MM_SOUND_DEVICE_OUT_DOCK)) {
		debug_warning("MM_SOUND_DEVICE_OUT_DOCK was not active. nothing to do here.");
		return;
	}

	/* if Dock was active, then do asm pause */
	debug_msg("Do pause here");
	_asm_pause_process (g_info.asm_handle);

	/* set DOCK device to none */
	debug_msg("Deactivate DOCK device\n");
	UNSET_ACTIVE(MM_SOUND_DEVICE_OUT_DOCK);

	/* activate current available device based on priority */
	__select_playback_active_device();

	/* Do set path and notify result */
	__set_route(ROUTE_PARAM_BROADCASTING | ROUTE_PARAM_CORK_DEVICE);

	dump_info ();
}

static void __handle_hdmi_on (void)
{
	/* ToDo : alarm/notification session ???? */
	if (IS_CALL_SESSION()) {
		debug_warning ("Current session is VOICECALL or VIDEOCALL or VOIP, no auto-activation!!!\n");
		return;
	}

	debug_log ("Activate HDMI device\n");
	SET_PLAYBACK_ONLY_ACTIVE(MM_SOUND_DEVICE_OUT_HDMI);

	/* Do set path and notify result */
	__set_route(ROUTE_PARAM_BROADCASTING | ROUTE_PARAM_CORK_DEVICE);

	dump_info ();
}

static void __handle_hdmi_off (void)
{
	if (!IS_ACTIVE(MM_SOUND_DEVICE_OUT_HDMI)) {
		debug_warning("MM_SOUND_DEVICE_OUT_HDMI was not active. nothing to do here.");
		return;
	}

	/* if HDMI was active, then do asm pause */
	debug_msg("Do pause here");
	_asm_pause_process (g_info.asm_handle);

	MMSoundMgrPulseUnLoadHDMI();

	/* set HDMI device to none */
	debug_msg("Deactivate HDMI device\n");
	UNSET_ACTIVE(MM_SOUND_DEVICE_OUT_HDMI);

	/* activate current available device based on priority */
	__select_playback_active_device();

	/* Do set path and notify result */
	__set_route(ROUTE_PARAM_BROADCASTING | ROUTE_PARAM_CORK_DEVICE);

	dump_info ();
}

static void __handle_mirroring_on (void)
{
	/* ToDo : alarm/notification session ???? */
	if (IS_CALL_SESSION()) {
		debug_warning ("Current session is VOICECALL or VIDEOCALL or VOIP, no auto-activation!!!\n");
		return;
	}

	debug_log ("Activate MIRRORING device\n");
	SET_PLAYBACK_ONLY_ACTIVE(MM_SOUND_DEVICE_OUT_MIRRORING);

	/* Do set path and notify result */
	__set_route(ROUTE_PARAM_BROADCASTING | ROUTE_PARAM_CORK_DEVICE);

	dump_info ();
}

static void __handle_mirroring_off (void)
{
	if (!IS_ACTIVE(MM_SOUND_DEVICE_OUT_MIRRORING)) {
		debug_warning("MM_SOUND_DEVICE_OUT_MIRRORING was not active. nothing to do here.");
		return;
	}

	/* if MIRRORING was active, then do asm pause */
	debug_msg("Do pause here");
	_asm_pause_process (g_info.asm_handle);

	/* set MIRRORING device to none */
	debug_msg("Deactivate MIRRORING device\n");
	UNSET_ACTIVE(MM_SOUND_DEVICE_OUT_MIRRORING);

	/* activate current available device based on priority */
	__select_playback_active_device();

	/* Do set path and notify result */
	__set_route(ROUTE_PARAM_BROADCASTING | ROUTE_PARAM_CORK_DEVICE);

	dump_info ();
}

static void __handle_usb_audio_on (void)
{
	int ret = MM_ERROR_NONE;

	if (IS_CALL_SESSION()) {
		debug_warning ("Current session is VOICECALL or VIDEOCALL or VOIP, no auto-activation!!!\n");
		return;
	}

	debug_log ("Activate USB Audio device\n");
	SET_PLAYBACK_ONLY_ACTIVE(MM_SOUND_DEVICE_OUT_USB_AUDIO);

	/* For sharing device information with PulseAudio */
	MMSoundMgrPulseSetActiveDevice(MM_SOUND_DEVICE_IN_NONE, GET_ACTIVE_PLAYBACK());

	ret = _mm_sound_mgr_device_active_device_callback(GET_ACTIVE_CAPTURE(), GET_ACTIVE_PLAYBACK());
	if (ret != MM_ERROR_NONE) {
		debug_error ("_mm_sound_mgr_device_active_device_callback() failed [%x]\n", ret);
	}

	dump_info ();
}

static void __handle_usb_audio_off (void)
{
	if (!IS_ACTIVE(MM_SOUND_DEVICE_OUT_USB_AUDIO)) {
		debug_warning("MM_SOUND_DEVICE_OUT_USB_AUDIO was not active. nothing to do here.");
		dump_info ();
		return;
	}

	/* if device was active, then do asm pause */
	debug_msg("Do pause here");
	_asm_pause_process (g_info.asm_handle);

	/* set USB Audio device to none */
	debug_msg("Deactivate USB Audio device\n");
	UNSET_ACTIVE(MM_SOUND_DEVICE_OUT_USB_AUDIO);

	/* activate current available device based on priority */
	__select_playback_active_device();

	/* Do set path and notify result */
	__set_route(ROUTE_PARAM_BROADCASTING | ROUTE_PARAM_CORK_DEVICE);

	dump_info ();
}

static void __handle_multimedia_dock_on (void)
{
	if (IS_CALL_SESSION()) {
		debug_warning ("Current session is VOICECALL or VIDEOCALL or VOIP, no auto-activation!!!\n");
		return;
	}

	/* If HDMI has already been actived, we just skip active. */
	if (IS_ACTIVE(MM_SOUND_DEVICE_OUT_HDMI)) {
		debug_warning ("HDMI device has been already actived. Just skip Multimedia Dock active action.\n");
		return;
	}

	debug_log ("Activate Multimedia Dock device\n");
	SET_PLAYBACK_ONLY_ACTIVE(MM_SOUND_DEVICE_OUT_MULTIMEDIA_DOCK);

	/* Do set path and notify result */
	__set_route(ROUTE_PARAM_BROADCASTING | ROUTE_PARAM_CORK_DEVICE);

	dump_info ();
}

static void __handle_multimedia_dock_off (void)
{
	if (!IS_ACTIVE(MM_SOUND_DEVICE_OUT_MULTIMEDIA_DOCK)) {
		debug_warning("MM_SOUND_DEVICE_OUT_MULTIMEDIA_DOCK was not active. nothing to do here.");
		dump_info ();
		return;
	}

	/* if device was active, then do asm pause */
	debug_msg("Do pause here");
	_asm_pause_process (g_info.asm_handle);

	/* set MultimediaDock device to none */
	debug_msg("Deactivate Multimedia Dock device\n");
	UNSET_ACTIVE(MM_SOUND_DEVICE_OUT_MULTIMEDIA_DOCK);

	/* activate current available device based on priority */
	__select_playback_active_device();

	/* Do set path and notify result */
	__set_route(ROUTE_PARAM_BROADCASTING | ROUTE_PARAM_CORK_DEVICE);

	dump_info ();
}


/* ------------------------- EXTERNAL FUNCTIONS ------------------------------------*/

int MMSoundMgrSessionMediaPause ()
{
	debug_msg ("[SESSION] Media pause requested...");
	_asm_pause_process (g_info.asm_handle);

	return MM_ERROR_NONE;
}

int MMSoundMgrSessionSetSCOBTWB (bool is_bt_wb)
{
	debug_msg ("[SESSION] Set SCOBT wb=%d", is_bt_wb);
	g_info.bt_info.is_wb = is_bt_wb;
	return MM_ERROR_NONE;
}

int MMSoundMgrSessionSetSCO (bool is_sco_on, bool is_bt_nrec, bool is_bt_wb)
{
	debug_msg ("[SESSION] Set SCO enable=%d, nrec=%d, wb=%d", is_sco_on, is_bt_nrec, is_bt_wb);

	g_info.bt_info.is_nrec = (is_sco_on)? is_bt_nrec : false;
	g_info.bt_info.is_wb = (is_sco_on)? is_bt_wb : false;

	if (is_sco_on) {
		__handle_bt_sco_on();
	} else {
		__handle_bt_sco_off();
	}
	return MM_ERROR_NONE;
}

/* DEVICE : Called by mgr_pulse for updating current default_sink_name */
int MMSoundMgrSessionSetDefaultSink (const char * const default_sink_name)
{
	LOCK_SESSION();

	strcpy (g_info.default_sink_name, default_sink_name);
	debug_msg ("[SESSION] default sink=[%s]\n", default_sink_name);

	/* ToDo: do something */

	UNLOCK_SESSION();

	return MM_ERROR_NONE;
}

/* DEVICE : Called by mgr_pulse for bt and mgr_headset for headset */
int MMSoundMgrSessionSetDeviceAvailable (device_type_t device, int available, int type, const char* name)
{
	LOCK_SESSION();

	debug_warning ("[SESSION] device = %d, available = %d, type = %d, name = %s\n", device, available, type, name);
	switch (device) {
	case DEVICE_WIRED:
		if (available) {

			if (!IS_AVAILABLE(MM_SOUND_DEVICE_OUT_WIRED_ACCESSORY)) {
				SET_AVAILABLE(MM_SOUND_DEVICE_OUT_WIRED_ACCESSORY);
				/* available device & send available callback */
				if (type == EARJACK_WITH_MIC) {
					SET_AVAILABLE(MM_SOUND_DEVICE_IN_WIRED_ACCESSORY);
					_mm_sound_mgr_device_available_device_callback(
											MM_SOUND_DEVICE_IN_WIRED_ACCESSORY,
											MM_SOUND_DEVICE_OUT_WIRED_ACCESSORY,
											AVAILABLE);
				} else {
					_mm_sound_mgr_device_available_device_callback(
											MM_SOUND_DEVICE_IN_NONE,
											MM_SOUND_DEVICE_OUT_WIRED_ACCESSORY,
											AVAILABLE);
				}

				/* Store earphone type */
				g_info.headset_type = type;

				/* activate device & send activated callback */
				__handle_headset_on(type);
			} else {
				debug_log ("Already device [%d] is available...\n", device);
			}

		} else {
			if (IS_AVAILABLE(MM_SOUND_DEVICE_OUT_WIRED_ACCESSORY)) {

				/* unavailable earphone & earmic(if available)*/
				UNSET_AVAILABLE(MM_SOUND_DEVICE_OUT_WIRED_ACCESSORY);
				if (g_info.headset_type == EARJACK_WITH_MIC)
					UNSET_AVAILABLE(MM_SOUND_DEVICE_IN_WIRED_ACCESSORY);

				/* Clear earphone type */
				g_info.headset_type = EARJACK_UNPLUGGED;

				/* unactivate device & send callback  */
				__handle_headset_off();

				/* Send unavailable callback */
				if (g_info.headset_type == EARJACK_WITH_MIC) {
					_mm_sound_mgr_device_available_device_callback(
											MM_SOUND_DEVICE_IN_WIRED_ACCESSORY,
											MM_SOUND_DEVICE_OUT_WIRED_ACCESSORY,
											NOT_AVAILABLE);
				} else {
					_mm_sound_mgr_device_available_device_callback(
											MM_SOUND_DEVICE_IN_NONE,
											MM_SOUND_DEVICE_OUT_WIRED_ACCESSORY,
											NOT_AVAILABLE);
				}


			} else {
				debug_log ("Already device [%d] is unavailable...\n", device);
			}
		}
		break;

	case DEVICE_BT_A2DP:
		strcpy (g_info.bt_info.name, (name)? name : "");
		if (available) {
			if (!IS_AVAILABLE(MM_SOUND_DEVICE_OUT_BT_A2DP)) {
				SET_AVAILABLE(MM_SOUND_DEVICE_OUT_BT_A2DP);
				_mm_sound_mgr_device_available_device_callback(
											MM_SOUND_DEVICE_IN_NONE,
											MM_SOUND_DEVICE_OUT_BT_A2DP,
											AVAILABLE);

				__handle_bt_a2dp_on();
			} else {
				debug_log ("Already device [%d] is available...\n", device);
			}
		} else {
			if (IS_AVAILABLE(MM_SOUND_DEVICE_OUT_BT_A2DP)) {
				UNSET_AVAILABLE(MM_SOUND_DEVICE_OUT_BT_A2DP);
				__handle_bt_a2dp_off();
				_mm_sound_mgr_device_available_device_callback(
											MM_SOUND_DEVICE_IN_NONE,
											MM_SOUND_DEVICE_OUT_BT_A2DP,
											NOT_AVAILABLE);


			} else {
				debug_log ("Already device [%d] is unavailable...\n", device);
			}
		}
		break;

	case DEVICE_BT_SCO:
		if (available) {
			if (!IS_AVAILABLE(MM_SOUND_DEVICE_OUT_BT_SCO)) {
				SET_AVAILABLE(MM_SOUND_DEVICE_OUT_BT_SCO);
				SET_AVAILABLE(MM_SOUND_DEVICE_IN_BT_SCO);
				_mm_sound_mgr_device_available_device_callback(
											MM_SOUND_DEVICE_IN_BT_SCO,
											MM_SOUND_DEVICE_OUT_BT_SCO,
											AVAILABLE);
			} else {
				debug_log ("Already device [%d] is available...\n", device);
			}
		} else {
			if (IS_AVAILABLE(MM_SOUND_DEVICE_OUT_BT_SCO)) {
				UNSET_AVAILABLE(MM_SOUND_DEVICE_OUT_BT_SCO);
				UNSET_AVAILABLE(MM_SOUND_DEVICE_IN_BT_SCO);
				__handle_bt_sco_off();
				_mm_sound_mgr_device_available_device_callback(
											MM_SOUND_DEVICE_IN_BT_SCO,
											MM_SOUND_DEVICE_OUT_BT_SCO,
											NOT_AVAILABLE);

			} else {
				debug_log ("Already device [%d] is unavailable...\n", device);
			}
		}
		break;

	case DEVICE_DOCK:
		if (available) {
			if (!IS_AVAILABLE(MM_SOUND_DEVICE_OUT_DOCK)) {
				SET_AVAILABLE(MM_SOUND_DEVICE_OUT_DOCK);
				_mm_sound_mgr_device_available_device_callback(
											MM_SOUND_DEVICE_IN_NONE,
											MM_SOUND_DEVICE_OUT_DOCK,
											AVAILABLE);
				__handle_dock_on();
			} else {
				debug_log ("Already device [%d] is available...\n", device);
			}
		} else {
			if (IS_AVAILABLE(MM_SOUND_DEVICE_OUT_DOCK)) {
				UNSET_AVAILABLE(MM_SOUND_DEVICE_OUT_DOCK);
				__handle_dock_off();
				_mm_sound_mgr_device_available_device_callback(
											MM_SOUND_DEVICE_IN_NONE,
											MM_SOUND_DEVICE_OUT_DOCK,
											NOT_AVAILABLE);

			} else {
				debug_log ("Already device [%d] is unavailable...\n", device);
			}
		}
		break;

	case DEVICE_HDMI:
		if (available) {
			if (!IS_AVAILABLE(MM_SOUND_DEVICE_OUT_HDMI)) {
				SET_AVAILABLE(MM_SOUND_DEVICE_OUT_HDMI);
				_mm_sound_mgr_device_available_device_callback(
											MM_SOUND_DEVICE_IN_NONE,
											MM_SOUND_DEVICE_OUT_HDMI,
											AVAILABLE);
				__handle_hdmi_on();
			} else {
				debug_log ("Already device [%d] is available...\n", device);
			}
		} else {
			if (IS_AVAILABLE(MM_SOUND_DEVICE_OUT_HDMI)) {
				UNSET_AVAILABLE(MM_SOUND_DEVICE_OUT_HDMI);
				__handle_hdmi_off();
				_mm_sound_mgr_device_available_device_callback(
											MM_SOUND_DEVICE_IN_NONE,
											MM_SOUND_DEVICE_OUT_HDMI,
											NOT_AVAILABLE);

			} else {
				debug_log ("Already device [%d] is unavailable...\n", device);
			}
		}
		break;

	case DEVICE_MIRRORING:
		if (available) {
			if (!IS_AVAILABLE(MM_SOUND_DEVICE_OUT_MIRRORING)) {
				SET_AVAILABLE(MM_SOUND_DEVICE_OUT_MIRRORING);
				_mm_sound_mgr_device_available_device_callback(
											MM_SOUND_DEVICE_IN_NONE,
											MM_SOUND_DEVICE_OUT_MIRRORING,
											AVAILABLE);
				__handle_mirroring_on();
			} else {
				debug_log ("Already device [%d] is available...\n", device);
			}
		} else {
			if (IS_AVAILABLE(MM_SOUND_DEVICE_OUT_MIRRORING)) {
				UNSET_AVAILABLE(MM_SOUND_DEVICE_OUT_MIRRORING);
				__handle_mirroring_off();
				_mm_sound_mgr_device_available_device_callback(
											MM_SOUND_DEVICE_IN_NONE,
											MM_SOUND_DEVICE_OUT_MIRRORING,
											NOT_AVAILABLE);

			} else {
				debug_log ("Already device [%d] is unavailable...\n", device);
			}
		}
		break;

	case DEVICE_USB_AUDIO:
		if (available) {
			if (!IS_AVAILABLE(MM_SOUND_DEVICE_OUT_USB_AUDIO)) {
				SET_AVAILABLE(MM_SOUND_DEVICE_OUT_USB_AUDIO);
				_mm_sound_mgr_device_available_device_callback(
											MM_SOUND_DEVICE_IN_NONE,
											MM_SOUND_DEVICE_OUT_USB_AUDIO,
											AVAILABLE);
				__handle_usb_audio_on();
			} else {
				debug_log ("Already device [%d] is available...\n", device);
			}
		} else {
			if (IS_AVAILABLE(MM_SOUND_DEVICE_OUT_USB_AUDIO)) {
				UNSET_AVAILABLE(MM_SOUND_DEVICE_OUT_USB_AUDIO);
				__handle_usb_audio_off();
				_mm_sound_mgr_device_available_device_callback(
											MM_SOUND_DEVICE_IN_NONE,
											MM_SOUND_DEVICE_OUT_USB_AUDIO,
											NOT_AVAILABLE);

			} else {
				debug_log ("Already device [%d] is unavailable...\n", device);
			}
		}
		break;
	case DEVICE_MULTIMEDIA_DOCK:
		if (available) {
			if (!IS_AVAILABLE(MM_SOUND_DEVICE_OUT_MULTIMEDIA_DOCK)) {
				SET_AVAILABLE(MM_SOUND_DEVICE_OUT_MULTIMEDIA_DOCK);
				_mm_sound_mgr_device_available_device_callback(
						MM_SOUND_DEVICE_IN_NONE,
						MM_SOUND_DEVICE_OUT_MULTIMEDIA_DOCK,
						AVAILABLE);
				__handle_multimedia_dock_on();
			} else {
				debug_log ("Already device [%d] is available...\n", device);
			}
		} else {
			if (IS_AVAILABLE(MM_SOUND_DEVICE_OUT_MULTIMEDIA_DOCK)) {
				UNSET_AVAILABLE(MM_SOUND_DEVICE_OUT_MULTIMEDIA_DOCK);
				if (IS_AVAILABLE(MM_SOUND_DEVICE_OUT_USB_AUDIO))
					UNSET_AVAILABLE(MM_SOUND_DEVICE_OUT_USB_AUDIO);
				__handle_multimedia_dock_off();
				_mm_sound_mgr_device_available_device_callback(
						MM_SOUND_DEVICE_IN_NONE,
						MM_SOUND_DEVICE_OUT_MULTIMEDIA_DOCK,
						NOT_AVAILABLE);

			} else {
				debug_log ("Already device [%d] is unavailable...\n", device);
			}
		}
		break;

	default:
		debug_warning ("device [%d] is not handled...\n", device);
		break;
	}

	UNLOCK_SESSION();

	return MM_ERROR_NONE;
}

int MMSoundMgrSessionIsDeviceAvailableNoLock (mm_sound_device_out playback, mm_sound_device_in capture, bool *available)
{
	int ret = MM_ERROR_NONE;
	debug_log ("[SESSION] query playback=[0x%X] capture=[0x%X], current available = [0x%X]\n",
			playback, capture, g_info.device_available);

	if (available) {
		if (playback == MM_SOUND_DEVICE_OUT_NONE) {
			*available = IS_AVAILABLE(capture);
		} else if (capture == MM_SOUND_DEVICE_IN_NONE) {
			*available = IS_AVAILABLE(playback);
		} else {
			*available = (IS_AVAILABLE(playback) && IS_AVAILABLE(capture));
		}
		debug_log ("return available = [%d]\n", *available);
	} else {
		debug_warning ("Invalid argument!!!\n");
		ret = MM_ERROR_INVALID_ARGUMENT;
	}

	return ret;
}


int MMSoundMgrSessionIsDeviceAvailable (mm_sound_device_out playback, mm_sound_device_in capture, bool *available)
{
	int ret = MM_ERROR_NONE;

	LOCK_SESSION();
	ret = MMSoundMgrSessionIsDeviceAvailableNoLock (playback, capture, available);
	UNLOCK_SESSION();

	return ret;
}

int MMSoundMgrSessionGetAvailableDevices (int *playback, int *capture)
{
	if (playback == NULL || capture == NULL) {
		debug_error ("Invalid input parameter\n");
		return MM_ERROR_INVALID_ARGUMENT;
	}

	LOCK_SESSION();

	*playback = GET_AVAILABLE_PLAYBACK();
	*capture  = GET_AVAILABLE_CAPTURE();
	debug_msg ("[SESSION] return available playback=[0x%X]/capture=[0x%X]\n",  *playback, *capture);

	UNLOCK_SESSION();

	return MM_ERROR_NONE;
}

int MMSoundMgrSessionSetDeviceActive (mm_sound_device_out playback, mm_sound_device_in capture, bool need_broadcast)
{
	int ret = MM_ERROR_NONE;
	int old_active = g_info.device_active;
	bool need_update = false;
	int is_loopback=0;
	bool need_cork = true;

	LOCK_SESSION();

	debug_warning ("[SESSION] playback=[0x%X] capture=[0x%X]\n", playback, capture);

	/* Check whether device is available */
	if ((playback && !IS_AVAILABLE(playback)) || (capture && !IS_AVAILABLE(capture))) {
		debug_warning ("Failed to set active state to unavailable device!!!\n");
		ret = MM_ERROR_INVALID_ARGUMENT;
		goto END_SET_DEVICE;
	}

	/* Update active state */
	debug_log ("Update active device as request\n");
	if (playback) {
		SET_PLAYBACK_ONLY_ACTIVE(playback);
	}
	if (capture) {
		SET_CAPTURE_ONLY_ACTIVE(capture);
	}

	mm_sound_get_factory_loopback_test(&is_loopback);

	if (((g_info.session == SESSION_VOICECALL) || (g_info.session == SESSION_VIDEOCALL)) && !is_loopback) {
#ifndef TIZEN_MICRO
		bool is_noise_reduction, is_extra_volume, is_upscaling_needed;

		is_noise_reduction = __is_noise_reduction_on();
		is_extra_volume = __is_extra_volume_on();
		is_upscaling_needed = __is_upscaling_needed();
		if ((g_info.is_noise_reduction != is_noise_reduction)
			|| (g_info.is_extra_volume != is_extra_volume)
			|| (g_info.is_upscaling_needed != is_upscaling_needed))
			need_update = true;
		g_info.is_noise_reduction = is_noise_reduction;
		g_info.is_extra_volume = is_extra_volume;
		g_info.is_upscaling_needed = is_upscaling_needed;
#else
		need_update = true;
#endif
		need_cork = false; // doesn't need cork during voice call
	}

	/* If there's changes do path set and inform callback */
	if (old_active != g_info.device_active || need_update == true) {
		mm_sound_route_param_t value = 0;
		debug_msg ("Changes happens....set path based on current active device and inform callback(%d)!!!\n", need_broadcast);

		/* Do set path based on current active state */
		if(need_broadcast)
			value = value | ROUTE_PARAM_BROADCASTING;
		if(need_cork)
			value = value | ROUTE_PARAM_CORK_DEVICE;

		__set_route(value);
	} else {
		debug_msg ("No changes....nothing to do...\n");
	}

END_SET_DEVICE:
	UNLOCK_SESSION();
	return ret;
}

int MMSoundMgrSessionSetDeviceActiveAuto (void)
{
	int ret = MM_ERROR_NONE;

	/* activate current available device based on priority */
	__select_playback_active_device();
	__select_capture_active_device();
	/* Do set path and notify result */
	ret = __set_route(ROUTE_PARAM_BROADCASTING | ROUTE_PARAM_CORK_DEVICE);
	if (ret != MM_ERROR_NONE) {
		debug_error("fail to MMSoundMgrSessionSetDeviceActiveAuto.\n");
	} else {
		debug_msg ("success : MMSoundMgrSessionSetDeviceActiveAuto\n");
	}
	return ret;
}

int MMSoundMgrSessionGetDeviceActive (mm_sound_device_out *playback, mm_sound_device_in *capture)
{
	if (playback == NULL || capture == NULL) {
		debug_error ("Invalid input parameter\n");
		return MM_ERROR_INVALID_ARGUMENT;
	}

	LOCK_SESSION();

	*playback = GET_ACTIVE_PLAYBACK();
	*capture  = GET_ACTIVE_CAPTURE();
	debug_msg ("[SESSION] return active playback=[0x%X]/capture=[0x%X]\n", *playback,*capture);

	UNLOCK_SESSION();
	return MM_ERROR_NONE;
}

/* SUBSESSION */
int MMSoundMgrSessionSetSession(session_t session, session_state_t state)
{
	LOCK_SESSION();

	debug_warning ("[SESSION] session=[%d] state=[%d]\n", session, state);

	/* Update Enable session */
	if (state) {
		g_info.session = session;
	} else {
		g_info.session = SESSION_MEDIA;
		g_info.subsession = SUBSESSION_VOICE; /* initialize subsession */
	}

	MMSoundMgrPulseSetSession(session, state);

	/* Do action based on new session */
	switch (session) {
	case SESSION_MEDIA:
		__set_playback_route_media (state);
		break;

	case SESSION_VOICECALL:
	case SESSION_VIDEOCALL:
		__set_playback_route_call (state);
		break;
	case SESSION_VOIP:
		__set_playback_route_voip (state);
		break;

	case SESSION_FMRADIO:
		__set_playback_route_fmradio (state);
		break;

	case SESSION_NOTIFICATION:
		__set_playback_route_notification (state);
		break;

	case SESSION_ALARM:
		__set_playback_route_alarm (state);
		break;

	case SESSION_EMERGENCY:
		__set_playback_route_emergency (state);
		break;

	case SESSION_VOICE_RECOGNITION:
		__set_playback_route_voicerecognition (state);
		break;

	default:
		debug_warning ("session [%d] is not handled...\n", session);
		break;
	}

	UNLOCK_SESSION();
	return MM_ERROR_NONE;
}

int MMSoundMgrSessionGetSession(session_t *session)
{
	if (session == NULL) {
		debug_error ("Invalid input parameter\n");
		return MM_ERROR_INVALID_ARGUMENT;
	}

	//LOCK_SESSION();
	*session = g_info.session;
	//UNLOCK_SESSION();

	return MM_ERROR_NONE;
}

int MMSoundMgrSessionSetDuplicateSubSession()
{
	/* This function is for set sub session duplicate
		When BT wb/nb is changed, the call application cannot set
		normal alsa scenario.
		Because call app is set sub session before BT band is decided.
		(Actually BT frw can know the band after SCO request
		from phone)

		Ref. mm_sound_mgr_pulse.c _bt_hf_cb function.
	*/

	int ret = 0;
	debug_msg ("Duplicated path control");

	LOCK_SESSION();

	ret = __set_route(ROUTE_PARAM_NONE);
	if(ret != MM_ERROR_NONE)
		debug_warning("Fail to set route");

	UNLOCK_SESSION();
	return MM_ERROR_NONE;
}

/* SUBSESSION */
int MMSoundMgrSessionSetSubSession(subsession_t subsession, int subsession_opt)
{
	bool need_update = false;

	LOCK_SESSION();

	MMSoundMgrPulseSetSubsession(subsession, subsession_opt);

	if (g_info.subsession == subsession) {
		debug_warning ("[SESSION] already subsession is [%d]. skip this!!\n", subsession);
	} else {
		g_info.subsession = subsession;
		need_update = true;
	}

	if (g_info.option != subsession_opt) {
		switch (subsession) {
		case SUBSESSION_VR_NORMAL:
#if 0 //def TIZEN_MICRO
			MMSoundMgrSessionSetDeviceAvailable (DEVICE_BT_SCO, 1, 0, NULL);
			MMSoundMgrSessionSetSCO (1);
#endif
		case SUBSESSION_VR_DRIVE:
			g_info.option = subsession_opt;
			break;
		default:
			g_info.option = MM_SUBSESSION_OPTION_NONE;
			break;
		}
		need_update = true;
	}

	if (need_update) {
		debug_warning ("[SESSION] subsession=[%d], resource=[%d]\n", g_info.subsession, g_info.option);
		__set_route(ROUTE_PARAM_NONE);
	}

	UNLOCK_SESSION();

	return MM_ERROR_NONE;
}

int MMSoundMgrSessionGetSubSession(subsession_t *subsession)
{
	if (subsession == NULL) {
		debug_error ("Invalid input parameter\n");
		return MM_ERROR_INVALID_ARGUMENT;
	}

	LOCK_SESSION();

	*subsession = g_info.subsession;

	UNLOCK_SESSION();

	return MM_ERROR_NONE;
}

char* MMSoundMgrSessionGetBtA2DPName ()
{
	return g_info.bt_info.name;
}

void MMSoundMgrSessionSetVoiceControlState (bool enable)
{
	int ret = MM_ERROR_NONE;
	LOCK_SESSION();
	g_info.is_voicecontrol = enable;

	debug_warning("MMSoundMgrSessionSetVoiceControlState --------g_info.session = %d,g_info.subsession = %d ",g_info.session,g_info.subsession);
	if (IS_CALL_SESSION() && g_info.subsession == SUBSESSION_VOICE) {
		debug_warning("already voice subsession in voice session");
		return;
	}

	ret = __set_sound_path_for_voicecontrol();
	UNLOCK_SESSION();
	if (ret != MM_ERROR_NONE) {
		debug_error ("__set_sound_path_for_voicecontrol() failed [%x]\n", ret);
		return;
	}

}

bool MMSoundMgrSessionGetVoiceControlState ()
{
	return g_info.is_voicecontrol;
}


#ifndef _TIZEN_PUBLIC_
#ifndef TIZEN_MICRO
/* -------------------------------- NOISE REDUCTION --------------------------------------------*/
static bool __is_noise_reduction_on (void)
{
	int noise_reduction_on = 1;

	if (vconf_get_bool(VCONF_KEY_NOISE_REDUCTION, &noise_reduction_on)) {
		debug_warning("vconf_get_bool for VCONF_KEY_NOISE_REDUCTION failed\n");
	}

	return (noise_reduction_on == 1) ? true : false;
}

/* -------------------------------- EXTRA VOLUME --------------------------------------------*/
static bool __is_extra_volume_on (void)
{
	int extra_volume_on = 1;

	if (vconf_get_bool(VCONF_KEY_EXTRA_VOLUME, &extra_volume_on )) {
		debug_warning("vconf_get_bool for VCONF_KEY_EXTRA_VOLUME failed\n");
	}

	return (extra_volume_on  == 1) ? true : false;
}
#endif

/* -------------------------------- UPSCALING --------------------------------------------*/
static bool __is_upscaling_needed (void)
{
	int is_wbamr = 1;

	if (vconf_get_bool(VCONF_KEY_WBAMR, &is_wbamr)) {
		debug_warning("vconf_get_bool for VCONF_KEY_WBAMR failed\n");
	}

	return (is_wbamr == 0) ? true : false;
}
/* -------------------------------- BT NREC --------------------------------------------*/
static bool __is_bt_nrec_on (void)
{
	return g_info.bt_info.is_nrec;
}

static bool __is_bt_wb_on (void)
{
	return g_info.bt_info.is_wb;
}

#endif /* _TIZEN_PUBLIC_ */

int MMSoundMgrSessionInit(void)
{
	LOCK_SESSION();

	debug_fenter();

	memset (&g_info, 0, sizeof (SESSION_INFO_STRUCT));

	/* FIXME: Initial status should be updated */
	__set_initial_active_device ();

	/* Register for headset unplug */
	if (_asm_register_for_headset (&g_info.asm_handle) == false) {
		debug_error ("Failed to register ASM for headset\n");
	}

	debug_fleave();

	UNLOCK_SESSION();
	return MM_ERROR_NONE;
}

int MMSoundMgrSessionFini(void)
{
	LOCK_SESSION();

	debug_fenter();

	/* Unregister for headset unplug */
	_asm_unregister_for_headset (&g_info.asm_handle);

	debug_fleave();

	UNLOCK_SESSION();

	return MM_ERROR_NONE;
}

