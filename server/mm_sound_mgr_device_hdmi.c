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
#include <vconf.h>
#include <vconf-keys.h>

#include <errno.h>

#include "include/mm_sound_mgr_common.h"
#include "../include/mm_sound_common.h"

#include <mm_error.h>
#include <mm_debug.h>

#include "include/mm_sound_mgr_device.h"
#include "include/mm_sound_mgr_device_hdmi.h"
#include "include/mm_sound_mgr_session.h"

/******************************* HDMI Code **********************************/

#include "mm_sound.h"

#define SUPPORT_DBUS_HDMI_AUDIO
#ifdef SUPPORT_DBUS_HDMI_AUDIO

#include <gio/gio.h>
#define DEVICED_SYSNOTI_PATH		"/Org/Tizen/System/DeviceD/SysNoti"
#define DEVICED_SYSNOTI_INTERFACE   "org.tizen.system.deviced.SysNoti"
#define HDMI_AUDIO_CHANGED_SIGNAL	"ChangedHDMIAudio"

#define VCONFKEY_SOUND_HDMI_SUPPORT "memory/private/sound/hdmisupport"
enum {
	MM_SOUND_HDMI_AUDIO_UNPLUGGED = -1,
	MM_SOUND_HDMI_AUDIO_NOT_AVAILABLE = 0,
	MM_SOUND_HDMI_AUDIO_AVAILABLE = 1
};

typedef enum {
	MM_SOUND_HDMI_SUPPORT_NOTHING = -1,
	MM_SOUND_HDMI_SUPPORT_ONLY_STEREO = 0,
	MM_SOUND_HDMI_SUPPORT_ONLY_SURROUND = 1,
	MM_SOUND_HDMI_SUPPORT_STEREO_AND_SURROUND =2,
} hdmi_support_type_t;

GDBusConnection *conn_hdmiaudio, *conn_popup;
guint sig_id_hdmiaudio, sig_id_popup;
int hdmi_available = false;

int _parse_hdmi_support_info(int hdmi_support, hdmi_support_type_t* support){
	int channels = 0;
	bool support_stereo = false, support_surround = false;

	channels = hdmi_support & 0x000000FF;

	if(channels == 0xFF){
		channels = 0;
		return 0;
	}

	if ((channels & 0x20) > 0)
	    support_surround = true;
	if ((channels & 0x02) > 0)
	    support_stereo = true;

	if (support_stereo && support_surround)
	    *support = MM_SOUND_HDMI_SUPPORT_STEREO_AND_SURROUND;
	else if (!support_stereo && support_surround)
	    *support = MM_SOUND_HDMI_SUPPORT_ONLY_SURROUND;
	else if (support_stereo && !support_surround)
	    *support = MM_SOUND_HDMI_SUPPORT_ONLY_STEREO;
	else
	    *support = MM_SOUND_HDMI_SUPPORT_NOTHING;

	return 1;
}


static void hdmi_audio_changed(GDBusConnection *conn,
							   const gchar *sender_name,
							   const gchar *object_path,
							   const gchar *interface_name,
							   const gchar *signal_name,
							   GVariant *parameters,
							   gpointer user_data)
{
	int value=0;
	hdmi_support_type_t hdmi_support_type = MM_SOUND_HDMI_SUPPORT_NOTHING;
	const GVariantType* value_type;

	debug_msg ("sender : %s, object : %s, interface : %s, signal : %s",
			sender_name, object_path, interface_name, signal_name);
	if(g_variant_is_of_type(parameters, G_VARIANT_TYPE("(i)")))
	{
		g_variant_get(parameters, "(i)",&value);
		debug_msg("singal[%s] = %X\n", HDMI_AUDIO_CHANGED_SIGNAL, value);
		vconf_set_int(VCONFKEY_SOUND_HDMI_SUPPORT, value);

		if(value >= MM_SOUND_HDMI_AUDIO_AVAILABLE) {
			hdmi_available = true;
			_parse_hdmi_support_info(value, &hdmi_support_type);
			debug_msg("HDMI support type : %d\n", hdmi_support_type);
			MMSoundMgrSessionSetDeviceAvailable (DEVICE_HDMI, hdmi_available, 0, NULL);
			MMSoundMgrDeviceUpdateStatus (DEVICE_UPDATE_STATUS_CONNECTED, DEVICE_TYPE_HDMI, DEVICE_IO_DIRECTION_OUT, DEVICE_ID_AUTO, NULL, 0, NULL);
		}

		if(value == MM_SOUND_HDMI_AUDIO_NOT_AVAILABLE || value == MM_SOUND_HDMI_AUDIO_UNPLUGGED){
			hdmi_available = false;
			MMSoundMgrSessionSetDeviceAvailable (DEVICE_HDMI, hdmi_available, 0, NULL);
			MMSoundMgrDeviceUpdateStatus (DEVICE_UPDATE_STATUS_DISCONNECTED, DEVICE_TYPE_HDMI, DEVICE_IO_DIRECTION_OUT, DEVICE_ID_AUTO, NULL, 0, NULL);
		}
	}
	else
	{
		value_type = g_variant_get_type(parameters);
		debug_warning("signal type is %s", value_type);
	}

}

void _deinit_hdmi_audio_dbus(void)
{
	debug_fenter ();
	g_dbus_connection_signal_unsubscribe(conn_hdmiaudio, sig_id_hdmiaudio);
	g_object_unref(conn_hdmiaudio);
	debug_fleave ();
}

int _init_hdmi_audio_dbus(void)
{
	GError *err = NULL;
	debug_fenter ();

	g_type_init();

	conn_hdmiaudio = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &err);
	if (!conn_hdmiaudio && err) {
		debug_error ("g_bus_get_sync() error (%s) ", err->message);
		g_error_free (err);
		goto error;
	}

	sig_id_hdmiaudio = g_dbus_connection_signal_subscribe(conn_hdmiaudio,
			NULL, DEVICED_SYSNOTI_INTERFACE, HDMI_AUDIO_CHANGED_SIGNAL, DEVICED_SYSNOTI_PATH, NULL, 0,
			hdmi_audio_changed, NULL, NULL);
	if (sig_id_hdmiaudio == 0) {
		debug_error ("g_dbus_connection_signal_subscribe() error (%d)", sig_id_hdmiaudio);
		goto sig_error;
	}

	debug_fleave ();
	return 0;

sig_error:
	g_dbus_connection_signal_unsubscribe(conn_hdmiaudio, sig_id_hdmiaudio);
	g_object_unref(conn_hdmiaudio);

error:
	return -1;

}
#endif

int MMSoundMgrHdmiInit(void)
{
	debug_enter("\n");
#ifdef SUPPORT_DBUS_HDMI_AUDIO
	if (_init_hdmi_audio_dbus() != 0) {
		debug_error ("Registering hdmi audio signal handler failed\n");
		return MM_ERROR_SOUND_INTERNAL;
	}
#endif
	debug_leave("\n");
	return MM_ERROR_NONE;
}

int MMSoundMgrHdmiFini(void)
{
	debug_enter("\n");
#ifdef SUPPORT_DBUS_HDMI_AUDIO
	_deinit_hdmi_audio_dbus();
#endif
	debug_leave("\n");
	return MM_ERROR_NONE;
}

