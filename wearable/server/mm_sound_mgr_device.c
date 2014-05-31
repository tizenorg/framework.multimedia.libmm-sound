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
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <sys/stat.h>
#include <glib.h>
#include <errno.h>

#include <mm_error.h>
#include <mm_types.h>
#include <mm_debug.h>
#include <mm_ipc.h>
#include <pulse/ext-echo-cancel.h>
#include "include/mm_sound_mgr_common.h"
#include "include/mm_sound_mgr_ipc.h"
#include "include/mm_sound_mgr_device.h"
#include "include/mm_sound_thread_pool.h"
#include "../include/mm_sound_msg.h"
#include "../include/mm_sound_common.h"
#include "../include/mm_sound_utils.h"

#include <vconf.h>
#include <vconf-keys.h>

#include "include/mm_sound_mgr_session.h"
#include "include/mm_sound_mgr_pulse.h"

void _mm_sound_get_devices_from_route(mm_sound_route route, mm_sound_device_in *device_in, mm_sound_device_out *device_out);

static GList *g_active_device_cb_list = NULL;
static pthread_mutex_t g_active_device_cb_mutex = PTHREAD_MUTEX_INITIALIZER;
static GList *g_available_route_cb_list = NULL;
static pthread_mutex_t g_available_route_cb_mutex = PTHREAD_MUTEX_INITIALIZER;
static GList *g_volume_cb_list = NULL;
static pthread_mutex_t g_volume_cb_mutex = PTHREAD_MUTEX_INITIALIZER;


void _mm_sound_mgr_device_volume_callback(keynode_t* node, void* data);
/* remove build warning : to be removed later */
extern int avsys_check_process(int check_pid);

static char *g_volume_vconf[VOLUME_TYPE_MAX] = {
	VCONF_KEY_VOLUME_TYPE_SYSTEM,       /* VOLUME_TYPE_SYSTEM */
	VCONF_KEY_VOLUME_TYPE_NOTIFICATION, /* VOLUME_TYPE_NOTIFICATION */
	VCONF_KEY_VOLUME_TYPE_ALARM,        /* VOLUME_TYPE_ALARM */
	VCONF_KEY_VOLUME_TYPE_RINGTONE,     /* VOLUME_TYPE_RINGTONE */
	VCONF_KEY_VOLUME_TYPE_MEDIA,        /* VOLUME_TYPE_MEDIA */
	VCONF_KEY_VOLUME_TYPE_CALL,         /* VOLUME_TYPE_CALL */
	VCONF_KEY_VOLUME_TYPE_VOIP,         /* VOLUME_TYPE_VOIP */
	VCONF_KEY_VOLUME_TYPE_SVOICE,       /* VOLUME_TYPE_SVOICE */
	VCONF_KEY_VOLUME_TYPE_ANDROID,      /* VOLUME_TYPE_FIXED */
	VCONF_KEY_VOLUME_TYPE_JAVA          /* VOLUME_TYPE_EXT_JAVA */
};

int _mm_sound_mgr_device_init(void)
{
	int i = 0;
	debug_fenter();

	for(i = 0 ; i < VOLUME_TYPE_MAX; i++)
		vconf_notify_key_changed(g_volume_vconf[i], _mm_sound_mgr_device_volume_callback, (void *)i);

	debug_fleave();
	return MM_ERROR_NONE;
}

int _mm_sound_mgr_device_fini(void)
{
	debug_fenter();

	debug_fleave();
	return MM_ERROR_NONE;
}

int _mm_sound_mgr_device_is_route_available(const _mm_sound_mgr_device_param_t *param, bool *is_available)
{
	mm_sound_route route = param->route;
	mm_sound_device_in device_in = MM_SOUND_DEVICE_IN_NONE;
	mm_sound_device_out device_out = MM_SOUND_DEVICE_OUT_NONE;
	int ret = MM_ERROR_NONE;

	debug_fenter();

	_mm_sound_get_devices_from_route(route, &device_in, &device_out);

	/* check given input & output device is available */
	ret = MMSoundMgrSessionIsDeviceAvailable(device_out, device_in, is_available);

	debug_fleave();
	return ret;
}

