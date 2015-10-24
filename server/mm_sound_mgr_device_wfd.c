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

#include <gio/gio.h>

#include "include/mm_sound_mgr_common.h"
#include "../include/mm_sound_common.h"

#include <mm_error.h>
#include <mm_debug.h>

#include "include/mm_sound_mgr_device.h"
#include "include/mm_sound_mgr_device_wfd.h"
#include "include/mm_sound_mgr_session.h"

/******************************* WFD Code **********************************/

#include "mm_sound.h"

#define DBUS_INTERFACE_MIRRORING_SERVER     "org.tizen.scmirroring.server"
#define DBUS_OBJECT_MIRRORING_SERVER        "/org/tizen/scmirroring/server"
#define DBUS_SIGNAL_MIRRORING_CHANGED       "miracast_wfd_source_status_changed"

GDBusConnection *conn_wfd;
guint sig_id_wfd;

static void _wfd_status_changed(GDBusConnection *conn,
							   const gchar *sender_name,
							   const gchar *object_path,
							   const gchar *interface_name,
							   const gchar *signal_name,
							   GVariant *parameters,
							   gpointer user_data)
{
	int miracast_wfd_status = 0;
	const GVariantType* value_type;

	debug_msg ("sender : %s, object : %s, interface : %s, signal : %s",
			sender_name, object_path, interface_name, signal_name);
	if (g_variant_is_of_type(parameters, G_VARIANT_TYPE("(i)"))) {
		g_variant_get(parameters, "(i)", &miracast_wfd_status);
		debug_msg("singal[%s] = %X\n", DBUS_SIGNAL_MIRRORING_CHANGED, miracast_wfd_status);
		MMSoundMgrSessionSetDeviceAvailable(DEVICE_MIRRORING, miracast_wfd_status, 0, NULL);
		MMSoundMgrDeviceUpdateStatus(miracast_wfd_status ? DEVICE_UPDATE_STATUS_CONNECTED : DEVICE_UPDATE_STATUS_DISCONNECTED, DEVICE_TYPE_MIRRORING, DEVICE_IO_DIRECTION_OUT, DEVICE_ID_AUTO, NULL, 0, NULL);

	} else {
		value_type = g_variant_get_type(parameters);
		debug_warning("signal type is %s", value_type);
	}
}

void _deinit_wfd_dbus(void)
{
	debug_fenter ();
	g_dbus_connection_signal_unsubscribe(conn_wfd, sig_id_wfd);
	g_object_unref(conn_wfd);
	debug_fleave ();
}

int _init_wfd_dbus(void)
{
	GError *err = NULL;
	debug_fenter ();

	g_type_init();

	conn_wfd = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &err);
	if (!conn_wfd && err) {
		debug_error ("g_bus_get_sync() error (%s) ", err->message);
		g_error_free (err);
		goto error;
	}

	sig_id_wfd = g_dbus_connection_signal_subscribe(conn_wfd,
			NULL, DBUS_INTERFACE_MIRRORING_SERVER, DBUS_SIGNAL_MIRRORING_CHANGED, DBUS_OBJECT_MIRRORING_SERVER, NULL, 0,
			_wfd_status_changed, NULL, NULL);
	if (sig_id_wfd == 0) {
		debug_error ("g_dbus_connection_signal_subscribe() error (%d)", sig_id_wfd);
		goto sig_error;
	}

	debug_fleave ();
	return 0;

sig_error:
	g_dbus_connection_signal_unsubscribe(conn_wfd, sig_id_wfd);
	if (conn_wfd)
		g_object_unref(conn_wfd);

error:
	return -1;

}

int MMSoundMgrWfdInit(void)
{
	debug_enter("\n");

	if (_init_wfd_dbus() != 0) {
		debug_error ("Registering WFD signal handler failed\n");
		return MM_ERROR_SOUND_INTERNAL;
	}

	debug_leave("\n");
	return MM_ERROR_NONE;
}

int MMSoundMgrWfdFini(void)
{
	debug_enter("\n");

	_deinit_wfd_dbus();

	debug_leave("\n");
	return MM_ERROR_NONE;
}

