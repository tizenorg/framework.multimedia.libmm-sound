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

//#include <audio-session-manager.h>
//#include <avsys-audio.h>

#include <pulse/pulseaudio.h>
#include <pulse/ext-policy.h>

#define SUPPORT_MONO_AUDIO

#define SUPPORT_BT_SCO_DETECT

#include "include/mm_sound_mgr_pulse.h"
#include "include/mm_sound_mgr_session.h"

#include "include/mm_sound_msg.h"
#include "include/mm_sound_mgr_ipc.h"


#include <vconf.h>
#include <vconf-keys.h>

#define MAX_STRING	32

typedef struct _pulse_info
{
	pa_threaded_mainloop *m;
	pa_context *context;
	char device_api_name[MAX_STRING];
	char device_bus_name[MAX_STRING];
	bool init_bt_status;
	int bt_idx;
	int usb_idx;
}pulse_info_t;

pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;

pulse_info_t* pulse_info = NULL;

#define DEVICE_BUS_USB	"usb"
#define DEVICE_BUS_BUILTIN "builtin"
#define IS_STREQ(str1, str2) (strcmp (str1, str2) == 0)
#define IS_BUS_USB(bus) IS_STREQ(bus, DEVICE_BUS_USB)

/* -------------------------------- PULSEAUDIO --------------------------------------------*/

static void server_info_cb(pa_context *c, const pa_server_info *i, void *userdata)
{
	int ret = 0;

	if (!i) {
		debug_error("error in server info callback\n");

    } else {
		debug_msg ("We got default sink = [%s]\n", i->default_sink_name);

		/* ToDo: Update server info */
		ret = MMSoundMgrSessionSetDefaultSink (i->default_sink_name);
		if (ret != MM_ERROR_NONE) {
			/* TODO : Error Handling */
			debug_error ("MMSoundMgrSessionSetDefaultSink failed....ret = [%x]\n", ret);
		}
    }


}

static void init_card_info_cb (pa_context *c, const pa_card_info *i, int eol, void *userdata)
{
	pulse_info_t *pinfo = (pulse_info_t *)userdata;
	if (eol || i == NULL) {
		debug_msg ("signaling--------------\n");
		pa_threaded_mainloop_signal (pinfo->m, 0);
		return;
	}

	if (strstr (i->name, "bluez")) {
		pinfo->init_bt_status = true;
	}
}

static void new_card_info_cb (pa_context *c, const pa_card_info *i, int eol, void *userdata)
{
	int ret = 0;
	char* desc = NULL;
	char* bus = NULL;
	pulse_info_t *pinfo = (pulse_info_t *)userdata;

	if (eol || i == NULL || pinfo == NULL) {
		return;
	}

	bus = pa_proplist_gets(i->proplist, PA_PROP_DEVICE_BUS);
	debug_msg ("[%s][%d] card name is [%s], card.property.device.bus = [%s]\n", __func__, __LINE__, i->name, bus);

	if (IS_BUS_USB(bus)) {
		ret = MMSoundMgrSessionSetDeviceAvailable (DEVICE_USB_AUDIO, AVAILABLE, 0, (desc)? desc : "NONAME");
		if (ret != MM_ERROR_NONE) {
			/* TODO : Error Handling */
			debug_error ("MMSoundMgrSessionSetDeviceAvailable failed....ret = [%x]\n", ret);
		}
		/* Store USB index for future removal */
		pinfo->usb_idx = i->index;

	} else { /* other than USB, we assume this is BT */
		/* Get device name : eg. SBH-600 */
		desc = pa_proplist_gets(i->proplist, PA_PROP_DEVICE_DESCRIPTION);
		debug_msg ("[%s][%d] card name is [%s], card.property.device.description = [%s]\n", __func__, __LINE__, i->name, desc);

		/* ToDo: Update Device */
		ret = MMSoundMgrSessionSetDeviceAvailable (DEVICE_BT_A2DP, AVAILABLE, 0, (desc)? desc : "NONAME");
		if (ret != MM_ERROR_NONE) {
			/* TODO : Error Handling */
			debug_error ("MMSoundMgrSessionSetDeviceAvailable failed....ret = [%x]\n", ret);
		}
		/* Store BT index for future removal */
		pinfo->bt_idx = i->index;
	}
}

