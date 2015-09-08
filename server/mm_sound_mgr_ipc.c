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

#include <errno.h>
#include <pthread.h>
#include "include/mm_sound_mgr_common.h"
#include "include/mm_sound_mgr_ipc.h"

#include "../include/mm_sound_common.h"
#include "../include/mm_sound_msg.h"
#include "include/mm_sound_thread_pool.h"
#include "include/mm_sound_mgr_codec.h"
#include "include/mm_sound_mgr_device.h"
#include <mm_error.h>
#include <mm_debug.h>

#include <audio-session-manager.h>

#include <gio/gio.h>

#define SHM_OPEN

#ifdef PULSE_CLIENT
#include "include/mm_sound_mgr_pulse.h"
#endif

/* workaround for AF volume gain tuning */
#define MM_SOUND_AF_FILE_PREFIX "/opt/ug/res/sounds/ug-camera-efl/sounds/af_"
#define PROC_DBUS_OBJECT 	"/Org/Tizen/ResourceD/Process"
#define PROC_DBUS_INTERFACE 	"org.tizen.resourced.process"
#define PROC_DBUS_METHOD 	"ProcExclude"

/* message id */
int g_rcvid;
int g_sndid;
int g_cbid;
static pthread_mutex_t g_msg_snd_cb_mutex = PTHREAD_MUTEX_INITIALIZER;


/* Msg processing */
static void _MMSoundMgrRun(void *data);
static int _MMSoundMgrStopCB(int msgid, void* msgcallback, void *msgdata, int id);	/* msg_type means instance for client */
static int _MMSoundMgrIpcPlayFile(int *codechandle, mm_ipc_msg_t *msg);	/* codechandle means codec slotid */
static int _MMSoundMgrIpcPlayMemory(int *codechandle, mm_ipc_msg_t *msg);
static int _MMSoundMgrIpcStop(mm_ipc_msg_t *msg);
static int _MMSoundMgrIpcPlayDTMF(int *codechandle, mm_ipc_msg_t *msg);
static int __mm_sound_mgr_ipc_is_route_available(mm_ipc_msg_t *msg, bool *is_available);
static int __mm_sound_mgr_ipc_foreach_available_route_cb(mm_ipc_msg_t *msg);
static int __mm_sound_mgr_ipc_set_active_route(mm_ipc_msg_t *msg);
static int __mm_sound_mgr_ipc_set_active_route_auto(void);
static int __mm_sound_mgr_ipc_get_active_device(mm_ipc_msg_t *msg, mm_sound_device_in *device_in, mm_sound_device_out *device_out);
static int __mm_sound_mgr_ipc_add_active_device_changed_cb(mm_ipc_msg_t *msg);
static int __mm_sound_mgr_ipc_remove_active_device_changed_cb(mm_ipc_msg_t *msg);
static int __mm_sound_mgr_ipc_add_available_device_changed_cb(mm_ipc_msg_t *msg);
static int __mm_sound_mgr_ipc_remove_available_device_changed_cb(mm_ipc_msg_t *msg);
static int __mm_sound_mgr_ipc_add_volume_changed_cb(mm_ipc_msg_t *msg);
static int __mm_sound_mgr_ipc_remove_volume_changed_cb(mm_ipc_msg_t *msg);
static int _MMIpcRecvMsg(int msgtype, mm_ipc_msg_t *msg);
static int _MMIpcSndMsg(mm_ipc_msg_t *msg);
static int __mm_sound_mgr_ipc_set_sound_path_for_active_device(mm_ipc_msg_t *msg);
static int __mm_sound_mgr_ipc_get_audio_path(mm_sound_device_in *device_in, mm_sound_device_out *device_out);
static int __mm_sound_mgr_ipc_get_current_connected_device_list(mm_ipc_msg_t *msg, GList **device_list, int *total_num);
static int __mm_sound_mgr_ipc_add_device_connected_cb(mm_ipc_msg_t *msg);
static int __mm_sound_mgr_ipc_remove_device_connected_cb(mm_ipc_msg_t *msg);
static int __mm_sound_mgr_ipc_add_device_info_changed_cb(mm_ipc_msg_t *msg);
static int __mm_sound_mgr_ipc_remove_device_info_changed_cb(mm_ipc_msg_t *msg);


int MMSoundMgrIpcInit(void)
{
	debug_fenter();

	/* create msg queue rcv, snd, cb */
	/* This func is called only once */
	g_rcvid = msgget(ftok(KEY_BASE_PATH, RCV_MSG), IPC_CREAT |0666);
	g_sndid = msgget(ftok(KEY_BASE_PATH, SND_MSG), IPC_CREAT |0666);
	g_cbid = msgget(ftok(KEY_BASE_PATH, CB_MSG), IPC_CREAT |0666);

	if ((g_rcvid == -1 || g_sndid == -1 || g_cbid == -1) != MM_ERROR_NONE) {
		if(errno == EACCES) {
			printf("Require ROOT permission.\n");
		} else if(errno == EEXIST) {
			printf("System memory is empty.\n");
		} else if(errno == ENOMEM) {
			printf("System memory is empty.\n");
		} else if(errno == ENOSPC) {
			printf("Resource is empty.\n");
		}
		debug_error("Fail to create msgid\n");
		exit(1);
		return MM_ERROR_SOUND_INTERNAL;
	}			

	debug_msg("Created server msg queue id : rcv[%d], snd[%d], cb[%d]\n", g_rcvid, g_sndid, g_cbid );

	g_type_init();

	debug_fleave();
	return MM_ERROR_NONE;
}

int MMSoundMgrIpcFini(void)
{
	return MM_ERROR_NONE;
}

