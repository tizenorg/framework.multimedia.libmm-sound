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

#ifndef __MM_SOUND_MGR_HEADSET_H__
#define __MM_SOUND_MGR_HEADSET_H__

#include "../../include/mm_ipc.h"

int MMSoundMgrHeadsetInit(void);
int MMSoundMgrHeadsetFini(void);
int MMSoundMgrHeadsetGetType (int *type);


#endif /* __MM_SOUND_MGR_HEADSET_H__ */

