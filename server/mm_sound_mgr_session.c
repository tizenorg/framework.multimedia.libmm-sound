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
#ifdef TIZEN_MICRO
#include <gio/gio.h>
#endif
#include <vconf.h>
#include <mm_error.h>
#include <mm_debug.h>
#include <audio-session-manager.h>

#include "../include/mm_sound_common.h"
#include "../include/mm_sound_utils.h"
#include "../include/mm_sound_device.h"
#include "include/mm_sound_mgr_common.h"
#include "include/mm_sound_mgr_session.h"
#include "include/mm_sound_mgr_device.h"
#include "include/mm_sound_mgr_device_headset.h"
#include "include/mm_sound_mgr_device_dock.h"
#include "include/mm_sound_mgr_pulse.h"
#include "include/mm_sound_mgr_asm.h"

#define EARJACK_UNPLUGGED	0
#define EARJACK_WITH_MIC	3

#define MAX_STRING_LEN	256

#define VCONF_SOUND_BURSTSHOT "memory/private/sound/burstshot"

#define DEVICE_API_BLUETOOTH	"bluez"
#define DEVICE_API_ALSA	"alsa"
#define DEVICE_BUS_BLUETOOTH "bluetooth"
#define DEVICE_BUS_USB "usb"
#define DEVICE_BUS_BUILTIN "builtin"

#define MIRRORING_MONITOR_SOURCE	"alsa_output.0.analog-stereo.monitor"
#define ALSA_SINK_HDMI	"alsa_output.1.analog-stereo"

#ifdef TIZEN_MICRO
/* Call pause resume scenario with D-bus */
/* Call application send dbus dbus signal */
#define DBUS_CALL_STATUS_PATH		"/Org/Tizen/Call/Status"
#define DBUS_CALL_STATUS_INTERFACE   "org.tizen.call.status"
#define DBUS_CALL_STATUS_CHANGED_SIGNAL	"call_status"
#endif

#define	MM_SOUND_DEVICE_OUT_ANY 0x000FFF00
#define MM_SOUND_DEVICE_IN_ANY	 0x000000FF

#define	MM_SOUND_DEVICE_OUT_FILTER 0x000000FF
#define MM_SOUND_DEVICE_IN_FILTER	 0x000FFF00

#define MAX_BURST_CHECK_RETRY 10
#define BURST_CHECK_INTERVAL 300000

#define STR_LEN 128

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

#ifdef TIZEN_MICRO
enum {
	MM_SOUND_CALL_STATUS_RESUME = 0,
	MM_SOUND_CALL_STATUS_PAUSE = 1,
};
#endif

static int __set_route(bool need_broadcast, bool need_cork);
static int __set_sound_path_for_current_active (bool need_broadcast, bool need_cork);
static int __set_sound_path_to_dual ();
static int __set_sound_path_to_earphone_only (void);
static int __set_sound_path_to_speaker ();
static int __set_sound_path_for_voicecontrol (void);
static void __select_playback_active_device (void);
static void __select_capture_active_device (void);

static const char* __get_session_string(session_t session);
static const char* __get_subsession_string(subsession_t session);
#ifndef _TIZEN_PUBLIC_
#ifndef TIZEN_MICRO
static bool __is_noise_reduction_on (void);
static bool __is_extra_volume_on (void);
#endif
static bool __is_upscaling_needed (void);
static bool __is_right_hand_on (void);
static bool __get_bt_nrec_status (void);
static int __get_ag_wb_status(void);
static int __get_hf_wb_status(void);
static int __get_hf_connection_state(void);
static const char* __get_bt_bandwidth_string(int bandwidth);
#endif

#define ENABLE_CALLBACK
#ifndef ENABLE_CALLBACK
#define _mm_sound_mgr_device_available_device_callback(a,b,c)	MM_ERROR_NONE
#define _mm_sound_mgr_device_active_device_callback(a,b)	MM_ERROR_NONE
#endif