int MMSoundMgrIpcReady(void)
{
	int ret = MM_ERROR_NONE;
	int err1, err2, err3;
	mm_ipc_msg_t msg = {0,};
	mm_ipc_msg_t resp  = {0,};

	debug_msg("Created server msg queue id : rcv[%d], snd[%d], cb[%d]\n", g_rcvid, g_sndid, g_cbid );

	/* Ready to recive message */
	while(1) {
		ret = _MMIpcRecvMsg(0, &msg);		
		if (ret != MM_ERROR_NONE) {
			debug_critical("Fail recieve message. \n");
			exit(1);
		}
			
		debug_msg("[ type:%d, id:%d, h:%d, vol:%d, vol_conf:%x keytone:%d, tone:%d, cb:%p, data:%p, prio:%d, route:%d ]\n",
				msg.sound_msg.msgtype, msg.sound_msg.msgid, msg.sound_msg.handle, msg.sound_msg.volume, msg.sound_msg.volume_config,
				msg.sound_msg.keytone, msg.sound_msg.tone, msg.sound_msg.callback, msg.sound_msg.cbdata, msg.sound_msg.priority, msg.sound_msg.handle_route);
		
		switch (msg.sound_msg.msgtype)
		{
		case MM_SOUND_MSG_REQ_FILE:
		case MM_SOUND_MSG_REQ_MEMORY:
		case MM_SOUND_MSG_REQ_STOP:
#ifdef PULSE_CLIENT
		case MM_SOUND_MSG_REQ_GET_AUDIO_ROUTE:
		case MM_SOUND_MSG_REQ_SET_AUDIO_ROUTE:
#endif
		case MM_SOUND_MSG_REQ_IS_BT_A2DP_ON:
		case MM_SOUND_MSG_REQ_DTMF:
		case MM_SOUND_MSG_REQ_IS_ROUTE_AVAILABLE:
		case MM_SOUND_MSG_REQ_FOREACH_AVAILABLE_ROUTE_CB:
		case MM_SOUND_MSG_REQ_SET_ACTIVE_ROUTE:
		case MM_SOUND_MSG_REQ_SET_ACTIVE_ROUTE_AUTO:
		case MM_SOUND_MSG_REQ_GET_ACTIVE_DEVICE:
		case MM_SOUND_MSG_REQ_ADD_ACTIVE_DEVICE_CB:
		case MM_SOUND_MSG_REQ_REMOVE_ACTIVE_DEVICE_CB:
		case MM_SOUND_MSG_REQ_ADD_AVAILABLE_ROUTE_CB:
		case MM_SOUND_MSG_REQ_REMOVE_AVAILABLE_ROUTE_CB:
		case MM_SOUND_MSG_REQ_ADD_VOLUME_CB:
		case MM_SOUND_MSG_REQ_REMOVE_VOLUME_CB:
		case MM_SOUND_MSG_REQ_SET_PATH_FOR_ACTIVE_DEVICE:
		case MM_SOUND_MSG_REQ_GET_AUDIO_PATH:
		case MM_SOUND_MSG_REQ_GET_CONNECTED_DEVICE_LIST:
		case MM_SOUND_MSG_REQ_ADD_DEVICE_CONNECTED_CB:
		case MM_SOUND_MSG_REQ_REMOVE_DEVICE_CONNECTED_CB:
		case MM_SOUND_MSG_REQ_ADD_DEVICE_INFO_CHANGED_CB:
		case MM_SOUND_MSG_REQ_REMOVE_DEVICE_INFO_CHANGED_CB:
			{
				/* Create msg to queue : this will be freed inside thread function after use */
				mm_ipc_msg_t* msg_to_queue = malloc (sizeof(mm_ipc_msg_t));
				if (msg_to_queue) {
					memcpy (msg_to_queue, &msg, sizeof (mm_ipc_msg_t));
					debug_log ("func = %p, alloc param(msg_to_queue) = %p\n", _MMSoundMgrRun, msg_to_queue);
					ret = MMSoundThreadPoolRun(msg_to_queue, _MMSoundMgrRun);
					/* In case of error condition */
					if (ret != MM_ERROR_NONE) {
						/* Do not send msg in Ready, Just print log */
						debug_critical("Fail to run thread [MgrRun]");
	
						SOUND_MSG_SET(resp.sound_msg, MM_SOUND_MSG_RES_ERROR, ret, -1, msg.sound_msg.msgid);
						ret = _MMIpcSndMsg(&resp);
						if (ret != MM_ERROR_NONE) {
							debug_error("Fail to send message in IPC ready\n");
					}
					}
				} else {
					debug_error ("failed to alloc msg\n");
				}
			}
			break;

		default:
			/*response err unknown operation*/;
			debug_critical("Error condition\n");
			debug_msg("The message Msg [%d] client id [%d]\n", msg.sound_msg.msgtype, msg.sound_msg.msgid);
			SOUND_MSG_SET(resp.sound_msg, MM_SOUND_MSG_RES_ERROR, ret, -1, msg.sound_msg.msgid);
			ret = _MMIpcSndMsg(&resp);
			if (ret != MM_ERROR_NONE) {
					debug_error("Fail to send message in IPC ready\n");
			}
			break;
		} /* end : switch (msg.sound_msg.msgtype) */
	}

	/* destroy msg queue */
	err1 = msgctl(g_rcvid, IPC_RMID, NULL);
	err2 = msgctl(g_sndid, IPC_RMID, NULL);
	err3 = msgctl(g_cbid, IPC_RMID, NULL);
	
	if (err1 == -1 ||err2 == -1 ||err3 ==-1) {
		debug_error("Base message node destroy fail");
		return MM_ERROR_SOUND_INTERNAL;
	}
	
	return MM_ERROR_NONE;
}

