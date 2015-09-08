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

#include "include/mm_sound_mgr_device.h"
#include "include/mm_sound_mgr_device_headset.h"
#include "include/mm_sound_mgr_session.h"

/******************************* Headset Code **********************************/

#include <stdio.h>
#include <sys/types.h>
#include <assert.h>
#include <string.h>
#include <pthread.h>

#include "mm_ipc.h"
#include "mm_sound_common.h"
#include "mm_sound.h"

#include <vconf.h>
#include <vconf-keys.h>

/* earjack status value */
static void _earjack_status_changed_cb(keynode_t* node, void* data)
{
	int earjack_status = 0;
	device_io_direction_e io_direction = DEVICE_IO_DIRECTION_OUT;
	char *name = NULL;

	/* Get actual vconf value */
	vconf_get_int(VCONFKEY_SYSMAN_EARJACK, &earjack_status);
	debug_msg ("[%s] changed callback called, status=[%d]\n", vconf_keynode_get_name(node), earjack_status);

	if (earjack_status == DEVICE_EARJACK_TYPE_SPK_WITH_MIC) {
		io_direction = DEVICE_IO_DIRECTION_BOTH;
		name = DEVICE_NAME_AUDIOJACK_4P;
	} else {
		io_direction = DEVICE_IO_DIRECTION_OUT;
		name = DEVICE_NAME_AUDIOJACK_3P;
	}
	MMSoundMgrDeviceUpdateStatus (earjack_status ? DEVICE_UPDATE_STATUS_CONNECTED : DEVICE_UPDATE_STATUS_DISCONNECTED, DEVICE_TYPE_AUDIOJACK, io_direction, DEVICE_ID_AUTO, name, 0, NULL);
	MMSoundMgrSessionSetDeviceAvailable (DEVICE_WIRED, earjack_status, earjack_status, NULL);
}

static int _register_earjack_status(void)
{
	/* set callback for vconf key change */
	int ret = vconf_notify_key_changed(VCONFKEY_SYSMAN_EARJACK, _earjack_status_changed_cb, NULL);
	debug_msg ("vconf [%s] set ret = [%d]\n", VCONFKEY_SYSMAN_EARJACK, ret);
	return ret;
}

int MMSoundMgrHeadsetInit(void)
{
	debug_enter("\n");
	_register_earjack_status();
	debug_leave("\n");

	return MM_ERROR_NONE;
}

int MMSoundMgrHeadsetFini(void)
{
	debug_enter("\n");
	debug_leave("\n");

	return MM_ERROR_NONE;
}