int _mm_sound_mgr_device_foreach_available_route_cb(mm_ipc_msg_t *msg)
{
	mm_sound_route *route_list = NULL;
	int route_list_count = 0;
	int route_index = 0;
	int available_count = 0;
	bool is_available = 0;
	int ret = MM_ERROR_NONE;

	debug_fenter();

	route_list_count = _mm_sound_get_valid_route_list(&route_list);
	for (route_index = 0; route_index < route_list_count; route_index++) {
		mm_sound_device_in device_in = MM_SOUND_DEVICE_IN_NONE;
		mm_sound_device_out device_out = MM_SOUND_DEVICE_OUT_NONE;

		_mm_sound_get_devices_from_route(route_list[route_index], &device_in, &device_out);
		/* check input & output device of given route is available */
		ret = MMSoundMgrSessionIsDeviceAvailable(device_out, device_in, &is_available);
		if (ret != MM_ERROR_NONE) {
			debug_error("MMSoundMgrSessionIsDeviceAvailable() failed (%d)\n", ret);
			goto FINISH;
		}

		/* add route to avaiable route list */
		if (is_available) {
			if (available_count >= (sizeof(msg->sound_msg.route_list) / sizeof(int))) {
				debug_error("Cannot add available route, list is full\n");
				ret = MM_ERROR_SOUND_INTERNAL;
				goto FINISH;
			}
			msg->sound_msg.route_list[available_count++] = route_list[route_index];
		}
	}
FINISH:
	debug_fleave();
	return ret;
}

int _mm_sound_mgr_device_set_active_route(const _mm_sound_mgr_device_param_t *param)
{
	mm_sound_route route = param->route;
	mm_sound_device_in device_in = MM_SOUND_DEVICE_IN_NONE;
	mm_sound_device_out device_out = MM_SOUND_DEVICE_OUT_NONE;
	bool is_available = 0;
	int ret = MM_ERROR_NONE;

	debug_fenter();

	_mm_sound_get_devices_from_route(route, &device_in, &device_out);
	/* check specific route is available */
	ret = _mm_sound_mgr_device_is_route_available(param, &is_available);
	if ((ret != MM_ERROR_NONE) || (!is_available)) {

	}

	ret = MMSoundMgrSessionSetDeviceActive(device_out, device_in, param->need_broadcast);

	debug_fleave();
	return ret;
}

int _mm_sound_mgr_device_set_active_route_auto(void)
{
	int ret = MM_ERROR_NONE;

	debug_fenter();

	ret = MMSoundMgrSessionSetDeviceActiveAuto();
	if (ret != MM_ERROR_NONE) {
		debug_error("fail to _mm_sound_mgr_device_set_active_route_auto.\n");
	} else {
		debug_msg ("success : _mm_sound_mgr_device_set_active_route_auto\n");
	}

	debug_fleave();
	return ret;
}
int _mm_sound_mgr_device_get_active_device(const _mm_sound_mgr_device_param_t *param, mm_sound_device_in *device_in, mm_sound_device_out *device_out)
{
	int ret = MM_ERROR_NONE;

#ifdef DEBUG_DETAIL
	debug_fenter();
#endif

	ret = MMSoundMgrSessionGetDeviceActive(device_out, device_in);

#ifdef DEBUG_DETAIL
	debug_fleave();
#endif
	return ret;
}

int _mm_sound_mgr_device_add_active_device_callback(const _mm_sound_mgr_device_param_t *param)
{
	int ret = MM_ERROR_NONE;
	GList *list = NULL;
	_mm_sound_mgr_device_param_t *cb_param = NULL;
	bool is_already_set = FALSE;

#ifdef DEBUG_DETAIL
	debug_fenter();
#endif

	MMSOUND_ENTER_CRITICAL_SECTION_WITH_RETURN(&g_active_device_cb_mutex, MM_ERROR_SOUND_INTERNAL);

	for (list = g_active_device_cb_list; list != NULL; list = list->next) {
		cb_param = (_mm_sound_mgr_device_param_t *)list->data;
		if ((cb_param) && (cb_param->pid == param->pid) && (!strcmp(cb_param->name, param->name))) {
			cb_param->callback = param->callback;
			cb_param->cbdata = param->cbdata;
			is_already_set = TRUE;
			break;
		}
	}

	if (!is_already_set) {
		cb_param = g_malloc(sizeof(_mm_sound_mgr_device_param_t));
		memcpy(cb_param, param, sizeof(_mm_sound_mgr_device_param_t));
		g_active_device_cb_list = g_list_append(g_active_device_cb_list, cb_param);
		if (g_active_device_cb_list) {
			debug_log("active device cb registered for pid [%d]", cb_param->pid);
		} else {
			debug_error("g_list_append failed\n");
			ret = MM_ERROR_SOUND_INTERNAL;
			goto FINISH;
		}

		__mm_sound_mgr_ipc_freeze_send (FREEZE_COMMAND_EXCLUDE, param->pid);
	}

FINISH:
	MMSOUND_LEAVE_CRITICAL_SECTION(&g_active_device_cb_mutex);
#ifdef DEBUG_DETAIL
	debug_fleave();
#endif
	return ret;
}

