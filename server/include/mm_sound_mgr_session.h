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

#ifndef __MM_SOUND_MGR_SESSION_H__
#define __MM_SOUND_MGR_SESSION_H__

//#include <pulse/pulseaudio.h>
#include "../../include/mm_ipc.h"
#include "include/mm_sound.h"
//#include <vconf.h>

typedef enum
{
	SESSION_END = 0,
	SESSION_START,
} session_state_t;

typedef enum
{
	NOT_AVAILABLE = 0,
	AVAILABLE,
} device_status_t;

/* MMSoundMgrPulseSetSession & pa_tizen_session_t should be updated for PA */
typedef enum
{
	SESSION_MEDIA = 0,
	SESSION_VOICECALL,
	SESSION_VIDEOCALL,
	SESSION_VOIP,
	SESSION_FMRADIO,
	SESSION_NOTIFICATION,
	SESSION_ALARM,
	SESSION_EMERGENCY,
	SESSION_VOICE_RECOGNITION,
	SESSION_NUM
} session_t;

/* MMSoundMgrPulseSetSubsession & pa_tizen_subsession_t should be updated for PA */
typedef enum
{
	SUBSESSION_VOICE = 0,
	SUBSESSION_RINGTONE,
	SUBSESSION_MEDIA,
	SUBSESSION_INIT,
	SUBSESSION_VR_NORMAL,
	SUBSESSION_VR_DRIVE,
	SUBSESSION_RECORD_STEREO,
	SUBSESSION_RECORD_MONO,
	SUBSESSION_NUM
} subsession_t;

typedef enum
{
	DEVICE_BUILTIN = 0,
	DEVICE_WIRED,
	DEVICE_BT_A2DP,
	DEVICE_BT_SCO,
	DEVICE_DOCK,
	DEVICE_HDMI,
	DEVICE_MIRRORING,
	DEVICE_USB_AUDIO,
	DEVICE_MULTIMEDIA_DOCK,
} device_type_t;

enum mm_sound_audio_mute_t {
	MM_SOUND_AUDIO_UNMUTE = 0,				/**< Unmute state */
	MM_SOUND_AUDIO_MUTE,					/**< Mute state */
};

#ifdef TIZEN_MICRO
enum mm_sound_bandwidth {
	MM_SOUND_BANDWIDTH_UNKNOWN = 0,
	MM_SOUND_BANDWIDTH_NB = 1,
	MM_SOUND_BANDWIDTH_WB = 2,
};

enum mm_sound_hfp_connection_state {
	MM_SOUND_HFP_STATUS_UNKNOWN = 0,
	MM_SOUND_HFP_STATUS_INCOMMING_CALL = 1, /* ringtone */
};
#endif

int MMSoundMgrSessionInit(void);
int MMSoundMgrSessionFini(void);

/* called by mgr headset, pulse */
int MMSoundMgrSessionSetDeviceAvailable (device_type_t device, int available, int type, const char* name);


int MMSoundMgrSessionIsDeviceAvailable (mm_sound_device_out playback, mm_sound_device_in capture, bool *available);
int MMSoundMgrSessionIsDeviceAvailableNoLock (mm_sound_device_out playback, mm_sound_device_in capture, bool *available);

int MMSoundMgrSessionGetAvailableDevices (int *playback, int *capture);

int MMSoundMgrSessionSetDeviceActive (mm_sound_device_out playback, mm_sound_device_in capture, bool need_broadcast);
int MMSoundMgrSessionGetDeviceActive (mm_sound_device_out *playback, mm_sound_device_in *capture);

int MMSoundMgrSessionSetSession(session_t session, session_state_t state);	/* called by mgr_asm */
int MMSoundMgrSessionGetSession(session_t *session);

int MMSoundMgrSessionSetSubSession(subsession_t subsession, int subsession_opt); /* called by mgr_asm */
int MMSoundMgrSessionGetSubSession(subsession_t *subsession);

char* MMSoundMgrSessionGetBtA2DPName ();

int MMSoundMgrSessionSetDefaultSink (const char * const default_sink_name);

int MMSoundMgrSessionSCOChanged (bool connected);

void MMSoundMgrSessionSetVoiceControlState (bool enable);
bool MMSoundMgrSessionGetVoiceControlState ();
int MMSoundMgrSessionSetSCO (bool is_sco_on, bool is_bt_nrec, bool is_bt_wb);
int MMSoundMgrSessionSetDeviceActiveAuto (void);
int  MMSoundMgrSessionSetSoundPathForActiveDevice (mm_sound_device_out playback, mm_sound_device_in capture);

#ifdef TIZEN_MICRO
int MMSoundMgrSessionEnableAgSCO (bool enable);
int MMSoundMgrSessionSetHFBandwidth (int bandwidth);
int MMSoundMgrSessionSetHFState (int stat);
int MMSoundMgrSessionMediaPause();
int MMSoundMgrSessionSetDuplicateSubSession(void);
const char* MMSoundMgrSessionGetSessionString(session_t session);
#endif
#endif /* __MM_SOUND_MGR_SESSION_H__ */

