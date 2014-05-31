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

typedef enum
{
	SESSION_MEDIA = 0,
	SESSION_VOICECALL,
	SESSION_VOIP,
	SESSION_FMRADIO,
	SESSION_NOTIFICATION,
	SESSION_EMERGENCY,
	SESSION_NUM
} session_t;

typedef enum
{
	SUBSESSION_VOICE = 0,
	SUBSESSION_RINGTONE,
	SUBSESSION_MEDIA,
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
	DEVICE_WFD,
	DEVICE_USB_AUDIO,
} device_type_t;

int MMSoundMgrSessionInit(void);
int MMSoundMgrSessionFini(void);

/* called by mgr headset, pulse */
int MMSoundMgrSessionSetDeviceAvailable (device_type_t device, int available, int type, char* name);


int MMSoundMgrSessionIsDeviceAvailable (mm_sound_device_out playback, mm_sound_device_in capture, bool *available);
int MMSoundMgrSessionIsDeviceAvailableNoLock (mm_sound_device_out playback, mm_sound_device_in capture, bool *available);

int MMSoundMgrSessionGetAvailableDevices (int *playback, int *capture);

int MMSoundMgrSessionSetDeviceActive (mm_sound_device_out playback, mm_sound_device_in capture);
int MMSoundMgrSessionGetDeviceActive (mm_sound_device_out *playback, mm_sound_device_in *capture);

int MMSoundMgrSessionSetSession(session_t session, session_state_t state);	/* called by mgr_asm */
int MMSoundMgrSessionGetSession(session_t *session);

int MMSoundMgrSessionSetSubSession(subsession_t subsession); /* called by mgr_asm */
int MMSoundMgrSessionGetSubSession(subsession_t *subsession);

char* MMSoundMgrSessionGetBtA2DPName ();

int MMSoundMgrSessionSetDefaultSink (char *default_sink_name);

int MMSoundMgrSessionSCOChanged (bool connected);

#endif /* __MM_SOUND_MGR_SESSION_H__ */

