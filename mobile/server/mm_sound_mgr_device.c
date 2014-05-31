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

#include "include/mm_sound_mgr_common.h"
#include "include/mm_sound_mgr_ipc.h"
#include "include/mm_sound_mgr_device.h"
#include "include/mm_sound_thread_pool.h"
#include "../include/mm_sound_msg.h"
#include "../include/mm_sound_common.h"
#include "../include/mm_sound_utils.h"

#include "include/mm_sound_mgr_session.h"

void _mm_sound_get_devices_from_route(mm_sound_route route, mm_sound_device_in *device_in, mm_sound_device_out *device_out);

static GList *g_active_device_cb_list = NULL;
static pthread_mutex_t g_active_device_cb_mutex = PTHREAD_MUTEX_INITIALIZER;
static GList *g_available_route_cb_list = NULL;
static pthread_mutex_t g_available_route_cb_mutex = PTHREAD_MUTEX_INITIALIZER;

int _mm_sound_mgr_device_init(void)
{
	debug_fenter();

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

	ret = MMSoundMgrSessionSetDeviceActive(device_out, device_in);

	debug_fleave();
	return ret;
}

int _mm_sound_mgr_device_get_active_device(const _mm_sound_mgr_device_param_t *param, mm_sound_device_in *device_in, mm_sound_device_out *device_out)
{
	int ret = MM_ERROR_NONE;

	debug_fenter();

	ret = MMSoundMgrSessionGetDeviceActive(device_out, device_in);

	debug_fleave();
	return ret;
}

int _mm_sound_mgr_device_add_active_device_callback(const _mm_sound_mgr_device_param_t *param)
{
	int ret = MM_ERROR_NONE;
	GList *list = NULL;
	_mm_sound_mgr_device_param_t *cb_param = NULL;
	bool is_already_set = FALSE;

	debug_fenter();

	MMSOUND_ENTER_CRITICAL_SECTION_WITH_RETURN(&g_active_device_cb_mutex, MM_ERROR_SOUND_INTERNAL);

	for (list = g_active_device_cb_list; list != NULL; list = list->next)
	{
		cb_param = (_mm_sound_mgr_device_param_t *)list->data;
		if ((cb_param) && (cb_param->pid == param->pid))
		{
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
	}

FINISH:
	MMSOUND_LEAVE_CRITICAL_SECTION(&g_active_device_cb_mutex);
	debug_fleave();
	return ret;
}

int _mm_sound_mgr_device_remove_active_device_callback(const _mm_sound_mgr_device_param_t *param)
{
	int ret = MM_ERROR_NONE;
	GList *list = NULL;
	_mm_sound_mgr_device_param_t *cb_param = NULL;

	debug_fenter();

	MMSOUND_ENTER_CRITICAL_SECTION_WITH_RETURN(&g_active_device_cb_mutex, MM_ERROR_SOUND_INTERNAL);

	for (list = g_active_device_cb_list; list != NULL; list = list->next)
	{
		cb_param = (_mm_sound_mgr_device_param_t *)list->data;
		if ((cb_param) && (cb_param->pid == param->pid))
		{
			g_active_device_cb_list = g_list_remove(g_active_device_cb_list, cb_param);
			g_free(cb_param);
			break;
		}
	}

	MMSOUND_LEAVE_CRITICAL_SECTION(&g_active_device_cb_mutex);

	debug_fleave();
	return ret;
}

int _mm_sound_mgr_device_active_device_callback(mm_sound_device_in device_in, mm_sound_device_out device_out)
{
	int ret = MM_ERROR_NONE;
	GList *list = NULL;
	_mm_sound_mgr_device_param_t *cb_param = NULL;
	mm_ipc_msg_t msg;

	debug_fenter();

	MMSOUND_ENTER_CRITICAL_SECTION_WITH_RETURN(&g_active_device_cb_mutex, MM_ERROR_SOUND_INTERNAL);

	for (list = g_active_device_cb_list; list != NULL; list = list->next)
	{
		cb_param = (_mm_sound_mgr_device_param_t *)list->data;
		if ((cb_param) && (cb_param->callback))
		{
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

	for (list = g_available_route_cb_list; list != NULL; list = list->next)
	{
		cb_param = (_mm_sound_mgr_device_param_t *)list->data;
		if ((cb_param) && (cb_param->pid == param->pid))
		{
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

	for (list = g_available_route_cb_list; list != NULL; list = list->next)
	{
		cb_param = (_mm_sound_mgr_device_param_t *)list->data;
		if ((cb_param) && (cb_param->pid == param->pid))
		{
			g_available_route_cb_list = g_list_remove(g_available_route_cb_list, cb_param);
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

	for (list = g_available_route_cb_list; list != NULL; list = list->next)
	{
		cb_param = (_mm_sound_mgr_device_param_t *)list->data;
		if ((cb_param) && (cb_param->callback))
		{
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