typedef struct _bt_info_struct
{
#ifdef TIZEN_MICRO
	int ag_wb;
	int hf_wb;
	int hfp_conn_state;
#endif
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
	bool is_noise_reduction;
	bool is_extra_volume;
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
#ifdef TIZEN_MICRO
GDBusConnection *conn_callstatus;
guint sig_id_callstatus;
#endif

static void dump_info ()
{
	int i = 0;

	const char *playback_device_str[] = { "SPEAKER ", "RECEIVER ", "HEADSET ", "BTSCO ", "BTA2DP ", "DOCK ", "HDMI ", "MIRRORING ", "USB " };
	const char *capture_device_str[] = { "MAINMIC ", "HEADSET ", "BTMIC "  };

	int playback_max = sizeof (playback_device_str) / sizeof (char*);
	int capture_max = sizeof (capture_device_str) / sizeof (char*);

	static char tmp_str[STR_LEN];
	static char tmp_str2[STR_LEN];

	debug_log ("<----------------------------------------------------->\n");


	strcpy (tmp_str, "PLAYBACK = [ ");
	for (i=0; i<playback_max; i++) {
		if (((g_info.device_available & MM_SOUND_DEVICE_OUT_ANY) >> 8) & (0x01 << i)) {
			strncat (tmp_str, playback_device_str[i], STR_LEN - strlen(tmp_str) -1);
		}
	}
	strcat (tmp_str, "]");

	strcpy (tmp_str2, "CAPTURE = [ ");
		for (i=0; i<capture_max; i++) {
			if ((g_info.device_available & MM_SOUND_DEVICE_IN_ANY) & (0x01 << i)) {
				strncat (tmp_str2, capture_device_str[i], STR_LEN - strlen(tmp_str2) -1);
			}
	}
	strcat (tmp_str2, "]");
	debug_warning ("*** Available = [0x%08x], %s %s", g_info.device_available, tmp_str, tmp_str2);

	strcpy (tmp_str, "PLAYBACK = [ ");
	for (i=0; i<playback_max; i++) {
		if (((g_info.device_active & MM_SOUND_DEVICE_OUT_ANY) >> 8) & (0x01 << i)) {
			strncat (tmp_str, playback_device_str[i], STR_LEN - strlen(tmp_str) -1);
		}
	}
	strcat (tmp_str, "]");

	strcpy (tmp_str2, "CAPTURE = [ ");
		for (i=0; i<capture_max; i++) {
			if ((g_info.device_active & MM_SOUND_DEVICE_IN_ANY) & (0x01 << i)) {
				strncat (tmp_str2, capture_device_str[i], STR_LEN - strlen(tmp_str2) -1);
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


static int __set_route(bool need_broadcast, bool need_cork)
{
	int ret = MM_ERROR_NONE;

	debug_msg ("need_broadcast=%d, need_cork=%d\n", need_broadcast, need_cork);

	LOCK_PATH();

	/* Set path based on current active device */
	ret = __set_sound_path_for_current_active(need_broadcast, need_cork);
	if (ret != MM_ERROR_NONE) {
		debug_error ("__set_sound_path_for_current_active() failed [%x]\n", ret);
		UNLOCK_PATH();
		return ret;
	}

	UNLOCK_PATH();
	return ret;
}

static int __set_route_nolock(bool need_broadcast, bool need_cork)
{
	int ret = MM_ERROR_NONE;

	debug_msg ("need_broadcast=%d, need_cork=%d\n", need_broadcast, need_cork);

	/* Set path based on current active device */
	ret = __set_sound_path_for_current_active(need_broadcast, need_cork);
	if (ret != MM_ERROR_NONE) {
		debug_error ("__set_sound_path_for_current_active() failed [%x]\n", ret);
		UNLOCK_PATH();
		return ret;
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
		__set_route(false, false);
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
		/* Enable Receiver Device */
		debug_log ("voip call session started...");

		/* Backup current active for future restore */
		__backup_current_active_device();

		/* Set default subsession as VOICE */
#ifdef TIZEN_MICRO
		g_info.subsession = SUBSESSION_MEDIA;
#else
		g_info.subsession = SUBSESSION_VOICE;
#endif

		/* OUT */
#ifdef TIZEN_MICRO
		SET_PLAYBACK_ONLY_ACTIVE(MM_SOUND_DEVICE_OUT_SPEAKER);
		SET_CAPTURE_ONLY_ACTIVE(MM_SOUND_DEVICE_IN_MIC);
#else
		if (IS_ACTIVE(MM_SOUND_DEVICE_OUT_SPEAKER)) {
			debug_log ("active out was SPEAKER => activate receiver!!\n");
			SET_PLAYBACK_ONLY_ACTIVE(MM_SOUND_DEVICE_OUT_RECEIVER);
		} else if (IS_ACTIVE(MM_SOUND_DEVICE_OUT_BT_A2DP)) {
			debug_log ("active out was BT A2DP => activate BT SCO!!\n");
			SET_PLAYBACK_ONLY_ACTIVE(MM_SOUND_DEVICE_OUT_BT_SCO);
			SET_CAPTURE_ONLY_ACTIVE(MM_SOUND_DEVICE_IN_BT_SCO);
		}
		__set_route(true, false);
#endif
		dump_info();
	} else { /* SESSION_END */
		/* RESET */
		if (g_info.session != SESSION_VOIP) {
			debug_warning ("Must be VOIP session but current session is [%s]\n",
				__get_session_string(g_info.session));
		}
		debug_log ("Reset ACTIVE, activate previous active device if still available, if not, set based on priority");
		__restore_previous_active_device();

		debug_log ("voip call session stopped...set path based on current active device");
		__set_route(true, false);

		dump_info();
	}
	debug_fleave();
	return ret;
}

static int __set_playback_route_call (session_state_t state)
{
	int ret = MM_ERROR_NONE;

	debug_fenter();

	if (state == SESSION_START) {
		debug_log ("voicecall session started...");

		/* Backup current active for future restore */
		__backup_current_active_device();

		/* Set default subsession as MEDIA */
		g_info.subsession = SUBSESSION_MEDIA;
#ifndef TIZEN_MICRO
		/* (speaker = receiver, headset = headset, bt a2dp = bt sco) */
		/* OUT */
		if (IS_ACTIVE(MM_SOUND_DEVICE_OUT_SPEAKER)) {
			debug_log ("active out was SPEAKER => activate receiver!!\n");
			SET_PLAYBACK_ONLY_ACTIVE(MM_SOUND_DEVICE_OUT_RECEIVER);
		} else if (IS_ACTIVE(MM_SOUND_DEVICE_OUT_BT_A2DP)) {
			debug_log ("active out was BT A2DP => activate BT SCO!!\n");
			SET_PLAYBACK_ONLY_ACTIVE(MM_SOUND_DEVICE_OUT_BT_SCO);
			SET_CAPTURE_ONLY_ACTIVE(MM_SOUND_DEVICE_IN_BT_SCO);
		}
		/* FIXME : Do we have to set IN device ??? */
		/* __set_path_with_notification(DO_NOTI); */
		/* For sharing device information with PulseAudio */
		MMSoundMgrPulseSetActiveDevice(GET_ACTIVE_CAPTURE(), GET_ACTIVE_PLAYBACK());
#endif
		dump_info();
	} else {
		/* SESSION_END */
		debug_log ("Reset ACTIVE, activate previous active device if still available, if not, set based on priority");
		__restore_previous_active_device();

		debug_log ("voicecall session stopped...set path based on current active device");
		__set_route(true, false);

		dump_info();
	}
	debug_fleave();

	return ret;
}

static int __set_playback_route_fmradio (session_state_t state)
{
	int ret = MM_ERROR_NONE;
	int out = MM_SOUND_DEVICE_OUT_NONE;

	debug_fenter();

	if (state == SESSION_START) {
		if (IS_ACTIVE(MM_SOUND_DEVICE_OUT_SPEAKER))
			out = MM_SOUND_DEVICE_OUT_SPEAKER;
		else if (IS_ACTIVE(MM_SOUND_DEVICE_OUT_WIRED_ACCESSORY))
			out = MM_SOUND_DEVICE_OUT_WIRED_ACCESSORY;
		else if (IS_ACTIVE(MM_SOUND_DEVICE_OUT_BT_A2DP))
			out = MM_SOUND_DEVICE_OUT_BT_A2DP;
		/* Note : FM radio input is internal device, not exported */
	} else {
		/* SESSION_END */
		/* Set as current active status */
		/* FIXME : Need to release path on fm radio scenario */
		__set_route(false, false);
	}
	/* FIXME : Need to update device status */
	debug_fleave();
	return ret;
}

static int __set_playback_route_notification (session_state_t state)
{
	int ret = MM_ERROR_NONE;

	debug_fenter();

	if (state == SESSION_START) {
		/* FIXME : Need to check using vconf key with check status and policy */
		if (_mm_sound_is_recording() || _mm_sound_is_mute_policy()) {
			/* Force earphone path for mute case */
			if ((ret = __set_sound_path_to_earphone_only ()) != MM_ERROR_NONE) {
				debug_error ("__set_sound_path_to_earphone_only() failed [%x]\n", ret);
			}
		} else {
			/* In case of B2s, lockup issue is occurred */
			/* No HEADSET device */
			/* No matter on YMU chipset */
			if ((ret = __set_sound_path_to_dual ()) != MM_ERROR_NONE) {
				debug_error ("__set_sound_path_to_dual() failed [%x]\n", ret);
			}
		}
	} else { 
		/* SESSION_END */
		__set_route(false, false);
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
		__set_route(false, false);
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
		__set_route(false, false);
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
		/* FIXME : On MICRO profile, need to check release path here */
		__set_route(true, false);
	}

	debug_fleave();

	return ret;
}

#if 0
/* FIXME : Need to check for private function */
static int __set_playback_route_timer_shot_and_recorder (subsession_state_t state)
{
	int ret = MM_ERROR_NONE;
	mm_sound_device_in device_in_after = MM_SOUND_DEVICE_IN_NONE;
	mm_sound_device_out device_out_after = MM_SOUND_DEVICE_OUT_NONE;
	bool is_available = 0;
	debug_fenter();

	LOCK_PATH();
	if (state == SUBSESSION_START) {
		MMSoundMgrSessionGetDeviceActive(&g_info.device_out_previous, &g_info.device_in_previous);
		g_info.device_available_previous = g_info.device_available;
		if(g_info.device_out_previous != MM_SOUND_DEVICE_OUT_SPEAKER) {
			__set_sound_path_for_active_device_nolock(MM_SOUND_DEVICE_OUT_SPEAKER, MM_SOUND_DEVICE_IN_NONE);
		}
		/* audio path is changed when subsession is set */
	} else { /* SESSION_END */
		MMSoundMgrSessionGetDeviceActive(&device_out_after, &device_in_after);
		if(device_out_after != MM_SOUND_DEVICE_OUT_SPEAKER) {
			__set_sound_path_for_active_device_nolock(device_out_after, device_in_after);
		} else if(device_out_after == MM_SOUND_DEVICE_OUT_SPEAKER && g_info.device_out_previous != MM_SOUND_DEVICE_OUT_SPEAKER) {
			MMSoundMgrSessionIsDeviceAvailable(g_info.device_out_previous, g_info.device_in_previous, &is_available);
			if(is_available) {
				__set_sound_path_for_active_device_nolock(g_info.device_out_previous, g_info.device_in_previous);
			} else {
				__select_playback_active_device();
				__select_capture_active_device();
				MMSoundMgrSessionGetDeviceActive(&device_out_after, &device_in_after);
				if(device_out_after != MM_SOUND_DEVICE_OUT_SPEAKER) {
					__set_sound_path_for_active_device_nolock(device_out_after, device_in_after);
				}
			}
		}
		MMSoundMgrSessionGetDeviceActive(&g_info.device_out_previous, &g_info.device_in_previous);
		g_info.device_available_previous = g_info.device_available;
	}

	UNLOCK_PATH();

	debug_fleave();

	return ret;
}
#endif

static bool __is_forced_session ()
{
	return (IS_ALARM_SESSION() || IS_NOTIFICATION_SESSION() || IS_EMERGENCY_SESSION())? true : false;
}

static bool __is_recording_subsession ()
{
	/* FIXME : Need to check routing option flag */
	/* On private, by record option routing flag is changed */
	bool is_recording = true;
	switch (g_info.subsession) {
		case SUBSESSION_RECORD_STEREO:
		case SUBSESSION_RECORD_MONO:
			break;
		default:
			is_recording = false;
			break;
	}
	return is_recording;
}

static void _wait_if_burstshot_exists(void)
{
	int retry = 0;
	int is_burstshot = 0;

	do {
		if (retry > 0)
			usleep (BURST_CHECK_INTERVAL);
		if (vconf_get_int(VCONF_SOUND_BURSTSHOT, &is_burstshot) != 0) {
			debug_warning ("Faided to get [%s], assume no burstshot....", VCONF_SOUND_BURSTSHOT);
			is_burstshot = 0;
		} else {
			debug_warning ("Is Burstshot [%d], retry...[%d/%d], interval usec = %d",
						is_burstshot, retry, MAX_BURST_CHECK_RETRY, BURST_CHECK_INTERVAL);
		}
	} while (is_burstshot && retry++ < MAX_BURST_CHECK_RETRY);
}


static int __set_sound_path_for_current_active (bool need_broadcast, bool need_cork)
{
	int ret = MM_ERROR_NONE;
	int in = 0, out = 0;

	debug_fenter();
	debug_msg ("session:%s, subsession:%s, option:%d, active in:0x%x, out:0x%x, need_cork:%d",
			__get_session_string(g_info.session), __get_subsession_string(g_info.subsession),
			g_info.option, GET_ACTIVE_CAPTURE(), GET_ACTIVE_PLAYBACK(), need_cork);

	if (__is_forced_session()) {
		debug_warning ("Current session is ALARM/NOTI/EMER, pending path setting. path set will be done after session ends");
		return MM_ERROR_SOUND_INVALID_STATE;
	}

	/* Wait if BurstShot is ongoing */
	_wait_if_burstshot_exists();

	if (need_cork)
		MMSoundMgrPulseSetCorkAll (true);

	in = GET_ACTIVE_CAPTURE();
	out = GET_ACTIVE_PLAYBACK();

#ifdef TIZEN_MICRO
	/* Wearable BT SCO headset case. Should be check nrec, wb, hf does't use sco */
    	if (in == MM_SOUND_DEVICE_IN_BT_SCO && out == MM_SOUND_DEVICE_OUT_BT_SCO) {
		bool nrec = 0;
		int bandwidth = MM_SOUND_BANDWIDTH_UNKNOWN;

		ret = MMSoundMgrPulseGetBluetoothInfo(&nrec, &bandwidth);
		if(ret == MM_ERROR_NONE) {
			g_info.bt_info.is_nrec = nrec;
			g_info.bt_info.ag_wb = bandwidth;
			debug_msg("get bt information successfully. nrec(%d), wb(%s)",
				g_info.bt_info.is_nrec, __get_bt_bandwidth_string(g_info.bt_info.ag_wb));
		} else {
			g_info.bt_info.is_nrec = false;
			g_info.bt_info.ag_wb = MM_SOUND_BANDWIDTH_NB;
			debug_msg("failed to get bt information. use default setting. nrec(off), wb(off)");
		}
		/* FIXME : How to send status of nrec & ag wideband state with option flag, on MICRO profile */
	}
#else
	if (in == MM_SOUND_DEVICE_IN_BT_SCO && out == MM_SOUND_DEVICE_OUT_BT_SCO) {
		/* FIXME : How to send status of nrec & ag wideband state with option flag */
	}
#endif

	/* prepare GAIN */
	switch (g_info.session) {
	case SESSION_MEDIA:
	case SESSION_NOTIFICATION:
	case SESSION_ALARM:
	case SESSION_EMERGENCY:
		if (IS_MEDIA_SESSION() && __is_recording_subsession()) {
			/* gain & recording option */
		} else {
			/* gain option */
			if (g_info.is_voicecontrol) {
				debug_warning ("VoiceControl");
				/* voice control option */
			}
		}
		break;
	case SESSION_VOIP:
		if (g_info.subsession == SUBSESSION_RINGTONE) {
			in = MM_SOUND_DEVICE_OUT_NONE;
			/* gain option */
			/* If active device was WFD(mirroring), set option */
			if (out == MM_SOUND_DEVICE_OUT_MIRRORING) {
				/* mirroring option */
			}
			if (_mm_sound_is_mute_policy ()) {
				/* Mute Ringtone */
				out = MM_SOUND_DEVICE_OUT_BT_A2DP;
			} else {
				/* Normal Ringtone */
				out = MM_SOUND_DEVICE_OUT_SPEAKER;
				/* dual out option */
			}
		} else if (g_info.subsession == SUBSESSION_VOICE) {
			/* gain option */
		} else {
			debug_warning ("Unexpected SUBSESSION [%s]\n", __get_subsession_string(g_info.subsession));
				/* gain voip option */
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
			in = MM_SOUND_DEVICE_IN_NONE;
			/* If active device was WFD(mirroring), set option */
			if (out == MM_SOUND_DEVICE_OUT_MIRRORING) {
				/* mirroring option */
			}

#ifndef _TIZEN_PUBLIC_
#ifdef TIZEN_MICRO
			if (vconf_get_bool(VCONF_KEY_VR_RINGTONE_ENABLED, &vr_ringtone_enabled)) {
				debug_warning("vconf_get_bool for %s failed\n", VCONF_KEY_VR_RINGTONE_ENABLED);
			}

			if (vr_ringtone_enabled) {
				/* bargin option */
			}
#else
			/* If voice control for incoming call is enabled, set capture path here for avoiding ringtone breaks */
			if (vconf_get_bool(VCONF_KEY_VR_ENABLED, &vr_enabled)) {
				debug_warning("vconf_get_bool for %s failed\n", VCONF_KEY_VR_ENABLED);
			} else if (vconf_get_bool(VCONF_KEY_VR_RINGTONE_ENABLED, &vr_ringtone_enabled)) {
				debug_warning("vconf_get_bool for %s failed\n", VCONF_KEY_VR_RINGTONE_ENABLED);
			}

			if (vr_enabled && vr_ringtone_enabled) {
				/* bargin option */
			}
#endif /* TIZEN_MICRO */
#endif /* #ifndef _TIZEN_PUBLIC_ */
			if (_mm_sound_is_mute_policy ()) {
				/* Mute Ringtone */
#ifdef TIZEN_MICRO
				out = MM_SOUND_DEVICE_OUT_SPEAKER;
#else
				out = MM_SOUND_DEVICE_OUT_BT_A2DP;
#endif
			} else {
				out = MM_SOUND_DEVICE_OUT_SPEAKER;
#ifdef TIZEN_MICRO
				/* for inband ringtone. hfp call ringtone is not used sco */
				int state = MM_SOUND_HFP_STATUS_UNKNOWN;
				state = __get_hf_connection_state();

				if(state == MM_SOUND_HFP_STATUS_INCOMMING_CALL) {
					/* call by BT option */
				}
#endif				
				/* Normal Ringtone */
				/* dual out option */
			}
		} else if (g_info.subsession == SUBSESSION_MEDIA) {
			/* call gain option */
			in = MM_SOUND_DEVICE_IN_NONE;
		} else if (g_info.subsession == SUBSESSION_VOICE) {
			/* gain for voicecall or videocall */
#ifdef _TIZEN_PUBLIC_
			if (out ==  MM_SOUND_DEVICE_OUT_WIRED_ACCESSORY) {
				debug_log ("Fix in path to headsetmic when out path is headset\n");
				in = MM_SOUND_DEVICE_IN_WIRED_ACCESSORY;
			}
			/* FIXME : Check to NB / WB option */
#endif /* _TIZEN_PUBLIC_*/
		} else {
			debug_warning ("Unexpected SUBSESSION [%s]\n", __get_subsession_string(g_info.subsession));
		}
		break;

	case SESSION_FMRADIO:
		break;

	case SESSION_VOICE_RECOGNITION:
#ifdef TIZEN_MICRO
		if (__is_right_hand_on()) {
		}
#endif
		if (g_info.subsession == SUBSESSION_VR_NORMAL) {
			/* NORMAL mode */
		} else if (g_info.subsession == SUBSESSION_VR_DRIVE) {
			/* DRIVE mode */
			/* FIXME : Need to check private mode or not */
		} else {
				debug_warning ("Unexpected SUBSESSION [%s]\n", __get_subsession_string(g_info.subsession));
		}
		break;
	default:
		debug_warning ("session [%s] is not handled...\n", __get_session_string(g_info.session));
		break;
	}

	debug_warning ("Trying to set device to pulseaudio : in[%d], out[%d]\n", in, out);

	/* Update Pulseaudio Active Device */
	if (IS_ACTIVE(MM_SOUND_DEVICE_OUT_BT_A2DP)) {
		MMSoundMgrPulseSetActiveDevice(GET_ACTIVE_CAPTURE(), GET_ACTIVE_PLAYBACK());
	} else if (IS_ACTIVE(MM_SOUND_DEVICE_OUT_USB_AUDIO)) {
		MMSoundMgrPulseSetActiveDevice(GET_ACTIVE_CAPTURE(), GET_ACTIVE_PLAYBACK());
	} else if (IS_ACTIVE(MM_SOUND_DEVICE_OUT_MULTIMEDIA_DOCK)) {
		MMSoundMgrPulseSetActiveDevice(GET_ACTIVE_CAPTURE(), GET_ACTIVE_PLAYBACK());
	} else {
		/* ALSA route */
		MMSoundMgrPulseSetActiveDevice(in, out);
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

	if (need_broadcast) {
		/* Notify current active device */
		ret = _mm_sound_mgr_device_active_device_callback(GET_ACTIVE_CAPTURE(), GET_ACTIVE_PLAYBACK());
		if (ret != MM_ERROR_NONE) {
			debug_error ("_mm_sound_mgr_device_active_device_callback() failed [%x]\n", ret);
		}
	}

	if (need_cork) {
		/* FIXME : Private issues, workaround for earjack inserting scenario during media playback */
		if (g_info.session == SESSION_MEDIA && (IS_ACTIVE(MM_SOUND_DEVICE_OUT_BT_A2DP) || IS_ACTIVE(MM_SOUND_DEVICE_OUT_WIRED_ACCESSORY))) {
			usleep(20000);
		}		
		MMSoundMgrPulseSetCorkAll (false);
	}
	MMSoundMgrPulseUpdateVolume();

	/* clean up */
	debug_fleave();
	return ret;
}

static int __set_sound_path_for_voicecontrol (void)
{
	int ret = MM_ERROR_NONE;
	int in = MM_SOUND_DEVICE_IN_NONE, out = MM_SOUND_DEVICE_OUT_NONE;

	debug_fenter();

	/* prepare IN */
	if (IS_ACTIVE(MM_SOUND_DEVICE_IN_MIC)) {
		in = MM_SOUND_DEVICE_IN_MIC;
	} else if (IS_ACTIVE(MM_SOUND_DEVICE_IN_WIRED_ACCESSORY)) {
		in = MM_SOUND_DEVICE_IN_WIRED_ACCESSORY;
	} else if (IS_ACTIVE(MM_SOUND_DEVICE_IN_BT_SCO)) {
		debug_warning ("[NOT EXPECTED CASE] BT SCO");
	}

	debug_warning ("g_info.session = %s ", __get_session_string(g_info.session));
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
		/* gain control KEYTONE */
	if (g_info.is_voicecontrol) {
#ifdef TIZEN_MICRO
			/* For inband ringtone. hfp call ringtone is not used sco */
			int state = MM_SOUND_HFP_STATUS_UNKNOWN;
			state = __get_hf_connection_state();
			if(state == MM_SOUND_HFP_STATUS_INCOMMING_CALL) {
				/* Call by BT option */
			}
#endif
			debug_warning ("VoiceControl\n");
			/* Bargein option */
		}
		break;

	case SESSION_FMRADIO:
	case SESSION_VOICE_RECOGNITION:
		debug_warning ("[NOT EXPECTED CASE] ");
		break;

	default:
		debug_warning ("session [%s] is not handled...\n", __get_session_string(g_info.session));
		break;
	}

	debug_warning ("Trying to set device to pulseaudio : in[%d], out[%d]\n", in, GET_ACTIVE_PLAYBACK());

	/* Set Path (IN, OUT) */
	MMSoundMgrPulseSetActiveDevice(in, GET_ACTIVE_PLAYBACK());

	debug_fleave();
	return ret;

}


static int __set_sound_path_to_dual (void)
{
	int ret = MM_ERROR_NONE;
	int in = MM_SOUND_DEVICE_IN_NONE;

	debug_fenter();

	in = IS_ACTIVE(MM_SOUND_DEVICE_IN_WIRED_ACCESSORY)? MM_SOUND_DEVICE_IN_WIRED_ACCESSORY : MM_SOUND_DEVICE_IN_MIC;

	/* If active device was WFD(mirroring), set option */
	if (IS_ACTIVE(MM_SOUND_DEVICE_OUT_MIRRORING)) {
		/* mirorring option */
	}
	if (g_info.is_voicecontrol) {
		debug_msg ("VoiceControl\n");
		/* bargein option */
	}
	/* Sound path for ALSA */
	debug_msg ("not supported, set path to DUAL-OUT");
	MMSoundMgrPulseSetActiveDevice(in, MM_SOUND_DEVICE_OUT_SPEAKER);

	/* clean up */
	debug_fleave();
	return ret;
}

static int __set_sound_path_to_earphone_only (void)
{
	int ret = MM_ERROR_NONE;
	int in = MM_SOUND_DEVICE_IN_NONE;

	debug_fenter();

	in = IS_ACTIVE(MM_SOUND_DEVICE_IN_WIRED_ACCESSORY)? MM_SOUND_DEVICE_IN_WIRED_ACCESSORY : MM_SOUND_DEVICE_IN_MIC;

	if (g_info.is_voicecontrol) {
		debug_msg ("VoiceControl\n");
		/* bargein option */
	}

	/* Sound path for ALSA */
	debug_msg ("Set path to EARPHONE only.\n");
	MMSoundMgrPulseSetActiveDevice(in, MM_SOUND_DEVICE_OUT_WIRED_ACCESSORY);

	/* clean up */
	debug_fleave();
	return ret;
}

int  MMSoundMgrSessionSetSoundPathForActiveDevice (mm_sound_device_out playback, mm_sound_device_in capture)
{
	int ret = MM_ERROR_NONE;

	debug_fenter();

	/* Sound path for ALSA */
	debug_log ("Set path for active device.playback:%x, capture:%x\n",playback,capture);
	if ((playback && !IS_AVAILABLE(playback)) || (capture && !IS_AVAILABLE(capture))) {
		debug_warning ("Failed to set active state to unavailable device!!!\n");
		ret = MM_ERROR_INVALID_ARGUMENT;
		goto END_SET_DEVICE;
	}

	LOCK_PATH();
	/* Update active state */
	debug_log ("Update active device as request\n");
	if (playback) {
		SET_PLAYBACK_ONLY_ACTIVE(playback);
	}
	if (capture) {
		SET_CAPTURE_ONLY_ACTIVE(capture);
	}
	MMSoundMgrPulseSetActiveDevice(GET_ACTIVE_CAPTURE(), GET_ACTIVE_PLAYBACK());
	UNLOCK_PATH();

END_SET_DEVICE:
	debug_fleave();
	return ret;
}

static int __set_sound_path_for_active_device_nolock (mm_sound_device_out playback, mm_sound_device_in capture)
{
	int ret = MM_ERROR_NONE;

	debug_fenter();

	/* Sound path for ALSA */
	debug_log ("Set path for active device.playback:%x, capture:%x\n",playback,capture);
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
	MMSoundMgrPulseSetActiveDevice(GET_ACTIVE_CAPTURE(), GET_ACTIVE_PLAYBACK());

END_SET_DEVICE:
	debug_fleave();
	return ret;
}

static int __set_sound_path_to_speaker (void)
{
	int ret = MM_ERROR_NONE;

	debug_fenter();

	/* Sound path for ALSA */
	debug_log ("Set path to SPEAKER.\n");
	MMSoundMgrPulseSetActiveDevice(GET_ACTIVE_CAPTURE(), MM_SOUND_DEVICE_OUT_SPEAKER);

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
		MMSoundMgrDeviceUpdateStatus (DEVICE_UPDATE_STATUS_CHANGED_INFO_IO_DIRECTION, DEVICE_TYPE_BLUETOOTH, DEVICE_IO_DIRECTION_OUT, DEVICE_ID_AUTO, NULL, DEVICE_STATE_ACTIVATED, NULL);
		MMSoundMgrDeviceUpdateStatus (DEVICE_UPDATE_STATUS_CHANGED_INFO_STATE, DEVICE_TYPE_BLUETOOTH, DEVICE_IO_DIRECTION_OUT, DEVICE_ID_AUTO, NULL, DEVICE_STATE_ACTIVATED, NULL);
	} else if (IS_AVAILABLE(MM_SOUND_DEVICE_OUT_MIRRORING)) {
		debug_log ("MIRRORING available, set as active!!\n");
		SET_ACTIVE(MM_SOUND_DEVICE_OUT_MIRRORING);
		MMSoundMgrDeviceUpdateStatus (DEVICE_UPDATE_STATUS_CHANGED_INFO_STATE, DEVICE_TYPE_MIRRORING, DEVICE_IO_DIRECTION_OUT, DEVICE_ID_AUTO, NULL, DEVICE_STATE_ACTIVATED, NULL);
	} else if (IS_AVAILABLE(MM_SOUND_DEVICE_OUT_DOCK)) {
		debug_log ("DOCK available, set as active!!\n");
		SET_ACTIVE(MM_SOUND_DEVICE_OUT_DOCK);
	} else if (IS_AVAILABLE(MM_SOUND_DEVICE_OUT_HDMI)) {
		debug_log ("HDMI available, set as active!!\n");
		SET_ACTIVE(MM_SOUND_DEVICE_OUT_HDMI);
		MMSoundMgrDeviceUpdateStatus (DEVICE_UPDATE_STATUS_CHANGED_INFO_STATE, DEVICE_TYPE_HDMI, DEVICE_IO_DIRECTION_OUT, DEVICE_ID_AUTO, NULL, DEVICE_STATE_ACTIVATED, NULL);
	} else if (IS_AVAILABLE(MM_SOUND_DEVICE_OUT_USB_AUDIO)) {
		debug_log ("USB Audio available, set as active!!\n");
		SET_ACTIVE(MM_SOUND_DEVICE_OUT_USB_AUDIO);
		MMSoundMgrDeviceUpdateStatus (DEVICE_UPDATE_STATUS_CHANGED_INFO_STATE, DEVICE_TYPE_USB_AUDIO, DEVICE_IO_DIRECTION_OUT, DEVICE_ID_AUTO, NULL, DEVICE_STATE_ACTIVATED, NULL);
	} else if (IS_AVAILABLE(MM_SOUND_DEVICE_OUT_MULTIMEDIA_DOCK)) {
		debug_log ("Multimedia Dock available, set as active!!\n");
		SET_ACTIVE(MM_SOUND_DEVICE_OUT_MULTIMEDIA_DOCK);
	} else if (IS_AVAILABLE(MM_SOUND_DEVICE_OUT_WIRED_ACCESSORY)) {
		debug_log ("WIRED available, set as active!!\n");
		SET_ACTIVE(MM_SOUND_DEVICE_OUT_WIRED_ACCESSORY);
		MMSoundMgrDeviceUpdateStatus (DEVICE_UPDATE_STATUS_CHANGED_INFO_STATE, DEVICE_TYPE_AUDIOJACK, 0, DEVICE_ID_AUTO, NULL, DEVICE_STATE_ACTIVATED, NULL);
	} else {
		debug_log ("SPEAKER/RECEIVER available, set SPEAKER as active!!\n");
		SET_ACTIVE(MM_SOUND_DEVICE_OUT_SPEAKER);
		MMSoundMgrDeviceUpdateStatus (DEVICE_UPDATE_STATUS_CHANGED_INFO_STATE, DEVICE_TYPE_BUILTIN_SPEAKER, DEVICE_IO_DIRECTION_OUT, DEVICE_ID_AUTO, NULL, DEVICE_STATE_ACTIVATED, NULL);
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
		MMSoundMgrDeviceUpdateStatus (DEVICE_UPDATE_STATUS_CHANGED_INFO_STATE, DEVICE_TYPE_BLUETOOTH, DEVICE_IO_DIRECTION_BOTH, DEVICE_ID_AUTO, NULL, DEVICE_STATE_ACTIVATED, NULL);
	} else if (IS_AVAILABLE(MM_SOUND_DEVICE_IN_WIRED_ACCESSORY)) {
		debug_log ("WIRED available, set as active!!\n");
		SET_ACTIVE(MM_SOUND_DEVICE_IN_WIRED_ACCESSORY);
		MMSoundMgrDeviceUpdateStatus (DEVICE_UPDATE_STATUS_CHANGED_INFO_STATE, DEVICE_TYPE_AUDIOJACK, DEVICE_IO_DIRECTION_BOTH, DEVICE_ID_AUTO, NULL, DEVICE_STATE_ACTIVATED, NULL);
	} else {
		debug_log ("MIC available, set as active!!\n");
		SET_ACTIVE(MM_SOUND_DEVICE_IN_MIC);
		MMSoundMgrDeviceUpdateStatus (DEVICE_UPDATE_STATUS_CHANGED_INFO_STATE, DEVICE_TYPE_BUILTIN_MIC, DEVICE_IO_DIRECTION_IN, DEVICE_ID_AUTO, NULL, DEVICE_STATE_ACTIVATED, NULL);
	}
}

static int __get_device_type_from_old_device (int old_device_type, device_type_e *device_type)
{
	int ret = MM_ERROR_NONE;

	switch(old_device_type) {
	case MM_SOUND_DEVICE_IN_MIC:
		*device_type = DEVICE_TYPE_BUILTIN_MIC;
		break;
	case MM_SOUND_DEVICE_IN_WIRED_ACCESSORY:
	case MM_SOUND_DEVICE_OUT_WIRED_ACCESSORY:
		*device_type = DEVICE_TYPE_AUDIOJACK;
		break;
	case MM_SOUND_DEVICE_IN_BT_SCO:
	case MM_SOUND_DEVICE_OUT_BT_SCO:
	case MM_SOUND_DEVICE_OUT_BT_A2DP:
		*device_type = DEVICE_TYPE_BLUETOOTH;
		break;
	case MM_SOUND_DEVICE_OUT_SPEAKER:
		*device_type = DEVICE_TYPE_BUILTIN_SPEAKER;
		break;
	case MM_SOUND_DEVICE_OUT_RECEIVER:
		*device_type = DEVICE_TYPE_BUILTIN_RECEIVER;
		break;
	case MM_SOUND_DEVICE_OUT_HDMI:
		*device_type = DEVICE_TYPE_HDMI;
		break;
	case MM_SOUND_DEVICE_OUT_MIRRORING:
		*device_type = DEVICE_TYPE_MIRRORING;
		break;
	case MM_SOUND_DEVICE_OUT_USB_AUDIO:
		*device_type = DEVICE_TYPE_USB_AUDIO;
		break;
	default:
		ret = MM_ERROR_NOT_SUPPORT_API;
		break;
	}
	return ret;
}

static void __set_deactivate_all_device_auto (mm_sound_device_in exception_in, mm_sound_device_out exception_out)
{
	/* DEACTIVATE OTHERS */
	/* IN */
	if (exception_in != MM_SOUND_DEVICE_IN_MIC) {
		MMSoundMgrDeviceUpdateStatus (DEVICE_UPDATE_STATUS_CHANGED_INFO_STATE, DEVICE_TYPE_BUILTIN_MIC, 0, DEVICE_ID_AUTO, NULL, DEVICE_STATE_DEACTIVATED, NULL);
	}
	if ((exception_in != MM_SOUND_DEVICE_IN_WIRED_ACCESSORY) && (exception_out != MM_SOUND_DEVICE_OUT_WIRED_ACCESSORY)) {
		MMSoundMgrDeviceUpdateStatus (DEVICE_UPDATE_STATUS_CHANGED_INFO_STATE, DEVICE_TYPE_AUDIOJACK, 0, DEVICE_ID_AUTO, NULL, DEVICE_STATE_DEACTIVATED, NULL);
	}
	if ((exception_in != MM_SOUND_DEVICE_IN_BT_SCO) && (exception_out != MM_SOUND_DEVICE_OUT_BT_A2DP) && (exception_out != MM_SOUND_DEVICE_OUT_BT_SCO)) {
		MMSoundMgrDeviceUpdateStatus (DEVICE_UPDATE_STATUS_CHANGED_INFO_STATE, DEVICE_TYPE_BLUETOOTH, 0, DEVICE_ID_AUTO, NULL, DEVICE_STATE_DEACTIVATED, NULL);
	}
	/* OUT */
	if (exception_out != MM_SOUND_DEVICE_OUT_SPEAKER) {
		MMSoundMgrDeviceUpdateStatus (DEVICE_UPDATE_STATUS_CHANGED_INFO_STATE, DEVICE_TYPE_BUILTIN_SPEAKER, 0, DEVICE_ID_AUTO, NULL, DEVICE_STATE_DEACTIVATED, NULL);
	}
	if (exception_out != MM_SOUND_DEVICE_OUT_RECEIVER) {
		MMSoundMgrDeviceUpdateStatus (DEVICE_UPDATE_STATUS_CHANGED_INFO_STATE, DEVICE_TYPE_BUILTIN_RECEIVER, 0, DEVICE_ID_AUTO, NULL, DEVICE_STATE_DEACTIVATED, NULL);
	}
	if ((exception_out != MM_SOUND_DEVICE_OUT_WIRED_ACCESSORY) && (exception_in != MM_SOUND_DEVICE_IN_WIRED_ACCESSORY)) {
		MMSoundMgrDeviceUpdateStatus (DEVICE_UPDATE_STATUS_CHANGED_INFO_STATE, DEVICE_TYPE_AUDIOJACK, 0, DEVICE_ID_AUTO, NULL, DEVICE_STATE_DEACTIVATED, NULL);
	}
	if (exception_out != MM_SOUND_DEVICE_OUT_MIRRORING) {
		MMSoundMgrDeviceUpdateStatus (DEVICE_UPDATE_STATUS_CHANGED_INFO_STATE, DEVICE_TYPE_MIRRORING, 0, DEVICE_ID_AUTO, NULL, DEVICE_STATE_DEACTIVATED, NULL);
	}
	if (exception_out != MM_SOUND_DEVICE_OUT_USB_AUDIO) {
		MMSoundMgrDeviceUpdateStatus (DEVICE_UPDATE_STATUS_CHANGED_INFO_STATE, DEVICE_TYPE_USB_AUDIO, 0, DEVICE_ID_AUTO, NULL, DEVICE_STATE_DEACTIVATED, NULL);
	}
	if ((exception_out != MM_SOUND_DEVICE_OUT_BT_A2DP) && (exception_out != MM_SOUND_DEVICE_OUT_BT_SCO) && (exception_in != MM_SOUND_DEVICE_IN_BT_SCO)) {
		MMSoundMgrDeviceUpdateStatus (DEVICE_UPDATE_STATUS_CHANGED_INFO_STATE, DEVICE_TYPE_BLUETOOTH, 0, DEVICE_ID_AUTO, NULL, DEVICE_STATE_DEACTIVATED, NULL);
	}
	if (exception_out != MM_SOUND_DEVICE_OUT_HDMI) {
		MMSoundMgrDeviceUpdateStatus (DEVICE_UPDATE_STATUS_CHANGED_INFO_STATE, DEVICE_TYPE_HDMI, 0, DEVICE_ID_AUTO, NULL, DEVICE_STATE_DEACTIVATED, NULL);
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
		if (g_info.headset_type == DEVICE_EARJACK_TYPE_SPK_WITH_MIC) {
			SET_AVAILABLE(MM_SOUND_DEVICE_IN_WIRED_ACCESSORY);
			MMSoundMgrDeviceUpdateStatus(DEVICE_UPDATE_STATUS_CONNECTED, DEVICE_TYPE_AUDIOJACK, DEVICE_IO_DIRECTION_BOTH, DEVICE_ID_AUTO, DEVICE_NAME_AUDIOJACK_4P, 0, NULL);
			MMSoundMgrDeviceUpdateStatus(DEVICE_UPDATE_STATUS_CHANGED_INFO_STATE, DEVICE_TYPE_AUDIOJACK, DEVICE_IO_DIRECTION_BOTH, DEVICE_ID_AUTO, DEVICE_NAME_AUDIOJACK_4P, DEVICE_STATE_ACTIVATED, NULL);
		} else {
			MMSoundMgrDeviceUpdateStatus(DEVICE_UPDATE_STATUS_CONNECTED, DEVICE_TYPE_AUDIOJACK, DEVICE_IO_DIRECTION_BOTH, DEVICE_ID_AUTO, DEVICE_NAME_AUDIOJACK_3P, 0, NULL);
			MMSoundMgrDeviceUpdateStatus(DEVICE_UPDATE_STATUS_CHANGED_INFO_STATE, DEVICE_TYPE_AUDIOJACK, DEVICE_IO_DIRECTION_BOTH, DEVICE_ID_AUTO, DEVICE_NAME_AUDIOJACK_3P, DEVICE_STATE_ACTIVATED, NULL);
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
		MMSoundMgrDeviceUpdateStatus(DEVICE_UPDATE_STATUS_CONNECTED, DEVICE_TYPE_BLUETOOTH, DEVICE_IO_DIRECTION_OUT, DEVICE_ID_AUTO, NULL, 0, NULL);
	}
	if (sco) {
		SET_AVAILABLE(MM_SOUND_DEVICE_OUT_BT_SCO);
		SET_AVAILABLE(MM_SOUND_DEVICE_IN_BT_SCO);
		MMSoundMgrDeviceUpdateStatus(DEVICE_UPDATE_STATUS_CONNECTED, DEVICE_TYPE_BLUETOOTH, DEVICE_IO_DIRECTION_BOTH, DEVICE_ID_AUTO, NULL, 0, NULL);
	}

	/* Set Active device based on priority */
	__select_playback_active_device ();
	__select_capture_active_device ();

	__set_route(true, false);
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

	/* ACTIVATE BLUETOOTH */
	MMSoundMgrDeviceUpdateStatus (DEVICE_UPDATE_STATUS_CHANGED_INFO_STATE, DEVICE_TYPE_BLUETOOTH, 0, DEVICE_ID_AUTO, NULL, DEVICE_STATE_ACTIVATED, NULL);
	/* DEACTIVATE OTHERS */
	__set_deactivate_all_device_auto (GET_ACTIVE_CAPTURE(), GET_ACTIVE_PLAYBACK());

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

	/* DEACTIVATE BLUETOOTH */
	MMSoundMgrDeviceUpdateStatus (DEVICE_UPDATE_STATUS_CHANGED_INFO_STATE, DEVICE_TYPE_BLUETOOTH, 0, DEVICE_ID_AUTO, NULL, DEVICE_STATE_DEACTIVATED, NULL);

	/* activate current available device based on priority */
	__select_playback_active_device();

	/* Do set path and notify result */
	__set_route_nolock(true, true);

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

	/* For wearable history */
	/* __set_route_nolock(ROUTE_PARAM_BROADCASTING | ROUTE_PARAM_CORK_DEVICE); */
	/* Do set path and notify result */
	__set_route(true, true);

	/* ACTIVATE BLUETOOTH */
	MMSoundMgrDeviceUpdateStatus (DEVICE_UPDATE_STATUS_CHANGED_INFO_STATE, DEVICE_TYPE_BLUETOOTH, 0, DEVICE_ID_AUTO, NULL, DEVICE_STATE_ACTIVATED, NULL);
	/* DEACTIVATE OTHERS */
	__set_deactivate_all_device_auto (GET_ACTIVE_CAPTURE(), GET_ACTIVE_PLAYBACK());

	dump_info ();
}

static void __handle_bt_sco_off (void)
{
	/* DEACTIVATE BLUETOOTH */
	MMSoundMgrDeviceUpdateStatus (DEVICE_UPDATE_STATUS_CHANGED_INFO_STATE, DEVICE_TYPE_BLUETOOTH, 0, DEVICE_ID_AUTO, NULL, DEVICE_STATE_DEACTIVATED, NULL);

	/* If sco is not activated, just return */
	if (!IS_ACTIVE(MM_SOUND_DEVICE_OUT_BT_SCO) && !IS_ACTIVE(MM_SOUND_DEVICE_IN_BT_SCO)) {
		debug_warning("BT SCO was not active. nothing to do here.");
		dump_info ();
		return;
	}

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

	/* Do set path and notify result */
	__set_route_nolock(true, true);

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
	if (type == DEVICE_EARJACK_TYPE_SPK_WITH_MIC) {
		debug_log ("Activate WIRED IN device\n");
		SET_CAPTURE_ONLY_ACTIVE(MM_SOUND_DEVICE_IN_WIRED_ACCESSORY);
	}

	/* Do set path and notify result */
	__set_route(true, true);

	/* ACTIVATE AUDIOJACK */
	MMSoundMgrDeviceUpdateStatus (DEVICE_UPDATE_STATUS_CHANGED_INFO_STATE, DEVICE_TYPE_AUDIOJACK, 0, DEVICE_ID_AUTO, NULL, DEVICE_STATE_ACTIVATED, NULL);
	/* DEACTIVATE OTHERS */
	__set_deactivate_all_device_auto (GET_ACTIVE_CAPTURE(), GET_ACTIVE_PLAYBACK());
	if (type == DEVICE_EARJACK_TYPE_SPK_WITH_MIC) {
		MMSoundMgrDeviceUpdateStatus (DEVICE_UPDATE_STATUS_CHANGED_INFO_STATE, DEVICE_TYPE_BUILTIN_MIC, 0, DEVICE_ID_AUTO, NULL, DEVICE_STATE_DEACTIVATED, NULL);
	}

	dump_info ();
}

static void __handle_headset_off (void)
{
	/* FIXME : Need to be removed volume seperation on public */
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
	__set_route(true, true);

	/* DEACTIVATE AUDIOJACK */
	MMSoundMgrDeviceUpdateStatus (DEVICE_UPDATE_STATUS_CHANGED_INFO_STATE, DEVICE_TYPE_AUDIOJACK, 0, DEVICE_ID_AUTO, NULL, DEVICE_STATE_DEACTIVATED, NULL);

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
	__set_route(true, true);

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
	__set_route(true, true);

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

	/* ACTIVATE HDMI */
	MMSoundMgrDeviceUpdateStatus (DEVICE_UPDATE_STATUS_CHANGED_INFO_STATE, DEVICE_TYPE_HDMI, 0, DEVICE_ID_AUTO, NULL, DEVICE_STATE_ACTIVATED, NULL);
	/* DEACTIVATE OTHERS */
	__set_deactivate_all_device_auto (GET_ACTIVE_CAPTURE(), GET_ACTIVE_PLAYBACK());

	/* Do set path and notify result */
	__set_route(true, true);

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


	/* DEACTIVATE HDMI */
	MMSoundMgrDeviceUpdateStatus (DEVICE_UPDATE_STATUS_CHANGED_INFO_STATE, DEVICE_TYPE_HDMI, 0, DEVICE_ID_AUTO, NULL, DEVICE_STATE_DEACTIVATED, NULL);

	/* activate current available device based on priority */
	__select_playback_active_device();

	/* Do set path and notify result */
	__set_route(true, true);

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

	/* ACTIVATE MIRRORING */
	MMSoundMgrDeviceUpdateStatus (DEVICE_UPDATE_STATUS_CHANGED_INFO_STATE, DEVICE_TYPE_MIRRORING, 0, DEVICE_ID_AUTO, NULL, DEVICE_STATE_ACTIVATED, NULL);
	/* DEACTIVATE OTHERS */
	__set_deactivate_all_device_auto (GET_ACTIVE_CAPTURE(), GET_ACTIVE_PLAYBACK());

	/* Do set path and notify result */
	__set_route(true, true);

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

	/* DEACTIVATE MIRRORING */
	MMSoundMgrDeviceUpdateStatus (DEVICE_UPDATE_STATUS_CHANGED_INFO_STATE, DEVICE_TYPE_MIRRORING, 0, DEVICE_ID_AUTO, NULL, DEVICE_STATE_DEACTIVATED, NULL);

	/* activate current available device based on priority */
	__select_playback_active_device();

	/* Do set path and notify result */
	__set_route(true, true);

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

	/* ACTIVATE USB_AUDIO */
	MMSoundMgrDeviceUpdateStatus (DEVICE_UPDATE_STATUS_CHANGED_INFO_STATE, DEVICE_TYPE_USB_AUDIO, 0, DEVICE_ID_AUTO, NULL, DEVICE_STATE_ACTIVATED, NULL);
	/* DEACTIVATE OTHERS */
	__set_deactivate_all_device_auto (GET_ACTIVE_CAPTURE(), GET_ACTIVE_PLAYBACK());

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

	/* DEACTIVATE USB_AUDIO */
	MMSoundMgrDeviceUpdateStatus (DEVICE_UPDATE_STATUS_CHANGED_INFO_STATE, DEVICE_TYPE_USB_AUDIO, 0, DEVICE_ID_AUTO, NULL, DEVICE_STATE_DEACTIVATED, NULL);

	/* activate current available device based on priority */
	__select_playback_active_device();

	/* Do set path and notify result */
	__set_route(true, true);

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
	__set_route(true, true);

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
	__set_route(true, true);

	dump_info ();
}

static const char* __get_session_string(session_t session)
{
	switch(session) {
		case SESSION_MEDIA:
			return "MEDIA";
		case SESSION_VOICECALL:
			return "VOICECALL";
		case SESSION_VIDEOCALL:
			return "VIDEOCALL";
		case SESSION_VOIP:
			return "VOIP";
		case SESSION_FMRADIO:
			return "FMRADIO";
		case SESSION_NOTIFICATION:
			return "NOTOFICATION";
		case SESSION_ALARM:
			return "ALARM";
		case SESSION_EMERGENCY:
			return "EMERGENCY";
		case SESSION_VOICE_RECOGNITION:
			return "VOICE_RECOGNITION";
		default:
		return "unknow session";
	}
}

static const char* __get_subsession_string(subsession_t session)
{
	switch(session) {
		case SUBSESSION_VOICE:
			return "VOICECALL";
		case SUBSESSION_RINGTONE:
			return "RINGTONE";
		case SUBSESSION_MEDIA:
			return "MEDIA";
		case SUBSESSION_INIT:
			return "SUBSESSION_INIT";
		case SUBSESSION_VR_NORMAL:
			return "VR_NORMAL";
		case SUBSESSION_VR_DRIVE:
			return "VR_DRIVE";
		case SUBSESSION_RECORD_STEREO:
			return "RECORD_STEREO";
		case SUBSESSION_RECORD_MONO:
			return "RECORD_MONO";
		default:
			return "unknow subsession";
	}
}

#ifdef TIZEN_MICRO
static const char* __get_bt_bandwidth_string(int bandwidth)
{
    switch(bandwidth) {
        case MM_SOUND_BANDWIDTH_UNKNOWN:
            return "none. maybe bt is disconnected";
        case MM_SOUND_BANDWIDTH_NB:
            return "NB";
        case MM_SOUND_BANDWIDTH_WB:
            return "WB";
        default:
            return "unknow bandwidth";
    }
}

static const char* __get_hfp_connection_state_string(int bandwidth)
{
    switch(bandwidth) {
        case MM_SOUND_HFP_STATUS_UNKNOWN:
            return "unknown";
        case MM_SOUND_HFP_STATUS_INCOMMING_CALL:
            return "HFP_INCOMMING_CALL:RINGTONE";
        default:
            return "unknow";
    }
}
#endif

static const char* __get_device_type_string(int device)
{
    switch(device) {
        case DEVICE_BUILTIN:
            return "builtin";
        case DEVICE_WIRED:
            return "wired";
        case DEVICE_BT_A2DP:
            return "bt-a2dp";
        case DEVICE_BT_SCO:
            return "bt-sco";
        case DEVICE_DOCK:
            return "dock";
        case DEVICE_HDMI:
            return "hdmi";
        case DEVICE_MIRRORING:
            return "mirroring";
        case DEVICE_USB_AUDIO:
            return "usb-audio";
        case DEVICE_MULTIMEDIA_DOCK:
            return "multimedia-dock";
        default:
            return "unknow device";
    }
}

/* ------------------------- EXTERNAL FUNCTIONS ------------------------------------*/
#ifdef TIZEN_MICRO
int MMSoundMgrSessionMediaPause ()
{
	debug_msg ("[SESSION] Media pause requested...");
	_asm_pause_process (g_info.asm_handle);

	return MM_ERROR_NONE;
}

int MMSoundMgrSessionSetHFBandwidth (int bandwidth)
{
	debug_msg ("Set HF SCO bandwidth=(%s)", __get_bt_bandwidth_string(bandwidth));
	g_info.bt_info.hf_wb = bandwidth;
	return MM_ERROR_NONE;
}

int MMSoundMgrSessionSetHFPConnectionState (int stat)
{
	debug_msg ("Set HF SCO Stat=(%s)", __get_hfp_connection_state_string(stat));
	g_info.bt_info.hfp_conn_state = stat;
	return MM_ERROR_NONE;
}
#endif	/* TIZEN_MICRO */

int MMSoundMgrSessionEnableAgSCO (bool enable)
{
	debug_msg ("Set AG SCO enable=%d\n", enable);

	if (enable) {
		__handle_bt_sco_on();
	} else {
		__handle_bt_sco_off();
	}
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

	MMSOUND_STRNCPY(g_info.default_sink_name, default_sink_name, MAX_STRING_LEN);

	debug_msg ("[SESSION] default sink=[%s]\n", default_sink_name);

	/* ToDo: do something */

	UNLOCK_SESSION();

	return MM_ERROR_NONE;
}

/* DEVICE : Called by mgr_pulse for bt and mgr_headset for headset */
int MMSoundMgrSessionSetDeviceAvailable (device_type_t device, int available, int type, const char* name)
{
	LOCK_SESSION();

	debug_warning ("[SESSION]set device available. device(%s), available(%d), type(%d), name(%s)\n",
		__get_device_type_string(device), available, type, name != NULL ? name : "null");
	switch (device) {
	case DEVICE_WIRED:
		if (available) {

			if (!IS_AVAILABLE(MM_SOUND_DEVICE_OUT_WIRED_ACCESSORY)) {
				SET_AVAILABLE(MM_SOUND_DEVICE_OUT_WIRED_ACCESSORY);
				/* available device & send available callback */
				if (type == DEVICE_EARJACK_TYPE_SPK_WITH_MIC) {
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
				if (g_info.headset_type == DEVICE_EARJACK_TYPE_SPK_WITH_MIC) {
					UNSET_AVAILABLE(MM_SOUND_DEVICE_IN_WIRED_ACCESSORY);
				}
				/* Clear earphone type */
				g_info.headset_type = EARJACK_UNPLUGGED;

				/* unactivate device & send callback  */
				__handle_headset_off();

				/* Send unavailable callback */
				if (g_info.headset_type == DEVICE_EARJACK_TYPE_SPK_WITH_MIC) {
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
		if (name)
			MMSOUND_STRNCPY(g_info.bt_info.name, name, MAX_STRING_LEN);
		else
			memset(g_info.bt_info.name, 0, MAX_STRING_LEN);

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
				if (IS_AVAILABLE(MM_SOUND_DEVICE_OUT_USB_AUDIO)) {
					UNSET_AVAILABLE(MM_SOUND_DEVICE_OUT_USB_AUDIO);
				}
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
	bool need_cork = true;
	LOCK_SESSION();

	debug_warning ("[SESSION] playback=[0x%X] capture=[0x%X]\n", playback, capture);

	/* Check whether device is available */
	if ((playback && !IS_AVAILABLE(playback)) || (capture && !IS_AVAILABLE(capture))) {
		debug_warning ("Failed to set active state to unavailable device!!!\n");
		ret = MM_ERROR_INVALID_ARGUMENT;
		goto END_SET_DEVICE;
	}

	LOCK_PATH();
	/* Update active state */
	debug_log ("Update active device as request\n");
	if (playback) {
		int d_ret = MM_ERROR_NONE;
		device_type_e device;

		SET_PLAYBACK_ONLY_ACTIVE(playback);

		d_ret = __get_device_type_from_old_device(playback, &device);
		if(d_ret) {
			debug_warning ("Failed to __get_device_type_from_old_device\n");
		} else {
			/* ACTIVATE device */
			MMSoundMgrDeviceUpdateStatus (DEVICE_UPDATE_STATUS_CHANGED_INFO_STATE, device, 0, DEVICE_ID_AUTO, NULL, DEVICE_STATE_ACTIVATED, NULL);
		}
	}
	if (capture) {
		int d_ret = MM_ERROR_NONE;
		device_type_e device;
		SET_CAPTURE_ONLY_ACTIVE(capture);

		d_ret = __get_device_type_from_old_device(capture, &device);
		if(d_ret) {
			debug_warning ("Failed to __get_device_type_from_old_device\n");
		} else {
			/* ACTIVATE device */
			MMSoundMgrDeviceUpdateStatus (DEVICE_UPDATE_STATUS_CHANGED_INFO_STATE, device, 0, DEVICE_ID_AUTO, NULL, DEVICE_STATE_ACTIVATED, NULL);
		}
	}
	/* DEACTIVATE OTHERS */
	__set_deactivate_all_device_auto (GET_ACTIVE_CAPTURE(), GET_ACTIVE_PLAYBACK());

	if (((g_info.session == SESSION_VOICECALL) || (g_info.session == SESSION_VIDEOCALL))) {
		bool is_wb, is_nrec;
#ifndef TIZEN_MICRO
		bool is_noise_reduction, is_extra_volume, is_upscaling_needed;
		is_noise_reduction = __is_noise_reduction_on();
		is_extra_volume = __is_extra_volume_on();
		is_upscaling_needed = __is_upscaling_needed();
		if ((g_info.is_noise_reduction != is_noise_reduction)
			|| (g_info.is_extra_volume != is_extra_volume)
			|| (g_info.is_upscaling_needed != is_upscaling_needed)) {
			need_update = true;
		}
		g_info.is_noise_reduction = is_noise_reduction;
		g_info.is_extra_volume = is_extra_volume;
		g_info.is_upscaling_needed = is_upscaling_needed;
#endif /* TIZEN_MICRO */
#ifdef SUPPORT_BT_SCO
		/* FIXME : Check all BT SCO */
		if (playback == MM_SOUND_DEVICE_OUT_BT_SCO && capture == MM_SOUND_DEVICE_IN_BT_SCO) {
			is_wb = MMSoundMgrPulseBTSCOWBStatus();
			is_nrec = MMSoundMgrPulseBTSCONRECStatus();
			if ((g_info.bt_info.is_nrec != is_nrec) || (g_info.bt_info.is_wb != is_wb)) {
				g_info.bt_info.is_wb = is_wb;
				g_info.bt_info.is_nrec = is_nrec;
				need_update = true;
			}
		}
#endif	/* SUPPORT_BT_SCO */
		/* NOT need to cork during voice call */
		need_cork = false;
	}
	UNLOCK_PATH();

	/* If there's changes do path set and inform callback */
	if (old_active != g_info.device_active || need_update == true) {
		debug_msg ("Changes happens....set path based on current active device and inform callback(%d)!!!\n", need_broadcast);

		/* Do set path based on current active state */
		__set_route(need_broadcast, need_cork);
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
	ret = __set_route(true, true);
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

int MMSoundMgrSessionGetAudioPath (mm_sound_device_out *playback, mm_sound_device_in *capture)
{
	if (playback == NULL || capture == NULL) {
		debug_error ("Invalid input parameter\n");
		return MM_ERROR_INVALID_ARGUMENT;
	}

	LOCK_SESSION();
	MMSoundMgrPulseGetPathInfo(playback, capture);
	debug_msg ("[SESSION] return current audio path which is set to ALSA . playback=[0x%X]/capture=[0x%X]\n", *playback,*capture);

	UNLOCK_SESSION();
	return MM_ERROR_NONE;
}

/* SUBSESSION */
int MMSoundMgrSessionSetSession(session_t session, session_state_t state)
{
	LOCK_SESSION();

	debug_warning ("[SESSION] session=[%s] state=[%s]\n", __get_session_string(session), state==SESSION_START ? "start" : "end");

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
		debug_warning ("session [%s] is not handled...\n", __get_session_string(session));
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

	/* LOCK_SESSION(); */
	*session = g_info.session;
	/* UNLOCK_SESSION(); */

	return MM_ERROR_NONE;
}

const char* MMSoundMgrSessionGetSessionString(session_t session)
{
    return __get_session_string(session);
}
#ifdef TIZEN_MICRO
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

	ret = __set_route(false, false);
	if(ret != MM_ERROR_NONE)
		debug_warning("Fail to set route");

	UNLOCK_SESSION();
	return MM_ERROR_NONE;
}
#endif

/* SUBSESSION */
int MMSoundMgrSessionSetSubSession(subsession_t subsession, int subsession_opt)
{
	bool need_update = false;
	bool need_cork = false;
	static subsession_t prev_subsession = SUBSESSION_VOICE; /* Initialize subsession */
	bool set_route_flag = true;
	int sub_session_opt = 0;
	LOCK_SESSION();

	if (g_info.subsession == subsession) {
		debug_warning ("[SESSION] already subsession is [%s]. skip this!!\n", __get_subsession_string(subsession));
	} else {
		MMSoundMgrPulseSetSubsession(subsession, subsession_opt);
		g_info.subsession = subsession;
		need_update = true;
	}

	/* FIXME : Need to check with private */
	/* FIXME : Need to check with private */
	/* FIXME : Need to check with private */	
	switch(subsession) {
#ifdef TIZEN_MICRO
        	// FIXME: we got timing issue between music and player and sound_server
		case SUBSESSION_VOICE:
		case SUBSESSION_RINGTONE:
			debug_msg("session(%s), subsession(%s). sound path will change to spk/mic",
				__get_session_string(g_info.session), __get_subsession_string(subsession));
			SET_PLAYBACK_ONLY_ACTIVE(MM_SOUND_DEVICE_OUT_SPEAKER);
			SET_CAPTURE_ONLY_ACTIVE(MM_SOUND_DEVICE_IN_MIC);
			need_update = true;
			/* suspend ALSA devices when starting call */
			if (subsession != SUBSESSION_RINGTONE) {
				need_cork |= true;
			}
			dump_info();
			break;
#endif	/* TIZEN_MICRO */
		case SUBSESSION_VR_NORMAL:
		case SUBSESSION_VR_DRIVE:
			if(g_info.option != subsession_opt) {
				g_info.option = subsession_opt;
				need_update = true;
			}
			break;
		default:
			if (g_info.option != subsession_opt) {
				g_info.option = MM_SUBSESSION_OPTION_NONE;
				need_update = true;
			}
			break;
	}	if (g_info.subsession == SUBSESSION_VOICE) {
		if (IS_ACTIVE(MM_SOUND_DEVICE_OUT_BT_SCO) && IS_ACTIVE(MM_SOUND_DEVICE_IN_BT_SCO)) {
			/* Update BT info */
			MMSoundMgrPulseUpdateBluetoothAGCodec();
		}
	}

	if (g_info.subsession == SUBSESSION_VOICE) {
		if (IS_ACTIVE(MM_SOUND_DEVICE_OUT_BT_SCO) && IS_ACTIVE(MM_SOUND_DEVICE_IN_BT_SCO)) {
			/* Update BT info */
			MMSoundMgrPulseUpdateBluetoothAGCodec();
		}
	}
	
	if (need_update) {
		debug_warning ("[SESSION] subsession=[%s], resource=[%d]\n", __get_subsession_string(g_info.subsession), g_info.option);
		__set_route(true, need_cork);
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

const char* MMSoundMgrSessionGetSubSessionString(subsession_t subsession)
{
    return __get_subsession_string(subsession);
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
	/* FIXME : Check whether need to set state to pulse, need to check with private */
	/* MMSoundMgrPulseSetVoicecontrolState(enable); */

	debug_warning("MMSoundMgrSessionSetVoiceControlState --------g_info.session = %s,g_info.subsession = %s ",
		__get_session_string(g_info.session), __get_subsession_string(g_info.subsession));
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
static bool __get_bt_nrec_status(void)
{
	debug_msg ("get bt nrec status.(%s)\n", g_info.bt_info.is_nrec==true ? "ON" : "OFF");
	return g_info.bt_info.is_nrec;
}

#ifdef TIZEN_MICRO
static int __get_ag_wb_status(void)
{
	debug_msg ("get ag wb status.(%s)\n", __get_bt_bandwidth_string(g_info.bt_info.ag_wb));
	return g_info.bt_info.ag_wb;
}

static int __get_hf_wb_status(void)
{
	debug_msg ("get hf wb status.(%s)\n", __get_bt_bandwidth_string(g_info.bt_info.hf_wb));
	return g_info.bt_info.hf_wb;
}

static int __get_hf_connection_state(void)
{
    debug_msg ("Set HF SCO connection state(%s)", __get_hfp_connection_state_string(g_info.bt_info.hfp_conn_state));
    return g_info.bt_info.hfp_conn_state;
}

/* ------------------------------- RIGHT HAND ------------------------------------------*/
static bool __is_right_hand_on (void)
{
	int is_right_hand = 0;
	if (vconf_get_bool(VCONF_KEY_VR_LEFTHAND_ENABLED, &is_right_hand)) {
		debug_warning("vconf_get_bool for %s failed\n", VCONF_KEY_VR_LEFTHAND_ENABLED);
	}
	debug_msg("is_right_hand : %d",is_right_hand);
	return (is_right_hand == 0) ? true : false;
}
#endif /* TIZEN_MICRO */
#endif /* _TIZEN_PUBLIC_ */
#ifdef TIZEN_MICRO
static void __media_pause_by_call (void)
{
	int asm_error = 0;
	int asm_status = 0;
	int handle = -1;

	debug_msg ("[SESSION] Media pause requested by call dbus ...");
	MMSOUND_ENTER_CRITICAL_SECTION( &_asm_mutex )

	if (vconf_get_int(SOUND_STATUS_KEY, &asm_status)) {
		debug_error(" vconf_set_int fail\n");
	}

	if ((asm_status & ~(ASM_STATUS_CALL|ASM_STATUS_NOTIFY|ASM_STATUS_ALARM|ASM_STATUS_EXCLUSIVE_RESOURCE))> 0) {
		/* Do Pause for call dbus event */
		debug_warning("Change state to pause by call dbus");

		if (!ASM_register_sound_ex (-1, &handle, ASM_EVENT_CALL, ASM_STATE_NONE, NULL, NULL, ASM_RESOURCE_NONE, &asm_error, __asm_process_message)) {
			debug_warning("Call event register failed with 0x%x", asm_error);
			MMSOUND_LEAVE_CRITICAL_SECTION( &_asm_mutex )
			return;
		}

		if (!ASM_set_sound_state_ex(handle, ASM_EVENT_CALL, ASM_STATE_PLAYING, ASM_RESOURCE_NONE, &asm_error, __asm_process_message)) {
			debug_error("Call event set sound state to playing failed with 0x%x\n", asm_error);
		}

		if (!ASM_set_sound_state_ex(handle, ASM_EVENT_CALL, ASM_STATE_STOP, ASM_RESOURCE_NONE, &asm_error, __asm_process_message)) {
			debug_error("Call event set sound state to stop failed with 0x%x", asm_error);
		}

		if (!ASM_unregister_sound_ex(handle, ASM_EVENT_CALL, &asm_error, __asm_process_message)) {
			debug_error("Call event unregister failed with 0x%x\n", asm_error);
			MMSOUND_LEAVE_CRITICAL_SECTION( &_asm_mutex )
			return;
		}

	} else {
		debug_msg("no need to pause others");
	}

	MMSOUND_LEAVE_CRITICAL_SECTION( &_asm_mutex )
	/* End of change of session state */

	return;
}
static void __call_status_changed(GDBusConnection *conn,
							   const gchar *sender_name,
							   const gchar *object_path,
							   const gchar *interface_name,
							   const gchar *signal_name,
							   GVariant *parameters,
							   gpointer user_data)
{
	int value=0;
	const GVariantType* value_type;

	debug_msg ("sender : %s, object : %s, interface : %s, signal : %s",
			sender_name, object_path, interface_name, signal_name);
	if(g_variant_is_of_type(parameters, G_VARIANT_TYPE("(i)")))
	{
		g_variant_get(parameters, "(i)",&value);
		debug_msg("singal[%s] = %X", signal_name, value);
		if(value==MM_SOUND_CALL_STATUS_PAUSE)
		{
			__media_pause_by_call();
		}
	}
	else
	{
		value_type = g_variant_get_type(parameters);
		debug_warning("signal type is %s", value_type);
	}

}

void _deinit_call_status_dbus(void)
{
	debug_fenter ();
	g_dbus_connection_signal_unsubscribe(conn_callstatus, sig_id_callstatus);
	g_object_unref(conn_callstatus);
	debug_fleave ();
}

int _init_call_status_dbus(void)
{
	GError *err = NULL;
	debug_fenter ();

	g_type_init();

	conn_callstatus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &err);
	if (!conn_callstatus && err) {
		debug_error ("g_bus_get_sync() error (%s) ", err->message);
		g_error_free (err);
		goto error;
	}

	sig_id_callstatus = g_dbus_connection_signal_subscribe(conn_callstatus,
			NULL, DBUS_CALL_STATUS_INTERFACE, DBUS_CALL_STATUS_CHANGED_SIGNAL, DBUS_CALL_STATUS_PATH, NULL, 0,
			__call_status_changed, NULL, NULL);
	if (sig_id_callstatus < 0) {
		debug_error ("g_dbus_connection_signal_subscribe() error (%d)", sig_id_callstatus);
		goto sig_error;
	}

	debug_fleave ();
	return 0;

sig_error:
	g_dbus_connection_signal_unsubscribe(conn_callstatus, sig_id_callstatus);
	g_object_unref(conn_callstatus);

error:
	return -1;

}
#endif /* TIZEN_MICRO */
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
#ifdef TIZEN_MICRO
	if (_init_call_status_dbus() != 0) {
		debug_error ("Registering call status signal handler failed\n");
	}
#endif

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