int _mm_sound_mgr_device_remove_active_device_callback(const _mm_sound_mgr_device_param_t *param)
{
	int ret = MM_ERROR_NONE;
	GList *list = NULL;
	bool is_same_pid_exists = false;
	_mm_sound_mgr_device_param_t *cb_param = NULL;

	debug_fenter();

	MMSOUND_ENTER_CRITICAL_SECTION_WITH_RETURN(&g_active_device_cb_mutex, MM_ERROR_SOUND_INTERNAL);

	for (list = g_active_device_cb_list; list != NULL; list = list->next) {
		cb_param = (_mm_sound_mgr_device_param_t *)list->data;
		if ((cb_param) && (cb_param->pid == param->pid) && (!strcmp(cb_param->name, param->name))) {
			g_active_device_cb_list = g_list_remove(g_active_device_cb_list, cb_param);
			g_free(cb_param);
			break;
		}
	}

	/* Check for PID still exists in the list, if not include freeze */
	for (list = g_active_device_cb_list; list != NULL; list = list->next) {
		cb_param = (_mm_sound_mgr_device_param_t *)list->data;
		if ((cb_param) && (cb_param->pid == param->pid)) {
			is_same_pid_exists = true;
			break;
		}
	}
	if (!is_same_pid_exists)
		__mm_sound_mgr_ipc_freeze_send (FREEZE_COMMAND_INCLUDE, param->pid);

	MMSOUND_LEAVE_CRITICAL_SECTION(&g_active_device_cb_mutex);

	debug_fleave();
	return ret;
}

int _mm_sound_mgr_device_add_volume_callback(const _mm_sound_mgr_device_param_t *param)
{
	int ret = MM_ERROR_NONE;
	GList *list = NULL;
	_mm_sound_mgr_device_param_t *cb_param = NULL;
	bool is_already_set = FALSE;

#ifdef DEBUG_DETAIL
	debug_fenter();
#endif

	MMSOUND_ENTER_CRITICAL_SECTION_WITH_RETURN(&g_volume_cb_mutex, MM_ERROR_SOUND_INTERNAL);

	for (list = g_volume_cb_list; list != NULL; list = list->next) {
		cb_param = (_mm_sound_mgr_device_param_t *)list->data;
		if ((cb_param) && (cb_param->pid == param->pid)) {
			cb_param->callback = param->callback;
			cb_param->cbdata = param->cbdata;
			is_already_set = TRUE;
			break;
		}
	}
	if (!is_already_set) {
		cb_param = g_malloc(sizeof(_mm_sound_mgr_device_param_t));
		memcpy(cb_param, param, sizeof(_mm_sound_mgr_device_param_t));
		g_volume_cb_list = g_list_append(g_volume_cb_list, cb_param);
		if (g_volume_cb_list) {
			debug_log("volume cb registered for pid [%d]", cb_param->pid);
		} else {
			debug_error("g_list_append failed\n");
			ret = MM_ERROR_SOUND_INTERNAL;
			goto FINISH;
		}

		__mm_sound_mgr_ipc_freeze_send (FREEZE_COMMAND_EXCLUDE, param->pid);
	}

FINISH:
	MMSOUND_LEAVE_CRITICAL_SECTION(&g_volume_cb_mutex);
#ifdef DEBUG_DETAIL
	debug_fleave();
#endif
	return ret;
}

