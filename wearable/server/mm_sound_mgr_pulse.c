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
#include "include/mm_sound_mgr_dock.h"
#include "../include/mm_sound_common.h"
#include "../include/mm_sound.h"

#include <mm_error.h>
#include <mm_debug.h>
#include <glib.h>

#include <pulse/pulseaudio.h>
#include <pulse/ext-policy.h>
#include <pulse/ext-echo-cancel.h>

#define SUPPORT_MONO_AUDIO
#define SUPPORT_AUDIO_BALANCE
#ifdef SUPPORT_BT_SCO
#define SUPPORT_BT_SCO_DETECT
#endif
#define SUPPORT_AUDIO_MUTEALL

#include "include/mm_sound_mgr_pulse.h"
#include "include/mm_sound_mgr_session.h"

#include "include/mm_sound_msg.h"
#include "include/mm_sound_mgr_ipc.h"


#include <vconf.h>
#include <vconf-keys.h>

#ifdef SUPPORT_BT_SCO_DETECT
#ifndef TIZEN_MICRO
#include "bluetooth.h"
#else
#include "bluetooth-api.h"
#include "bluetooth-audio-api.h"
#include "avsys-audio.h"
#include "../include/mm_sound_utils.h"
#endif
#endif

#define VCONF_BT_STATUS "db/bluetooth/status"

#define MAX_STRING	32
enum{
	DEVICE_NONE,
	DEVICE_IN_MIC_OUT_SPEAKER,
	DEVICE_IN_MIC_OUT_RECEIVER,
	DEVICE_IN_MIC_OUT_WIRED,
	DEVICE_IN_WIRED_OUT_WIRED,
	DEVICE_IN_BT_SCO_OUT_BT_SCO,
};

typedef struct _pulse_info
{
	pa_threaded_mainloop *m;
	pa_threaded_mainloop *m_operation;

	pa_context *context;

	char device_api_name[MAX_STRING];
	char device_bus_name[MAX_STRING];
	char *usb_sink_name;
	char *dock_sink_name;
	bool init_bt_status;

	bool is_sco_init;

	int bt_idx;
	int usb_idx;
	int dock_idx;
	int device_in_out;
	int aec_module_idx;
	int card_idx;
	int sink_idx;

	pthread_t thread;
	GAsyncQueue *queue;
}pulse_info_t;

typedef enum {
	PA_CLIENT_NOT_USED,
	PA_CLIENT_GET_CARD_INFO_BY_INDEX,
	PA_CLIENT_GET_SERVER_INFO,
	PA_CLIENT_DESTROY,
	PA_CLIENT_MAX
}pa_client_command_t;

static const char* command_str[] =
{
	"NotUsed",
	"GetCardInfoByIndex",
	"GetServerInfo",
	"Destroy"
	"Max"
};

pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;

pulse_info_t* pulse_info = NULL;
#ifdef TIZEN_MICRO
static int g_bt_hf_volume_control = 0;
static int g_bt_hf_sco_nb = 0;
#define CLIENT_VOL_MAX 15
#define SPK_VOL_MAX 6
#endif
#define DEVICE_BUS_USB	"usb"
#define DEVICE_BUS_BUILTIN "builtin"
#define IS_STREQ(str1, str2) (strcmp (str1, str2) == 0)
#define IS_BUS_USB(bus) IS_STREQ(bus, DEVICE_BUS_USB)

#define CHECK_CONTEXT_SUCCESS_GOTO(p, expression, label) \
	do { \
		if (!(expression)) { \
			goto label; \
		} \
	} while(0);

#define CHECK_CONTEXT_DEAD_GOTO(c, label) \
	do { \
		if (!PA_CONTEXT_IS_GOOD(pa_context_get_state(c))) {\
			goto label; \
		} \
	} while(0);


/* -------------------------------- PULSEAUDIO --------------------------------------------*/


static void pa_context_subscribe_success_cb(pa_context *c, int success, void *userdata)
{
	pulse_info_t *pinfo = (pulse_info_t*)userdata;
	debug_msg("\n");
	pa_threaded_mainloop_signal(pinfo->m, 0);
	return;
}

static void server_info_cb(pa_context *c, const pa_server_info *i, void *userdata)
{
	int ret = 0;
	pulse_info_t *pinfo = (pulse_info_t*)userdata;

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

	pa_threaded_mainloop_signal(pinfo->m, 0);
	return;
}

static void init_card_info_cb (pa_context *c, const pa_card_info *i, int eol, void *userdata)
{
	pulse_info_t *pinfo = (pulse_info_t *)userdata;
	if (pinfo == NULL) {
		debug_error ("pinfo is null, return");
		return;
	}

	debug_msg("\n");

	if (eol || i == NULL) {
		debug_msg ("signaling--------------\n");
		pa_threaded_mainloop_signal (pinfo->m, 0);
		return;
	}

	if (strstr (i->name, "bluez")) {
		pinfo->init_bt_status = true;
	}

	return;
}

static void _store_usb_info (pulse_info_t *pinfo, bool is_usb_dock, int index, const char* name)
{
	if (is_usb_dock) {
		if (pinfo->dock_sink_name)
			free(pinfo->dock_sink_name);

		pinfo->dock_sink_name = strdup(name);
		pinfo->dock_idx = index;
	} else {
		if (pinfo->usb_sink_name)
			free(pinfo->usb_sink_name);

		pinfo->usb_sink_name = strdup(name);
		pinfo->usb_idx = index;
	}
}

static void new_card_info_cb (pa_context *c, const pa_card_info *i, int eol, void *userdata)
{
	pulse_info_t *pinfo = (pulse_info_t *)userdata;
	int ret = 0;
	const char* bus = NULL;

	if (eol || i == NULL || pinfo == NULL) {
		pa_threaded_mainloop_signal(pinfo->m, 0);
		return;
	}

	bus = pa_proplist_gets(i->proplist, PA_PROP_DEVICE_BUS);
	if (IS_BUS_USB(bus)) {
		int dock_status = 0;
		const char* serial = pa_proplist_gets(i->proplist, PA_PROP_DEVICE_SERIAL);
		debug_msg ("name=[%s], bus=[%s], serial=[%s]\n", i->name, bus, serial);

		vconf_get_int(VCONFKEY_SYSMAN_CRADLE_STATUS, &dock_status);
		if ((pinfo->dock_idx == PA_INVALID_INDEX) &&
			((dock_status == DOCK_AUDIODOCK) || (dock_status == DOCK_SMARTDOCK))) {
			_store_usb_info(pinfo, true, i->index, serial);
			ret = MMSoundMgrSessionSetDeviceAvailable (DEVICE_MULTIMEDIA_DOCK, AVAILABLE, 0, (serial)? serial : "NONAME");
			if (ret != MM_ERROR_NONE) {
				/* TODO : Error Handling */
				debug_error ("MMSoundMgrSessionSetDeviceAvailable failed....ret = [%x]", ret);
			}
		} else {
			_store_usb_info(pinfo, false, i->index, serial);
			ret = MMSoundMgrSessionSetDeviceAvailable (DEVICE_USB_AUDIO, AVAILABLE, 0, (serial)? serial : "NONAME");
			if (ret != MM_ERROR_NONE) {
				/* TODO : Error Handling */
				debug_error ("MMSoundMgrSessionSetDeviceAvailable failed....ret = [%x]", ret);
			}
		}
	} else { /* other than USB, we assume this is BT */
		/* Get device name : eg. SBH-600 */
		const char* desc = pa_proplist_gets(i->proplist, PA_PROP_DEVICE_DESCRIPTION);
		debug_msg ("name=[%s], bus=[%s], desc=[%s]", i->name, bus, desc);

		/* Store BT index for future removal */
		pinfo->bt_idx = i->index;
		ret = MMSoundMgrSessionSetDeviceAvailable (DEVICE_BT_A2DP, AVAILABLE, 0, (desc)? desc : "NONAME");
		if (ret != MM_ERROR_NONE) {
			/* TODO : Error Handling */
			debug_error ("MMSoundMgrSessionSetDeviceAvailable failed....ret = [%x]", ret);
		}
	}
	return;
}

static void context_subscribe_cb (pa_context * c, pa_subscription_event_type_t t, uint32_t idx, void *userdata)
{
	pulse_info_t *pinfo = (pulse_info_t *)userdata;

	if (pinfo == NULL) {
		debug_error ("pinfo is null, return");
		return;
	}

	if ((t &  PA_SUBSCRIPTION_EVENT_FACILITY_MASK) == PA_SUBSCRIPTION_EVENT_CARD) {
		debug_msg ("EVENT CARD : type=(0x%x) idx=(%u) pinfo=(%p)\n", t, idx, pinfo);
		/* FIXME: We assumed that card is bt, card new/remove = bt new/remove */
		if ((t & PA_SUBSCRIPTION_EVENT_TYPE_MASK) == PA_SUBSCRIPTION_EVENT_REMOVE) { /* BT/USB is removed */
			if (idx == pinfo->bt_idx) {
				MMSoundMgrSessionSetDeviceAvailable (DEVICE_BT_A2DP, NOT_AVAILABLE, 0, NULL);
				pinfo->bt_idx = PA_INVALID_INDEX;
			} else if (idx == pinfo->usb_idx) {
				MMSoundMgrSessionSetDeviceAvailable (DEVICE_USB_AUDIO, NOT_AVAILABLE, 0, NULL);
				pinfo->usb_idx = PA_INVALID_INDEX;
				if (pinfo->usb_sink_name) {
					free(pinfo->usb_sink_name);
					pinfo->usb_sink_name = NULL;
				}
			} else if (idx == pinfo->dock_idx) {
				MMSoundMgrSessionSetDeviceAvailable (DEVICE_MULTIMEDIA_DOCK, NOT_AVAILABLE, 0, NULL);
				pinfo->dock_idx = PA_INVALID_INDEX;
				if (pinfo->dock_sink_name) {
					free(pinfo->dock_sink_name);
					pinfo->dock_sink_name = NULL;
				}
			} else {
				debug_warning ("Unexpected card index [%d] is removed. (Current bt index=[%d], usb index=[%d], dock index=[%d]", idx, pinfo->bt_idx, pinfo->usb_idx, pinfo->dock_idx);
			}
		} else if ((t & PA_SUBSCRIPTION_EVENT_TYPE_MASK) == PA_SUBSCRIPTION_EVENT_NEW) { /* BT/USB is loaded */
			/* Get more additional information for this card */
			pinfo->card_idx = idx;
			g_async_queue_push(pinfo->queue, (gpointer)PA_CLIENT_GET_CARD_INFO_BY_INDEX);
		}
	} else if ((t &  PA_SUBSCRIPTION_EVENT_FACILITY_MASK) == PA_SUBSCRIPTION_EVENT_SERVER) {
		debug_msg ("EVENT SERVER : type=(0x%x) idx=(%u) pinfo=(%p)\n", t, idx, pinfo);
#if 0
		/* FIXME : This cause crash on TIZEN_MICRO, to be removed completely */
		g_async_queue_push(pinfo->queue, (gpointer)PA_CLIENT_GET_SERVER_INFO);
#endif
	} else {
		debug_msg ("type=(0x%x) idx=(%u) is not card or server event, skip...\n", t, idx);
		return;
	}
}

static void context_state_cb (pa_context *c, void *userdata)
{
	pulse_info_t *pinfo = (pulse_info_t *)userdata;

	if (pinfo == NULL) {
		debug_error ("pinfo is null");
		return;
	}

	switch (pa_context_get_state(c)) {
		case PA_CONTEXT_UNCONNECTED:
		case PA_CONTEXT_CONNECTING:
		case PA_CONTEXT_AUTHORIZING:
		case PA_CONTEXT_SETTING_NAME:
			break;
		case PA_CONTEXT_FAILED:
			{
			    // wait for pa_ready file creation.
			    int fd = -1;

			    do {
				fd = open(PA_READY, O_RDONLY);
				if(fd < 0)
				    usleep(20000);
			    } while(fd < 0);
			    close(fd);

			    debug_error("pulseaudio crash!! sound_server will be restart!!");
			    kill(getpid(), SIGKILL);
			}
			break;
		case PA_CONTEXT_TERMINATED:
			pa_threaded_mainloop_signal(pinfo->m, 0);
			break;

		case PA_CONTEXT_READY:
			pa_threaded_mainloop_signal(pinfo->m, 0);
			break;
	}
}

