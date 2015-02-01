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
#include <sys/types.h>
#include <dirent.h>

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
#include "include/mm_sound_mgr_device_headset.h"

#include "include/mm_sound_thread_pool.h"
#include "../include/mm_sound_msg.h"
#include "../include/mm_sound_common.h"
#include "../include/mm_sound_device.h"
#include "../include/mm_sound_utils.h"

#include <vconf.h>
#include <vconf-keys.h>

#include "include/mm_sound_mgr_session.h"
#include "include/mm_sound_mgr_pulse.h"

#define MAX_SUPPORT_DEVICE_NUM    256
static char g_device_id_array[MAX_SUPPORT_DEVICE_NUM];

void _mm_sound_get_devices_from_route(mm_sound_route route, mm_sound_device_in *device_in, mm_sound_device_out *device_out);

static GList *g_active_device_cb_list = NULL;
static pthread_mutex_t g_active_device_cb_mutex = PTHREAD_MUTEX_INITIALIZER;
static GList *g_available_route_cb_list = NULL;
static pthread_mutex_t g_available_route_cb_mutex = PTHREAD_MUTEX_INITIALIZER;
static GList *g_volume_cb_list = NULL;
static pthread_mutex_t g_volume_cb_mutex = PTHREAD_MUTEX_INITIALIZER;

static GList *g_device_connected_cb_list = NULL;
static pthread_mutex_t g_device_connected_cb_mutex = PTHREAD_MUTEX_INITIALIZER;
static GList *g_device_info_changed_cb_list = NULL;
static pthread_mutex_t g_device_info_changed_cb_mutex = PTHREAD_MUTEX_INITIALIZER;
static GList *g_connected_device_list = NULL;
static pthread_mutex_t g_connected_device_list_mutex = PTHREAD_MUTEX_INITIALIZER;
typedef struct {
	GList *list;
}connected_device_list_s;
connected_device_list_s g_connected_device_list_s;

static int _mm_sound_mgr_device_volume_callback(keynode_t* node, void* data);

static char *g_volume_vconf[VOLUME_TYPE_MAX] = {
	VCONF_KEY_VOLUME_TYPE_SYSTEM,       /* VOLUME_TYPE_SYSTEM */
	VCONF_KEY_VOLUME_TYPE_NOTIFICATION, /* VOLUME_TYPE_NOTIFICATION */
	VCONF_KEY_VOLUME_TYPE_ALARM,        /* VOLUME_TYPE_ALARM */
	VCONF_KEY_VOLUME_TYPE_RINGTONE,     /* VOLUME_TYPE_RINGTONE */
	VCONF_KEY_VOLUME_TYPE_MEDIA,        /* VOLUME_TYPE_MEDIA */
	VCONF_KEY_VOLUME_TYPE_CALL,         /* VOLUME_TYPE_CALL */
	VCONF_KEY_VOLUME_TYPE_VOIP,         /* VOLUME_TYPE_VOIP */
	VCONF_KEY_VOLUME_TYPE_VOICE,		/* VOLUME_TYPE_VOICE */
	VCONF_KEY_VOLUME_TYPE_ANDROID		/* VOLUME_TYPE_FIXED */
};