static void context_subscribe_cb (pa_context * c, pa_subscription_event_type_t t, uint32_t idx, void *userdata)
{
	pulse_info_t *pinfo = (pulse_info_t *)userdata;
	debug_msg (">>>>>>>>> [%s][%d] type=(0x%x) idx=(%u) pinfo=(%p)\n", __func__, __LINE__,  t, idx, pinfo);

	if (pinfo == NULL) {
		debug_error ("pinfo is null, return");
		return;
	}

	if ((t &  PA_SUBSCRIPTION_EVENT_FACILITY_MASK) == PA_SUBSCRIPTION_EVENT_CARD) {
		/* FIXME: We assumed that card is bt, card new/remove = bt new/remove */

		if ((t & PA_SUBSCRIPTION_EVENT_TYPE_MASK) == PA_SUBSCRIPTION_EVENT_REMOVE) { /* BT/USB is removed */
			if (idx == pinfo->bt_idx) {
				MMSoundMgrSessionSetDeviceAvailable (DEVICE_BT_A2DP, NOT_AVAILABLE, 0, NULL);
			} else if (idx == pinfo->usb_idx) {
				MMSoundMgrSessionSetDeviceAvailable (DEVICE_USB_AUDIO, NOT_AVAILABLE, 0, NULL);
			} else {
				debug_warning ("Unexpected card index [%d] is removed. (Current bt index=[%d], usb index=[%d]", idx, pinfo->bt_idx, pinfo->usb_idx);
			}
		} else if ((t & PA_SUBSCRIPTION_EVENT_TYPE_MASK) == PA_SUBSCRIPTION_EVENT_NEW) { /* BT/USB is loaded */
			/* Get more additional information for this card */
			pa_operation *o;
			if (!(o = 	pa_context_get_card_info_by_index (c, idx, new_card_info_cb, pinfo))) {
				return;
			}
			pa_operation_unref(o);

		}

	} else if ((t &  PA_SUBSCRIPTION_EVENT_FACILITY_MASK) == PA_SUBSCRIPTION_EVENT_SERVER) {
		pa_operation_unref(pa_context_get_server_info(c, server_info_cb, NULL));
	} else {
		debug_msg ("[%s][%d] type=(0x%x) idx=(%u) is not card or server event, skip...\n", __func__, __LINE__,  t, idx);
		return;
	}
}


static void context_state_cb (pa_context *c, void *userdata)
{
	pulse_info_t *pinfo = (pulse_info_t *)userdata;

	switch (pa_context_get_state(c)) {
		case PA_CONTEXT_UNCONNECTED:
		case PA_CONTEXT_CONNECTING:
		case PA_CONTEXT_AUTHORIZING:
		case PA_CONTEXT_SETTING_NAME:
		case PA_CONTEXT_FAILED:
		case PA_CONTEXT_TERMINATED:
			break;

		case PA_CONTEXT_READY:
		{
			 if (pinfo->context == c) {

				 /* Do CARD and SERVER Subscribe */
				pa_context_set_subscribe_callback(c, context_subscribe_cb, pinfo);
				pa_operation *o;
				if (!(o = pa_context_subscribe(c, (pa_subscription_mask_t)PA_SUBSCRIPTION_MASK_CARD | PA_SUBSCRIPTION_MASK_SERVER, NULL, NULL))) {
					return;
				}
				pa_operation_unref(o);

				pa_operation_unref(pa_context_get_card_info_list (pinfo->context, init_card_info_cb, pinfo));

				/* signaling will be done after get card info in card info callback */
			}
			break;
		}
	}
}


static int pulse_init (pulse_info_t * pinfo)
{
	int res;

	debug_msg (">>>>>>>>> [%s][%d]\n", __func__, __LINE__);

	/* Create new mainloop */
	pinfo->m = pa_threaded_mainloop_new();
	//g_assert(g_m);

	res = pa_threaded_mainloop_start (pinfo->m);
	//g_assert (res == 0);

	/* LOCK thread */
	pa_threaded_mainloop_lock (pinfo->m);

	/* Get mainloop API */
	pa_mainloop_api *api = pa_threaded_mainloop_get_api(pinfo->m);

	/* Create new Context */
	pinfo->context = pa_context_new(api, "SOUND_SERVER_ROUTE_MANAGER");

	/* Set Callback */
	pa_context_set_state_callback (pinfo->context, context_state_cb, pinfo);

	/* Connect */
	if (pa_context_connect (pinfo->context, NULL, PA_CONTEXT_NOAUTOSPAWN, NULL) < 0) {
		debug_error ("connection error\n");
	}

	for (;;) {
		pa_context_state_t state;

		state = pa_context_get_state (pinfo->context);

		debug_msg ("context state is now %d\n", state);

		if (!PA_CONTEXT_IS_GOOD (state)) {
			debug_error ("connection failed\n");
			break;
		}

		if (state == PA_CONTEXT_READY)
			break;

		/* Wait until the context is ready */
		debug_msg ("waiting..................\n");
		pa_threaded_mainloop_wait (pinfo->m);
	}

	/* UNLOCK thread */
	pa_threaded_mainloop_unlock (pinfo->m);

	debug_msg ("<<<<<<<<<< [%s][%d]\n", __func__, __LINE__);

	return res;
}