void *pulse_client_thread_run (void *args)
{
	pulse_info_t *pinfo = (pulse_info_t*)args;
	pa_operation *o = NULL;
	pa_client_command_t cmd = PA_CLIENT_NOT_USED;

	while(1)
	{
		cmd = (pa_client_command_t)g_async_queue_pop(pinfo->queue);
		debug_msg("pop cmd = [%d][%s]", cmd, command_str[cmd]);
		if(cmd <= PA_CLIENT_NOT_USED || cmd >= PA_CLIENT_MAX)
			continue;

		pa_threaded_mainloop_lock(pinfo->m);
		CHECK_CONTEXT_DEAD_GOTO(pinfo->context, unlock_and_fail);

		switch((pa_client_command_t)cmd)
		{
			case PA_CLIENT_GET_CARD_INFO_BY_INDEX:
				o = pa_context_get_card_info_by_index (pinfo->context, pinfo->card_idx, new_card_info_cb, pinfo);
				break;

			case PA_CLIENT_GET_SERVER_INFO:
				o = pa_context_get_server_info(pinfo->context, server_info_cb, pinfo);
				break;

			case PA_CLIENT_DESTROY:
				goto destroy;

			default:
				debug_msg("unsupported command\n");
				goto unlock_and_fail;
		}

		CHECK_CONTEXT_SUCCESS_GOTO(pinfo->context, o, unlock_and_fail);
		while (pa_operation_get_state(o) == PA_OPERATION_RUNNING) {
			pa_threaded_mainloop_wait(pinfo->m);
			CHECK_CONTEXT_DEAD_GOTO(pinfo->context, unlock_and_fail);
		}
		pa_operation_unref(o);
		pa_threaded_mainloop_unlock(pinfo->m);

		continue;

unlock_and_fail:
		if (o) {
			pa_operation_cancel(o);
			pa_operation_unref(o);
		}
		pa_threaded_mainloop_unlock(pinfo->m);
	}

	return 0;

destroy:
	pa_threaded_mainloop_unlock(pinfo->m);

	return 0;
}

static int pulse_client_thread_init(pulse_info_t *pinfo)
{
	debug_msg("\n");

	pinfo->queue = g_async_queue_new();
	if(!pinfo->queue)
		return -1;

	if(pthread_create(&pinfo->thread, NULL, pulse_client_thread_run, pinfo) < 0) {
		return -1;
	}

	return 0;
}

static int pulse_init (pulse_info_t * pinfo)
{
	int res;
	pa_operation *o = NULL;

	debug_msg (">>>>>>>>> \n");

	if (pinfo == NULL) {
		debug_error ("pinfo is null");
		return -1;
	}

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
		pa_context_state_t state = pa_context_get_state (pinfo->context);
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

	pa_context_set_subscribe_callback(pinfo->context, context_subscribe_cb, pinfo);
	o = pa_context_subscribe(pinfo->context,
			(pa_subscription_mask_t)PA_SUBSCRIPTION_MASK_CARD | PA_SUBSCRIPTION_MASK_SINK | PA_SUBSCRIPTION_MASK_SERVER,
			pa_context_subscribe_success_cb,pinfo);

	CHECK_CONTEXT_SUCCESS_GOTO(pinfo->context, o, unlock_and_fail);
	while (pa_operation_get_state(o) == PA_OPERATION_RUNNING) {
		pa_threaded_mainloop_wait(pinfo->m);
		CHECK_CONTEXT_DEAD_GOTO(pinfo->context, unlock_and_fail);
	}
	pa_operation_unref(o);

	/* Get initial card info */
	o = pa_context_get_card_info_list (pinfo->context, init_card_info_cb, pinfo);

	CHECK_CONTEXT_SUCCESS_GOTO(pinfo->context, o, unlock_and_fail);
	while (pa_operation_get_state(o) == PA_OPERATION_RUNNING) {
		pa_threaded_mainloop_wait(pinfo->m);
		CHECK_CONTEXT_DEAD_GOTO(pinfo->context, unlock_and_fail);
	}
	pa_operation_unref(o);

	/* UNLOCK thread */
	pa_threaded_mainloop_unlock (pinfo->m);

	debug_msg ("<<<<<<<<<<\n");

	return res;

unlock_and_fail:
	if (o) {
		pa_operation_cancel(o);
		pa_operation_unref(o);
	}
	pa_threaded_mainloop_unlock(pinfo->m);

	debug_msg ("<<<<<<<<<<\n");

	return res;
}

static int pulse_deinit (pulse_info_t * pinfo)
{
	if (pinfo == NULL) {
		debug_error ("pinfo is null");
		return -1;
	}

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

	debug_msg ("<<<<<<<<<<\n");

	return 0;

}

#ifdef _TIZEN_PUBLIC_
#define AEC_ARGUMENT "aec_method=speex"
#else
#define AEC_ARGUMENT "aec_method=lvvefs sink_master=alsa_output.0.analog-stereo source_master=alsa_input.0.analog-stereo"
#endif

#ifndef TIZEN_MICRO
static void aec_load_module_cb(pa_context *c, uint32_t idx, void *userdata)
{
	pulse_info_t *pinfo = (pulse_info_t *)userdata;
	if (pinfo == NULL) {
		debug_error ("pinfo is null");
		return;
	}

	if (idx != PA_INVALID_INDEX) {
		debug_msg ("[PA_CB] m[%p] c[%p] AEC load success idx:%d", pinfo->m, c, idx);
		pinfo->aec_module_idx = idx;
	} else {
		debug_error("[PA_CB] m[%p] c[%p] AEC load fail:%s", pinfo->m, c, pa_strerror(pa_context_errno(c)));
	}

	pa_threaded_mainloop_signal(pinfo->m, 0);
}

static void aec_unload_module_cb(pa_context *c, int success, void *userdata)
{
	pulse_info_t *pinfo = (pulse_info_t *)userdata;
	if (pinfo == NULL) {
		debug_error ("pinfo is null");
		return;
	}

	if (success) {
		debug_msg ("[PA_CB] m[%p] c[%p] AEC unload success", pinfo->m, c);
		pinfo->aec_module_idx = PA_INVALID_INDEX;
	} else {
		debug_error("[PA_CB] m[%p] c[%p] AEC unload fail:%s", pinfo->m, c, pa_strerror(pa_context_errno(c)));
	}

	pa_threaded_mainloop_signal(pinfo->m, 0);
}
#endif

static void unload_hdmi_cb(pa_context *c, int success, void *userdata)
{
	pulse_info_t *pinfo = (pulse_info_t *)userdata;
	if (pinfo == NULL) {
		debug_error ("pinfo is null");
		return;
	}

	if (success) {
		debug_msg ("[PA_CB] m[%p] c[%p] HDMI unload success", pinfo->m, c);
	} else {
		debug_error("[PA_CB] m[%p] c[%p] HDMI unload fail:%s", pinfo->m, c, pa_strerror(pa_context_errno(c)));
	}

	pa_threaded_mainloop_signal(pinfo->m, 0);
}

#ifndef TIZEN_MICRO
static void aec_set_device_cb(pa_context *c, int success, void *userdata)
{
	pulse_info_t *pinfo = (pulse_info_t *)userdata;
	if (pinfo == NULL) {
		debug_error ("pinfo is null");
		return;
	}

	if (success) {
		debug_msg ("[PA_CB] m[%p] c[%p] AEC set device success", pinfo->m, c);
	} else {
		debug_error("[PA_CB] m[%p] c[%p] AEC set device fail:%s", pinfo->m, c, pa_strerror(pa_context_errno(c)));
	}

	pa_threaded_mainloop_signal(pinfo->m, 0);
}

static void aec_set_device_nosignal_cb(pa_context *c, int success, void *userdata)
{
	pulse_info_t *pinfo = (pulse_info_t *)userdata;
	if (pinfo == NULL) {
		debug_error ("pinfo is null");
		return;
	}

	if (success) {
		debug_msg ("[PA_CB] m[%p] c[%p] AEC set device success", pinfo->m, c);
	} else {
		debug_error("[PA_CB] m[%p] c[%p] AEC set device fail:%s", pinfo->m, c, pa_strerror(pa_context_errno(c)));
	}
}

void MMSoundMgrPulseHandleAECLoadModule()
{
	int key_value;
	pa_operation *o = NULL;

	if (pulse_info == NULL) {
		debug_error ("Pulse module in sound server not loaded");
		return;
	}
	if (pulse_info->aec_module_idx == PA_INVALID_INDEX) {
		pa_threaded_mainloop_lock(pulse_info->m);
		CHECK_CONTEXT_DEAD_GOTO(pulse_info->context, unlock_and_fail);

		debug_msg("[PA] pa_context_load_module m[%p] c[%p]", pulse_info->m, pulse_info->context);
		o = pa_context_load_module(pulse_info->context, "module-echo-cancel", AEC_ARGUMENT, aec_load_module_cb, pulse_info);
		CHECK_CONTEXT_SUCCESS_GOTO(pulse_info->context, o, unlock_and_fail);
		while (pa_operation_get_state(o) == PA_OPERATION_RUNNING) {
			pa_threaded_mainloop_wait(pulse_info->m);
			CHECK_CONTEXT_DEAD_GOTO(pulse_info->context, unlock_and_fail);
		}
		pa_operation_unref(o);

#ifdef _TIZEN_PUBLIC_

#else
		/* to set initial device to AEC library */
		debug_msg("[PA] pa_ext_echo_cancel_set_device m[%p] c[%p] device_in_out:%d", pulse_info->m, pulse_info->context, pulse_info->device_in_out);
		o = pa_ext_echo_cancel_set_device (pulse_info->context, pulse_info->device_in_out , aec_set_device_cb, pulse_info);
		CHECK_CONTEXT_SUCCESS_GOTO(pulse_info->context, o, unlock_and_fail);
		while (pa_operation_get_state(o) == PA_OPERATION_RUNNING) {
			pa_threaded_mainloop_wait(pulse_info->m);
			CHECK_CONTEXT_DEAD_GOTO(pulse_info->context, unlock_and_fail);
		}
		pa_operation_unref(o);

		/* to set initial volume to AEC library */
		vconf_get_int(VCONF_KEY_VOLUME_TYPE_VOIP, &key_value);
		debug_msg("[PA] pa_ext_echo_cancel_set_volume m[%p] c[%p] volume:%d", pulse_info->m, pulse_info->context, key_value);
		o = pa_ext_echo_cancel_set_volume(pulse_info->context, key_value, aec_set_volume_cb, pulse_info);
		CHECK_CONTEXT_SUCCESS_GOTO(pulse_info->context, o, unlock_and_fail);
		while (pa_operation_get_state(o) == PA_OPERATION_RUNNING) {
			pa_threaded_mainloop_wait(pulse_info->m);
			CHECK_CONTEXT_DEAD_GOTO(pulse_info->context, unlock_and_fail);
		}
		pa_operation_unref(o);
#endif

		pa_threaded_mainloop_unlock(pulse_info->m);
	} else {
		debug_msg ("Echo cancel module is already loaded");
	}
	return;

unlock_and_fail:
	if (o) {
		pa_operation_cancel(o);
		pa_operation_unref(o);
	}
	pa_threaded_mainloop_unlock(pulse_info->m);
}

void MMSoundMgrPulseHandleAECUnLoadModule()
{
	pa_operation *o = NULL;
	if (pulse_info == NULL) {
		debug_error ("Pulse module in sound server not loaded");
		return;
	}
	if (pulse_info->aec_module_idx != PA_INVALID_INDEX) {
		pa_threaded_mainloop_lock(pulse_info->m);
		CHECK_CONTEXT_DEAD_GOTO(pulse_info->context, unlock_and_fail);

		debug_msg("[PA] pa_context_unload_module m[%p] c[%p] idx:%d", pulse_info->m, pulse_info->context, pulse_info->aec_module_idx);
		o = pa_context_unload_module(pulse_info->context, pulse_info->aec_module_idx, aec_unload_module_cb, pulse_info);
		CHECK_CONTEXT_SUCCESS_GOTO(pulse_info->context, o, unlock_and_fail);
		while (pa_operation_get_state(o) == PA_OPERATION_RUNNING) {
			pa_threaded_mainloop_wait(pulse_info->m);
			CHECK_CONTEXT_DEAD_GOTO(pulse_info->context, unlock_and_fail);
		}
		pa_operation_unref(o);

		pa_threaded_mainloop_unlock(pulse_info->m);
	}
	return;

unlock_and_fail:
	if (o) {
		pa_operation_cancel(o);
		pa_operation_unref(o);
	}
	pa_threaded_mainloop_unlock(pulse_info->m);
}