int _mm_sound_mgr_device_remove_volume_callback(const _mm_sound_mgr_device_param_t *param)
{
	int ret = MM_ERROR_NONE;
	GList *list = NULL;
	_mm_sound_mgr_device_param_t *cb_param = NULL;

	debug_fenter();

	MMSOUND_ENTER_CRITICAL_SECTION_WITH_RETURN(&g_volume_cb_mutex, MM_ERROR_SOUND_INTERNAL);

	for (list = g_volume_cb_list; list != NULL; list = list->next) {
		cb_param = (_mm_sound_mgr_device_param_t *)list->data;
		if ((cb_param) && (cb_param->pid == param->pid)) {
			g_volume_cb_list = g_list_remove(g_volume_cb_list, cb_param);
			__mm_sound_mgr_ipc_freeze_send (FREEZE_COMMAND_INCLUDE, param->pid);
			g_free(cb_param);
			break;
		}
	}

	MMSOUND_LEAVE_CRITICAL_SECTION(&g_volume_cb_mutex);

	debug_fleave();
	return ret;
}


#define CLEAR_DEAD_CB_LIST(x)  do { \
	debug_warning ("cb_list = %p, cb_param = %p, pid=[%d]", x, cb_param, (cb_param)? cb_param->pid : -1); \
	if (x && cb_param && avsys_check_process (cb_param->pid) != 0) { \
		debug_warning("PID:%d does not exist now! remove from device cb list\n", cb_param->pid); \
		g_free (cb_param); \
		x = g_list_remove (x, cb_param); \
	} \
}while(0)

static void _clear_available_cb_list_func (_mm_sound_mgr_device_param_t * cb_param, gpointer user_data)
{
	CLEAR_DEAD_CB_LIST(g_available_route_cb_list);
}

static void _clear_active_cb_list_func (_mm_sound_mgr_device_param_t * cb_param, gpointer user_data)
{
	CLEAR_DEAD_CB_LIST(g_active_device_cb_list);
}

static void _clear_volume_cb_list_func (_mm_sound_mgr_device_param_t * cb_param, gpointer user_data)
{
	CLEAR_DEAD_CB_LIST(g_volume_cb_list);
}

void _mm_sound_mgr_device_volume_callback(keynode_t* node, void* data)
{
	int ret = MM_ERROR_NONE;
	GList *list = NULL;
	_mm_sound_mgr_device_param_t *cb_param = NULL;
	mm_ipc_msg_t msg;
	volume_type_t type = (volume_type_t)data;
	mm_sound_device_in device_in;
	mm_sound_device_out device_out;
	char *str = NULL;
	unsigned int value;

	debug_enter("[%s] changed callback called", vconf_keynode_get_name(node));

	MMSOUND_ENTER_CRITICAL_SECTION(&g_volume_cb_mutex);

	MMSoundMgrSessionGetDeviceActive(&device_out, &device_in);

	/* Get volume value from VCONF */
	if (vconf_get_int(g_volume_vconf[type], &value)) {
		debug_error ("vconf_get_int(%s) failed..\n", g_volume_vconf[type]);
		ret = MM_ERROR_SOUND_INTERNAL;
		goto FINISH;
	}

	/* Update list for dead process */
	g_list_foreach (g_volume_cb_list, (GFunc)_clear_volume_cb_list_func, NULL);

	for (list = g_volume_cb_list; list != NULL; list = list->next) {
		cb_param = (_mm_sound_mgr_device_param_t *)list->data;
		if ((cb_param) && (cb_param->callback)) {
			memset(&msg, 0, sizeof(mm_ipc_msg_t));
			SOUND_MSG_SET(msg.sound_msg, MM_SOUND_MSG_INF_VOLUME_CB, 0, MM_ERROR_NONE, cb_param->pid);
			msg.sound_msg.type = type;
			msg.sound_msg.val = value;
			msg.sound_msg.callback = cb_param->callback;
			msg.sound_msg.cbdata = cb_param->cbdata;

			ret = _MMIpcCBSndMsg(&msg);
			if (ret != MM_ERROR_NONE) {
				debug_error("Fail to send callback message (%x)\n", ret);
				goto FINISH;
			}
		}
	}

FINISH:
	MMSOUND_LEAVE_CRITICAL_SECTION(&g_volume_cb_mutex);

	debug_leave();
}