static void _MMSoundMgrRun(void *data)
{
	mm_ipc_msg_t respmsg = {0,};
	int ret = MM_ERROR_NONE;
	int instance;
	int handle = -1;
	bool is_available = 0;
	mm_sound_device_in device_in = MM_SOUND_DEVICE_IN_NONE;
	mm_sound_device_out device_out = MM_SOUND_DEVICE_OUT_NONE;
	mm_ipc_msg_t *msg = (mm_ipc_msg_t *)data;

	instance = msg->sound_msg.msgid;

	switch (msg->sound_msg.msgtype)
	{
	case MM_SOUND_MSG_REQ_FILE:
		debug_msg("==================== Recv REQUEST FILE MSG from pid(%d) ====================\n", instance);
		ret = _MMSoundMgrIpcPlayFile(&handle, msg);
		if ( ret != MM_ERROR_NONE)	{
			debug_error("Error to MM_SOUND_MSG_REQ_FILE\n");
			SOUND_MSG_SET(respmsg.sound_msg, MM_SOUND_MSG_RES_ERROR, -1, ret, instance);
		} else {
			SOUND_MSG_SET(respmsg.sound_msg, MM_SOUND_MSG_RES_FILE, handle, MM_ERROR_NONE, instance);
		}
		break;

	case MM_SOUND_MSG_REQ_MEMORY:
		debug_msg("==================== Recv REQUEST MEMORY MSG from pid(%d) ====================\n", instance);
		ret =_MMSoundMgrIpcPlayMemory(&handle, msg);
		if (ret != MM_ERROR_NONE) {
			debug_error("Error to MM_SOUND_MSG_REQ_MEMORY.\n");
			SOUND_MSG_SET(respmsg.sound_msg, MM_SOUND_MSG_RES_ERROR, -1, ret, instance);
		} else {
			SOUND_MSG_SET(respmsg.sound_msg, MM_SOUND_MSG_RES_MEMORY, handle, MM_ERROR_NONE, instance);
		}
		break;
		
	case MM_SOUND_MSG_REQ_STOP:
		debug_msg("==================== Recv STOP msg from pid(%d) ====================\n", instance);
		debug_msg("STOP Handle(codec slot ID) %d \n", msg->sound_msg.handle);
		ret = _MMSoundMgrIpcStop(msg);
		if ( ret != MM_ERROR_NONE) {
			debug_error("Error to MM_SOUND_MSG_REQ_STOP.\n");
			SOUND_MSG_SET(respmsg.sound_msg, MM_SOUND_MSG_RES_ERROR, -1, ret, instance);
		} else {
			SOUND_MSG_SET(respmsg.sound_msg, MM_SOUND_MSG_RES_STOP, msg->sound_msg.handle, MM_ERROR_NONE, instance);
		}
		break;
		
#ifdef PULSE_CLIENT
	case MM_SOUND_MSG_REQ_IS_BT_A2DP_ON:
		debug_msg("==================== Recv REQ_IS_BT_A2DP_ON msg from pid(%d) ====================\n", instance);
		MMSoundMgrPulseHandleIsBtA2DPOnReq (msg,_MMIpcSndMsg);
		return;
#endif // PULSE_CLIENT

	case MM_SOUND_MSG_REQ_DTMF:
		debug_msg("==================== Recv DTMF msg from pid(%d) ====================\n", instance);
		debug_msg(" Handle(codec slot ID) %d \n", msg->sound_msg.handle);
		ret = _MMSoundMgrIpcPlayDTMF(&handle, msg);
		if ( ret != MM_ERROR_NONE) {
			debug_error("Error to MM_SOUND_MSG_REQ_STOP.\n");
			SOUND_MSG_SET(respmsg.sound_msg, MM_SOUND_MSG_RES_ERROR, -1, ret, instance);
		} else {
			SOUND_MSG_SET(respmsg.sound_msg, MM_SOUND_MSG_RES_DTMF, handle, MM_ERROR_NONE, instance);
		}
		break;

	case MM_SOUND_MSG_REQ_IS_ROUTE_AVAILABLE:
		debug_msg("==================== Recv MM_SOUND_MSG_REQ_IS_ROUTE_AVAILABLE msg from pid(%d) ====================\n", instance);
		ret = __mm_sound_mgr_ipc_is_route_available(msg, &is_available);
		if (ret != MM_ERROR_NONE) {
			debug_error("Error to MM_SOUND_MSG_REQ_IS_ROUTE_AVAILABLE.\n");
			SOUND_MSG_SET(respmsg.sound_msg, MM_SOUND_MSG_RES_ERROR, -1, ret, instance);
		} else {
			SOUND_MSG_SET(respmsg.sound_msg, MM_SOUND_MSG_RES_IS_ROUTE_AVAILABLE, 0, MM_ERROR_NONE, instance);
			respmsg.sound_msg.is_available = is_available;
		}
		break;

	case MM_SOUND_MSG_REQ_FOREACH_AVAILABLE_ROUTE_CB:
		debug_msg("==================== Recv REQ_FOREACH_AVAILABLE_ROUTE_CB msg from pid(%d) ====================\n", instance);
		ret = __mm_sound_mgr_ipc_foreach_available_route_cb(&respmsg);
		if (ret != MM_ERROR_NONE) {
			debug_error("Error to MM_SOUND_MSG_REQ_FOREACH_AVAILABLE_ROUTE_CB.\n");
			SOUND_MSG_SET(respmsg.sound_msg, MM_SOUND_MSG_RES_ERROR, -1, ret, instance);
		} else {
			SOUND_MSG_SET(respmsg.sound_msg, MM_SOUND_MSG_RES_FOREACH_AVAILABLE_ROUTE_CB, 0, MM_ERROR_NONE, instance);
		}
		break;

	case MM_SOUND_MSG_REQ_SET_ACTIVE_ROUTE:
		debug_msg("==================== Recv REQ_SET_ACTIVE_ROUTE msg from pid(%d) ====================\n", instance);
		ret = __mm_sound_mgr_ipc_set_active_route(msg);
		if (ret != MM_ERROR_NONE) {
			debug_error("Error to MM_SOUND_MSG_REQ_SET_ACTIVE_ROUTE.\n");
			SOUND_MSG_SET(respmsg.sound_msg, MM_SOUND_MSG_RES_ERROR, -1, ret, instance);
		} else {
			SOUND_MSG_SET(respmsg.sound_msg, MM_SOUND_MSG_RES_SET_ACTIVE_ROUTE, 0, MM_ERROR_NONE, instance);
		}
		break;
	case MM_SOUND_MSG_REQ_SET_ACTIVE_ROUTE_AUTO:
		debug_msg("==================== Recv REQ_SET_ACTIVE_ROUTE_AUTO msg from pid(%d) ====================\n", instance);
		ret = __mm_sound_mgr_ipc_set_active_route_auto();
		if (ret != MM_ERROR_NONE) {
			debug_error("Error to MM_SOUND_MSG_REQ_SET_ACTIVE_ROUTE_AUTO.\n");
			SOUND_MSG_SET(respmsg.sound_msg, MM_SOUND_MSG_RES_ERROR, -1, ret, instance);
		} else {
			SOUND_MSG_SET(respmsg.sound_msg, MM_SOUND_MSG_RES_SET_ACTIVE_ROUTE_AUTO, 0, MM_ERROR_NONE, instance);
		}
		break;

	case MM_SOUND_MSG_REQ_GET_ACTIVE_DEVICE:
		debug_msg("==================== Recv REQ_GET_ACTIVE_DEVICE msg from pid(%d) ====================\n", instance);
		ret = __mm_sound_mgr_ipc_get_active_device(msg, &device_in, &device_out);
		if (ret != MM_ERROR_NONE) {
			debug_error("Error to MM_SOUND_MSG_REQ_GET_ACTIVE_DEVICE.\n");
			SOUND_MSG_SET(respmsg.sound_msg, MM_SOUND_MSG_RES_ERROR, -1, ret, instance);
		} else {
			SOUND_MSG_SET(respmsg.sound_msg, MM_SOUND_MSG_RES_GET_ACTIVE_DEVICE, 0, MM_ERROR_NONE, instance);
			respmsg.sound_msg.device_in = device_in;
			respmsg.sound_msg.device_out = device_out;
		}
		break;

	case MM_SOUND_MSG_REQ_ADD_ACTIVE_DEVICE_CB:
		debug_msg("==================== Recv REQ_ADD_ACTIVE_DEVICE_CB msg from pid(%d) ====================\n", instance);
		ret = __mm_sound_mgr_ipc_add_active_device_changed_cb(msg);
		if (ret != MM_ERROR_NONE) {
			debug_error("Error to MM_SOUND_MSG_REQ_ADD_DEVICE_CB.\n");
			SOUND_MSG_SET(respmsg.sound_msg, MM_SOUND_MSG_RES_ERROR, -1, ret, instance);
		} else {
			SOUND_MSG_SET(respmsg.sound_msg, MM_SOUND_MSG_RES_ADD_ACTIVE_DEVICE_CB, 0, MM_ERROR_NONE, instance);
		}
		break;

	case MM_SOUND_MSG_REQ_REMOVE_ACTIVE_DEVICE_CB:
		debug_msg("==================== Recv REQ_REMOVE_ACTIVE_DEVICE_CB msg from pid(%d) ====================\n", instance);
		ret = __mm_sound_mgr_ipc_remove_active_device_changed_cb(msg);
		if (ret != MM_ERROR_NONE) {
			debug_error("Error to MM_SOUND_MSG_REQ_REMOVE_DEVICE_CB.\n");
			SOUND_MSG_SET(respmsg.sound_msg, MM_SOUND_MSG_RES_ERROR, -1, ret, instance);
		} else {
			SOUND_MSG_SET(respmsg.sound_msg, MM_SOUND_MSG_RES_REMOVE_ACTIVE_DEVICE_CB, 0, MM_ERROR_NONE, instance);
		}
		break;

	case MM_SOUND_MSG_REQ_ADD_VOLUME_CB:
		debug_msg("==================== Recv REQ_ADD_VOLUME_CB msg from pid(%d) ====================\n", instance);
		ret = __mm_sound_mgr_ipc_add_volume_changed_cb(msg);
		if (ret != MM_ERROR_NONE) {
			debug_error("Error to MM_SOUND_MSG_REQ_VOLUME_CB.\n");
			SOUND_MSG_SET(respmsg.sound_msg, MM_SOUND_MSG_RES_ERROR, -1, ret, instance);
		} else {
			SOUND_MSG_SET(respmsg.sound_msg, MM_SOUND_MSG_RES_ADD_VOLUME_CB, 0, MM_ERROR_NONE, instance);
		}
		break;

	case MM_SOUND_MSG_REQ_REMOVE_VOLUME_CB:
		debug_msg("==================== Recv REQ_REMOVE_VOLUME_CB msg from pid(%d) ====================\n", instance);
		ret = __mm_sound_mgr_ipc_remove_volume_changed_cb(msg);
		if (ret != MM_ERROR_NONE) {
			debug_error("Error to MM_SOUND_MSG_REQ_REMOVE_VOLUME_CB.\n");
			SOUND_MSG_SET(respmsg.sound_msg, MM_SOUND_MSG_RES_ERROR, -1, ret, instance);
		} else {
			SOUND_MSG_SET(respmsg.sound_msg, MM_SOUND_MSG_RES_REMOVE_VOLUME_CB, 0, MM_ERROR_NONE, instance);
		}
		break;

	case MM_SOUND_MSG_REQ_ADD_AVAILABLE_ROUTE_CB:
		debug_msg("==================== Recv REQ_ADD_AVAILABLE_ROUTE_CB msg from pid(%d)====================\n", instance);
		ret = __mm_sound_mgr_ipc_add_available_device_changed_cb(msg);
		if (ret != MM_ERROR_NONE) {
			debug_error("Error to MM_SOUND_MSG_REQ_ADD_DEVICE_CB.\n");
			SOUND_MSG_SET(respmsg.sound_msg, MM_SOUND_MSG_RES_ERROR, -1, ret, instance);
		} else {
			SOUND_MSG_SET(respmsg.sound_msg, MM_SOUND_MSG_RES_ADD_AVAILABLE_ROUTE_CB, 0, MM_ERROR_NONE, instance);
		}
		break;

	case MM_SOUND_MSG_REQ_REMOVE_AVAILABLE_ROUTE_CB:
		debug_msg("==================== Recv REQ_REMOVE_AVAILABLE_ROUTE_CB msg from pid(%d)====================\n", instance);
		ret = __mm_sound_mgr_ipc_remove_available_device_changed_cb(msg);
		if (ret != MM_ERROR_NONE) {
			debug_error("Error to MM_SOUND_MSG_REQ_REMOVE_DEVICE_CB.\n");
			SOUND_MSG_SET(respmsg.sound_msg, MM_SOUND_MSG_RES_ERROR, -1, ret, instance);
		} else {
			SOUND_MSG_SET(respmsg.sound_msg, MM_SOUND_MSG_RES_REMOVE_AVAILABLE_ROUTE_CB, 0, MM_ERROR_NONE, instance);
		}
		break;

	case MM_SOUND_MSG_REQ_SET_PATH_FOR_ACTIVE_DEVICE:
		debug_msg("Recv MM_SOUND_MSG_REQ_SET_PATH_FOR_ACTIVE_DEVICE msg\n");
		ret = __mm_sound_mgr_ipc_set_sound_path_for_active_device(msg);
		if (ret != MM_ERROR_NONE) {
			debug_error("Error to MM_SOUND_MSG_REQ_SET_PATH_FOR_ACTIVE_DEVICE.\n");
			SOUND_MSG_SET(respmsg.sound_msg, MM_SOUND_MSG_RES_ERROR, -1, ret, instance);
		} else {
			SOUND_MSG_SET(respmsg.sound_msg, MM_SOUND_MSG_RES_SET_PATH_FOR_ACTIVE_DEVICE, 0, MM_ERROR_NONE, instance);
		}
		break;
	case MM_SOUND_MSG_REQ_GET_AUDIO_PATH:
		debug_msg("Recv MM_SOUND_MSG_REQ_GET_AUDIO_PATH msg\n");
		ret = __mm_sound_mgr_ipc_get_audio_path(&device_in, &device_out);
		if (ret != MM_ERROR_NONE) {
			debug_error("Error to MM_SOUND_MSG_REQ_GET_AUDIO_PATH.\n");
			SOUND_MSG_SET(respmsg.sound_msg, MM_SOUND_MSG_RES_ERROR, -1, ret, instance);
		} else {
			SOUND_MSG_SET(respmsg.sound_msg, MM_SOUND_MSG_RES_GET_AUDIO_PATH, 0, MM_ERROR_NONE, instance);
			respmsg.sound_msg.device_in = device_in;
			respmsg.sound_msg.device_out = device_out;
		}
		break;

	case MM_SOUND_MSG_REQ_GET_CONNECTED_DEVICE_LIST:
	{
		GList *device_list = NULL;
		GList *list = NULL;
		int total_num_of_device = 0;
		int i = 0;
		debug_msg("==================== Recv REQ_GET_CONNECTED_DEVICE_LIST msg from pid(%d) ====================\n", instance);
		ret = __mm_sound_mgr_ipc_get_current_connected_device_list(msg, &device_list, &total_num_of_device);
		if (ret != MM_ERROR_NONE) {
			debug_error("Error to REQ_GET_CONNECTED_DEVICE_LIST. ret[0x%x]\n", ret);
			SOUND_MSG_SET(respmsg.sound_msg, MM_SOUND_MSG_RES_ERROR, -1, ret, instance);
		} else {
			SOUND_MSG_SET(respmsg.sound_msg, MM_SOUND_MSG_RES_GET_CONNECTED_DEVICE_LIST, 0, MM_ERROR_NONE, instance);
			respmsg.sound_msg.total_device_num = total_num_of_device;
			for (list = device_list; list != NULL; list = list->next) {
				memcpy(&(respmsg.sound_msg.device_handle), (mm_sound_device_t*)list->data, sizeof(mm_sound_device_t));
				debug_msg("[Server] memory copied to msg device handle(handle[0x%x], type[%d], id[%d]), before sending device info, total [%d] msg remains\n",
						&(respmsg.sound_msg.device_handle), ((mm_sound_device_t*)list->data)->type, ((mm_sound_device_t*)list->data)->id, total_num_of_device);
				if (total_num_of_device-- > 1) {
					ret = _MMIpcSndMsg(&respmsg);
					if (ret != MM_ERROR_NONE) {
						debug_error ("Fail to send message \n");
					}
				}
			}
		}
	}
		break;

	case MM_SOUND_MSG_REQ_ADD_DEVICE_CONNECTED_CB:
		debug_msg("==================== Recv REQ_ADD_DEVICE_CONNECTED_CB msg from pid(%d) ====================\n", instance);
		ret = __mm_sound_mgr_ipc_add_device_connected_cb(msg);
		if (ret != MM_ERROR_NONE) {
			debug_error("Error to MM_SOUND_MSG_REQ_ADD_DEVICE_CONNECTED_CB.\n");
			SOUND_MSG_SET(respmsg.sound_msg, MM_SOUND_MSG_RES_ERROR, -1, ret, instance);
		} else {
			SOUND_MSG_SET(respmsg.sound_msg, MM_SOUND_MSG_RES_ADD_DEVICE_CONNECTED_CB, 0, MM_ERROR_NONE, instance);
		}
		break;

	case MM_SOUND_MSG_REQ_REMOVE_DEVICE_CONNECTED_CB:
		debug_msg("==================== Recv REQ_REMOVE_DEVICE_CONNECTED_CB msg from pid(%d) ====================\n", instance);
		ret = __mm_sound_mgr_ipc_remove_device_connected_cb(msg);
		if (ret != MM_ERROR_NONE) {
			debug_error("Error to MM_SOUND_MSG_REQ_REMOVE_DEVICE_CONNECTED_CB.\n");
			SOUND_MSG_SET(respmsg.sound_msg, MM_SOUND_MSG_RES_ERROR, -1, ret, instance);
		} else {
			SOUND_MSG_SET(respmsg.sound_msg, MM_SOUND_MSG_RES_REMOVE_DEVICE_CONNECTED_CB, 0, MM_ERROR_NONE, instance);
		}
		break;

	case MM_SOUND_MSG_REQ_ADD_DEVICE_INFO_CHANGED_CB:
		debug_msg("==================== Recv REQ_ADD_DEVICE_INFO_CHANGED_CB msg from pid(%d) ====================\n", instance);
		ret = __mm_sound_mgr_ipc_add_device_info_changed_cb(msg);
		if (ret != MM_ERROR_NONE) {
			debug_error("Error to MM_SOUND_MSG_REQ_ADD_DEVICE_INFO_CHANGED_CB.\n");
			SOUND_MSG_SET(respmsg.sound_msg, MM_SOUND_MSG_RES_ERROR, -1, ret, instance);
		} else {
			SOUND_MSG_SET(respmsg.sound_msg, MM_SOUND_MSG_RES_ADD_DEVICE_INFO_CHANGED_CB, 0, MM_ERROR_NONE, instance);
		}
		break;

	case MM_SOUND_MSG_REQ_REMOVE_DEVICE_INFO_CHANGED_CB:
		debug_msg("==================== Recv REQ_REMOVE_DEVICE_INFO_CHANGED_CB msg from pid(%d) ====================\n", instance);
		ret = __mm_sound_mgr_ipc_remove_device_info_changed_cb(msg);
		if (ret != MM_ERROR_NONE) {
			debug_error("Error to MM_SOUND_MSG_REQ_REMOVE_DEVICE_INFO_CHANGED_CB.\n");
			SOUND_MSG_SET(respmsg.sound_msg, MM_SOUND_MSG_RES_ERROR, -1, ret, instance);
		} else {
			SOUND_MSG_SET(respmsg.sound_msg, MM_SOUND_MSG_RES_REMOVE_DEVICE_INFO_CHANGED_CB, 0, MM_ERROR_NONE, instance);
		}
		break;

	default:
		/* Response error unknown operation */;
		debug_critical("Unexpected msg. %d for PID:%d\n", msg->sound_msg.msgtype, msg->sound_msg.msgid );		
		SOUND_MSG_SET(respmsg.sound_msg, MM_SOUND_MSG_RES_ERROR, -1, MM_ERROR_SOUND_INTERNAL, instance);
		break;
	} /* switch (msg->sound_msg.msgtype) */

	ret = _MMIpcSndMsg(&respmsg);
	if (ret != MM_ERROR_NONE) {
		debug_error ("Fail to send message \n");
	}

	debug_log("Sent msg to client msgid [%d] [codechandle %d][message type %d] (code 0x%08X)\n",
		msg->sound_msg.msgid, respmsg.sound_msg.handle, respmsg.sound_msg.msgtype, respmsg.sound_msg.code);

	if (msg) {
#ifdef DEBUG_DETAIL
		debug_log ("Free mm_ipc_msg_t [%p]\n", msg);
#endif
		free (msg);
	}
}

