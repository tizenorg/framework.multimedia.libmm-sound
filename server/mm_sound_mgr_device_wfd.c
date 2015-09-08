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

#include "include/mm_sound_mgr_device.h"
#include "include/mm_sound_mgr_device_wfd.h"
#include "include/mm_sound_mgr_session.h"

/******************************* WFD Code **********************************/

#include "mm_sound.h"

#include <vconf.h>
#include <vconf-keys.h>


/* WFD status value */
static void _miracast_wfd_status_changed_cb(keynode_t* node, void* data)
{
	int miracast_wfd_status = 0;

	/* Get actual vconf value */
	vconf_get_bool(VCONFKEY_MIRACAST_WFD_SOURCE_STATUS, &miracast_wfd_status);
	debug_msg ("[%s] changed callback called, status=[%d]\n", vconf_keynode_get_name(node), miracast_wfd_status);

	MMSoundMgrSessionSetDeviceAvailable (DEVICE_MIRRORING, miracast_wfd_status, 0, NULL);
	MMSoundMgrDeviceUpdateStatus (miracast_wfd_status ? DEVICE_UPDATE_STATUS_CONNECTED : DEVICE_UPDATE_STATUS_DISCONNECTED, DEVICE_TYPE_MIRRORING, DEVICE_IO_DIRECTION_OUT, DEVICE_ID_AUTO, NULL, 0, NULL);
}

static int _register_wfd_status(void)
{
	/* set callback for vconf key change */
	int ret = vconf_notify_key_changed(VCONFKEY_MIRACAST_WFD_SOURCE_STATUS, _miracast_wfd_status_changed_cb, NULL);
	debug_msg ("vconf [%s] set ret = [%d]\n", VCONFKEY_MIRACAST_WFD_SOURCE_STATUS, ret);
	return ret;
}

int MMSoundMgrWfdInit(void)
{
	debug_enter("\n");

	if (_register_wfd_status() != 0) {
		debug_error ("Registering WFD status failed\n");
		return MM_ERROR_SOUND_INTERNAL;
	}

	debug_leave("\n");
	return MM_ERROR_NONE;
}

int MMSoundMgrWfdFini(void)
{
	debug_enter("\n");

	debug_leave("\n");
	return MM_ERROR_NONE;
}