static void sink_info_cb (pa_context *c, const pa_sink_info *i, int is_last, void *userdata)
{
	pulse_info_t * pinfo = (pulse_info_t *)userdata;

	if (is_last || i == NULL || pinfo == NULL) {
		if (is_last < 0) {
			debug_error("Failed to get sink information: %s\n", pa_strerror(pa_context_errno(c)));
		}
		return;
	}

	debug_log ("Checking for sink name=[%s] Starts, finding api=[%s],bus=[%s]\n", i->name, pinfo->device_api_name, pinfo->device_bus_name);

	if (i->name && i->proplist) {
		const char *api_string = pa_proplist_gets (i->proplist, "device.api");
		if (api_string) {
			debug_log (" Found api = [%s]\n", api_string);
			if (IS_STREQ(api_string, pinfo->device_api_name)) {
				const char *bus_string = pa_proplist_gets (i->proplist, "device.bus");
				if (bus_string) {
					debug_log (" Found bus = [%s]\n", bus_string);
					if (IS_STREQ(bus_string, pinfo->device_bus_name)) {
						debug_log ("  ** FOUND!!! set default sink to [%s]\n", i->name);
						pa_operation_unref(pa_context_set_default_sink(pinfo->context, i->name, NULL, NULL));
					} else {
						debug_warning ("No string [%s] match!!!!\n", pinfo->device_bus_name);
					}
				} else {
					debug_log (" Found no bus ");
					if (IS_STREQ(DEVICE_BUS_BUILTIN, pinfo->device_bus_name)) {
						debug_log (" searching bus was builtin, then select this");
						pa_operation_unref(pa_context_set_default_sink(pinfo->context, i->name, NULL, NULL));
					}
				}
			} else {
				debug_warning ("No string [%s] match!!!!\n", pinfo->device_api_name);
			}
		}
	}

	debug_log ("Checked for sink name=[%s]\n", i->name);
}


static void pulse_set_default_sink (pulse_info_t * pinfo, char *device_api_name, char *device_bus_name)
{
	strcpy (pinfo->device_api_name, device_api_name);
	strcpy (pinfo->device_bus_name, device_bus_name);
	pa_operation_unref(pa_context_get_sink_info_list(pinfo->context, sink_info_cb, pinfo));
}

static int pulse_deinit (pulse_info_t * pinfo)
{
	pa_threaded_mainloop_lock (pinfo->m);
	if (pinfo->context) {
		pa_context_disconnect (pinfo->context);

		/* Make sure we don't get any further callbacks */
		pa_context_set_state_callback (pinfo->context, NULL, NULL);

		pa_context_unref (pinfo->context);
		pinfo->context = NULL;
	}
	pa_threaded_mainloop_unlock (pinfo->m);

	pa_threaded_mainloop_stop (pinfo->m);
	pa_threaded_mainloop_free (pinfo->m);

	debug_msg ("<<<<<<<<<< [%s][%d]\n", __func__, __LINE__);

	return 0;

}

/* -------------------------------- MONO AUDIO --------------------------------------------*/
#ifdef SUPPORT_MONO_AUDIO
#define MONO_KEY VCONFKEY_SETAPPL_ACCESSIBILITY_MONO_AUDIO

static void success_cb (pa_context *c, int success, void *userdata)
{
	debug_msg ("success = %d\n", success);
}

static void mono_changed_cb(keynode_t* node, void* data)
{
	int key_value;
	pulse_info_t* pinfo = (pulse_info_t*)data;

	debug_msg ("%s changed callback called\n",vconf_keynode_get_name(node));

	vconf_get_bool(MONO_KEY, &key_value);
	debug_msg ("key value = %d\n", key_value);

	pa_operation_unref (pa_ext_policy_set_mono (pinfo->context, key_value, success_cb, NULL));
}

int MMSoundMgrPulseHandleRegisterMonoAudio (void* pinfo)
{
	int ret = vconf_notify_key_changed(MONO_KEY, mono_changed_cb, pinfo);
	debug_msg ("vconf [%s] set ret = %d\n", MONO_KEY, ret);
	return ret;
}
#endif /* SUPPORT_MONO_AUDIO */


/* -------------------------------- BT SCO --------------------------------------------*/
#ifdef SUPPORT_BT_SCO_DETECT