static int _MMSoundMgrStopCB(int msgid, void* msgcallback, void *msgdata, int id)
{
	/* msgid means client instance(msg_type) must be unique */
	mm_ipc_msg_t resp = {0,};
	int ret = MM_ERROR_SOUND_INTERNAL;
	
	SOUND_MSG_SET(resp.sound_msg, MM_SOUND_MSG_INF_STOP_CB, 0, MM_ERROR_NONE, msgid);
	resp.sound_msg.callback = msgcallback;
	resp.sound_msg.cbdata = msgdata;
	resp.sound_msg.handle = id;
	
	ret = _MMIpcCBSndMsg(&resp);
	if (ret != MM_ERROR_NONE) {
		debug_error("Fail to send callback message\n");
	}

	return MM_ERROR_NONE;
}

static int _MMSoundMgrIpcPlayFile(int *codechandle, mm_ipc_msg_t *msg)
{
	mmsound_mgr_codec_param_t param = {0,};
	MMSourceType *source = NULL;
	int ret = MM_ERROR_NONE;
	int mm_session_type = MM_SESSION_TYPE_MEDIA;
	
	/* Set source */
	source = (MMSourceType*)malloc(sizeof(MMSourceType));
	if (!source) {
		debug_error("malloc fail!!\n");
		return MM_ERROR_OUT_OF_MEMORY;
	}

	ret = mm_source_open_file(msg->sound_msg.filename, source, MM_SOURCE_CHECK_DRM_CONTENTS);
	if(ret != MM_ERROR_NONE) {
		debug_error("Fail to open file\n");
		if (source) {
			free(source);
		}
		return ret;		
	}

	/* Set sound player parameter */
	param.tone = msg->sound_msg.tone;
	param.repeat_count = msg->sound_msg.repeat;
	param.param = (void*)msg->sound_msg.msgid; //this is pid of client
	param.volume = msg->sound_msg.volume;
	param.volume_config = msg->sound_msg.volume_config;
	param.priority = msg->sound_msg.priority;
	mm_session_type = msg->sound_msg.session_type;
	param.callback = _MMSoundMgrStopCB;
	param.keytone =  msg->sound_msg.keytone;
	param.msgcallback = msg->sound_msg.callback;
	param.msgdata = msg->sound_msg.cbdata;
	param.source = source;
	param.handle_route = msg->sound_msg.handle_route;
	param.enable_session = msg->sound_msg.enable_session;

	/* workaround for AF volume gain tuning */
	if (strncmp(msg->sound_msg.filename, MM_SOUND_AF_FILE_PREFIX, strlen(MM_SOUND_AF_FILE_PREFIX)) == 0) {
		param.volume_config |= VOLUME_GAIN_AF;
		debug_msg("Volume Gain AF\n");
	}

	debug_msg("File[%s] DTMF[%d] Loop[%d] Volume[%f] Priority[%d] VolCfg[0x%x] callback[%p] param[%d] src_type[%d] src_ptr[%p] keytone[%d] route[%d] enable_session[%d]",
			msg->sound_msg.filename,
			param.tone, param.repeat_count, param.volume, param.priority, param.volume_config, param.callback,
			(int)param.param, param.source->type, param.source->ptr, param.keytone, param.handle_route, param.enable_session);

	//convert mm_session_type to asm_event_type
	switch(mm_session_type)
	{
	case MM_SESSION_TYPE_MEDIA:
		param.session_type = ASM_EVENT_MEDIA_MMSOUND;
		break;
	case MM_SESSION_TYPE_NOTIFY:
		param.session_type = ASM_EVENT_NOTIFY;
		break;
	case MM_SESSION_TYPE_ALARM:
		param.session_type = ASM_EVENT_ALARM;
		break;
	case MM_SESSION_TYPE_EMERGENCY:
		param.session_type = ASM_EVENT_EMERGENCY;
		break;
	case MM_SESSION_TYPE_CALL:
		param.session_type = ASM_EVENT_CALL;
		break;
	case MM_SESSION_TYPE_VIDEOCALL:
		param.session_type = ASM_EVENT_VIDEOCALL;
		break;
	case MM_SESSION_TYPE_VOIP:
		param.session_type = ASM_EVENT_VOIP;
		break;
	default:
		debug_error("Unknown session type - use default shared type. %s %d\n", __FUNCTION__, __LINE__);
		param.session_type = ASM_EVENT_MEDIA_MMSOUND;
		break;
	}


	ret = MMSoundMgrCodecPlay(codechandle, &param);
	if ( ret != MM_ERROR_NONE) {
		debug_error("Will be closed a sources, codechandle : 0x%08X\n", *codechandle);
		mm_source_close(source);
		if (source) {
			free(source);
			source = NULL;
		}
		return ret;
	}

	return MM_ERROR_NONE;
}
static int _MMSoundMgrIpcStop(mm_ipc_msg_t *msg)
{
	int ret = MM_ERROR_NONE;

	ret = MMSoundMgrCodecStop(msg->sound_msg.handle);

	if (ret != MM_ERROR_NONE) {
		debug_error("Fail to stop sound\n");
		return ret;
	}

	return MM_ERROR_NONE;
}