void MMSoundMgrPulseHandleAecSetDevice(mm_sound_device_in device_in, mm_sound_device_out device_out)
{
#ifdef _TIZEN_PUBLIC_
	return;
#else
	int value;
	pa_operation *o = NULL;

	if (pulse_info == NULL || pulse_info->aec_module_idx == PA_INVALID_INDEX) {
		debug_error ("Pulse module in sound server not loaded");
		return;
	}

	if ((device_in == MM_SOUND_DEVICE_IN_MIC || device_in == MM_SOUND_DEVICE_IN_NONE) && device_out == MM_SOUND_DEVICE_OUT_SPEAKER) {
		value = DEVICE_IN_MIC_OUT_SPEAKER;
	} else if ((device_in == MM_SOUND_DEVICE_IN_MIC || device_in == MM_SOUND_DEVICE_IN_NONE) && device_out == MM_SOUND_DEVICE_OUT_RECEIVER){
		value = DEVICE_IN_MIC_OUT_RECEIVER;
	} else if ((device_in == MM_SOUND_DEVICE_IN_MIC || device_in == MM_SOUND_DEVICE_IN_NONE) && device_out == MM_SOUND_DEVICE_OUT_WIRED_ACCESSORY){
		value = DEVICE_IN_MIC_OUT_WIRED;
	} else if (device_in == MM_SOUND_DEVICE_IN_WIRED_ACCESSORY && device_out == MM_SOUND_DEVICE_OUT_WIRED_ACCESSORY){
		value = DEVICE_IN_WIRED_OUT_WIRED;
	} else if (device_in == MM_SOUND_DEVICE_IN_BT_SCO && device_out == MM_SOUND_DEVICE_OUT_BT_SCO) {
		value = DEVICE_IN_BT_SCO_OUT_BT_SCO;
	} else {
		value = DEVICE_NONE;
	}

	if (value > DEVICE_NONE && value <= DEVICE_IN_BT_SCO_OUT_BT_SCO) {
		if (pa_threaded_mainloop_in_thread(pulse_info->m)) {
			o = pa_ext_echo_cancel_set_device (pulse_info->context, value , aec_set_device_nosignal_cb, pulse_info);
			pa_operation_unref(o);
		} else {
			pa_threaded_mainloop_lock(pulse_info->m);
			CHECK_CONTEXT_DEAD_GOTO(pulse_info->context, unlock_and_fail);

			debug_msg("[PA] pa_ext_echo_cancel_set_device m[%p] c[%p] device:%d", pulse_info->m, pulse_info->context, value);
			o = pa_ext_echo_cancel_set_device (pulse_info->context, value , aec_set_device_cb, pulse_info);
			CHECK_CONTEXT_SUCCESS_GOTO(pulse_info->context, o, unlock_and_fail);
			while (pa_operation_get_state(o) == PA_OPERATION_RUNNING) {
				pa_threaded_mainloop_wait(pulse_info->m);
				CHECK_CONTEXT_DEAD_GOTO(pulse_info->context, unlock_and_fail);
			}
			pa_operation_unref(o);
			pa_threaded_mainloop_unlock(pulse_info->m);
		}
	} else {
		debug_msg ("Not a proper device");
	}
	return;

unlock_and_fail:
	if (o) {
		pa_operation_cancel(o);
		pa_operation_unref(o);
	}
	pa_threaded_mainloop_unlock(pulse_info->m);
#endif
}


static void aec_set_volume_changed_cb(keynode_t* node, void* data)
{
#ifdef _TIZEN_PUBLIC_
	return;
#else
	int key_value;
	pa_operation *o = NULL;
	pulse_info_t* pinfo = (pulse_info_t*)data;

	if (pinfo == NULL || pulse_info->aec_module_idx == PA_INVALID_INDEX) {
		debug_error("pinfo NULL in aec_set_volume_changed_cb function");
		return;
	}
	vconf_get_int(VCONF_KEY_VOLUME_TYPE_VOIP, &key_value);
	debug_msg ("%s changed callback called, key value = %d\n",vconf_keynode_get_name(node), key_value);

	pa_threaded_mainloop_lock(pinfo->m);
	CHECK_CONTEXT_DEAD_GOTO(pinfo->context, unlock_and_fail);

	debug_msg("[PA] pa_ext_echo_cancel_set_volume m[%p] c[%p] volume:%d", pinfo->m, pinfo->context, key_value);
	o = pa_ext_echo_cancel_set_volume(pinfo->context, key_value, aec_set_volume_cb, pinfo);
	CHECK_CONTEXT_SUCCESS_GOTO(pinfo->context, o, unlock_and_fail);
	while (pa_operation_get_state(o) == PA_OPERATION_RUNNING) {
		pa_threaded_mainloop_wait(pinfo->m);
		CHECK_CONTEXT_DEAD_GOTO(pinfo->context, unlock_and_fail);
	}
	pa_operation_unref(o);

	pa_threaded_mainloop_unlock(pinfo->m);
	return;

unlock_and_fail:
	if (o) {
		pa_operation_cancel(o);
		pa_operation_unref(o);
	}
	pa_threaded_mainloop_unlock(pinfo->m);
#endif
}


int MMSoundMgrPulseHandleRegisterAecSetVolume(void)
{
	int ret = vconf_notify_key_changed(VCONF_KEY_VOLUME_TYPE_VOIP, aec_set_volume_changed_cb, pulse_info);
	debug_msg ("vconf [%s] set ret = %d\n", VCONF_KEY_VOLUME_TYPE_VOIP, ret);
	return ret;
}

#else

static void aec_set_volume_cb (pa_context *c, int success, void *userdata)
{
	pulse_info_t *pinfo = (pulse_info_t *)userdata;
	if (pinfo == NULL) {
		debug_error ("pinfo is null");
		return;
	}

	if (success) {
		debug_msg("[PA_CB] m[%p] c[%p] AEC set volume success\n", pinfo->m, c);
	} else {
		debug_error("[PA_CB] m[%p] c[%p] AEC set volume fail:%s", pinfo->m, c, pa_strerror(pa_context_errno(c)));
	}

	pa_threaded_mainloop_signal(pinfo->m, 0);
}

static void aec_set_volume_nosignal_cb (pa_context *c, int success, void *userdata)
{
	pulse_info_t *pinfo = (pulse_info_t *)userdata;
	if (pinfo == NULL) {
		debug_error ("pinfo is null");
		return;
	}

	if (success) {
		debug_msg("[PA_CB] m[%p] c[%p] AEC set volume success\n", pinfo->m, c);
	} else {
		debug_error("[PA_CB] m[%p] c[%p] AEC set volume fail:%s", pinfo->m, c, pa_strerror(pa_context_errno(c)));
	}
}

void MMSoundMgrPulseHandleAecSetVolume(volume_type_t volume_type)
{
#ifdef _TIZEN_PUBLIC_
	return;
#else
	int value = 0;
	unsigned int volume_level = 0;
	pa_operation *o = NULL;

#ifndef TIZEN_MICRO
	if (pulse_info == NULL || pulse_info->aec_module_idx == PA_INVALID_INDEX) {
		debug_error ("Pulse module in sound server not loaded");
		return;
	}
#else
	if (pulse_info == NULL) {
		debug_error ("pulse_info is null");
		return;
	}
#endif

	_mm_sound_volume_get_value_by_type(volume_type, &volume_level);
	if (volume_level >= 14)
		value = 7;
	else if (volume_level >= 12)
		value = 6;
	else if (volume_level >= 10)
		value = 5;
	else if (volume_level >= 8)
		value = 4;
	else if (volume_level >= 6)
		value = 3;
	else if (volume_level >= 4)
		value = 2;
	else if (volume_level >= 2)
		value = 1;
	else
		value = 0;

	if (pa_threaded_mainloop_in_thread(pulse_info->m)) {
		o = pa_ext_echo_cancel_set_volume (pulse_info->context, value , aec_set_volume_nosignal_cb, pulse_info);
		pa_operation_unref(o);
	} else {
		pa_threaded_mainloop_lock(pulse_info->m);
		CHECK_CONTEXT_DEAD_GOTO(pulse_info->context, unlock_and_fail);

		debug_msg("[PA] pa_ext_echo_cancel_set_device m[%p] c[%p] device:%d", pulse_info->m, pulse_info->context, value);
		o = pa_ext_echo_cancel_set_volume (pulse_info->context, value , aec_set_volume_cb, pulse_info);
		CHECK_CONTEXT_SUCCESS_GOTO(pulse_info->context, o, unlock_and_fail);
		while (pa_operation_get_state(o) == PA_OPERATION_RUNNING) {
			pa_threaded_mainloop_wait(pulse_info->m);
			CHECK_CONTEXT_DEAD_GOTO(pulse_info->context, unlock_and_fail);
		}
		pa_operation_unref(o);
		pa_threaded_mainloop_unlock(pulse_info->m);
	}
	return;

unlock_and_fail:
	if (o) {
		pa_operation_cancel(o);
		pa_operation_unref(o);
	}
	pa_threaded_mainloop_unlock(pulse_info->m);
#endif
}
#endif


/* -------------------------------- MONO AUDIO --------------------------------------------*/
#ifdef SUPPORT_MONO_AUDIO
#define MONO_KEY VCONFKEY_SETAPPL_ACCESSIBILITY_MONO_AUDIO
#define TOUCH_SOUND_PLAY_COMLETE_TIME 50000

static void set_mono_cb (pa_context *c, int success, void *userdata)
{
	pulse_info_t *pinfo = (pulse_info_t *)userdata;
	if (pinfo == NULL) {
		debug_error ("pinfo is null");
		return;
	}

	if (success) {
		debug_msg ("[PA_CB] m[%p] c[%p] set mono success", pinfo->m, c);
	} else {
		debug_error("[PA_CB] m[%p] c[%p] set mono fail:%s", pinfo->m, c, pa_strerror(pa_context_errno(c)));
	}
	pa_threaded_mainloop_signal(pinfo->m, 0);
}

static void mono_changed_cb(keynode_t* node, void* data)
{
	int key_value;
	pa_operation *o = NULL;
	pulse_info_t* pinfo = (pulse_info_t*)data;

	if (pinfo == NULL) {
		debug_error ("pinfo is null");
		return;
	}

	vconf_get_bool(MONO_KEY, &key_value);
	debug_msg ("%s changed callback called, key value = %d\n",vconf_keynode_get_name(node), key_value);
	/*for make sure touch sound play complete*/
	usleep(TOUCH_SOUND_PLAY_COMLETE_TIME);

	pa_threaded_mainloop_lock(pinfo->m);
	CHECK_CONTEXT_DEAD_GOTO(pinfo->context, unlock_and_fail);

	debug_msg("[PA] pa_ext_policy_set_mono m[%p] c[%p] mono:%d", pinfo->m, pinfo->context, key_value);
	o = pa_ext_policy_set_mono (pinfo->context, key_value, set_mono_cb, pinfo);
	CHECK_CONTEXT_SUCCESS_GOTO(pinfo->context, o, unlock_and_fail);
	while (pa_operation_get_state(o) == PA_OPERATION_RUNNING) {
		pa_threaded_mainloop_wait(pinfo->m);
		CHECK_CONTEXT_DEAD_GOTO(pinfo->context, unlock_and_fail);
	}
	pa_operation_unref(o);

	pa_threaded_mainloop_unlock(pinfo->m);
	return;

unlock_and_fail:
	if (o) {
		pa_operation_cancel(o);
		pa_operation_unref(o);
	}
	pa_threaded_mainloop_unlock(pinfo->m);
}

int MMSoundMgrPulseHandleRegisterMonoAudio (void* pinfo)
{
	int ret = vconf_notify_key_changed(MONO_KEY, mono_changed_cb, pinfo);
	debug_msg ("vconf [%s] set ret = %d\n", MONO_KEY, ret);
	return ret;
}
#endif /* SUPPORT_MONO_AUDIO */

/* -------------------------------- Audio Balance --------------------------------------------*/
#ifdef SUPPORT_AUDIO_BALANCE
static void set_balance_cb (pa_context *c, int success, void *userdata)
{
	pulse_info_t *pinfo = (pulse_info_t *)userdata;
	if (pinfo == NULL) {
		debug_error ("pinfo is null");
		return;
	}

	if (success) {
		debug_msg ("[PA_CB] m[%p] c[%p] set balance success", pinfo->m, c);
	} else {
		debug_error("[PA_CB] m[%p] c[%p] set balance fail:%s", pinfo->m, c, pa_strerror(pa_context_errno(c)));
	}
	pa_threaded_mainloop_signal(pinfo->m, 0);
}