int _mm_sound_mgr_device_init(void)
{
	int i = 0;
	int ret = MM_ERROR_NONE;
	debug_fenter();

	for(i = 0 ; i < VOLUME_TYPE_MAX; i++) {
		vconf_notify_key_changed(g_volume_vconf[i], (void (*) (keynode_t *, void *))_mm_sound_mgr_device_volume_callback, (void *)i);
	}
	g_connected_device_list_s.list = g_connected_device_list;
	memset(g_device_id_array, 0, MAX_SUPPORT_DEVICE_NUM);

	/* forcedly, add internal sound device info. */
	ret = MMSoundMgrDeviceUpdateStatus (DEVICE_UPDATE_STATUS_CONNECTED, DEVICE_TYPE_BUILTIN_SPEAKER, DEVICE_IO_DIRECTION_OUT, DEVICE_ID_AUTO, DEVICE_NAME_BUILTIN_SPK, DEVICE_STATE_DEACTIVATED, NULL);
	if (ret) {
		debug_error("failed to MMSoundMgrDeviceUpdateStatus() failed (%d)\n", ret);
	}
	ret = MMSoundMgrDeviceUpdateStatus (DEVICE_UPDATE_STATUS_CONNECTED, DEVICE_TYPE_BUILTIN_RECEIVER, DEVICE_IO_DIRECTION_OUT, DEVICE_ID_AUTO, DEVICE_NAME_BUILTIN_RCV, DEVICE_STATE_DEACTIVATED, NULL);
	if (ret) {
		debug_error("failed to MMSoundMgrDeviceUpdateStatus() failed (%d)\n", ret);
	}
	ret = MMSoundMgrDeviceUpdateStatus (DEVICE_UPDATE_STATUS_CONNECTED, DEVICE_TYPE_BUILTIN_MIC, DEVICE_IO_DIRECTION_IN, DEVICE_ID_AUTO, DEVICE_NAME_BUILTIN_MIC, DEVICE_STATE_DEACTIVATED, NULL);
	if (ret) {
		debug_error("failed to MMSoundMgrDeviceUpdateStatus() failed (%d)\n", ret);
	}

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

int _mm_sound_mgr_device_get_audio_path(mm_sound_device_in *device_in, mm_sound_device_out *device_out)
{
	int ret = MM_ERROR_NONE;

#ifdef DEBUG_DETAIL
	debug_fenter();
#endif

	ret = MMSoundMgrSessionGetAudioPath(device_out, device_in);

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
	if (!is_same_pid_exists) {
		__mm_sound_mgr_ipc_freeze_send (FREEZE_COMMAND_INCLUDE, param->pid);
	}
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
	int ret = MM_ERROR_SOUND_INTERNAL;
	GList *list = NULL;
	_mm_sound_mgr_device_param_t *cb_param = NULL;

	debug_fenter();

	MMSOUND_ENTER_CRITICAL_SECTION_WITH_RETURN(&g_volume_cb_mutex, MM_ERROR_SOUND_INTERNAL);

	for (list = g_volume_cb_list; list != NULL; list = list->next) {
		cb_param = (_mm_sound_mgr_device_param_t *)list->data;
		if ((cb_param) && (cb_param->pid == param->pid) && (!strcmp(cb_param->name, param->name))) {
			g_volume_cb_list = g_list_remove(g_volume_cb_list, cb_param);
			__mm_sound_mgr_ipc_freeze_send (FREEZE_COMMAND_INCLUDE, param->pid);
			g_free(cb_param);
			ret = MM_ERROR_NONE;
			break;
		}
	}

	MMSOUND_LEAVE_CRITICAL_SECTION(&g_volume_cb_mutex);

	debug_fleave();
	return ret;
}

int __mm_sound_mgr_device_check_process(int pid)
{
	DIR *dir = NULL;
	char check_path[128] = "";
	int exist = MM_ERROR_NONE;

	memset(check_path, '\0', sizeof(check_path));
	snprintf(check_path, sizeof(check_path) - 1, "/proc/%d", pid);

	dir = opendir(check_path);
	if (dir == NULL) {
		switch (errno) {
			case ENOENT:
				debug_error("pid %d does not exist anymore\n", pid);
				exist = MM_ERROR_SOUND_INTERNAL;
				break;
			case EACCES:
				debug_error("Permission denied\n");
				break;
			case EMFILE:
				debug_error("Too many file descriptors in use by process\n");
				break;
			case ENFILE:
				debug_error("Too many files are currently open in the system\n");
				break;
			default:
				debug_error("Other error : %d\n", errno);
				break;
		}
	} else {
		debug_warning("pid : %d still alive\n", pid);
		if (-1 == closedir(dir)) {
			debug_error("closedir failed with errno : %d\n", errno);
		}
	}
	return exist;
}

int _mm_sound_mgr_device_get_current_connected_dev_list(const _mm_sound_mgr_device_param_t *param, GList **device_list)
{
	int ret = MM_ERROR_NONE;
	mm_sound_device_t *device_node = NULL;

#ifdef DEBUG_DETAIL
	debug_fenter();
#endif
	MMSOUND_ENTER_CRITICAL_SECTION_WITH_RETURN(&g_connected_device_list_mutex, MM_ERROR_SOUND_INTERNAL);

	*device_list = g_connected_device_list;
	debug_msg("address of g_connected_device_list[0x%x]", g_connected_device_list);

FINISH:
	MMSOUND_LEAVE_CRITICAL_SECTION(&g_connected_device_list_mutex);
#ifdef DEBUG_DETAIL
	debug_fleave();
#endif
	return ret;
}

int _mm_sound_mgr_device_add_connected_callback(const _mm_sound_mgr_device_param_t *param)
{
	int ret = MM_ERROR_NONE;
	GList *list = NULL;
	_mm_sound_mgr_device_param_t *cb_param = NULL;
	bool is_already_set = FALSE;

#ifdef DEBUG_DETAIL
	debug_fenter();
#endif

	MMSOUND_ENTER_CRITICAL_SECTION_WITH_RETURN(&g_device_connected_cb_mutex, MM_ERROR_SOUND_INTERNAL);

	for (list = g_device_connected_cb_list; list != NULL; list = list->next) {
		cb_param = (_mm_sound_mgr_device_param_t *)list->data;
		if (cb_param && (cb_param->pid == param->pid)) {
			cb_param->callback = param->callback;
			cb_param->cbdata = param->cbdata;
			is_already_set = TRUE;
			break;
		}
	}

	if (!is_already_set) {
		cb_param = g_malloc(sizeof(_mm_sound_mgr_device_param_t));
		memcpy(cb_param, param, sizeof(_mm_sound_mgr_device_param_t));
		g_device_connected_cb_list = g_list_append(g_device_connected_cb_list, cb_param);
		if (g_device_connected_cb_list) {
			debug_log("device connected cb registered for pid [%d]", cb_param->pid);
		} else {
			debug_error("g_list_append failed\n");
			ret = MM_ERROR_SOUND_INTERNAL;
			goto FINISH;
		}

		__mm_sound_mgr_ipc_freeze_send (FREEZE_COMMAND_EXCLUDE, param->pid);
	}

FINISH:
	MMSOUND_LEAVE_CRITICAL_SECTION(&g_device_connected_cb_mutex);
#ifdef DEBUG_DETAIL
	debug_fleave();
#endif
	return ret;
}

int _mm_sound_mgr_device_remove_connected_callback(const _mm_sound_mgr_device_param_t *param)
{
	int ret = MM_ERROR_SOUND_INTERNAL;
	GList *list = NULL;
	bool is_same_pid_exists = false;
	_mm_sound_mgr_device_param_t *cb_param = NULL;

	debug_fenter();

	MMSOUND_ENTER_CRITICAL_SECTION_WITH_RETURN(&g_device_connected_cb_mutex, MM_ERROR_SOUND_INTERNAL);

	for (list = g_device_connected_cb_list; list != NULL; list = list->next) {
		cb_param = (_mm_sound_mgr_device_param_t *)list->data;
		if (cb_param && (cb_param->pid == param->pid)) {
			g_device_connected_cb_list = g_list_remove(g_device_connected_cb_list, cb_param);
			g_free(cb_param);
			ret = MM_ERROR_NONE;
			break;
		}
	}

	/* Check for PID still exists in the list, if not include freeze */
	for (list = g_device_connected_cb_list; list != NULL; list = list->next) {
		cb_param = (_mm_sound_mgr_device_param_t *)list->data;
		if ((cb_param) && (cb_param->pid == param->pid)) {
			is_same_pid_exists = true;
			break;
		}
	}
	if (!is_same_pid_exists) {
		__mm_sound_mgr_ipc_freeze_send (FREEZE_COMMAND_INCLUDE, param->pid);
	}
	MMSOUND_LEAVE_CRITICAL_SECTION(&g_device_connected_cb_mutex);

	debug_fleave();
	return ret;
}

int _mm_sound_mgr_device_add_info_changed_callback(const _mm_sound_mgr_device_param_t *param)
{
	int ret = MM_ERROR_NONE;
	GList *list = NULL;
	_mm_sound_mgr_device_param_t *cb_param = NULL;
	bool is_already_set = FALSE;

#ifdef DEBUG_DETAIL
	debug_fenter();
#endif

	MMSOUND_ENTER_CRITICAL_SECTION_WITH_RETURN(&g_device_info_changed_cb_mutex, MM_ERROR_SOUND_INTERNAL);

	for (list = g_device_info_changed_cb_list; list != NULL; list = list->next) {
		cb_param = (_mm_sound_mgr_device_param_t *)list->data;
		if (cb_param && (cb_param->pid == param->pid)) {
			cb_param->callback = param->callback;
			cb_param->cbdata = param->cbdata;
			is_already_set = TRUE;
			break;
		}
	}

	if (!is_already_set) {
		cb_param = g_malloc(sizeof(_mm_sound_mgr_device_param_t));
		memcpy(cb_param, param, sizeof(_mm_sound_mgr_device_param_t));
		g_device_info_changed_cb_list = g_list_append(g_device_info_changed_cb_list, cb_param);
		if (g_device_info_changed_cb_list) {
			debug_log("device information changed cb registered for pid [%d]", cb_param->pid);
		} else {
			debug_error("g_list_append failed\n");
			ret = MM_ERROR_SOUND_INTERNAL;
			goto FINISH;
		}

		__mm_sound_mgr_ipc_freeze_send (FREEZE_COMMAND_EXCLUDE, param->pid);
	}

FINISH:
	MMSOUND_LEAVE_CRITICAL_SECTION(&g_device_info_changed_cb_mutex);
#ifdef DEBUG_DETAIL
	debug_fleave();
#endif
	return ret;
}

int _mm_sound_mgr_device_remove_info_changed_callback(const _mm_sound_mgr_device_param_t *param)
{
	int ret = MM_ERROR_SOUND_INTERNAL;
	GList *list = NULL;
	bool is_same_pid_exists = false;
	_mm_sound_mgr_device_param_t *cb_param = NULL;

	debug_fenter();

	MMSOUND_ENTER_CRITICAL_SECTION_WITH_RETURN(&g_device_info_changed_cb_mutex, MM_ERROR_SOUND_INTERNAL);

	for (list = g_device_info_changed_cb_list; list != NULL; list = list->next) {
		cb_param = (_mm_sound_mgr_device_param_t *)list->data;
		if (cb_param && (cb_param->pid == param->pid)) {
			g_device_info_changed_cb_list = g_list_remove(g_device_info_changed_cb_list, cb_param);
			g_free(cb_param);
			ret = MM_ERROR_NONE;
			break;
		}
	}

	/* Check for PID still exists in the list, if not include freeze */
	for (list = g_device_info_changed_cb_list; list != NULL; list = list->next) {
		cb_param = (_mm_sound_mgr_device_param_t *)list->data;
		if ((cb_param) && (cb_param->pid == param->pid)) {
			is_same_pid_exists = true;
			break;
		}
	}
	if (!is_same_pid_exists) {
		__mm_sound_mgr_ipc_freeze_send (FREEZE_COMMAND_INCLUDE, param->pid);
	}
	MMSOUND_LEAVE_CRITICAL_SECTION(&g_device_info_changed_cb_mutex);

	debug_fleave();
	return ret;
}

/* debug_warning ("cb_list = %p, cb_param = %p, pid=[%d]", x, cb_param, (cb_param)? cb_param->pid : -1); \ remove logs*/
#define CLEAR_DEAD_CB_LIST(x)  do { \
	debug_warning ("cb_list = %p, cb_param = %p, pid=[%d]", x, cb_param, (cb_param)? cb_param->pid : -1); \
	if (x && cb_param && __mm_sound_mgr_device_check_process (cb_param->pid) != 0) { \
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

static void _clear_device_connected_cb_list_func (_mm_sound_mgr_device_param_t * cb_param, gpointer user_data)
{
	CLEAR_DEAD_CB_LIST(g_device_connected_cb_list);
}

static void _clear_device_info_changed_cb_list_func (_mm_sound_mgr_device_param_t * cb_param, gpointer user_data)
{
	CLEAR_DEAD_CB_LIST(g_device_info_changed_cb_list);
}

static int _mm_sound_mgr_device_volume_callback(keynode_t* node, void* data)
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

	MMSOUND_ENTER_CRITICAL_SECTION_WITH_RETURN(&g_volume_cb_mutex, MM_ERROR_SOUND_INTERNAL);

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

int _mm_sound_mgr_device_set_sound_path_for_active_device(mm_sound_device_out playback, mm_sound_device_in capture)
{
	int ret = MM_ERROR_NONE;

	debug_fenter();
	ret = MMSoundMgrSessionSetSoundPathForActiveDevice(playback, capture);

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

int _mm_sound_mgr_device_update_volume()
{
	int i=0;
	char* str = NULL;
	int ret = MM_ERROR_NONE;

	for (i=0; i<VOLUME_TYPE_MAX; i++) {
		/* Update vconf */
		str = vconf_get_str(g_volume_vconf[i]);
		/* FIXME : Need to check history */
		/*
		if (vconf_set_str_no_fdatasync(g_volume_vconf[i], str) != 0) {
			debug_error("vconf_set_str_no_fdatasync(%s) failed", g_volume_vconf[i]);
			ret = MM_ERROR_SOUND_INTERNAL;
			continue;
		}
		*/
		if(str != NULL) {
			free(str);
			str = NULL;
		}
	}

	return ret;
}

#define RELEASE_DEVICE_INFO_ID(x_id) \
do { \
	if (g_device_id_array[x_id] == 1) { \
		g_device_id_array[x_id] = 0; \
	} else { \
		debug_error("could not release the id[%d]\n", x_id); \
	} \
} while(0)

#define SET_DEVICE_INFO_ID_AUTO(x_device_h) \
do { \
	int device_id = 0; \
	int cnt = 0; \
	if (!x_device_h) { \
		debug_error("device_h is null, could not set device id"); \
		break; \
	} \
	for (cnt = 0; cnt < MAX_SUPPORT_DEVICE_NUM; cnt++) { \
		if (g_device_id_array[cnt] == 0) { \
			break; \
		} \
	} \
	if (cnt == MAX_SUPPORT_DEVICE_NUM) { \
		debug_error("could not get a new id, device array is full\n"); \
		device_id = -1; \
	} else { \
		device_id = cnt; \
		g_device_id_array[cnt] = 1; \
	} \
	x_device_h->id = device_id; \
} while(0)

#define GET_DEVICE_INFO_ID(x_device_h, x_id) \
do { \
	if (!x_device_h) { \
		debug_error("device_h is null, could not get device id"); \
		break; \
	} \
	if (x_id) \
		*x_id = x_device_h->id; \
} while(0)

#define SET_DEVICE_INFO_TYPE(x_device_h, x_type) \
do { \
	if (!x_device_h) { \
		debug_error("device_h is null, could not set device type"); \
		break; \
	} \
	x_device_h->type = x_type; \
} while(0)

#define SET_DEVICE_INFO_IO_DIRECTION(x_device_h, x_io_direction) \
do { \
	if (!x_device_h) { \
		debug_error("device_h is null, could not set device io direction"); \
		break; \
	} \
	x_device_h->io_direction = x_io_direction; \
} while(0)

#define GET_DEVICE_INFO_IO_DIRECTION(x_device_h, x_io_direction) \
do { \
	if (!x_device_h) { \
		debug_error("device_h is null, could not get device io direction"); \
		break; \
	} \
	x_io_direction = x_device_h->io_direction; \
} while(0)

#define SET_DEVICE_INFO_NAME(x_device_h, x_name) \
do { \
	if (!x_device_h) { \
		debug_error("device_h is null, could not set device name"); \
		break; \
	} \
	if(x_name) { \
		int size = strlen(x_name); \
		memcpy (x_device_h->name, x_name, (MAX_DEVICE_NAME_NUM > size)? size : MAX_DEVICE_NAME_NUM-1); \
	} \
} while(0)

#define SET_DEVICE_INFO_STATE(x_device_h, x_state) \
do { \
	if (!x_device_h) { \
		debug_error("device_h is null, could not set device state"); \
		break; \
	} \
	x_device_h->state = x_state; \
} while(0)

#define GET_DEVICE_INFO_STATE(x_device_h, x_state) \
do { \
	if (!x_device_h) { \
		debug_error("device_h is null, could not get device state"); \
		break; \
	} \
	x_state = x_device_h->state; \
} while(0)

static const char* device_update_str[] =
{
	"DISCONNECTED",
	"CONNECTED",
	"STATE",
	"IO-DIRECTION",
};

static const char* device_type_str[] =
{
	"BUILTIN_SPK",
	"BUILTIN_RCV",
	"BUILTIN_MIC",
	"AUDIOJACK",
	"BLUETOOTH",
	"HDMI",
	"MIRRORING",
	"USB_AUDIO",
};

static const char* device_state_str[] =
{
	"DEACTIVATED",
	"ACTIVATED",
};

static const char* device_io_direction_str[] =
{
	"IN",
	"OUT",
	"BOTH",
};

static int __mm_sound_mgr_device_check_flags_to_trigger (int device_flags, mm_sound_device_t *device_h, bool *is_good_to_go)
{
	bool need_to_go = false;
	int need_to_check_for_io_direction = device_flags & DEVICE_IO_DIRECTION_FLAGS;
	int need_to_check_for_state = device_flags & DEVICE_STATE_FLAGS;
	int need_to_check_for_type = device_flags & DEVICE_TYPE_FLAGS;

	debug_warning("device_h[0x%x], device_flags[0x%x], need_to_check(io_direction[0x%x],state[0x%x],type[0x%x])\n",
			device_h, device_flags, need_to_check_for_io_direction, need_to_check_for_state, need_to_check_for_type);

	if(!device_h) {
		return MM_ERROR_INVALID_ARGUMENT;
	}
	if (device_flags == DEVICE_ALL_FLAG) {
		*is_good_to_go = true;
		return MM_ERROR_NONE;
	}

	if (need_to_check_for_io_direction) {
		if ((device_h->io_direction == DEVICE_IO_DIRECTION_IN) && (device_flags & DEVICE_IO_DIRECTION_IN_FLAG)) {
			need_to_go = true;
		} else if ((device_h->io_direction == DEVICE_IO_DIRECTION_OUT) && (device_flags & DEVICE_IO_DIRECTION_OUT_FLAG)) {
			need_to_go = true;
		} else if ((device_h->io_direction == DEVICE_IO_DIRECTION_BOTH) && (device_flags & DEVICE_IO_DIRECTION_BOTH_FLAG)) {
			need_to_go = true;
		}
		if (need_to_go) {
			if (!need_to_check_for_state && !need_to_check_for_type) {
				*is_good_to_go = true;
				return MM_ERROR_NONE;
			}
		} else {
			*is_good_to_go = false;
			return MM_ERROR_NONE;
		}
	}
	if (need_to_check_for_state) {
		need_to_go = false;
		if ((device_h->state == DEVICE_STATE_DEACTIVATED) && (device_flags & DEVICE_STATE_DEACTIVATED_FLAG)) {
			need_to_go = true;
		} else if ((device_h->state == DEVICE_STATE_ACTIVATED) && (device_flags & DEVICE_STATE_ACTIVATED_FLAG)) {
			need_to_go = true;
		}
		if (need_to_go) {
			if (!need_to_check_for_type) {
				*is_good_to_go = true;
				return MM_ERROR_NONE;
			}
		} else {
			*is_good_to_go = false;
			return MM_ERROR_NONE;
		}
	}
	if (need_to_check_for_type) {
		need_to_go = false;
		bool is_internal_device = IS_INTERNAL_DEVICE(device_h->type);
		if (is_internal_device && (device_flags & DEVICE_TYPE_INTERNAL_FLAG)) {
			need_to_go = true;
		} else if (!is_internal_device && (device_flags & DEVICE_TYPE_EXTERNAL_FLAG)) {
			need_to_go = true;
		}
		if (need_to_go) {
			*is_good_to_go = true;
			return MM_ERROR_NONE;
		} else {
			*is_good_to_go = false;
			return MM_ERROR_NONE;
		}
	}
	return MM_ERROR_NONE;
}

int _mm_sound_mgr_device_connected_callback(mm_sound_device_t *device_h, bool is_connected)
{
	int ret = MM_ERROR_NONE;
	GList *list = NULL;
	_mm_sound_mgr_device_param_t *cb_param = NULL;
	mm_ipc_msg_t msg;
	bool is_good_to_go = true;

	debug_fenter();

	MMSOUND_ENTER_CRITICAL_SECTION_WITH_RETURN(&g_device_connected_cb_mutex, MM_ERROR_SOUND_INTERNAL);

	/* Update list for dead process */
	 g_list_foreach (g_device_connected_cb_list, (GFunc)_clear_device_connected_cb_list_func, NULL);

	for (list = g_device_connected_cb_list; list != NULL; list = list->next) {
		cb_param = (_mm_sound_mgr_device_param_t *)list->data;
		if (!cb_param) {
			continue;
		}
		/* check flags */
		ret = __mm_sound_mgr_device_check_flags_to_trigger (cb_param->device_flags, device_h, &is_good_to_go);
		if (!ret && is_good_to_go) {
			if (cb_param->callback) {
				memset(&msg, 0, sizeof(mm_ipc_msg_t));
				SOUND_MSG_SET(msg.sound_msg, MM_SOUND_MSG_INF_DEVICE_CONNECTED_CB, 0, MM_ERROR_NONE, cb_param->pid);
				//msg.sound_msg.device_handle = device_h;
				memcpy (&(msg.sound_msg.device_handle), device_h, sizeof(mm_sound_device_t));
				msg.sound_msg.is_connected = is_connected;
				msg.sound_msg.callback = cb_param->callback;
				msg.sound_msg.cbdata = cb_param->cbdata;

				ret = _MMIpcCBSndMsg(&msg);
				if (ret != MM_ERROR_NONE) {
					debug_error("Fail to send callback message (%x)\n", ret);
					goto FINISH;
				}
			}
		} else {
			if (ret) {
				debug_error("failed to __mm_sound_mgr_device_check_flags_to_trigger(), ret[0x%x], is_good_to_go[%d]\n", ret, is_good_to_go);
			} else {
				debug_warning("No need to trigger the callback, is_good_to_go[%d]\n", ret, is_good_to_go);
			}
		}
	}

FINISH:
	MMSOUND_LEAVE_CRITICAL_SECTION(&g_device_connected_cb_mutex);

	debug_fleave();
	return ret;
}

int _mm_sound_mgr_device_info_changed_callback(mm_sound_device_t *device_h, int changed_info_type)
{
	int ret = MM_ERROR_NONE;
	GList *list = NULL;
	_mm_sound_mgr_device_param_t *cb_param = NULL;
	mm_ipc_msg_t msg;
	bool is_good_to_go = true;

	debug_fenter();

	MMSOUND_ENTER_CRITICAL_SECTION_WITH_RETURN(&g_device_info_changed_cb_mutex, MM_ERROR_SOUND_INTERNAL);

	/* Update list for dead process */
	 g_list_foreach (g_device_info_changed_cb_list, (GFunc)_clear_device_info_changed_cb_list_func, NULL);

	for (list = g_device_info_changed_cb_list; list != NULL; list = list->next) {
		cb_param = (_mm_sound_mgr_device_param_t *)list->data;
		if (!cb_param) {
			continue;
		}
		/* check flags */
		ret = __mm_sound_mgr_device_check_flags_to_trigger (cb_param->device_flags, device_h, &is_good_to_go);
		if (!ret && is_good_to_go) {
			if (cb_param->callback) {
				memset(&msg, 0, sizeof(mm_ipc_msg_t));
				SOUND_MSG_SET(msg.sound_msg, MM_SOUND_MSG_INF_DEVICE_INFO_CHANGED_CB, 0, MM_ERROR_NONE, cb_param->pid);
				memcpy (&(msg.sound_msg.device_handle), device_h, sizeof(mm_sound_device_t));
				msg.sound_msg.changed_device_info_type = changed_info_type;
				msg.sound_msg.callback = cb_param->callback;
				msg.sound_msg.cbdata = cb_param->cbdata;

				ret = _MMIpcCBSndMsg(&msg);
				if (ret != MM_ERROR_NONE) {
					debug_error("Fail to send callback message (%x)\n", ret);
					goto FINISH;
				}
			}
		} else {
			debug_error("failed to __mm_sound_mgr_device_check_flags_to_trigger(), ret[0x%x], is_good_to_go[%d]\n", ret, is_good_to_go);
		}
	}

FINISH:
	MMSOUND_LEAVE_CRITICAL_SECTION(&g_device_info_changed_cb_mutex);

	debug_fleave();
	return ret;
}

int _mm_sound_mgr_device_connected_dev_list_find_item (mm_sound_device_t **device_h, device_type_e device_type, int id)
{
	int ret = MM_ERROR_NONE;

	MMSOUND_ENTER_CRITICAL_SECTION_WITH_RETURN(&g_connected_device_list_mutex, MM_ERROR_SOUND_INTERNAL);

	GList *list = NULL;
	mm_sound_device_t *device_node = NULL;
	for (list = g_connected_device_list; list != NULL; list = list->next) {
		device_node = (mm_sound_device_t *)list->data;
		if (((device_node) && (device_node->type == device_type) && (id == DEVICE_ID_AUTO)) || ((device_node) && (device_node->id == id))) {
			debug_log("found the suitable item, device_type[%d], id[%d]\n", device_node->type, device_node->id);
			break;
		}
	}
	if (list != NULL) {
		*device_h = device_node;
	} else {
		debug_error("no suitable item for device_type[%d], id[%d]\n", device_type, id);
		ret = MM_ERROR_SOUND_NO_DATA;
	}

	MMSOUND_LEAVE_CRITICAL_SECTION(&g_connected_device_list_mutex);

	return ret;
}

int _mm_sound_mgr_device_connected_dev_list_add_item (mm_sound_device_t **device_h)
{
	int ret = MM_ERROR_NONE;
	mm_sound_device_t *device_node = g_malloc0(sizeof(mm_sound_device_t));
	debug_log("new device_node[0x%x]\n", device_node);

	MMSOUND_ENTER_CRITICAL_SECTION_WITH_RETURN(&g_connected_device_list_mutex, MM_ERROR_SOUND_INTERNAL);

	g_connected_device_list = g_list_append(g_connected_device_list, device_node);

	MMSOUND_LEAVE_CRITICAL_SECTION(&g_connected_device_list_mutex);

	*device_h = device_node;

	return ret;
}

int _mm_sound_mgr_device_connected_dev_list_remove_item (mm_sound_device_t *device_h)
{
	int ret = MM_ERROR_NONE;
	GList *list = NULL;
	mm_sound_device_t *device_node = NULL;
	if (!device_h) {
		debug_error("device_h is null\n");
		ret = MM_ERROR_INVALID_ARGUMENT;
	} else {
		MMSOUND_ENTER_CRITICAL_SECTION_WITH_RETURN(&g_connected_device_list_mutex, MM_ERROR_SOUND_INTERNAL);
		for (list = g_connected_device_list; list != NULL; list = list->next) {
			device_node = (mm_sound_device_t *)list->data;
			if ((device_node) && (device_node->type == device_h->type)) {
				RELEASE_DEVICE_INFO_ID(device_node->id);
				g_connected_device_list = g_list_remove(g_connected_device_list, device_node);
				g_free(device_node);
				break;
			}
		}
		MMSOUND_LEAVE_CRITICAL_SECTION(&g_connected_device_list_mutex);
	}

	debug_log("=============== Remain devices =========================================================\n");
	_mm_sound_mgr_device_connected_dev_list_dump();

	return ret;
}

int _mm_sound_mgr_device_connected_dev_list_dump ()
{
	int ret = MM_ERROR_NONE;
	GList *list = NULL;
	mm_sound_device_t *device_node = NULL;
	int count = 0;

	debug_log("================================== device list : start =====================================\n");
	MMSOUND_ENTER_CRITICAL_SECTION_WITH_RETURN(&g_connected_device_list_mutex, MM_ERROR_SOUND_INTERNAL);
	for (list = g_connected_device_list; list != NULL; list = list->next) {
		device_node = (mm_sound_device_t *)list->data;
		if (device_node) {
			debug_log(" idx[%d] >>>  type[%12s], id[%2d], io_direction[%4s], state[%12s], name[%s]\n",
				count++, device_type_str[device_node->type], device_node->id, device_io_direction_str[device_node->io_direction], device_state_str[device_node->state], device_node->name);
		}
	}
	MMSOUND_LEAVE_CRITICAL_SECTION(&g_connected_device_list_mutex);
	debug_log("================================== device list : end =======================================\n");

	return ret;
}

int MMSoundMgrDeviceGetIoDirectionById (int id, device_io_direction_e *io_direction)
{
	int ret = MM_ERROR_NONE;
	/* find the device handle by device_type and(or) id */
	mm_sound_device_t *device_h = NULL;
	ret = _mm_sound_mgr_device_connected_dev_list_find_item (&device_h, 0, id);
	if(!ret && device_h) {
		debug_log("found device_h[0x%x] for id[%d], io_direction[%d]\n", device_h, id, device_h->io_direction);
		*io_direction = device_h->io_direction;
	} else {
		debug_error("failed to _mm_sound_mgr_device_connected_dev_list_find_item() for id[%d], ret[0x%x]\n", id, ret);
		ret = MM_ERROR_SOUND_NO_DATA;
	}
	return ret;
}

/* if DEVICE is connected => add new entry (alloc id) => update status => call callback func */
/* if DEVICE is disconnected => update status(deactivated) => call changed info callback => copy disconnected entry */
/*                            => remove the entry (dealloc id) => call disconnected callback with copied entry */
/* if DEVICE's info. is changed => get device_h (by type and(or) id) => update status => call callback func */
int MMSoundMgrDeviceUpdateStatus (device_update_status_e update_status, device_type_e device_type, device_io_direction_e io_direction, int id, const char* name, device_state_e state, int *alloc_id)
{
	debug_warning ("[UpdateDeviceStatus] update_for[%12s], type[%11s], direction[%4s], id[%d], name[%s], state[%11s], alloc_id[0x%x]\n",
			device_update_str[update_status], device_type_str[device_type], device_io_direction_str[io_direction], id, name, device_state_str[state], alloc_id);
	switch (update_status) {
	case DEVICE_UPDATE_STATUS_CONNECTED:
	{
		int ret = MM_ERROR_NONE;
		/* find the device handle by device_type and(or) id */
		mm_sound_device_t *device_h = NULL;
		ret = _mm_sound_mgr_device_connected_dev_list_find_item (&device_h, device_type, id);
		if(ret == MM_ERROR_SOUND_NO_DATA && !device_h) {
			/* add new entry for the type */
			ret = _mm_sound_mgr_device_connected_dev_list_add_item (&device_h);
			if (ret || !device_h) {
				debug_error("could not add a new item for this type[%d], device_h[0x%x], ret[0x%x]\n", device_type, device_h, ret);
			} else {
				debug_error("update info for this device_h[0x%x]\n", device_h);
				/* update device information */
				/* 1. type */
				SET_DEVICE_INFO_TYPE(device_h, device_type);
				/* 2. io direction */
				SET_DEVICE_INFO_IO_DIRECTION(device_h, io_direction);
				/* 3. name */
				SET_DEVICE_INFO_NAME(device_h, name);
				/* 4. state */
				SET_DEVICE_INFO_STATE(device_h, DEVICE_STATE_DEACTIVATED);
				/* 4. id */
				SET_DEVICE_INFO_ID_AUTO(device_h);
				GET_DEVICE_INFO_ID(device_h, alloc_id);

				_mm_sound_mgr_device_connected_dev_list_dump();

				/* trigger callback functions */
				ret = _mm_sound_mgr_device_connected_callback(device_h, true);
				if (ret) {
					debug_error("failed to _mm_sound_mgr_device_connected_callback() for this type[%d], device_h[0x%x], ret[0x%x]\n", device_type, device_h, ret);
				}
				/******************************************************/
				/* ACTIVATE this device for routing if needed */
				/* do here */
				/******************************************************/

			}
		} else {
			debug_error("failed to _mm_sound_mgr_device_connected_dev_list_find_item() for this type[%d], device_h[0x%x], ret[0x%x]\n", device_type, device_h, ret);
		}
	}
		break;

	case DEVICE_UPDATE_STATUS_DISCONNECTED:
	{
		int ret = MM_ERROR_NONE;
		/* find the device handle by device_type and(or) id */
		mm_sound_device_t *device_h = NULL;
		_mm_sound_mgr_device_connected_dev_list_dump();
		ret = _mm_sound_mgr_device_connected_dev_list_find_item (&device_h, device_type, id);
		if(!ret && device_h) {
			debug_log("found device_h[0x%x] for this type[%d]/id[%d]\n", device_h, device_type, id);

			/********************************************************/
			/* DEACTIVATE this device for routing if needed */
			/* do here */
			/********************************************************/

			/* update device information */
			/* 1. state */
			device_state_e prev_state = DEVICE_STATE_DEACTIVATED;
			GET_DEVICE_INFO_STATE(device_h, prev_state);
			SET_DEVICE_INFO_STATE(device_h, DEVICE_STATE_DEACTIVATED);

			/* trigger callback functions */
			if (prev_state == DEVICE_STATE_ACTIVATED) {
				ret = _mm_sound_mgr_device_info_changed_callback(device_h, DEVICE_CHANGED_INFO_STATE);
				if (ret) {
					debug_error("failed to _mm_sound_mgr_device_info_changed_callback(DEVICE_CHANGED_INFO_STATE), device_h[0x%x], ret[0x%x]\n", device_h, ret);
				}
			}
			/* copy this device structure */
			mm_sound_device_t copied_device_h;
			memcpy ((void*)&copied_device_h, (void*)device_h, sizeof(mm_sound_device_t));

			/* remove this device from the list */
			ret = _mm_sound_mgr_device_connected_dev_list_remove_item (device_h);
			if (ret) {
				debug_error("failed to _mm_sound_mgr_device_connected_dev_list_remove_item(), device_h[0x%x], ret[0x%x]\n", device_h, ret);
			}

			ret = _mm_sound_mgr_device_connected_callback(&copied_device_h, false);
			if (ret) {
				debug_error("failed to _mm_sound_mgr_device_connected_callback() for this type[%d], device_h[0x%x], ret[0x%x]\n", device_type, copied_device_h, ret);
			}

		} else {
			debug_error("failed to _mm_sound_mgr_device_connected_dev_list_find_item() for this type[%d], device_h[0x%x], ret[0x%x]\n", device_type, device_h, ret);
		}
	}
		break;

	case DEVICE_UPDATE_STATUS_CHANGED_INFO_STATE:
	{
		int ret = MM_ERROR_NONE;
		/* find the device handle by device_type and(or) id */
		mm_sound_device_t *device_h = NULL;
		ret = _mm_sound_mgr_device_connected_dev_list_find_item (&device_h, device_type, id);
		if(!ret && device_h) {
			debug_log("we are going to change STATE[to %d] of this item[0x%x]\n", state, device_h);

			device_state_e prev_state = DEVICE_STATE_DEACTIVATED;
			GET_DEVICE_INFO_STATE(device_h, prev_state);
			if (prev_state == state) {
				debug_log("previous state is same as new one, no need to set\n");
			} else {
				SET_DEVICE_INFO_STATE(device_h, state);
				_mm_sound_mgr_device_connected_dev_list_dump();
				ret = _mm_sound_mgr_device_info_changed_callback(device_h, DEVICE_CHANGED_INFO_STATE);
				if (ret) {
					debug_error("failed to _mm_sound_mgr_device_info_changed_callback(DEVICE_CHANGED_INFO_STATE), device_h[0x%x], ret[0x%x]\n", device_h, ret);
				}
			}

		} else {
			debug_error("failed to _mm_sound_mgr_device_connected_dev_list_find_item() for this type[%d], device_h[0x%x], ret[0x%x]\n", device_type, device_h, ret);
		}
	}
		break;

	case DEVICE_UPDATE_STATUS_CHANGED_INFO_IO_DIRECTION:
	{
		int ret = MM_ERROR_NONE;
		/* find the device handle by device_type and(or) id */
		mm_sound_device_t *device_h = NULL;
		ret = _mm_sound_mgr_device_connected_dev_list_find_item (&device_h, device_type, id);
		if(!ret && device_h) {
			debug_log("we are going to change IO DIRECTION[to %d] of this item[0x%x]\n", io_direction, device_h);

			device_io_direction_e prev_io_direction = DEVICE_IO_DIRECTION_IN;
			GET_DEVICE_INFO_IO_DIRECTION(device_h, prev_io_direction);
			if (prev_io_direction == io_direction) {
				debug_log("previous io_direction is same as new one, no need to set\n");
			} else {
				SET_DEVICE_INFO_IO_DIRECTION(device_h, io_direction);
				_mm_sound_mgr_device_connected_dev_list_dump();
				ret = _mm_sound_mgr_device_info_changed_callback(device_h, DEVICE_CHANGED_INFO_IO_DIRECTION);
				if (ret) {
					debug_error("failed to _mm_sound_mgr_device_info_changed_callback(DEVICE_CHANGED_INFO_DIRECTION), device_h[0x%x], ret[0x%x]\n", device_h, ret);
				}
			}

		} else {
			debug_error("failed to _mm_sound_mgr_device_connected_dev_list_find_item() for this type[%d], device_h[0x%x], ret[0x%x]\n", device_type, device_h, ret);
		}
	}
		break;
	}

	return MM_ERROR_NONE;
}

int MMSoundMgrDeviceUpdateStatusWithoutNotification (device_update_status_e update_status, device_type_e device_type, device_io_direction_e io_direction, int id, const char* name, device_state_e state, int *alloc_id)
{
	debug_warning ("[Update Device Status] update_status[%d], device type[%11s], io_direction[%4s], id[%d], name[%s], state[%11s], alloc_id[0x%x]\n",
						update_status, device_type_str[device_type], device_io_direction_str[io_direction], id, name, device_state_str[state], alloc_id);
	switch (update_status) {
	case DEVICE_UPDATE_STATUS_CONNECTED:
	{
		int ret = MM_ERROR_NONE;
		/* find the device handle by device_type and(or) id */
		mm_sound_device_t *device_h = NULL;
		ret = _mm_sound_mgr_device_connected_dev_list_find_item (&device_h, device_type, id);
		if(ret == MM_ERROR_SOUND_NO_DATA && !device_h) {
			debug_log("we are going to add new item for this type[%d]/id[%d]\n", device_type, id);
			/* add new entry for the type */
			ret = _mm_sound_mgr_device_connected_dev_list_add_item (&device_h);
			if (ret || !device_h) {
				debug_error("could not add a new item for this type[%d], device_h[0x%x], ret[0x%x]\n", device_type, device_h, ret);
			} else {
				debug_error("update info for this device_h[0x%x]\n", device_h);
				/* update device information */
				/* 1. type */
				SET_DEVICE_INFO_TYPE(device_h, device_type);
				/* 2. io direction */
				SET_DEVICE_INFO_IO_DIRECTION(device_h, io_direction);
				/* 3. name */
				SET_DEVICE_INFO_NAME(device_h, name);
				/* 4. state */
				SET_DEVICE_INFO_STATE(device_h, DEVICE_STATE_DEACTIVATED);
				/* 4. id */
				SET_DEVICE_INFO_ID_AUTO(device_h);
				GET_DEVICE_INFO_ID(device_h, alloc_id);

				_mm_sound_mgr_device_connected_dev_list_dump();

				/******************************************************/
				/* ACTIVATE this device for routing if needed */
				/* do here */
				/******************************************************/

			}
		} else {
			debug_error("failed to _mm_sound_mgr_device_connected_dev_list_find_item() for this type[%d], device_h[0x%x], ret[0x%x]\n", device_type, device_h, ret);
		}
	}
		break;

	case DEVICE_UPDATE_STATUS_DISCONNECTED:
	{
		int ret = MM_ERROR_NONE;
		/* find the device handle by device_type and(or) id */
		mm_sound_device_t *device_h = NULL;
		_mm_sound_mgr_device_connected_dev_list_dump();
		ret = _mm_sound_mgr_device_connected_dev_list_find_item (&device_h, device_type, id);
		if(!ret && device_h) {
			debug_log("found device_h[0x%x] for this type[%d]/id[%d]\n", device_h, device_type, id);

			/********************************************************/
			/* DEACTIVATE this device for routing if needed */
			/* do here */
			/********************************************************/

			/* update device information */
			/* 1. state */
			device_state_e prev_state = DEVICE_STATE_DEACTIVATED;
			GET_DEVICE_INFO_STATE(device_h, prev_state);
			SET_DEVICE_INFO_STATE(device_h, DEVICE_STATE_DEACTIVATED);

			/* remove this device from the list */
			ret = _mm_sound_mgr_device_connected_dev_list_remove_item (device_h);
			if (ret) {
				debug_error("failed to _mm_sound_mgr_device_connected_dev_list_remove_item(), device_h[0x%x], ret[0x%x]\n", device_h, ret);
			}

		} else {
			debug_error("failed to _mm_sound_mgr_device_connected_dev_list_find_item() for this type[%d], device_h[0x%x], ret[0x%x]\n", device_type, device_h, ret);
		}
	}
		break;

	case DEVICE_UPDATE_STATUS_CHANGED_INFO_STATE:
	{
		int ret = MM_ERROR_NONE;
		/* find the device handle by device_type and(or) id */
		mm_sound_device_t *device_h = NULL;
		ret = _mm_sound_mgr_device_connected_dev_list_find_item (&device_h, device_type, id);
		if(!ret && device_h) {
			debug_log("we are going to change STATE[to %d] of this item[0x%x]\n", state, device_h);

			device_state_e prev_state = DEVICE_STATE_DEACTIVATED;
			GET_DEVICE_INFO_STATE(device_h, prev_state);
			if (prev_state == state) {
				debug_warning("previous state is same as new one, no need to set\n");
			} else {
				SET_DEVICE_INFO_STATE(device_h, state);
				_mm_sound_mgr_device_connected_dev_list_dump();
			}

		} else {
			debug_error("failed to _mm_sound_mgr_device_connected_dev_list_find_item() for this type[%d], device_h[0x%x], ret[0x%x]\n", device_type, device_h, ret);
		}
	}
		break;

	case DEVICE_UPDATE_STATUS_CHANGED_INFO_IO_DIRECTION:
	{
		int ret = MM_ERROR_NONE;
		/* find the device handle by device_type and(or) id */
		mm_sound_device_t *device_h = NULL;
		ret = _mm_sound_mgr_device_connected_dev_list_find_item (&device_h, device_type, id);
		if(!ret && device_h) {
			debug_log("we are going to change IO DIRECTION[to %d] of this item[0x%x]\n", io_direction, device_h);

			device_io_direction_e prev_io_direction = DEVICE_IO_DIRECTION_IN;
			GET_DEVICE_INFO_IO_DIRECTION(device_h, prev_io_direction);
			if (prev_io_direction == io_direction) {
				debug_warning("previous io_direction is same as new one, no need to set\n");
			} else {
				SET_DEVICE_INFO_IO_DIRECTION(device_h, io_direction);
				_mm_sound_mgr_device_connected_dev_list_dump();
			}

		} else {
			debug_error("failed to _mm_sound_mgr_device_connected_dev_list_find_item() for this type[%d], device_h[0x%x], ret[0x%x]\n", device_type, device_h, ret);
		}
	}
		break;
	}

	return MM_ERROR_NONE;
}
