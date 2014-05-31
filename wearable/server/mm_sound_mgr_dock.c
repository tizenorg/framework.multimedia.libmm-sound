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
#include "../include/mm_sound_common.h"

#include <mm_error.h>
#include <mm_debug.h>

#include "include/mm_sound_mgr_dock.h"
#include "include/mm_sound_mgr_session.h"

/******************************* Dock Code **********************************/

#include <sys/types.h>
#include <stdbool.h>
#include <errno.h>
#include <assert.h>

#include "mm_ipc.h"
#include "mm_sound_common.h"
#include "mm_sound.h"

#include <vconf.h>
#include <vconf-keys.h>

#define SOUND_DOCK_ON	"/usr/share/sounds/sound-server/dock.ogg"
#define SOUND_DOCK_OFF	"/usr/share/sounds/sound-server/undock.ogg"
#define SOUND_DOCK_ON_DELAY 1000000
#define SOUND_DOCK_OFF_DELAY 10000

#define DOCK_OFF false
#define DOCK_ON true

#define DOCK_NO_RESTORE false
#define DOCK_RESTORE true

int g_saved_dock_status;

static void __dock_sound_finished_cb (void *data, int id)
{
	bool* is_finish = (bool*)data;
	debug_log ("dock sound play id[%d] finished!!!", id);

	if (is_finish) {
		*is_finish = true;
	}
}
static void __play_dock_sound_sync(bool is_on, bool need_restore)
{
	int handle;
	bool is_play_finished = false;
	int dock_sound = false;

	if (g_saved_dock_status == -1) {
		debug_log ("skip dock sound because status is not valid [%d]\n", g_saved_dock_status);
		return;
	}

	/* check dock sound */
	vconf_get_bool(VCONFKEY_SETAPPL_ACCESSORY_DOCK_SOUND, &dock_sound);
	if (dock_sound == 0) {
		debug_log ("dock sound is disabled in setting app");
		return;
	}

	debug_log ("start to play dock sound : is_on=[%d], need_restore=[%d]\n", is_on, need_restore);

	if (need_restore) {
		mm_sound_play_loud_solo_sound((is_on? SOUND_DOCK_ON:SOUND_DOCK_OFF), VOLUME_TYPE_FIXED, __dock_sound_finished_cb, &is_play_finished, &handle);
		debug_log ("waiting for dock sound finish\n");
		while (!is_play_finished) {
			usleep (SOUND_DOCK_OFF_DELAY);
		}
	} else {
		mm_sound_play_loud_solo_sound_no_restore((is_on? SOUND_DOCK_ON:SOUND_DOCK_OFF), VOLUME_TYPE_FIXED, __dock_sound_finished_cb, &is_play_finished, &handle);
		debug_log ("waiting for dock sound finish\n");
		usleep (SOUND_DOCK_ON_DELAY);
	}

	debug_log ("dock sound finished!!!\n");
}

/* DOCK status value from system server */
static void _dock_status_changed_cb(keynode_t* node, void* data)
{
	int ret = 0;
	int dock_status = 0;

	if (node == NULL) {
		debug_error ("node is null...\n");
		return;
	}

	debug_msg ("Handling [%s] Starts\n", vconf_keynode_get_name(node));

	/* Get actual vconf value */
	vconf_get_int(VCONFKEY_SYSMAN_CRADLE_STATUS, &dock_status);
	debug_msg ("DOCK : [%s]=[%d]\n", VCONFKEY_SYSMAN_CRADLE_STATUS, dock_status);

	if (g_saved_dock_status == dock_status) {
		debug_warning ("No changes in DOCK STATUS, do nothing....\n");
		return;
	}

	/* Set available/non-available based on current dock status value */
	switch (dock_status)
	{
	case DOCK_DESKDOCK:
	case DOCK_CARDOCK:
		/* Play ON sound, no path restore due to dock routing  */
		__play_dock_sound_sync (DOCK_ON, DOCK_NO_RESTORE);

		/* Update device available status (available) */
		ret = MMSoundMgrSessionSetDeviceAvailable (DEVICE_DOCK, DOCK_ON, 0, NULL);
		if (ret != MM_ERROR_NONE) {
			debug_error ("MMSoundMgrSessionSetDeviceAvailable() failed...ret=%x\n", ret);
		}
		break;

	case DOCK_AUDIODOCK:
	case DOCK_SMARTDOCK:
		/* Play ON sound, no path restore due to USB Audio routing */
		__play_dock_sound_sync (DOCK_ON, DOCK_NO_RESTORE);
		break;

	case DOCK_NONE:
		/* Update device available status (non-available) */
		ret = MMSoundMgrSessionSetDeviceAvailable (DEVICE_DOCK, DOCK_OFF, 0, NULL);
		if (ret != MM_ERROR_NONE) {
			debug_error ("MMSoundMgrSessionSetDeviceAvailable() failed...ret=%x\n", ret);
		}

		/* Play OFF sound  */
		__play_dock_sound_sync (DOCK_OFF, DOCK_RESTORE);

		break;

	default:
		debug_warning ("Unexpected Dock Status = [%d]\n", dock_status);
		break;
	}

	g_saved_dock_status = dock_status;
	debug_msg ("Handling [%s] Ends normally\n", vconf_keynode_get_name(node));
}

static int _register_dock_status ()
{
	/* set callback for vconf key change */
	int ret = vconf_notify_key_changed(VCONFKEY_SYSMAN_CRADLE_STATUS, _dock_status_changed_cb, NULL);
	debug_msg ("vconf [%s] set ret = [%d]\n", VCONFKEY_SYSMAN_CRADLE_STATUS, ret);
	return ret;
}

int MMSoundMgrDockInit(void)
{
	int ret = 0;
	debug_enter("\n");

	g_saved_dock_status = -1;

	ret = vconf_get_int(VCONFKEY_SYSMAN_CRADLE_STATUS, &g_saved_dock_status);
	debug_warning ("Initial status is [%d], ret=[%d]\n", g_saved_dock_status, ret);

	if (_register_dock_status () != 0) {
		debug_error ("Registering dock status failed\n");
		return MM_ERROR_SOUND_INTERNAL;
	}

	debug_leave("\n");
	return MM_ERROR_NONE;
}

int MMSoundMgrDockFini(void)
{
	debug_enter("\n");

	debug_leave("\n");
	return MM_ERROR_NONE;
}