static int _MMSoundMgrIpcPlayMemory(int *codechandle, mm_ipc_msg_t *msg)
{
	mmsound_mgr_codec_param_t param = {0,};
	MMSourceType *source = NULL;
	int ret = MM_ERROR_NONE;
	int shm_fd = -1;
	void* mmap_buf = NULL;

#ifndef SHM_OPEN
	if ((shmid = shmget((key_t)(msg->sound_msg.sharedkey), msg->sound_msg.memsize, 0)) == -1)
	{
		if(errno == ENOENT)
		{
			debug_error("Not initialized.\n");
		}
		else if(errno == EACCES)
		{
			debug_error("Require ROOT permission.\n");
		}
		else if(errno == ENOSPC)
		{
			debug_critical("Resource is empty.\n");
		}
		return MM_ERROR_SOUND_INTERNAL;
	}

	source = (MMSourceType*)malloc(sizeof(MMSourceType));

	if (mm_source_open_full_memory(shmat(shmid, 0, 0), msg->sound_msg.memsize, 0, source) != MM_ERROR_NONE)
	{
		debug_error("Fail to set source\n");
		free(source);
		return MM_ERROR_SOUND_INTERNAL;
	}
#else

	debug_msg("Shm file name : %s\n", msg->sound_msg.filename);

	if(msg->sound_msg.sharedkey != 1) {
		debug_error("NOT memory interface\n");
		return MM_ERROR_SOUND_INVALID_PATH;
	}

	shm_fd = shm_open(msg->sound_msg.filename, O_RDONLY, 0666);
	if(shm_fd < 0) {
		debug_error("Fail to open\n");
		return MM_ERROR_SOUND_INTERNAL;
	}

	mmap_buf = mmap (0, MEMTYPE_SUPPORT_MAX, PROT_READ , MAP_SHARED, shm_fd, 0);
	if (mmap_buf == MAP_FAILED) {
		perror("Fail to mmap\n");
		debug_error("MMAP failed \n");
		return MM_ERROR_SOUND_INTERNAL;
	}

	/* Set source */
	source = (MMSourceType*)malloc(sizeof(MMSourceType));
	if(!source) {
		debug_error("Can not allocate memory");
		munmap(mmap_buf, MEMTYPE_SUPPORT_MAX);
		mmap_buf = NULL;
		return MM_ERROR_OUT_OF_MEMORY;
	}

	if (mm_source_open_full_memory(mmap_buf, msg->sound_msg.memsize, 0, source) != MM_ERROR_NONE) {
		debug_error("Fail to set source\n");
		free(source);
		return MM_ERROR_SOUND_INTERNAL;
	}
#endif	

	/* Set sound player parameter */
	param.tone = msg->sound_msg.tone;
	param.repeat_count = msg->sound_msg.repeat;
	param.param = (void*)msg->sound_msg.msgid;
	param.volume = msg->sound_msg.volume;
	param.volume_config = msg->sound_msg.volume_config;
	param.callback = _MMSoundMgrStopCB;
	param.keytone =  msg->sound_msg.keytone;
	param.msgcallback = msg->sound_msg.callback;
	param.msgdata = msg->sound_msg.cbdata;
	param.priority = msg->sound_msg.priority;
	param.source = source;
	param.enable_session = msg->sound_msg.enable_session;

	debug_msg("DTMF %d\n", param.tone);
	debug_msg("Loop %d\n", param.repeat_count);
	debug_msg("Volume %d\n",param.volume);
	debug_msg("Priority %d\n",param.priority);
	debug_msg("VolumeConfig %x\n",param.volume_config);
	debug_msg("callback %p\n", param.callback);
	debug_msg("param %d\n", (int)param.param);
	debug_msg("source type %d\n", param.source->type);
	debug_msg("source ptr %p\n", param.source->ptr);
	debug_msg("keytone %d\n", param.keytone);
	debug_msg("enable_session %d\n", param.enable_session);

	ret = MMSoundMgrCodecPlay(codechandle, &param);
	if ( ret != MM_ERROR_NONE) {
		debug_error("Will be closed a sources, codec handle : [0x%d]\n", *codechandle);
		mm_source_close(source);
		if (source) {
			free(source);
		}
		return ret;		
	}

	return ret;
}

