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

#include "include/mm_sound_mgr_common.h"
#include "include/mm_sound_thread_pool.h"
#include "../include/mm_sound_common.h"

#include <mm_error.h>
#include <mm_debug.h>

//#include <audio-session-manager.h>
#include <avsys-audio.h>

#include "include/mm_sound_mgr_headset.h"
//#include "include/mm_sound_mgr_asm.h"
#include "include/mm_sound_mgr_session.h"

/******************************* Headset Code **********************************/

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <errno.h>
#include <assert.h>

#include <mm_error.h>
#include <mm_debug.h>
#include <pthread.h>
#include <avsys-audio.h>

#include "mm_ipc.h"
#include "mm_sound_common.h"
#include "mm_sound.h"

#include <string.h>

#define EARJACK_EJECTED	0
#define NO_FORCE_RESET  0

void __headset_main_run (void* param)
{
	int current_type = 0;
	int new_type = 0;
	int waitfd = 0;
	int err = AVSYS_STATE_SUCCESS;
	int eject_event_count = 0;
	int need_mute = 0;
	int ret = 0;

	/* re-check current path */
	avsys_audio_path_ex_reset(NO_FORCE_RESET);

	if (AVSYS_FAIL(avsys_audio_earjack_manager_init(&current_type, &waitfd)))
		return;

	while(1) {
		//waiting earjack event
		err = avsys_audio_earjack_manager_wait(waitfd, &current_type, &new_type, &need_mute);
		debug_log ("wait result  = %x, current_type= %d, new_type = %d, need_mute = %d\n", err, current_type, new_type, need_mute);
		if (err & AVSYS_STATE_ERROR) {
#if !defined(_MMFW_I386_ALL_SIMULATOR)
			if (err != AVSYS_STATE_ERR_NULL_POINTER) {
				if (AVSYS_FAIL(avsys_audio_earjack_manager_unlock())) {
					debug_error("avsys_audio_earjack_manager_unlock() failed in %s\n",__func__);
					err = MM_ERROR_SOUND_INTERNAL;
					goto fail;
				}
			}
#endif
			break;
		} else if ((err & AVSYS_STATE_WARING)) {
			if (err != AVSYS_STATE_WAR_INVALID_VALUE) {
				if (AVSYS_FAIL(avsys_audio_earjack_manager_unlock())) {
					debug_error("avsys_audio_earjack_manager_unlock() failed in %s\n",__func__);
					err = MM_ERROR_SOUND_INTERNAL;
					goto fail;
				}
			}
			continue; /* Ignore current changes and do wait again */
		}
		debug_warning("Current type is %d, New type is %d\n", current_type, new_type);

		if (current_type == new_type) {
			if (AVSYS_FAIL(avsys_audio_earjack_manager_unlock())) {
				debug_error("avsys_audio_earjack_manager_unlock() failed in %s\n",__func__);
				err = MM_ERROR_SOUND_INTERNAL;
				goto fail;
			}
			continue; /* Ignore current changes and do wait again */
		} else {
			current_type = new_type;
		}
		debug_warning("Current type is %d\n", current_type);

		/* mute if needed, unmute will be done end of this loop */
		if (need_mute) {
			if (AVSYS_FAIL(avsys_audio_set_global_mute(AVSYS_AUDIO_MUTE_NOLOCK)))
				debug_error("Set mute failed\n");
		}

		//current_type '0' means earjack ejected
		if (current_type == EARJACK_EJECTED) {
			eject_event_count++;
			if (eject_event_count == 1) {
				debug_msg ("earjack [EJECTED]\n");

				/* ToDo: Device Update */
				MMSoundMgrSessionSetDeviceAvailable (DEVICE_WIRED, NOT_AVAILABLE, 0, NULL);

			}
		} else if (current_type != EARJACK_EJECTED) { /* INSERT */
			debug_msg ("earjack is [INSERTED]\n");
			eject_event_count = 0;

			/* ToDo: Device Update */
			ret = MMSoundMgrSessionSetDeviceAvailable (DEVICE_WIRED, AVAILABLE, current_type, NULL);
			if (ret != MM_ERROR_NONE) {
				/* TODO : Error Handling */
				debug_error ("MMSoundMgrSessionSetDeviceAvailable failed....ret = [%x]\n", ret);
			}
		}

		//process change
		err = avsys_audio_earjack_manager_process(current_type);
		if (err & AVSYS_STATE_ERROR) {
			debug_error("Earjack Managing Fatal Error 0x%x\n", err);
			if (need_mute) {
				if (AVSYS_FAIL(avsys_audio_set_global_mute(AVSYS_AUDIO_UNMUTE_NOLOCK))) {
					debug_error("Set unmute failed\n");
				}
			}
#if !defined(_MMFW_I386_ALL_SIMULATOR)
			if (AVSYS_FAIL(avsys_audio_earjack_manager_unlock())) {
				debug_error("avsys_audio_earjack_manager_unlock() failed in %s\n",__func__);
				err = MM_ERROR_SOUND_INTERNAL;
				goto fail;
			}
#endif
			break;
		} else if (err & AVSYS_STATE_WARING) {
			debug_error("Earjack Managing Warning 0x%x\n", err);
			if (need_mute) {
				if (AVSYS_FAIL(avsys_audio_set_global_mute(AVSYS_AUDIO_UNMUTE_NOLOCK))) {
					debug_error("Set unmute failed\n");
				}
			}

			if (AVSYS_FAIL(avsys_audio_earjack_manager_unlock())) {
				debug_error("avsys_audio_earjack_manager_unlock() failed in %s\n",__func__);
				err = MM_ERROR_SOUND_INTERNAL;
				goto fail;
			}
			continue;
		}

		/* Unmute if muted */
		if (need_mute) {
			if (AVSYS_FAIL(avsys_audio_set_global_mute(AVSYS_AUDIO_UNMUTE_NOLOCK))) {
				debug_error("Set unmute failed\n");
			}
		}

		if (AVSYS_FAIL(avsys_audio_earjack_manager_unlock())) {
			debug_error("avsys_audio_earjack_manager_unlock() failed in %s\n",__func__);
			err = MM_ERROR_SOUND_INTERNAL;
			goto fail;
		}
	} /* while (1) */

	if (AVSYS_FAIL(avsys_audio_earjack_manager_deinit(waitfd))) {
		err = MM_ERROR_SOUND_INTERNAL;
		goto fail;
	}

	return;

fail:
	debug_error("earjack manager exit with 0x%x\n", err);
}

int MMSoundMgrHeadsetGetType (int *type)
{
	if (type) {
		*type = avsys_audio_earjack_manager_get_type();
	}
	return MM_ERROR_NONE;
}


int MMSoundMgrHeadsetInit(void)
{
	debug_enter("\n");

	MMSoundThreadPoolRun(NULL, __headset_main_run);

	debug_leave("\n");
	return MM_ERROR_NONE;
}

int MMSoundMgrHeadsetFini(void)
{
	debug_enter("\n");

	debug_leave("\n");
	return MM_ERROR_NONE;
}