int _mm_sound_mgr_device_active_device_callback(mm_sound_device_in device_in, mm_sound_device_out device_out)
{
	int ret = MM_ERROR_NONE;
	GList *list = NULL;
	_mm_sound_mgr_device_param_t *cb_param = NULL;
	mm_ipc_msg_t msg;

	debug_fenter();

	MMSOUND_ENTER_CRITICAL_SECTION_WITH_RETURN(&g_active_device_cb_mutex, MM_ERROR_SOUND_INTERNAL);

	/* Update list for dead process */
	 g_list_foreach (g_active_device_cb_list, (GFunc)_clear_active_cb_list_func, NULL);

	for (list = g_active_device_cb_list; list != NULL; list = list->next) {
		cb_param = (_mm_sound_mgr_device_param_t *)list->data;
		if ((cb_param) && (cb_param->callback)) {
			memset(&msg, 0, sizeof(mm_ipc_msg_t));
			SOUND_MSG_SET(msg.sound_msg, MM_SOUND_MSG_INF_ACTIVE_DEVICE_CB, 0, MM_ERROR_NONE, cb_param->pid);
			msg.sound_msg.device_in = device_in;
			msg.sound_msg.device_out = device_out;
			msg.sound_msg.callback = cb_param->callback;
			msg.sound_msg.cbdata = cb_param->cbdata;

			ret = _MMIpcCBSndMsg(&msg);
			if (ret != MM_ERROR_NONE) {
				debug_error("Fail to send callback message (%x)\n", ret);
				goto FINISH;
			}
		}
	}
#ifndef TIZEN_MICRO
	MMSoundMgrPulseHandleAecSetDevice(device_in,device_out);
#endif
FINISH:
	MMSOUND_LEAVE_CRITICAL_SECTION(&g_active_device_cb_mutex);

	debug_fleave();
	return ret;
}

int _mm_sound_mgr_device_add_available_route_callback(const _mm_sound_mgr_device_param_t *param)
{
	int ret = MM_ERROR_NONE;
	GList *list = NULL;
	_mm_sound_mgr_device_param_t *cb_param = NULL;
	bool is_already_set = FALSE;

	debug_fenter();

	MMSOUND_ENTER_CRITICAL_SECTION_WITH_RETURN(&g_available_route_cb_mutex, MM_ERROR_SOUND_INTERNAL);

	for (list = g_available_route_cb_list; list != NULL; list = list->next) {
		cb_param = (_mm_sound_mgr_device_param_t *)list->data;
		if ((cb_param) && (cb_param->pid == param->pid)) {
			cb_param->callback = param->callback;
			cb_param->cbdata = param->cbdata;
			is_already_set = TRUE;
			break;
		}
	}
	if (!is_already_set) {
		cb_param = g_malloc(sizeof(_mm_sound_mgr_device_param_t));
		memcpy(cb_param, param, sizeof(_mm_sound_mgr_device_param_t));
		g_available_route_cb_list = g_list_append(g_available_route_cb_list, cb_param);
		if (g_available_route_cb_list) {
			debug_log("available route cb registered for pid [%d]", cb_param->pid);
		} else {
			debug_error("g_list_append failed\n");
			ret = MM_ERROR_SOUND_INTERNAL;
			goto FINISH;
		}

		__mm_sound_mgr_ipc_freeze_send (FREEZE_COMMAND_EXCLUDE, param->pid);
	}
FINISH:
	MMSOUND_LEAVE_CRITICAL_SECTION(&g_available_route_cb_mutex);

	debug_fleave();
	return ret;
}

int _mm_sound_mgr_device_remove_available_route_callback(const _mm_sound_mgr_device_param_t *param)
{
	int ret = MM_ERROR_NONE;
	GList *list = NULL;
	_mm_sound_mgr_device_param_t *cb_param = NULL;

	debug_fenter();

	MMSOUND_ENTER_CRITICAL_SECTION_WITH_RETURN(&g_available_route_cb_mutex, MM_ERROR_SOUND_INTERNAL);

	for (list = g_available_route_cb_list; list != NULL; list = list->next) {
		cb_param = (_mm_sound_mgr_device_param_t *)list->data;
		if ((cb_param) && (cb_param->pid == param->pid)) {
			g_available_route_cb_list = g_list_remove(g_available_route_cb_list, cb_param);
			__mm_sound_mgr_ipc_freeze_send (FREEZE_COMMAND_INCLUDE, param->pid);
			g_free(cb_param);
			break;
		}
	}

	MMSOUND_LEAVE_CRITICAL_SECTION(&g_available_route_cb_mutex);

	debug_fleave();
	return ret;
}