static void _balance_changed_cb(keynode_t* node, void* data)
{
	double balance_value;
	pulse_info_t* pinfo = (pulse_info_t*)data;
	pa_operation *o = NULL;

	if (pinfo == NULL) {
		debug_error ("pinfo is null");
		return;
	}

	vconf_get_dbl(VCONF_KEY_VOLUME_BALANCE, &balance_value);
	debug_msg ("%s changed callback called, balance value = %f\n",vconf_keynode_get_name(node), balance_value);

	pa_threaded_mainloop_lock(pinfo->m);
	CHECK_CONTEXT_DEAD_GOTO(pinfo->context, unlock_and_fail);

	debug_msg("[PA] pa_ext_policy_set_balance m[%p] c[%p] balance:%.2f", pinfo->m, pinfo->context, balance_value);
	o = pa_ext_policy_set_balance (pinfo->context, &balance_value, set_balance_cb, pinfo);
	CHECK_CONTEXT_SUCCESS_GOTO(pinfo->context, o, unlock_and_fail);
	while (pa_operation_get_state(o) == PA_OPERATION_RUNNING) {
		pa_threaded_mainloop_wait(pinfo->m);
		CHECK_CONTEXT_DEAD_GOTO(pinfo->context, unlock_and_fail);
	}
	pa_operation_unref(o);

	pa_threaded_mainloop_unlock(pinfo->m);
	return;

unlock_and_fail:
	if (o) {
		pa_operation_cancel(o);
		pa_operation_unref(o);
	}
	pa_threaded_mainloop_unlock(pinfo->m);
}

int MMSoundMgrPulseHandleRegisterAudioBalance (void* pinfo)
{
	int ret = vconf_notify_key_changed(VCONF_KEY_VOLUME_BALANCE, _balance_changed_cb, pinfo);
	debug_msg ("vconf [%s] set ret = %d\n", VCONF_KEY_VOLUME_BALANCE, ret);
	return ret;
}

void MMSoundMgrPulseHandleResetAudioBalanceOnBoot(void* data)
{
	double balance_value;
	pulse_info_t* pinfo = (pulse_info_t*)data;
	pa_operation *o = NULL;

	if (pinfo == NULL) {
		debug_error ("pinfo is null");
		return;
	}

	if (vconf_get_dbl(VCONF_KEY_VOLUME_BALANCE, &balance_value)) {
		debug_error ("vconf_get_dbl(VCONF_KEY_VOLUME_BALANCE) failed..\n");
		balance_value = 0;
		vconf_set_dbl(VCONF_KEY_VOLUME_BALANCE, balance_value);
	} else {
		pa_threaded_mainloop_lock(pinfo->m);
		CHECK_CONTEXT_DEAD_GOTO(pinfo->context, unlock_and_fail);

		debug_msg("[PA] pa_ext_policy_set_balance m[%p] c[%p] balance:%.2f", pinfo->m, pinfo->context, balance_value);
		o = pa_ext_policy_set_balance (pinfo->context, &balance_value, set_balance_cb, pinfo);
		CHECK_CONTEXT_SUCCESS_GOTO(pinfo->context, o, unlock_and_fail);
		while (pa_operation_get_state(o) == PA_OPERATION_RUNNING) {
			pa_threaded_mainloop_wait(pinfo->m);
			CHECK_CONTEXT_DEAD_GOTO(pinfo->context, unlock_and_fail);
		}
		pa_operation_unref(o);

		pa_threaded_mainloop_unlock(pinfo->m);
	}
	return;

unlock_and_fail:
	if (o) {
		pa_operation_cancel(o);
		pa_operation_unref(o);
	}
	pa_threaded_mainloop_unlock(pinfo->m);
}
#endif /* SUPPORT_AUDIO_BALANCE */


#ifdef SUPPORT_AUDIO_MUTEALL
static void set_muteall_cb (pa_context *c, int success, void *userdata)
{
	pulse_info_t *pinfo = (pulse_info_t *)userdata;
	if (pinfo == NULL) {
		debug_error ("pinfo is null");
		return;
	}

	if (success) {
		debug_msg ("[PA_CB] m[%p] c[%p] set muteall success", pinfo->m, c);
	} else {
		debug_error("[PA_CB] m[%p] c[%p] set muteall fail:%s", pinfo->m, c, pa_strerror(pa_context_errno(c)));
	}
	pa_threaded_mainloop_signal(pinfo->m, 0);
}

static void _muteall_changed_cb(keynode_t* node, void* data)
{
	int key_value;
	int i;
	pulse_info_t* pinfo = (pulse_info_t*)data;
	pa_operation *o = NULL;

	if (pinfo == NULL) {
		debug_error ("pinfo is null\n");
		return;
	}

	vconf_get_int(VCONF_KEY_MUTE_ALL, &key_value);
	debug_msg ("%s changed callback called, muteall value = %d\n", vconf_keynode_get_name(node), key_value);

	mm_sound_mute_all(key_value);

	pa_threaded_mainloop_lock(pinfo->m);
	CHECK_CONTEXT_DEAD_GOTO(pinfo->context, unlock_and_fail);

	debug_msg("[PA] pa_ext_policy_set_muteall m[%p] c[%p] muteall:%d", pinfo->m, pinfo->context, key_value);
	o = pa_ext_policy_set_muteall (pinfo->context, key_value, set_muteall_cb, pinfo);
	CHECK_CONTEXT_SUCCESS_GOTO(pinfo->context, o, unlock_and_fail);
	while (pa_operation_get_state(o) == PA_OPERATION_RUNNING) {
		pa_threaded_mainloop_wait(pinfo->m);
		CHECK_CONTEXT_DEAD_GOTO(pinfo->context, unlock_and_fail);
	}
	pa_operation_unref(o);

	pa_threaded_mainloop_unlock(pinfo->m);

	if (!key_value) {
		for (i =0;i<VOLUME_TYPE_MAX;i++) {
			unsigned int vconf_value;
			mm_sound_volume_get_value(i,&vconf_value);
			mm_sound_volume_set_value(i,vconf_value);
		}
	}
	return;

unlock_and_fail:
	if (o) {
		pa_operation_cancel(o);
		pa_operation_unref(o);
	}
	pa_threaded_mainloop_unlock(pinfo->m);
}

int MMSoundMgrPulseHandleRegisterAudioMuteall (void* pinfo)
{
	int ret = vconf_notify_key_changed(VCONF_KEY_MUTE_ALL, _muteall_changed_cb, pinfo);
	debug_msg ("vconf [%s] set ret = %d\n", VCONF_KEY_MUTE_ALL, ret);
	return ret;
}

void MMSoundMgrPulseHandleResetAudioMuteallOnBoot(void* data)
{
	int key_value;
	pulse_info_t* pinfo = (pulse_info_t*)data;
	pa_operation *o = NULL;

	if (pinfo == NULL) {
		debug_error ("pinfo is null\n");
		return;
	}

	if (vconf_get_int(VCONF_KEY_MUTE_ALL, &key_value)) {
		debug_error ("vconf_get_int(VCONF_KEY_MUTE_ALL) failed..\n");
		key_value = 0;
		vconf_set_int(VCONF_KEY_MUTE_ALL, key_value);
	} else {
		mm_sound_mute_all(key_value);

		pa_threaded_mainloop_lock(pinfo->m);
		CHECK_CONTEXT_DEAD_GOTO(pinfo->context, unlock_and_fail);

		debug_msg("[PA] pa_ext_policy_set_muteall m[%p] c[%p] muteall:%d", pinfo->m, pinfo->context, key_value);
		o = pa_ext_policy_set_muteall (pinfo->context, key_value, set_muteall_cb, pinfo);
		CHECK_CONTEXT_SUCCESS_GOTO(pinfo->context, o, unlock_and_fail);
		while (pa_operation_get_state(o) == PA_OPERATION_RUNNING) {
			pa_threaded_mainloop_wait(pinfo->m);
			CHECK_CONTEXT_DEAD_GOTO(pinfo->context, unlock_and_fail);
		}
		pa_operation_unref(o);

		pa_threaded_mainloop_unlock(pinfo->m);
	}
	return;

unlock_and_fail:
	if (o) {
		pa_operation_cancel(o);
		pa_operation_unref(o);
	}
	pa_threaded_mainloop_unlock(pinfo->m);
}
#endif /* SUPPORT_AUDIO_MUTEALL */



/* -------------------------------- BT SCO --------------------------------------------*/
#ifdef SUPPORT_BT_SCO_DETECT

#ifndef TIZEN_MICRO
static void __bt_audio_connection_state_changed_cb(int result,
					bool connected, const char *remote_address,
					bt_audio_profile_type_e type, void *user_data)
{
	/*	0 : BT_AUDIO_PROFILE_TYPE_ALL
		1 : BT_AUDIO_PROFILE_TYPE_HSP_HFP
		2 : BT_AUDIO_PROFILE_TYPE_A2DP	*/
	const static char * type_str[3] = { "ALL", "HSP/HFP", "A2DP" };

	if (connected) {
		debug_warning ("result[%d], connected[%d], address[%s], type=[%d][%s]",
				result, connected, remote_address, type, type_str[type]);
	} else {
		debug_warning ("result[%d], connected[%d]", result, connected);
	}

	/* Set SCO Device available */
	if (type == BT_AUDIO_PROFILE_TYPE_HSP_HFP) {
		MMSoundMgrSessionSetDeviceAvailable (DEVICE_BT_SCO, connected, 0, NULL);
	}
}

static void __bt_ag_sco_state_changed_cb(int result, bool opened, void *user_data)
{
	int ret = 0;
	bool is_nrec_enabled = 0;
	bool is_wb_enabled = 0;

	/* Check NREC enabled if opened */
	if (opened) {
		ret = bt_ag_is_nrec_enabled (&is_nrec_enabled);
		if (ret != BT_ERROR_NONE) {
			debug_error ("bt_ag_is_nrec_enabled error!!! [%d]", ret);
		}

		ret = bt_ag_is_wbs_mode (&is_wb_enabled);
		if (ret != BT_ERROR_NONE) {
			debug_error ("bt_ag_is_wbs_mode error!!! [%d]", ret);
		}
	}

	debug_warning ("result[%d], opened[%d], is_nrec_enabled[%d], is_wb_enabled[%d]\n",
			result, opened, is_nrec_enabled, is_wb_enabled);

	MMSoundMgrSessionSetSCO (opened, is_nrec_enabled, is_wb_enabled);
}

static int __bt_ag_initialize()
{
	int ret = 0;

	ret = bt_initialize();
	if (ret != BT_ERROR_NONE) {
		debug_error ("bt_initialize error!!! [%d]", ret);
		goto FAIL;
	}
	ret = bt_audio_initialize();
	if (ret != BT_ERROR_NONE) {
		debug_error ("bt_audio_initialize error!!! [%d]", ret);
		goto FAIL;
	}
	ret = bt_audio_set_connection_state_changed_cb(__bt_audio_connection_state_changed_cb, NULL);
	if (ret != BT_ERROR_NONE) {
		debug_error ("bt_audio_set_connection_state_changed_cb error!!! [%d]", ret);
		goto FAIL;
	}
	ret = bt_ag_set_sco_state_changed_cb(__bt_ag_sco_state_changed_cb, NULL);
	if (ret != BT_ERROR_NONE) {
		debug_error ("bt_ag_set_sco_state_changed_cb error!!! [%d]", ret);
		goto FAIL;
	}
FAIL:
	return ret;
}

static int __bt_ag_deinitialize()
{
	int ret = 0;

	ret = bt_audio_unset_connection_state_changed_cb();
	if (ret != BT_ERROR_NONE) {
		debug_error ("bt_audio_unset_connection_state_changed_cb error!!! [%d]", ret);
		goto FAIL;
	}
	ret = bt_ag_unset_sco_state_changed_cb();
	if (ret != BT_ERROR_NONE) {
		debug_error ("bt_ag_unset_sco_state_changed_cb error!!! [%d]", ret);
		goto FAIL;
	}
	ret = bt_audio_deinitialize();
	if (ret != BT_ERROR_NONE) {
		debug_error ("bt_audio_deinitialize error!!! [%d]", ret);
		goto FAIL;
	}
	ret = bt_deinitialize();
	if (ret != BT_ERROR_NONE) {
		debug_error ("bt_deinitialize error!!! [%d]", ret);
		goto FAIL;
	}

FAIL:
	return ret;
}

