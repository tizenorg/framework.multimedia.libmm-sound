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

#ifndef __MM_SOUND_MGR_PULSE_H__
#define __MM_SOUND_MGR_PULSE_H__


#include "../../include/mm_ipc.h"
#include "mm_sound_mgr_session.h"

void* MMSoundMgrPulseInit(void);
int MMSoundMgrPulseFini(void* handle);

void MMSoundMgrPulseSetUSBDefaultSink (int usb_device);
void MMSoundMgrPulseSetDefaultSink (char* device_api_name, char* device_bus_name);
void MMSoundMgrPulseSetDefaultSinkByName (char* name);
void MMSoundMgrPulseSetSourcemutebyname (char* sourcename, int mute);

int MMSoundMgrPulseHandleRegisterMonoAudio (void* pinfo);
#ifndef TIZEN_MICRO
void MMSoundMgrPulseHandleAECLoadModule();
void MMSoundMgrPulseHandleAECUnLoadModule();
void MMSoundMgrPulseHandleAecSetDevice(mm_sound_device_in device_in, mm_sound_device_out device_out);
int MMSoundMgrPulseHandleRegisterAecSetVolume(void);
#else
void MMSoundMgrPulseHandleAecSetVolume(volume_type_t volume_type);
#endif
int MMSoundMgrPulseHandleRegisterAudioBalance (void* pinfo);
int MMSoundMgrPulseHandleRegisterBluetoothStatus (void* pinfo);
int MMSoundMgrPulseHandleRegisterSurroundSoundStatus(void *pinfo);

int MMSoundMgrPulseHandleIsBtA2DPOnReq (mm_ipc_msg_t *msg, int (*sendfunc)(mm_ipc_msg_t*));
void MMSoundMgrPulseGetInitialBTStatus (bool *a2dp, bool *sco);

void MMSoundMgrPulseSetSession(session_t session, session_state_t state);
void MMSoundMgrPulseSetSubsession(subsession_t subsession, int subsession_opt);
void MMSoundMgrPulseSetActiveDevice(mm_sound_device_in device_in, mm_sound_device_out device_out);
void MMSoundMgrPulseSetVolumeLevel(volume_type_t volume_type, unsigned int volume_level);
void MMSoundMgrPulseUpdateVolume(void);

void MMSoundMgrPulseSetCorkAll (bool cork);
void MMSoundMgrPulseUnLoadHDMI();

#endif /* __MM_SOUND_MGR_PULSE_H__ */