static void bt_changed_cb(keynode_t* node, void* data)
{
	int bt_status = VCONFKEY_BT_DEVICE_NONE;
	int available = 0;

	debug_msg ("[%s] changed callback called\n", vconf_keynode_get_name(node));

	/* Get actual vconf value */
	vconf_get_int(VCONFKEY_BT_DEVICE, &bt_status);
	debug_msg ("key value = 0x%x\n", bt_status);

	/* Set device available based on vconf key value */
	available = (bt_status & VCONFKEY_BT_DEVICE_HEADSET_CONNECTED)? AVAILABLE : NOT_AVAILABLE;
	MMSoundMgrSessionSetDeviceAvailable (DEVICE_BT_SCO, available, 0, NULL);
}

int MMSoundMgrPulseHandleRegisterBluetoothStatus (void* pinfo)
{
	/* set callback for vconf key change */
	int ret = vconf_notify_key_changed(VCONFKEY_BT_DEVICE , bt_changed_cb, pinfo);
	debug_msg ("vconf [%s] set ret = %d\n", VCONFKEY_BT_DEVICE, ret);
	return ret;
}
#endif /* SUPPORT_BT_SCO_DETECT */

/* -------------------------------- MGR MAIN --------------------------------------------*/

int MMSoundMgrPulseHandleIsBtA2DPOnReq (mm_ipc_msg_t *msg, int (*sendfunc)(mm_ipc_msg_t*))
{
	int ret = 0;
	mm_ipc_msg_t respmsg = {0,};
	char* bt_name;
	bool is_bt_on = false;
	pthread_mutex_lock(&g_mutex);

	debug_enter("msg = %p, sendfunc = %p\n", msg, sendfunc);

	bt_name = MMSoundMgrSessionGetBtA2DPName();
	if (bt_name && strlen(bt_name) > 0) {
		is_bt_on = true;
	}

	debug_log ("is_bt_on = [%d], name = [%s]\n", is_bt_on, bt_name);

	SOUND_MSG_SET(respmsg.sound_msg,
				MM_SOUND_MSG_RES_IS_BT_A2DP_ON, msg->sound_msg.handle, is_bt_on, msg->sound_msg.msgid);
	strncpy (respmsg.sound_msg.filename, bt_name,  sizeof (respmsg.sound_msg.filename)-1);

	/* Send Response */
	ret = sendfunc (&respmsg);
	if (ret != MM_ERROR_NONE) {
		/* TODO : Error Handling */
		debug_error ("sendfunc failed....ret = [%x]\n", ret);
	}

	pthread_mutex_unlock(&g_mutex);

	debug_leave("\n");

	return ret;
}

EXPORT_API
void MMSoundMgrPulseSetDefaultSink (char* device_api_name, char* device_bus_name)
{
	debug_enter("\n");

	pulse_set_default_sink(pulse_info, device_api_name, device_bus_name);

	debug_leave("\n");
}

void MMSoundMgrPulseGetInitialBTStatus (bool *a2dp, bool *sco)
{
	int bt_status = VCONFKEY_BT_DEVICE_NONE;

	if (a2dp == NULL || sco == NULL) {
		debug_error ("Invalide arguments!!!\n");
		return;
	}

	/* Get saved bt status */
	*a2dp = pulse_info->init_bt_status;

	/* Get actual vconf value */
	vconf_get_int(VCONFKEY_BT_DEVICE, &bt_status);
	debug_msg ("key value = 0x%x\n", bt_status);
	*sco = (bt_status & VCONFKEY_BT_DEVICE_HEADSET_CONNECTED)? true : false;

	debug_msg ("returning a2dp=[%d], sco=[%d]\n", *a2dp, *sco);
}

void* MMSoundMgrPulseInit(void)
{
	pulse_info = (pulse_info_t*) malloc (sizeof(pulse_info_t));
	memset (pulse_info, 0, sizeof(pulse_info_t));

	debug_enter("\n");

	pulse_init(pulse_info);

#ifdef SUPPORT_MONO_AUDIO
	MMSoundMgrPulseHandleRegisterMonoAudio(pulse_info);
#endif

#ifdef SUPPORT_BT_SCO_DETECT
	MMSoundMgrPulseHandleRegisterBluetoothStatus(pulse_info);
#endif

	debug_leave("\n");
	return pulse_info;
}

int MMSoundMgrPulseFini(void* handle)
{
	debug_enter("\n");

	pulse_deinit((pulse_info_t *)handle);

	debug_leave("\n");
	return MM_ERROR_NONE;
}