#else	/* TIZEN_MICRO */
static int _bt_hf_set_volume_by_client(const unsigned int vol)
{
	unsigned int Cilent_volume_table[CLIENT_VOL_MAX+1] = {1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5, 6};
	unsigned int value;
	int ret;
	session_t cur_session;

	debug_msg("volume value %d \n", vol);

	if(vol > CLIENT_VOL_MAX) {
		debug_error("Volume value is invalid %d\n", vol);
		value = CLIENT_VOL_MAX;
	} else {
		value = Cilent_volume_table[vol];
	}

	ret = MMSoundMgrSessionGetSession(&cur_session);
	if(ret)
		debug_warning("Fail to get current session");

	debug_msg("Volume Changed callback is called session [%d]", cur_session);

	if(cur_session == SESSION_VOICECALL) {
		g_bt_hf_volume_control = 1;
		_mm_sound_volume_set_value_by_type(VOLUME_TYPE_CALL, value);
		MMSoundMgrPulseSetVolumeLevel(VOLUME_TYPE_CALL, value);
	}
	else if(cur_session == SESSION_VOIP) {
		g_bt_hf_volume_control = 1;
		_mm_sound_volume_set_value_by_type(VOLUME_TYPE_VOIP, value);
		MMSoundMgrPulseSetVolumeLevel(VOLUME_TYPE_VOIP, value);
	}
	else
	{
		debug_warning("Invalid volume control cur session [%d]", cur_session);
	}

	return MM_ERROR_NONE;
}

void _bt_hf_cb (int event, bt_hf_event_param_t *event_param, void * user_data)
{
	unsigned int vol_value = 0;
	char str_message[256];
	int ret = 0;
	unsigned int codec_id;
	subsession_t sub = 0;

	if (event_param == NULL) {
		debug_critical("Param data is NULL\n");
		return;
	}

	sprintf (str_message, "HF event : [0x%04x], event_param : 0x%p, user_data : 0x%p", event, event_param, user_data);

	switch (event) {
	case BLUETOOTH_EVENT_HF_CONNECTED:
		/* get WB capability here and set */
		ret = bluetooth_hf_get_codec(&codec_id);
		if (ret == BLUETOOTH_ERROR_NONE) {
			switch (codec_id) {
			case BLUETOOTH_CODEC_ID_CVSD: /* NB */
				g_bt_hf_sco_nb = 1;
				MMSoundMgrSessionSetSCOBTWB (false);
				break;
			case BLUETOOTH_CODEC_ID_MSBC: /* WB */
				g_bt_hf_sco_nb = 0;
				MMSoundMgrSessionSetSCOBTWB (true);
				break;
			default:
				break;
			}
		}
		debug_msg("%s >> %s", str_message, "BLUETOOTH_EVENT_HF_CONNECTED");
		break;

	case BLUETOOTH_EVENT_HF_DISCONNECTED:
		debug_msg("%s >> %s", str_message, "BLUETOOTH_EVENT_HF_DISCONNECTED");
		break;

	case BLUETOOTH_EVENT_HF_CALL_STATUS:
		{
			bt_hf_call_list_s * call_list = event_param->param_data;
			bt_hf_call_status_info_t **call_info;

			if (call_list == NULL) {
				debug_error("call_list is NULL");
				break;
			}

			debug_msg("%s >> %s : call_list_count = %d", str_message, "BLUETOOTH_EVENT_HF_CALL_STATUS",  call_list->count);

			if (call_list->count > 0) {
				call_info = g_malloc0(sizeof(bt_hf_call_status_info_t *) * call_list->count);
				if (call_info) {
					if (bluetooth_hf_get_call_list(call_list->list, call_info) == BLUETOOTH_ERROR_NONE) {
						/* Note:  status=2 (MO Dialing), 3 (MO Alerting) */
						if (call_list->count == 1 && (call_info[0]->status == 2 || call_info[0]->status == 3)) {
							debug_msg("Call status : %d ", call_info[0]->status);

							/* Do pause here */
							MMSoundMgrSessionMediaPause();
						}
					}

					g_free(call_info);
					call_info = NULL;
				} else {
					debug_error("call_info is NULL");
				}
			}
		}
		break;

	case BLUETOOTH_EVENT_HF_AUDIO_CONNECTED:
		ret = MMSoundMgrSessionGetSubSession(&sub);

		/* get WB capability here and set */
		ret = bluetooth_hf_get_codec(&codec_id);
		if (ret == BLUETOOTH_ERROR_NONE) {
			switch (codec_id) {
			case BLUETOOTH_CODEC_ID_CVSD: /* NB */
				MMSoundMgrSessionSetSCOBTWB (false);
				debug_msg("Previous band NB %d",g_bt_hf_sco_nb);
				if(!g_bt_hf_sco_nb && sub == SUBSESSION_VOICE ) {
					ret = MMSoundMgrSessionSetDuplicateSubSession();
					if(!ret)
						debug_warning("Fail to set duplicate sub session");
				}
				g_bt_hf_sco_nb = 1;
				break;
			case BLUETOOTH_CODEC_ID_MSBC: /* WB */
				MMSoundMgrSessionSetSCOBTWB (true);
				debug_msg("Previous band NB %d",g_bt_hf_sco_nb);
				if(g_bt_hf_sco_nb && sub == SUBSESSION_VOICE) {
					ret = MMSoundMgrSessionSetDuplicateSubSession();
					if(!ret)
						debug_warning("Fail to set duplicate sub session");
				}
				g_bt_hf_sco_nb = 0;
				break;
			default:
				break;
			}
		}
		debug_msg("%s >> %s : codec_id=%d(NB:1, WB:2)", str_message, "BLUETOOTH_EVENT_HF_AUDIO_CONNECTED", codec_id);
		break;

	case BLUETOOTH_EVENT_HF_AUDIO_DISCONNECTED:
		debug_msg("%s >> %s", str_message, "BLUETOOTH_EVENT_HF_AUDIO_DISCONNECTED");
		break;

	case BLUETOOTH_EVENT_HF_RING_INDICATOR:
		debug_msg("%s >> %s : %s", str_message, "BLUETOOTH_EVENT_HF_RING_INDICATOR",
				(event_param->param_data)? event_param->param_data : "NULL");
		break;

	case BLUETOOTH_EVENT_HF_CALL_TERMINATED:
		debug_msg("%s >> %s", str_message, "BLUETOOTH_EVENT_HF_CALL_TERMINATED");
		break;

	case BLUETOOTH_EVENT_HF_CALL_STARTED:
		debug_msg("%s >> %s", str_message, "BLUETOOTH_EVENT_HF_CALL_STARTED");
		strcat (str_message, "");
		break;

	case BLUETOOTH_EVENT_HF_CALL_ENDED:
		debug_msg("%s >> %s", str_message, "BLUETOOTH_EVENT_HF_CALL_ENDED");
		break;

	case BLUETOOTH_EVENT_HF_VOICE_RECOGNITION_ENABLED:
		debug_msg("%s >> %s", str_message, "BLUETOOTH_EVENT_HF_VOICE_RECOGNITION_ENABLED");
		/* BT connection signal before audio connected */
		break;

	case BLUETOOTH_EVENT_HF_VOICE_RECOGNITION_DISABLED:
		debug_msg("%s >> %s", str_message, "BLUETOOTH_EVENT_HF_VOICE_RECOGNITION_DISABLED");
		break;

	case BLUETOOTH_EVENT_HF_VOLUME_SPEAKER:
		vol_value = *(unsigned int *)(event_param->param_data);
		debug_msg("%s >> %s : %d", str_message, "BLUETOOTH_EVENT_HF_VOLUME_SPEAKER", vol_value);
		_bt_hf_set_volume_by_client(vol_value);
		break;

	default:
		debug_log("%s >> %s", str_message, "Unhandled event...");
		break;
	}
}

void _speaker_volume_changed_cb()
{
	unsigned int value, vconf_value = 0;
	int  ret = 0;
	session_t cur_session = 0;
	unsigned int volume_table[SPK_VOL_MAX+1] = {0, 0, 3, 6, 9, 12, 15};

	if(g_bt_hf_volume_control) {
		debug_msg("In case of changed volume by client");
		g_bt_hf_volume_control = 0;
		return;
	}

	/* In case of changing vconf key call/voip volume by pulse audio */
	ret = MMSoundMgrSessionGetSession(&cur_session);
	if(ret)
		debug_warning("Fail to get current session");

	debug_msg("Volume Changed callback is called session [%d]", cur_session);
	if(cur_session == SESSION_VOICECALL)
		ret = _mm_sound_volume_get_value_by_type(VOLUME_TYPE_CALL, &vconf_value);
	else if(cur_session == SESSION_VOIP)
		ret = _mm_sound_volume_get_value_by_type(VOLUME_TYPE_VOIP, &vconf_value);
	else
		debug_warning("Invalid volume control by [%d] session", cur_session);

	if(ret)
		debug_warning("Fail to get volume value %x", ret);

	value = vconf_value;
	if(value > SPK_VOL_MAX) value = SPK_VOL_MAX;
	else if(value < 0) value = 0;

	/* Set in case of control by application */
	debug_msg("Set volume by application");
	ret = bluetooth_hf_set_speaker_gain(volume_table[value]);
	if(ret)
		debug_warning("Set bt hf speaker gain failed %x", ret);

	debug_msg("volume_get_value %d vol value %d", value, volume_table[value]);
}

static int __bt_hf_initialize()
{
	int ret = 0;

	debug_error ("__bt_hf_initialize start");
	ret = bluetooth_hf_init(_bt_hf_cb, NULL);
	if (ret != BLUETOOTH_ERROR_NONE) {
		debug_error ("bluetooth_hf_init error!!! [%d]", ret);
		goto FAIL;
	}

	if(vconf_notify_key_changed(VCONF_KEY_VOLUME_TYPE_CALL, _speaker_volume_changed_cb, NULL)) {
		debug_error("vconf_notify_key_changed fail\n");
	}

	if(vconf_notify_key_changed(VCONF_KEY_VOLUME_TYPE_VOIP, _speaker_volume_changed_cb, NULL)) {
		debug_error("vconf_notify_key_changed fail\n");
	}

	debug_error ("__bt_hf_initialize success");

FAIL:
	return ret;
}

static int __bt_hf_deinitialize()
{
	int ret = 0;

	debug_error ("__bt_hf_deinitialize start");
	ret = bluetooth_hf_deinit();
	if (ret != BLUETOOTH_ERROR_NONE) {
		debug_error ("bluetooth_hf_deinit error!!! [%d]", ret);
		goto FAIL;
	}

	if(vconf_ignore_key_changed(VCONF_KEY_VOLUME_TYPE_CALL, _speaker_volume_changed_cb)) {
		debug_error("vconf_ignore_key_changed fail\n");
	}

	if(vconf_ignore_key_changed(VCONF_KEY_VOLUME_TYPE_VOIP, _speaker_volume_changed_cb)) {
		debug_error("vconf_ignore_key_changed fail\n");
	}

	debug_error ("__bt_hf_deinitialize success");
FAIL:
		return ret;
}

#endif	/* TIZEN_MICRO */

static void bt_changed_cb(keynode_t* node, void* data)
{
	pulse_info_t* pinfo = (pulse_info_t*)data;
	int bt_status = 0;

	if (pinfo == NULL) {
		debug_error ("pinfo is null\n");
		return;
	}

	/* Get actual vconf value */
	vconf_get_int(VCONF_BT_STATUS, &bt_status);
	debug_warning ("[%s] changed callback called, status=0x%x, sco_init = %d\n", vconf_keynode_get_name(node), bt_status, pinfo->is_sco_init);

	/* Init/Deinit BT AG */
	if (bt_status && pinfo->is_sco_init == false) {
#ifndef TIZEN_MICRO
		if (__bt_ag_initialize() == 0) {
			pinfo->is_sco_init = true;
		}
#else
		if (!__bt_hf_initialize()) {
			pinfo->is_sco_init = true;
		}
#endif
	} else if (bt_status == 0 && pinfo->is_sco_init == true) {
#ifndef TIZEN_MICRO
		if (__bt_ag_deinitialize() == 0) {
			pinfo->is_sco_init = false;
		}
#else
		if (!__bt_hf_deinitialize()) {
			pinfo->is_sco_init = false;
		}
#endif
	}
}

