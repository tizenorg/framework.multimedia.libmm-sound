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

void* MMSoundMgrPulseInit(void);
int MMSoundMgrPulseFini(void* handle);

void MMSoundMgrPulseSetDefaultSink (char* device_api_name, char* device_bus_name);

int MMSoundMgrPulseHandleRegisterMonoAudio (void* pinfo);
int MMSoundMgrPulseHandleRegisterBluetoothStatus (void* pinfo);

int MMSoundMgrPulseHandleIsBtA2DPOnReq (mm_ipc_msg_t *msg, int (*sendfunc)(mm_ipc_msg_t*));
void MMSoundMgrPulseGetInitialBTStatus (bool *a2dp, bool *sco);

#endif /* __MM_SOUND_MGR_PULSE_H__ */