int _mm_sound_mgr_device_available_device_callback(mm_sound_device_in device_in, mm_sound_device_out device_out, bool available)
{
	int ret = MM_ERROR_NONE;
	_mm_sound_mgr_device_param_t *cb_param = NULL;
	mm_ipc_msg_t msg;
	int route_list_count = 0;
	int route_index = 0;
	int available_count = 0;
	mm_sound_route *route_list = NULL;
	GList *list = NULL;

	debug_fenter();

	memset (&msg, 0, sizeof(mm_ipc_msg_t));

	route_list_count = _mm_sound_get_valid_route_list(&route_list);
	debug_log("in=[%x], out=[%x], route_list_count = [%d], available = [%d]", device_in, device_out, route_list_count, available);
	for (route_index = 0; route_index < route_list_count; route_index++) {
		mm_sound_device_in route_device_in = MM_SOUND_DEVICE_IN_NONE;
		mm_sound_device_out route_device_out = MM_SOUND_DEVICE_OUT_NONE;
		bool is_changed = 0;

		_mm_sound_get_devices_from_route(route_list[route_index], &route_device_in, &route_device_out);

		if ((device_in != MM_SOUND_DEVICE_IN_NONE) && (device_in == route_device_in)) {
			/* device(in&out) changed together & they can be combined as this route */
			if ((device_out != MM_SOUND_DEVICE_OUT_NONE) && (device_out == route_device_out)) {
				is_changed = 1;
			/* device(in) changed & this route has device(in) only */
			} else if (route_device_out == MM_SOUND_DEVICE_OUT_NONE) {
				is_changed = 1;
			/* device(in) changed & this route have device(in&out), we need to check availability of output device of this route */
			} else {
				MMSoundMgrSessionIsDeviceAvailableNoLock(route_device_out, MM_SOUND_DEVICE_IN_NONE, &is_changed);
			}
		}
		if ((is_changed == 0) && (device_out != MM_SOUND_DEVICE_OUT_NONE) && (device_out == route_device_out)) {
			/* device(out) changed & this route has device(out) only */
			if (route_device_in == MM_SOUND_DEVICE_IN_NONE) {
				is_changed = 1;
			/* device(out) changed & this route have device(in&out), we need to check availability of input device of this route */
			} else {
				MMSoundMgrSessionIsDeviceAvailableNoLock(MM_SOUND_DEVICE_OUT_NONE, route_device_in, &is_changed);
			}
		}
		/* add route to avaiable route list */
		if (is_changed) {
			if (available_count >= (sizeof(msg.sound_msg.route_list) / sizeof(int))) {
				debug_error("Cannot add available route, list is full\n");
				return MM_ERROR_SOUND_INTERNAL;
			}
			debug_log("route_index [%d] is added to route_list [%d]", route_index, available_count);
			msg.sound_msg.route_list[available_count++] = route_list[route_index];
		}
	}

	MMSOUND_ENTER_CRITICAL_SECTION_WITH_RETURN(&g_available_route_cb_mutex, MM_ERROR_SOUND_INTERNAL);

	/* Update list for dead process */
	g_list_foreach (g_available_route_cb_list, (GFunc)_clear_available_cb_list_func, NULL);

	for (list = g_available_route_cb_list; list != NULL; list = list->next) {
		cb_param = (_mm_sound_mgr_device_param_t *)list->data;
		if ((cb_param) && (cb_param->callback)) {
			SOUND_MSG_SET(msg.sound_msg, MM_SOUND_MSG_INF_AVAILABLE_ROUTE_CB, 0, MM_ERROR_NONE, cb_param->pid);
			msg.sound_msg.is_available = available;
			msg.sound_msg.callback = cb_param->callback;
			msg.sound_msg.cbdata = cb_param->cbdata;

			ret = _MMIpcCBSndMsg(&msg);
			if (ret != MM_ERROR_NONE) {
				debug_error("Fail to send callback message\n");
				goto FINISH;
			}
		}
	}
FINISH:
	MMSOUND_LEAVE_CRITICAL_SECTION(&g_available_route_cb_mutex);

	debug_fleave();
	return ret;
}