int MMSoundMgrPulseHandleRegisterBluetoothStatus (void* pinfo)
{
	int ret = 0;

	if (pinfo == NULL) {
		debug_error ("pinfo is null\n");
		return MM_ERROR_INVALID_ARGUMENT;
	}

	/* set callback for vconf key change */
	ret = vconf_notify_key_changed(VCONF_BT_STATUS, bt_changed_cb, pinfo);
	debug_msg ("vconf [%s] set ret = %d\n", VCONF_BT_STATUS, ret);

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
	strncpy (respmsg.sound_msg.filename, bt_name, sizeof(respmsg.sound_msg.filename)-1);

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

static void set_default_sink_cb(pa_context *c, int success, void *userdata)
{
	pulse_info_t *pinfo = (pulse_info_t *)userdata;
	if (pinfo == NULL) {
		debug_error ("pinfo is null");
		return;
	}

	if (success) {
		debug_msg("[PA_CB] m[%p] c[%p] set default sink success\n", pinfo->m, c);
	} else {
		debug_error("[PA_CB] m[%p] c[%p] set default sink fail:%s", pinfo->m, c, pa_strerror(pa_context_errno(c)));
	}
	pa_threaded_mainloop_signal(pinfo->m, 0);
}

static void set_default_sink_nosignal_cb(pa_context *c, int success, void *userdata)
{
	pulse_info_t *pinfo = (pulse_info_t *)userdata;
	if (pinfo == NULL) {
		debug_error ("pinfo is null");
		return;
	}

	if (success) {
		debug_msg("[PA_CB] m[%p] c[%p] set default sink success\n", pinfo->m, c);
	} else {
		debug_error("[PA_CB] m[%p] c[%p] set default sink fail:%s", pinfo->m, c, pa_strerror(pa_context_errno(c)));
	}
}

static void set_cork_all_cb(pa_context *c, int success, void *userdata)
{
	pulse_info_t *pinfo = (pulse_info_t *)userdata;
	if (pinfo == NULL) {
		debug_error ("pinfo is null");
		return;
	}

	if (success) {
		debug_msg("[PA_CB] m[%p] c[%p] set cork all success\n", pinfo->m, c);
	} else {
		debug_error("[PA_CB] m[%p] c[%p] set cork all fail:%s", pinfo->m, c, pa_strerror(pa_context_errno(c)));
	}
	pa_threaded_mainloop_signal(pinfo->m, 0);
}

static void set_cork_all_nosignal_cb(pa_context *c, int success, void *userdata)
{
	pulse_info_t *pinfo = (pulse_info_t *)userdata;
	if (pinfo == NULL) {
		debug_error ("pinfo is null");
		return;
	}

	if (success) {
		debug_msg("[PA_CB] m[%p] c[%p] set cork all success\n", pinfo->m, c);
	} else {
		debug_error("[PA_CB] m[%p] c[%p] set cork all fail:%s", pinfo->m, c, pa_strerror(pa_context_errno(c)));
	}
}

void MMSoundMgrPulseSetUSBDefaultSink (int usb_device)
{
	pa_operation *o = NULL;

	debug_enter("\n");

	if (pa_threaded_mainloop_in_thread(pulse_info->m)) {
		o = pa_context_set_default_sink_for_usb(pulse_info->context,
				(usb_device == MM_SOUND_DEVICE_OUT_USB_AUDIO)? pulse_info->usb_sink_name : pulse_info->dock_sink_name,
				set_default_sink_nosignal_cb, pulse_info);
		pa_operation_unref(o);
	} else {
		pa_threaded_mainloop_lock(pulse_info->m);
		CHECK_CONTEXT_DEAD_GOTO(pulse_info->context, unlock_and_fail);

		debug_msg("[PA] pa_context_set_default_sink m[%p] c[%p]", pulse_info->m, pulse_info->context);
		o = pa_context_set_default_sink_for_usb(pulse_info->context,
				(usb_device == MM_SOUND_DEVICE_OUT_USB_AUDIO)? pulse_info->usb_sink_name : pulse_info->dock_sink_name,
				set_default_sink_cb, pulse_info);
		CHECK_CONTEXT_SUCCESS_GOTO(pulse_info->context, o, unlock_and_fail);
		while (pa_operation_get_state(o) == PA_OPERATION_RUNNING) {
			pa_threaded_mainloop_wait(pulse_info->m);
			CHECK_CONTEXT_DEAD_GOTO(pulse_info->context, unlock_and_fail);
		}
		pa_operation_unref(o);

		pa_threaded_mainloop_unlock(pulse_info->m);
	}

	debug_leave("\n");
	return;

unlock_and_fail:
	if (o) {
		pa_operation_cancel(o);
		pa_operation_unref(o);
	}
	pa_threaded_mainloop_unlock(pulse_info->m);

	debug_leave("\n");
}

void MMSoundMgrPulseSetDefaultSink (char* device_api_name, char* device_bus_name)
{
	pa_operation *o = NULL;

	debug_enter("\n");

	if (device_api_name == NULL || device_bus_name == NULL) {
		debug_error ("one of string is null\n");
		return;
	}

	strcpy (pulse_info->device_api_name, device_api_name);
	strcpy (pulse_info->device_bus_name, device_bus_name);

	if (pa_threaded_mainloop_in_thread(pulse_info->m)) {
		o = pa_context_set_default_sink_by_api_bus(pulse_info->context, pulse_info->device_api_name, pulse_info->device_bus_name, set_default_sink_nosignal_cb, pulse_info);
		pa_operation_unref(o);
	} else {
		pa_threaded_mainloop_lock(pulse_info->m);
		CHECK_CONTEXT_DEAD_GOTO(pulse_info->context, unlock_and_fail);

		debug_msg("[PA] pa_context_set_default_sink_by_api_bus m[%p] c[%p]", pulse_info->m, pulse_info->context);
		o = pa_context_set_default_sink_by_api_bus(pulse_info->context, pulse_info->device_api_name, pulse_info->device_bus_name, set_default_sink_cb, pulse_info);
		CHECK_CONTEXT_SUCCESS_GOTO(pulse_info->context, o, unlock_and_fail);
		while (pa_operation_get_state(o) == PA_OPERATION_RUNNING) {
			pa_threaded_mainloop_wait(pulse_info->m);
			CHECK_CONTEXT_DEAD_GOTO(pulse_info->context, unlock_and_fail);
		}
		pa_operation_unref(o);

		pa_threaded_mainloop_unlock(pulse_info->m);
	}
	debug_leave("\n");
	return;

unlock_and_fail:
	if (o) {
		pa_operation_cancel(o);
		pa_operation_unref(o);
	}
	pa_threaded_mainloop_unlock(pulse_info->m);
	debug_leave("\n");
}


void MMSoundMgrPulseSetDefaultSinkByName (char* name)
{
	pa_operation *o = NULL;

	debug_enter("\n");

	if (!name) {
		debug_error ("Invalid param\n");
		return;
	}

	if (pa_threaded_mainloop_in_thread(pulse_info->m)) {
		o = pa_context_set_default_sink(pulse_info->context, name, set_default_sink_cb, pulse_info);
		pa_operation_unref(o);
	} else {
		pa_threaded_mainloop_lock(pulse_info->m);
		CHECK_CONTEXT_DEAD_GOTO(pulse_info->context, unlock_and_fail);

		debug_msg("[PA] MMSoundMgrPulseSetDefaultSinkByName m[%p] c[%p]", pulse_info->m, pulse_info->context);
		o = pa_context_set_default_sink(pulse_info->context, name, set_default_sink_cb, pulse_info);
		CHECK_CONTEXT_SUCCESS_GOTO(pulse_info->context, o, unlock_and_fail);
		while (pa_operation_get_state(o) == PA_OPERATION_RUNNING) {
			pa_threaded_mainloop_wait(pulse_info->m);
			CHECK_CONTEXT_DEAD_GOTO(pulse_info->context, unlock_and_fail);
		}
		pa_operation_unref(o);

		pa_threaded_mainloop_unlock(pulse_info->m);
	}
	debug_leave("\n");
	return;

unlock_and_fail:
	if (o) {
		pa_operation_cancel(o);
		pa_operation_unref(o);
	}
	pa_threaded_mainloop_unlock(pulse_info->m);
	debug_leave("\n");
}

void MMSoundMgrPulseSetCorkAll (bool cork)
{
	pa_operation *o = NULL;

	debug_enter("\n");

	if (pa_threaded_mainloop_in_thread(pulse_info->m)) {
		o = pa_context_set_cork_all(pulse_info->context, cork, set_cork_all_nosignal_cb, pulse_info);
		pa_operation_unref(o);
	} else {
		pa_threaded_mainloop_lock(pulse_info->m);
		CHECK_CONTEXT_DEAD_GOTO(pulse_info->context, unlock_and_fail);

		debug_msg("[PA] MMSoundMgrPulseSetCorkAll m[%p] c[%p]", pulse_info->m, pulse_info->context);
		o = pa_context_set_cork_all(pulse_info->context, cork, set_cork_all_cb, pulse_info);
		CHECK_CONTEXT_SUCCESS_GOTO(pulse_info->context, o, unlock_and_fail);
		while (pa_operation_get_state(o) == PA_OPERATION_RUNNING) {
			pa_threaded_mainloop_wait(pulse_info->m);
			CHECK_CONTEXT_DEAD_GOTO(pulse_info->context, unlock_and_fail);
		}
		pa_operation_unref(o);

		pa_threaded_mainloop_unlock(pulse_info->m);
	}
	debug_leave("\n");
	return;

unlock_and_fail:
	if (o) {
		pa_operation_cancel(o);
		pa_operation_unref(o);
	}
	pa_threaded_mainloop_unlock(pulse_info->m);
	debug_leave("\n");
}

void MMSoundMgrPulseUnLoadHDMI()
{
	pa_operation *o = NULL;
	if (pulse_info == NULL) {
		debug_error ("Pulse module in sound server not loaded");
		return;
	}

	pa_threaded_mainloop_lock(pulse_info->m);
	CHECK_CONTEXT_DEAD_GOTO(pulse_info->context, unlock_and_fail);

	debug_msg("[PA] pa_context_unload_hdmi m[%p] c[%p]", pulse_info->m, pulse_info->context);
	o = pa_ext_policy_unload_hdmi(pulse_info->context, unload_hdmi_cb, pulse_info);
	CHECK_CONTEXT_SUCCESS_GOTO(pulse_info->context, o, unlock_and_fail);
	while (pa_operation_get_state(o) == PA_OPERATION_RUNNING) {
		pa_threaded_mainloop_wait(pulse_info->m);
		CHECK_CONTEXT_DEAD_GOTO(pulse_info->context, unlock_and_fail);
	}
	pa_operation_unref(o);

	pa_threaded_mainloop_unlock(pulse_info->m);
	return;

unlock_and_fail:
	if (o) {
		pa_operation_cancel(o);
		pa_operation_unref(o);
	}
	pa_threaded_mainloop_unlock(pulse_info->m);
}

#if 0
static void set_source_mute_by_name_cb (pa_context *c, int success, void *userdata)
{
	pulse_info_t *pinfo = (pulse_info_t *)userdata;
	if (pinfo == NULL) {
		debug_error ("pinfo is null");
		return;
	}

	if (success) {
		debug_msg("[PA_CB] m[%p] c[%p] set source mute by name success\n", pinfo->m, c);
	} else {
		debug_error("[PA_CB] m[%p] c[%p] set source mute by name fail:%s", pinfo->m, c, pa_strerror(pa_context_errno(c)));
	}
	pa_threaded_mainloop_signal(pinfo->m, 0);
}

static void set_source_mute_by_name_nosignal_cb (pa_context *c, int success, void *userdata)
{
	pulse_info_t *pinfo = (pulse_info_t *)userdata;
	if (pinfo == NULL) {
		debug_error ("pinfo is null");
		return;
	}

	if (success) {
		debug_msg("[PA_CB] m[%p] c[%p] set source mute by name success\n", pinfo->m, c);
	} else {
		debug_error("[PA_CB] m[%p] c[%p] set source mute by name fail:%s", pinfo->m, c, pa_strerror(pa_context_errno(c)));
	}
}
#endif

void MMSoundMgrPulseSetSourcemutebyname (char* sourcename, int mute)
{
#if 0
	pa_operation *o = NULL;

	debug_enter("\n");

	if (sourcename == NULL) {
		debug_error ("Invalid arguments!!!\n");
		return;
	}

	if (pa_threaded_mainloop_in_thread(pulse_info->m)) {
		o = pa_context_set_source_mute_by_name(pulse_info->context, sourcename, mute, set_source_mute_by_name_nosignal_cb, pulse_info);
		pa_operation_unref(o);
	} else {
		pa_threaded_mainloop_lock(pulse_info->m);
		CHECK_CONTEXT_DEAD_GOTO(pulse_info->context, unlock_and_fail);

		debug_msg("[PA] pa_context_set_source_mute_by_name m[%p] c[%p] name:%s mute:%d", pulse_info->m, pulse_info->context, sourcename, mute);
		o = pa_context_set_source_mute_by_name (pulse_info->context, sourcename, mute, set_source_mute_by_name_cb, pulse_info);
		CHECK_CONTEXT_SUCCESS_GOTO(pulse_info->context, o, unlock_and_fail);
		while (pa_operation_get_state(o) == PA_OPERATION_RUNNING) {
			pa_threaded_mainloop_wait(pulse_info->m);
			CHECK_CONTEXT_DEAD_GOTO(pulse_info->context, unlock_and_fail);
		}
		pa_operation_unref(o);

		pa_threaded_mainloop_unlock(pulse_info->m);
	}
	debug_leave("\n");
	return;

unlock_and_fail:
	if (o) {
		pa_operation_cancel(o);
		pa_operation_unref(o);
	}
	pa_threaded_mainloop_unlock(pulse_info->m);

	debug_leave("\n");
#endif
}

void MMSoundMgrPulseGetInitialBTStatus (bool *a2dp, bool *sco)
{
	int bt_status = VCONFKEY_BT_DEVICE_NONE;

	if (a2dp == NULL || sco == NULL) {
		debug_error ("Invalid arguments!!!\n");
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

static void set_session_cb(pa_context *c, int success, void *userdata)
{
	pulse_info_t *pinfo = (pulse_info_t *)userdata;
	if (pinfo == NULL) {
		debug_error ("pinfo is null");
		return;
	}

	if (success) {
		debug_msg ("[PA_CB] m[%p] c[%p] set session success", pinfo->m, c);
	} else {
		debug_error("[PA_CB] m[%p] c[%p] set session fail:%s", pinfo->m, c, pa_strerror(pa_context_errno(c)));
	}

	pa_threaded_mainloop_signal(pinfo->m, 0);
}

static void set_session_nosignal_cb(pa_context *c, int success, void *userdata)
{
	pulse_info_t *pinfo = (pulse_info_t *)userdata;
	if (pinfo == NULL) {
		debug_error ("pinfo is null");
		return;
	}

	if (success) {
		debug_msg ("[PA_CB] m[%p] c[%p] set session success", pinfo->m, c);
	} else {
		debug_error("[PA_CB] m[%p] c[%p] set session fail:%s", pinfo->m, c, pa_strerror(pa_context_errno(c)));
	}
}

void MMSoundMgrPulseSetSession(session_t session, session_state_t state)
{
	pa_operation *o = NULL;
	uint32_t session_pa = 0;

	/* convert subsession enum for PA */
	switch (session) {
	case SESSION_MEDIA:					session_pa = PA_TIZEN_SESSION_MEDIA;			break;
	case SESSION_VOICECALL:				session_pa = PA_TIZEN_SESSION_VOICECALL;		break;
	case SESSION_VIDEOCALL:				session_pa = PA_TIZEN_SESSION_VIDEOCALL;		break;
	case SESSION_VOIP:					session_pa = PA_TIZEN_SESSION_VOIP;				break;
	case SESSION_FMRADIO:				session_pa = PA_TIZEN_SESSION_FMRADIO;			break;
	case SESSION_NOTIFICATION:			session_pa = PA_TIZEN_SESSION_NOTIFICATION;		break;
	case SESSION_ALARM:					session_pa = PA_TIZEN_SESSION_ALARM;			break;
	case SESSION_EMERGENCY:				session_pa = PA_TIZEN_SESSION_EMERGENCY;		break;
	case SESSION_VOICE_RECOGNITION:		session_pa = PA_TIZEN_SESSION_VOICE_RECOGNITION;break;
	default:
		debug_error("inavlid session:%d", session);
		return;
	}

	if (pa_threaded_mainloop_in_thread(pulse_info->m)) {
		o = pa_ext_policy_set_session(pulse_info->context, session_pa, state, set_session_nosignal_cb, pulse_info);
		pa_operation_unref(o);
	} else {
		pa_threaded_mainloop_lock(pulse_info->m);
		CHECK_CONTEXT_DEAD_GOTO(pulse_info->context, unlock_and_fail);

		debug_msg("[PA] pa_ext_policy_set_session m[%p] c[%p] session:%d state:%d", pulse_info->m, pulse_info->context, session_pa, state);
		o = pa_ext_policy_set_session(pulse_info->context, session_pa, state, set_session_cb, pulse_info);
		CHECK_CONTEXT_SUCCESS_GOTO(pulse_info->context, o, unlock_and_fail);
		while (pa_operation_get_state(o) == PA_OPERATION_RUNNING) {
			pa_threaded_mainloop_wait(pulse_info->m);
			CHECK_CONTEXT_DEAD_GOTO(pulse_info->context, unlock_and_fail);
		}
		pa_operation_unref(o);
		pa_threaded_mainloop_unlock(pulse_info->m);
	}
	return;

unlock_and_fail:
	if (o) {
		pa_operation_cancel(o);
		pa_operation_unref(o);
	}
	pa_threaded_mainloop_unlock(pulse_info->m);
}

static void set_subsession_cb(pa_context *c, int success, void *userdata)
{
	pulse_info_t *pinfo = (pulse_info_t *)userdata;
	if (pinfo == NULL) {
		debug_error ("pinfo is null");
		return;
	}

	if (success) {
		debug_msg ("[PA_CB] m[%p] c[%p] set subsession success", pinfo->m, c);
	} else {
		debug_error("[PA_CB] m[%p] c[%p] set subsession fail:%s", pinfo->m, c, pa_strerror(pa_context_errno(c)));
	}

	pa_threaded_mainloop_signal(pinfo->m, 0);
}

static void set_subsession_nosignal_cb(pa_context *c, int success, void *userdata)
{
	pulse_info_t *pinfo = (pulse_info_t *)userdata;
	if (pinfo == NULL) {
		debug_error ("pinfo is null");
		return;
	}

	if (success) {
		debug_msg ("[PA_CB] m[%p] c[%p] set subsession success", pinfo->m, c);
	} else {
		debug_error("[PA_CB] m[%p] c[%p] set subsession fail:%s", pinfo->m, c, pa_strerror(pa_context_errno(c)));
	}
}

void MMSoundMgrPulseSetSubsession(subsession_t subsession, int subsession_opt)
{
	pa_operation *o = NULL;
	uint32_t subsession_pa = 0, subsession_opt_pa = 0;

	/* convert subsession enum for PA */
	switch (subsession) {
	case SUBSESSION_VOICE:				subsession_pa = PA_TIZEN_SUBSESSION_VOICE;		break;
	case SUBSESSION_RINGTONE:			subsession_pa = PA_TIZEN_SUBSESSION_RINGTONE;	break;
	case SUBSESSION_MEDIA:				subsession_pa = PA_TIZEN_SUBSESSION_MEDIA;		break;
	case SUBSESSION_INIT:				subsession_pa = PA_TIZEN_SUBSESSION_VR_INIT;	break;
	case SUBSESSION_VR_NORMAL:			subsession_pa = PA_TIZEN_SUBSESSION_VR_NORMAL;	break;
	case SUBSESSION_VR_DRIVE:			subsession_pa = PA_TIZEN_SUBSESSION_VR_DRIVE;	break;
#ifndef _TIZEN_PUBLIC_
	case SUBSESSION_RECORD_MONO:
	case SUBSESSION_RECORD_STEREO:
	case SUBSESSION_RECORD_STEREO_FOR_INTERVIEW:
	case SUBSESSION_RECORD_STEREO_FOR_CONVERSATION:	subsession_pa = PA_TIZEN_SUBSESSION_STEREO_REC;	break; /* should be modified with pulseaudio*/
	case SUBSESSION_VOICE_ANSWER_PLAY:	subsession_pa = PA_TIZEN_SUBSESSION_AM_PLAY;	break;
	case SUBSESSION_VOICE_ANSWER_REC:	subsession_pa = PA_TIZEN_SUBSESSION_AM_REC;		break;
#endif
	default:
		debug_error("inavlid subsession:%d", subsession);
		return;
	}

	/* convert subsession_opt enum for PA */
	if (subsession_opt & MM_SUBSESSION_OPTION_SVOICE)
		subsession_opt_pa |= PA_TIZEN_SUBSESSION_OPT_SVOICE;
	if (subsession_opt & MM_SUBSESSION_OPTION_PRIV_SVOICE_WAKEUP)
		subsession_opt_pa |= PA_TIZEN_SUBSESSION_OPT_WAKEUP;
	if (subsession_opt & MM_SUBSESSION_OPTION_PRIV_SVOICE_COMMAND)
		subsession_opt_pa |= PA_TIZEN_SUBSESSION_OPT_COMMAND;

	if (pa_threaded_mainloop_in_thread(pulse_info->m)) {
		o = pa_ext_policy_set_subsession(pulse_info->context, subsession_pa, subsession_opt_pa, set_subsession_nosignal_cb, pulse_info);
		pa_operation_unref(o);
	} else {
		pa_threaded_mainloop_lock(pulse_info->m);
		CHECK_CONTEXT_DEAD_GOTO(pulse_info->context, unlock_and_fail);

		debug_msg("[PA] pa_ext_policy_set_session m[%p] c[%p] subsession:%d opt:%x", pulse_info->m, pulse_info->context, subsession_pa, subsession_opt);
		o = pa_ext_policy_set_subsession(pulse_info->context, subsession_pa, subsession_opt_pa, set_subsession_cb, pulse_info);
		CHECK_CONTEXT_SUCCESS_GOTO(pulse_info->context, o, unlock_and_fail);
		while (pa_operation_get_state(o) == PA_OPERATION_RUNNING) {
			pa_threaded_mainloop_wait(pulse_info->m);
			CHECK_CONTEXT_DEAD_GOTO(pulse_info->context, unlock_and_fail);
		}
		pa_operation_unref(o);
		pa_threaded_mainloop_unlock(pulse_info->m);
	}
	return;

unlock_and_fail:
	if (o) {
		pa_operation_cancel(o);
		pa_operation_unref(o);
	}
	pa_threaded_mainloop_unlock(pulse_info->m);
}

static void set_active_device_cb(pa_context *c, int success, void *userdata)
{
	pulse_info_t *pinfo = (pulse_info_t *)userdata;
	if (pinfo == NULL) {
		debug_error ("pinfo is null");
		return;
	}

	if (success) {
		debug_msg ("[PA_CB] m[%p] c[%p] set active device success", pinfo->m, c);
	} else {
		debug_error("[PA_CB] m[%p] c[%p] set active device fail:%s", pinfo->m, c, pa_strerror(pa_context_errno(c)));
	}

	pa_threaded_mainloop_signal(pinfo->m, 0);
}

static void set_active_device_nosignal_cb(pa_context *c, int success, void *userdata)
{
	pulse_info_t *pinfo = (pulse_info_t *)userdata;
	if (pinfo == NULL) {
		debug_error ("pinfo is null");
		return;
	}

	if (success) {
		debug_msg ("[PA_CB] m[%p] c[%p] set active device success", pinfo->m, c);
	} else {
		debug_error("[PA_CB] m[%p] c[%p] set active device fail:%s", pinfo->m, c, pa_strerror(pa_context_errno(c)));
	}
}

void MMSoundMgrPulseSetActiveDevice(mm_sound_device_in device_in, mm_sound_device_out device_out)
{
	pa_operation *o = NULL;
	uint32_t device_in_pa = 0, device_out_pa = 0;

	/* convert device_in enum for PA */
	switch (device_in) {
	case MM_SOUND_DEVICE_IN_NONE:				device_in_pa = PA_TIZEN_DEVICE_IN_NONE;					break;
	case MM_SOUND_DEVICE_IN_MIC:				device_in_pa = PA_TIZEN_DEVICE_IN_MIC;					break;
	case MM_SOUND_DEVICE_IN_WIRED_ACCESSORY:	device_in_pa = PA_TIZEN_DEVICE_IN_WIRED_ACCESSORY;		break;
	case MM_SOUND_DEVICE_IN_BT_SCO:				device_in_pa = PA_TIZEN_DEVICE_IN_BT_SCO;				break;
	default:
		debug_error("inavlid device_in:%x", device_in);
		return;
	}

	/* convert device_out enum for PA */
	switch (device_out) {
	case MM_SOUND_DEVICE_OUT_NONE:				device_out_pa = PA_TIZEN_DEVICE_OUT_NONE;				break;
	case MM_SOUND_DEVICE_OUT_SPEAKER:			device_out_pa = PA_TIZEN_DEVICE_OUT_SPEAKER;			break;
	case MM_SOUND_DEVICE_OUT_RECEIVER:			device_out_pa = PA_TIZEN_DEVICE_OUT_RECEIVER;			break;
	case MM_SOUND_DEVICE_OUT_WIRED_ACCESSORY:	device_out_pa = PA_TIZEN_DEVICE_OUT_WIRED_ACCESSORY;	break;
	case MM_SOUND_DEVICE_OUT_BT_SCO:			device_out_pa = PA_TIZEN_DEVICE_OUT_BT_SCO;				break;
	case MM_SOUND_DEVICE_OUT_BT_A2DP:			device_out_pa = PA_TIZEN_DEVICE_OUT_BT_A2DP;			break;
	case MM_SOUND_DEVICE_OUT_DOCK:				device_out_pa = PA_TIZEN_DEVICE_OUT_DOCK;				break;
	case MM_SOUND_DEVICE_OUT_HDMI:				device_out_pa = PA_TIZEN_DEVICE_OUT_HDMI;				break;
	case MM_SOUND_DEVICE_OUT_MIRRORING:			device_out_pa = PA_TIZEN_DEVICE_OUT_MIRRORING;			break;
	case MM_SOUND_DEVICE_OUT_USB_AUDIO:			device_out_pa = PA_TIZEN_DEVICE_OUT_USB_AUDIO;			break;
	case MM_SOUND_DEVICE_OUT_MULTIMEDIA_DOCK:	device_out_pa = PA_TIZEN_DEVICE_OUT_MULTIMEDIA_DOCK;	break;
	default:
		debug_error("inavlid device_out:%x", device_out);
		return;
	}

	if (pa_threaded_mainloop_in_thread(pulse_info->m)) {
		o = pa_ext_policy_set_active_device(pulse_info->context, device_in_pa, device_out_pa, set_active_device_nosignal_cb, pulse_info);
		pa_operation_unref(o);
	} else {
		pa_threaded_mainloop_lock(pulse_info->m);
		CHECK_CONTEXT_DEAD_GOTO(pulse_info->context, unlock_and_fail);

		debug_msg("[PA] pa_ext_policy_set_active_device m[%p] c[%p] device_in:%d device_out:%d", pulse_info->m, pulse_info->context, device_in_pa, device_out_pa);
		o = pa_ext_policy_set_active_device(pulse_info->context, device_in_pa, device_out_pa, set_active_device_cb, pulse_info);
		CHECK_CONTEXT_SUCCESS_GOTO(pulse_info->context, o, unlock_and_fail);
		while (pa_operation_get_state(o) == PA_OPERATION_RUNNING) {
			pa_threaded_mainloop_wait(pulse_info->m);
			CHECK_CONTEXT_DEAD_GOTO(pulse_info->context, unlock_and_fail);
		}
		pa_operation_unref(o);
		pa_threaded_mainloop_unlock(pulse_info->m);
	}
	return;

unlock_and_fail:
	if (o) {
		pa_operation_cancel(o);
		pa_operation_unref(o);
	}
	pa_threaded_mainloop_unlock(pulse_info->m);
}

static void set_volume_level_cb(pa_context *c, int success, void *userdata)
{
	pulse_info_t *pinfo = (pulse_info_t *)userdata;
	if (pinfo == NULL) {
		debug_error ("pinfo is null");
		return;
	}

	if (success) {
		debug_msg ("[PA_CB] m[%p] c[%p] set volume level success", pinfo->m, c);
	} else {
		debug_error("[PA_CB] m[%p] c[%p] set volume level fail:%s", pinfo->m, c, pa_strerror(pa_context_errno(c)));
	}

	pa_threaded_mainloop_signal(pinfo->m, 0);
}

static void set_volume_level_nosignal_cb(pa_context *c, int success, void *userdata)
{
	pulse_info_t *pinfo = (pulse_info_t *)userdata;
	if (pinfo == NULL) {
		debug_error ("pinfo is null");
		return;
	}

	if (success) {
		debug_msg ("[PA_CB] m[%p] c[%p] set volume level success", pinfo->m, c);
	} else {
		debug_error("[PA_CB] m[%p] c[%p] set volume level fail:%s", pinfo->m, c, pa_strerror(pa_context_errno(c)));
	}
}

void MMSoundMgrPulseSetVolumeLevel(volume_type_t volume_type, unsigned int volume_level)
{
	pa_operation *o = NULL;

	if (pa_threaded_mainloop_in_thread(pulse_info->m)) {
		o = pa_ext_policy_set_volume_level(pulse_info->context, -1, volume_type, volume_level, set_volume_level_nosignal_cb, pulse_info);
		pa_operation_unref(o);
	} else {
		pa_threaded_mainloop_lock(pulse_info->m);
		CHECK_CONTEXT_DEAD_GOTO(pulse_info->context, unlock_and_fail);

		debug_msg("[PA] pa_ext_policy_set_volume_level m[%p] c[%p]", pulse_info->m, pulse_info->context);
		o = pa_ext_policy_set_volume_level(pulse_info->context, -1, volume_type, volume_level, set_volume_level_cb, pulse_info);
		CHECK_CONTEXT_SUCCESS_GOTO(pulse_info->context, o, unlock_and_fail);
		while (pa_operation_get_state(o) == PA_OPERATION_RUNNING) {
			pa_threaded_mainloop_wait(pulse_info->m);
			CHECK_CONTEXT_DEAD_GOTO(pulse_info->context, unlock_and_fail);
		}
		pa_operation_unref(o);
		pa_threaded_mainloop_unlock(pulse_info->m);
	}
	return;

unlock_and_fail:
	if (o) {
		pa_operation_cancel(o);
		pa_operation_unref(o);
	}
	pa_threaded_mainloop_unlock(pulse_info->m);
}

static void set_update_volume_cb(pa_context *c, int success, void *userdata)
{
	pulse_info_t *pinfo = (pulse_info_t *)userdata;
	if (pinfo == NULL) {
		debug_error ("pinfo is null");
		return;
	}

	if (success) {
		debug_msg ("[PA_CB] m[%p] c[%p] update volume success", pinfo->m, c);
	} else {
		debug_error("[PA_CB] m[%p] c[%p] update volume fail:%s", pinfo->m, c, pa_strerror(pa_context_errno(c)));
	}

	pa_threaded_mainloop_signal(pinfo->m, 0);
}

static void set_update_volume_nosignal_cb(pa_context *c, int success, void *userdata)
{
	pulse_info_t *pinfo = (pulse_info_t *)userdata;
	if (pinfo == NULL) {
		debug_error ("pinfo is null");
		return;
	}

	if (success) {
		debug_msg ("[PA_CB] m[%p] c[%p] update volume success", pinfo->m, c);
	} else {
		debug_error("[PA_CB] m[%p] c[%p] update volume fail:%s", pinfo->m, c, pa_strerror(pa_context_errno(c)));
	}
}

void MMSoundMgrPulseUpdateVolume(void)
{
	pa_operation *o = NULL;

	if (pa_threaded_mainloop_in_thread(pulse_info->m)) {
		o = pa_ext_policy_update_volume(pulse_info->context, set_update_volume_nosignal_cb, pulse_info);
		pa_operation_unref(o);
	} else {
		pa_threaded_mainloop_lock(pulse_info->m);
		CHECK_CONTEXT_DEAD_GOTO(pulse_info->context, unlock_and_fail);

		debug_msg("[PA] pa_ext_update_volume m[%p] c[%p]", pulse_info->m, pulse_info->context);
		o = pa_ext_policy_update_volume(pulse_info->context, set_update_volume_cb, pulse_info);
		CHECK_CONTEXT_SUCCESS_GOTO(pulse_info->context, o, unlock_and_fail);
		while (pa_operation_get_state(o) == PA_OPERATION_RUNNING) {
			pa_threaded_mainloop_wait(pulse_info->m);
			CHECK_CONTEXT_DEAD_GOTO(pulse_info->context, unlock_and_fail);
		}
		pa_operation_unref(o);
		pa_threaded_mainloop_unlock(pulse_info->m);
	}
	return;

unlock_and_fail:
	if (o) {
		pa_operation_cancel(o);
		pa_operation_unref(o);
	}
	pa_threaded_mainloop_unlock(pulse_info->m);
}

/* -------------------------------- booting sound  --------------------------------------------*/

#define VCONF_BOOTING "memory/private/sound/booting"

static void __pa_play_sample_cb (pa_context *c, uint32_t stream_index, void *userdata)
{
	pulse_info_t *pinfo = (pulse_info_t *)userdata;
	if (pinfo == NULL) {
		debug_error ("pinfo is null");
		return;
	}

	if (stream_index > -1) {
		debug_msg ("[PA_CB] m[%p] c[%p] set booting[stream:%d] success", pinfo->m, c, stream_index);
	} else {
		debug_error("[PA_CB] m[%p] c[%p] set booting fail:%s", pinfo->m, c, pa_strerror(pa_context_errno(c)));
	}
	pa_threaded_mainloop_signal(pinfo->m, 0);
}

static void _booting_changed_cb(keynode_t* node, void* data)
{
	char* booting = NULL;
	pulse_info_t* pinfo = (pulse_info_t*)data;
	pa_operation *o = NULL;
	unsigned int value;

	if (pinfo == NULL) {
		debug_error ("pinfo is null");
		return;
	}

	booting = vconf_get_str(VCONF_BOOTING);
	debug_msg ("%s changed callback called, booting value = %s\n",vconf_keynode_get_name(node), booting);
	if (booting)
		free(booting);

	pa_threaded_mainloop_lock(pinfo->m);
	CHECK_CONTEXT_DEAD_GOTO(pinfo->context, unlock_and_fail);

	mm_sound_volume_get_value(VOLUME_TYPE_SYSTEM, &value);
	o = pa_ext_policy_play_sample(pinfo->context, "booting", VOLUME_TYPE_SYSTEM, VOLUME_GAIN_BOOTING, value, __pa_play_sample_cb, pinfo);

	CHECK_CONTEXT_SUCCESS_GOTO(pinfo->context, o, unlock_and_fail);
	while (pa_operation_get_state(o) == PA_OPERATION_RUNNING) {
		pa_threaded_mainloop_wait(pinfo->m);
		CHECK_CONTEXT_DEAD_GOTO(pinfo->context, unlock_and_fail);
	}
	pa_operation_unref(o);

	pa_threaded_mainloop_unlock(pinfo->m);
	return;

unlock_and_fail:
	if (o) {
		pa_operation_cancel(o);
		pa_operation_unref(o);
	}
	pa_threaded_mainloop_unlock(pinfo->m);
}

int MMSoundMgrPulseHandleRegisterBooting (void* pinfo)
{
	int ret = vconf_notify_key_changed(VCONF_BOOTING, _booting_changed_cb, pinfo);
	debug_msg ("vconf [%s] set ret = %d\n", VCONF_BOOTING, ret);
	return ret;
}

void* MMSoundMgrPulseInit(void)
{
	pulse_info = (pulse_info_t*) malloc (sizeof(pulse_info_t));
	memset (pulse_info, 0, sizeof(pulse_info_t));

	debug_enter("\n");

	pulse_init(pulse_info);
	pulse_client_thread_init(pulse_info);

	pulse_info->device_in_out = PA_INVALID_INDEX;
	pulse_info->aec_module_idx = PA_INVALID_INDEX;
	pulse_info->bt_idx = PA_INVALID_INDEX;
	pulse_info->usb_idx = PA_INVALID_INDEX;
	pulse_info->dock_idx = PA_INVALID_INDEX;
#ifdef SUPPORT_MONO_AUDIO
	MMSoundMgrPulseHandleRegisterMonoAudio(pulse_info);
#endif
#ifndef TIZEN_MICRO
	MMSoundMgrPulseHandleRegisterAecSetVolume(pulse_info);
#endif

#ifdef SUPPORT_AUDIO_BALANCE
	MMSoundMgrPulseHandleRegisterAudioBalance(pulse_info);
	MMSoundMgrPulseHandleResetAudioBalanceOnBoot(pulse_info);
#endif

#ifdef SUPPORT_AUDIO_MUTEALL
	MMSoundMgrPulseHandleRegisterAudioMuteall(pulse_info);
	MMSoundMgrPulseHandleResetAudioMuteallOnBoot(pulse_info);
#endif

#ifdef SUPPORT_BT_SCO_DETECT
	MMSoundMgrPulseHandleRegisterBluetoothStatus(pulse_info);
#endif

	MMSoundMgrPulseHandleRegisterBooting(pulse_info);

	debug_leave("\n");
	return pulse_info;
}

int MMSoundMgrPulseFini(void* handle)
{
	pulse_info_t *pinfo = (pulse_info_t *)handle;

	debug_enter("\n");

	pulse_deinit(pinfo);

	g_async_queue_push(pinfo->queue, (gpointer)PA_CLIENT_DESTROY);

	pthread_join(pinfo->thread, 0);
	g_async_queue_unref(pinfo->queue);

	debug_leave("\n");
	return MM_ERROR_NONE;
}