static int _MMSoundMgrIpcPlayDTMF(int *codechandle, mm_ipc_msg_t *msg)
{
	mmsound_mgr_codec_param_t param = {0,};
	int ret = MM_ERROR_NONE;

	/* Set sound player parameter */
	param.tone = msg->sound_msg.tone;
	param.repeat_count = msg->sound_msg.repeat;
	param.param = (void*)msg->sound_msg.msgid;
	param.volume = msg->sound_msg.volume;
	param.volume_config = msg->sound_msg.volume_config;
	param.priority = msg->sound_msg.priority;
	param.callback = _MMSoundMgrStopCB;
	param.msgcallback = msg->sound_msg.callback;
	param.msgdata = msg->sound_msg.cbdata;
	param.session_options = msg->sound_msg.session_options;
	param.enable_session = msg->sound_msg.enable_session;

	//convert mm_session_type to asm_event_type
	switch(msg->sound_msg.session_type)
	{
		case MM_SESSION_TYPE_MEDIA:
			param.session_type = ASM_EVENT_MEDIA_MMSOUND;
			break;
		case MM_SESSION_TYPE_NOTIFY:
			param.session_type = ASM_EVENT_NOTIFY;
			break;
		case MM_SESSION_TYPE_ALARM:
			param.session_type = ASM_EVENT_ALARM;
			break;
		case MM_SESSION_TYPE_EMERGENCY:
			param.session_type = ASM_EVENT_EMERGENCY;
			break;
		case MM_SESSION_TYPE_CALL:
			param.session_type = ASM_EVENT_CALL;
			break;
		case MM_SESSION_TYPE_VIDEOCALL:
			param.session_type = ASM_EVENT_VIDEOCALL;
			break;
		case MM_SESSION_TYPE_VOIP:
			param.session_type = ASM_EVENT_VOIP;
			break;
		default:
			debug_error("Unknown session type - use default media type. %s %d\n", __FUNCTION__, __LINE__);
			param.session_type = ASM_EVENT_MEDIA_MMSOUND;
			break;
	}

	debug_msg("DTMF %d\n", param.tone);
	debug_msg("Loop %d\n", param.repeat_count);
	debug_msg("Volume %d\n",param.volume);
	debug_msg("VolumeConfig %x\n",param.volume_config);
	debug_msg("Priority %d\n", param.priority);
	debug_msg("callback %p\n", param.callback);
	debug_msg("param %d\n", (int)param.param);
	debug_msg("session %d\n", param.session_type);
	debug_msg("session options %x\n", param.session_options);
	debug_msg("enable_session %d\n", param.enable_session);

	ret = MMSoundMgrCodecPlayDtmf(codechandle, &param);
	if ( ret != MM_ERROR_NONE) {
		debug_error("Will be closed a sources, codec handle : [0x%d]\n", *codechandle);
		return ret;		
	}

	return ret;
}

static int __mm_sound_mgr_ipc_is_route_available(mm_ipc_msg_t *msg, bool *is_available)
{
	_mm_sound_mgr_device_param_t param;
	int ret = MM_ERROR_NONE;

	param.route = msg->sound_msg.route;
	ret = _mm_sound_mgr_device_is_route_available(&param, is_available);

	return ret;
}

static int __mm_sound_mgr_ipc_foreach_available_route_cb(mm_ipc_msg_t *msg)
{
	int ret = MM_ERROR_NONE;

	ret = _mm_sound_mgr_device_foreach_available_route_cb(msg);

	return ret;
}

static int __mm_sound_mgr_ipc_set_active_route(mm_ipc_msg_t *msg)
{
	_mm_sound_mgr_device_param_t param;
	int ret = MM_ERROR_NONE;

	param.pid = msg->sound_msg.msgid;
	param.route = msg->sound_msg.route;
	param.need_broadcast = msg->sound_msg.need_broadcast;

	ret = _mm_sound_mgr_device_set_active_route(&param);

	return ret;
}

static int __mm_sound_mgr_ipc_set_active_route_auto(void)
{
	int ret = MM_ERROR_NONE;

	ret = _mm_sound_mgr_device_set_active_route_auto();

	return ret;
}

static int __mm_sound_mgr_ipc_get_active_device(mm_ipc_msg_t *msg, mm_sound_device_in *device_in, mm_sound_device_out *device_out)
{
	_mm_sound_mgr_device_param_t param;
	int ret = MM_ERROR_NONE;

	memset(&param, 0x00, sizeof(_mm_sound_mgr_device_param_t));
	param.pid = msg->sound_msg.msgid;

	ret = _mm_sound_mgr_device_get_active_device(&param, device_in, device_out);

	return ret;
}

static int __mm_sound_mgr_ipc_add_active_device_changed_cb(mm_ipc_msg_t *msg)
{
	_mm_sound_mgr_device_param_t param;
	int ret = MM_ERROR_NONE;

	memset(&param, 0x00, sizeof(_mm_sound_mgr_device_param_t));
	param.pid = msg->sound_msg.msgid;
	param.callback = msg->sound_msg.callback;
	param.cbdata = msg->sound_msg.cbdata;
	MMSOUND_STRNCPY(param.name, msg->sound_msg.name, MM_SOUND_NAME_NUM);

	ret = _mm_sound_mgr_device_add_active_device_callback(&param);

	return ret;
}

static int __mm_sound_mgr_ipc_remove_active_device_changed_cb(mm_ipc_msg_t *msg)
{
	_mm_sound_mgr_device_param_t param;
	int ret = MM_ERROR_NONE;

	memset(&param, 0x00, sizeof(_mm_sound_mgr_device_param_t));
	param.pid = msg->sound_msg.msgid;
	MMSOUND_STRNCPY(param.name, msg->sound_msg.name, MM_SOUND_NAME_NUM);

	ret = _mm_sound_mgr_device_remove_active_device_callback(&param);

	return ret;
}

static int __mm_sound_mgr_ipc_add_volume_changed_cb(mm_ipc_msg_t *msg)
{
	_mm_sound_mgr_device_param_t param;
	int ret = MM_ERROR_NONE;

	memset(&param, 0x00, sizeof(_mm_sound_mgr_device_param_t));
	param.pid = msg->sound_msg.msgid;
	param.callback = msg->sound_msg.callback;
	param.cbdata = msg->sound_msg.cbdata;
	MMSOUND_STRNCPY(param.name, msg->sound_msg.name, MM_SOUND_NAME_NUM);

	ret = _mm_sound_mgr_device_add_volume_callback(&param);

	return ret;
}

static int __mm_sound_mgr_ipc_remove_volume_changed_cb(mm_ipc_msg_t *msg)
{
	_mm_sound_mgr_device_param_t param;
	int ret = MM_ERROR_NONE;

	memset(&param, 0x00, sizeof(_mm_sound_mgr_device_param_t));
	param.pid = msg->sound_msg.msgid;
	MMSOUND_STRNCPY(param.name, msg->sound_msg.name, MM_SOUND_NAME_NUM);

	ret = _mm_sound_mgr_device_remove_volume_callback(&param);

	return ret;
}


static int __mm_sound_mgr_ipc_add_available_device_changed_cb(mm_ipc_msg_t *msg)
{
	_mm_sound_mgr_device_param_t param;
	int ret = MM_ERROR_NONE;

	memset(&param, 0x00, sizeof(_mm_sound_mgr_device_param_t));
	param.pid = msg->sound_msg.msgid;
	param.callback = msg->sound_msg.callback;
	param.cbdata = msg->sound_msg.cbdata;

	ret = _mm_sound_mgr_device_add_available_route_callback(&param);

	return ret;
}

static int __mm_sound_mgr_ipc_remove_available_device_changed_cb(mm_ipc_msg_t *msg)
{
	_mm_sound_mgr_device_param_t param;
	int ret = MM_ERROR_NONE;

	memset(&param, 0x00, sizeof(_mm_sound_mgr_device_param_t));
	param.pid = msg->sound_msg.msgid;

	ret = _mm_sound_mgr_device_remove_available_route_callback(&param);

	return ret;
}

static int __mm_sound_mgr_ipc_set_sound_path_for_active_device(mm_ipc_msg_t *msg)
{
	int ret = MM_ERROR_NONE;
	mm_sound_device_in device_in = MM_SOUND_DEVICE_IN_NONE;
	mm_sound_device_out device_out = MM_SOUND_DEVICE_OUT_NONE;
	device_in = msg->sound_msg.device_in;
	device_out = msg->sound_msg.device_out;
	ret = _mm_sound_mgr_device_set_sound_path_for_active_device(device_out, device_in);

	return ret;
}

static int __mm_sound_mgr_ipc_get_audio_path(mm_sound_device_in *device_in, mm_sound_device_out *device_out)
{
	int ret = MM_ERROR_NONE;

	ret = _mm_sound_mgr_device_get_audio_path(device_in, device_out);

	return ret;
}

static int __mm_sound_mgr_ipc_get_current_connected_device_list(mm_ipc_msg_t *msg, GList **device_list, int *total_num)
{
	_mm_sound_mgr_device_param_t param;
	int ret = MM_ERROR_NONE;
	int num = 0;

	memset(&param, 0x00, sizeof(_mm_sound_mgr_device_param_t));
	param.pid = msg->sound_msg.msgid;
	param.device_flags = msg->sound_msg.device_flags;

	ret = _mm_sound_mgr_device_get_current_connected_dev_list(&param, device_list);

	num = g_list_length(*device_list);
	if (num) {
		*total_num = num;
	} else {
		*device_list = NULL;
		ret = MM_ERROR_SOUND_NO_DATA;
	}
	debug_msg("address of device_list[0x%x], number of device [%d]", *device_list, num);

	return ret;
}

static int __mm_sound_mgr_ipc_add_device_connected_cb(mm_ipc_msg_t *msg)
{
	_mm_sound_mgr_device_param_t param;
	int ret = MM_ERROR_NONE;

	memset(&param, 0x00, sizeof(_mm_sound_mgr_device_param_t));
	param.pid = msg->sound_msg.msgid;
	param.callback = msg->sound_msg.callback;
	param.cbdata = msg->sound_msg.cbdata;
	param.device_flags = msg->sound_msg.device_flags;

	ret = _mm_sound_mgr_device_add_connected_callback(&param);

	return ret;
}

static int __mm_sound_mgr_ipc_remove_device_connected_cb(mm_ipc_msg_t *msg)
{
	_mm_sound_mgr_device_param_t param;
	int ret = MM_ERROR_NONE;

	memset(&param, 0x00, sizeof(_mm_sound_mgr_device_param_t));
	param.pid = msg->sound_msg.msgid;

	ret = _mm_sound_mgr_device_remove_connected_callback(&param);

	return ret;
}

static int __mm_sound_mgr_ipc_add_device_info_changed_cb(mm_ipc_msg_t *msg)
{
	_mm_sound_mgr_device_param_t param;
	int ret = MM_ERROR_NONE;

	memset(&param, 0x00, sizeof(_mm_sound_mgr_device_param_t));
	param.pid = msg->sound_msg.msgid;
	param.callback = msg->sound_msg.callback;
	param.cbdata = msg->sound_msg.cbdata;
	param.device_flags = msg->sound_msg.device_flags;

	ret = _mm_sound_mgr_device_add_info_changed_callback(&param);

	return ret;
}

static int __mm_sound_mgr_ipc_remove_device_info_changed_cb(mm_ipc_msg_t *msg)
{
	_mm_sound_mgr_device_param_t param;
	int ret = MM_ERROR_NONE;

	memset(&param, 0x00, sizeof(_mm_sound_mgr_device_param_t));
	param.pid = msg->sound_msg.msgid;

	ret = _mm_sound_mgr_device_remove_info_changed_callback(&param);

	return ret;
}

int __mm_sound_mgr_ipc_freeze_send (char* command, int pid)
{
	GError *err = NULL;
	GDBusConnection *conn = NULL;
	gboolean ret;

	if (command == NULL || pid <= 0) {
		debug_error ("invalid arguments [%s][%d]", command, pid);
		return -1;
	}

	conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &err);
	if (!conn && err) {
		debug_error ("g_bus_get_sync() error (%s) ", err->message);
		g_error_free (err);
		return -1;
	}

	ret = g_dbus_connection_emit_signal (conn,
				NULL, PROC_DBUS_OBJECT, PROC_DBUS_INTERFACE, PROC_DBUS_METHOD,
				g_variant_new ("(si)", command, pid),
				&err);
	if (!ret && err) {
		debug_error ("g_dbus_connection_emit_signal() error (%s) ", err->message);
		goto error;
	}

	ret = g_dbus_connection_flush_sync(conn, NULL, &err);
	if (!ret && err) {
		debug_error ("g_dbus_connection_flush_sync() error (%s) ", err->message);
		goto error;
	}

	g_object_unref(conn);
	debug_msg ("sending [%s] for pid (%d) success", command, pid);

	return 0;

error:
	g_error_free (err);
	g_object_unref(conn);
	return -1;
}

static int _MMIpcRecvMsg(int msgtype, mm_ipc_msg_t *msg)
{
	/* rcv message */
	if(msgrcv(g_rcvid, msg, DSIZE, 0, 0) == -1)
	{
		if(errno == E2BIG) {
			debug_warning("Not acces.\n");
		} else if(errno == EACCES) {
			debug_warning("Access denied\n");
		} else if(errno == ENOMSG) {
			debug_warning("Blocked process [msgflag & IPC_NOWAIT != 0]\n");
		} else if(errno == EIDRM) {
			debug_warning("Removed msgid from system\n");
		} else if(errno == EINTR) {
			debug_warning("Iterrrupted by singnal\n");
		} else if(errno == EINVAL) {
			debug_warning("Invalid msgid \n");
		}

		debug_warning("Fail to recive msg queue : [%d] \n", g_rcvid);
		return MM_ERROR_COMMON_UNKNOWN;
	}
	return MM_ERROR_NONE;
}

int _MMIpcSndMsg(mm_ipc_msg_t *msg)
{
	/* snd message */
	int error = MM_ERROR_NONE;

	msg->msg_type = msg->sound_msg.msgid;

	debug_log("Send message type (for client) : [%ld]\n",msg->msg_type);
	error = msgsnd(g_sndid, msg,DSIZE, 0);
	if ( error == -1)
	{
		if(errno == EACCES) {
			debug_warning("Not acces.\n");
		} else if(errno == EAGAIN) {
			debug_warning("Blocked process [msgflag & IPC_NOWAIT != 0]\n");
		} else if(errno == EIDRM) {
			debug_warning("Removed msgid from system\n");
		} else if(errno == EINTR) {
			debug_warning("Iterrrupted by singnal\n");
		} else if(errno == EINVAL) {
			debug_warning("Invalid msgid or msgtype < 1 or out of data size \n");
		} else if(errno == EFAULT) {
			debug_warning("The address pointed to by msgp isn't accessible \n");
		} else if(errno == ENOMEM) {
			debug_warning("The system does not have enough memory to make a copy of the message pointed to by msgp\n");
		}
		debug_critical("Fail to send message msg queue : [%d] \n", g_sndid);
		debug_critical("Fail to send message msg queue : [%d] \n", errno);
		return MM_ERROR_SOUND_INTERNAL;
	}
	return MM_ERROR_NONE;
}

int _MMIpcCBSndMsg(mm_ipc_msg_t *msg)
{
	/* rcv message */
	int try_again = 0;

	MMSOUND_ENTER_CRITICAL_SECTION_WITH_RETURN(&g_msg_snd_cb_mutex, MM_ERROR_SOUND_INTERNAL);
	msg->msg_type = msg->sound_msg.msgid;
	debug_log("Send CB message type (for client) : [%ld]\n",msg->msg_type);
	/* message queue is full ,we try to send it again*/
	while(try_again <= 10) {
		if (msgsnd(g_cbid, msg, DSIZE, IPC_NOWAIT) == -1)
		{
			if (errno == EACCES) {
				debug_warning("Not acces.\n");
			} else if (errno == EAGAIN) {
				mm_ipc_msg_t msgdata = {0,};
				debug_warning("Blocked process [msgflag & IPC_NOWAIT != 0]\n");
				/* wait 10 msec ,then it will try again */
				usleep(10000);
				/*  it will try 5 times, after 5 times ,if it still fail ,then it will clear the message queue */
				if (try_again <= 5) {
					try_again ++;
					continue;
				}
				/* message queue is full ,it need to clear the queue */
				while( msgrcv(g_cbid, &msgdata, DSIZE, 0, IPC_NOWAIT) != -1 ) {
					debug_warning("msg queue is full ,remove msgtype:[%d] from the queue",msgdata.sound_msg.msgtype);
				}
				try_again++;
				continue;
			} else if (errno == EIDRM) {
				debug_warning("Removed msgid from system\n");
			} else if (errno == EINTR) {
				debug_warning("Iterrrupted by singnal\n");
			} else if (errno == EINVAL) {
				debug_warning("Invalid msgid or msgtype < 1 or out of data size \n");
			}
			debug_critical("Fail to callback send message msg queue : [%d] \n", g_cbid);
			MMSOUND_LEAVE_CRITICAL_SECTION(&g_msg_snd_cb_mutex);
			return MM_ERROR_SOUND_INTERNAL;
		} else {
			break;
		}
	}

	MMSOUND_LEAVE_CRITICAL_SECTION(&g_msg_snd_cb_mutex);
	return MM_ERROR_NONE;
}


