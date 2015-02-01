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
#include <sys/shm.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/msg.h>
#include <assert.h>
#include <errno.h>

#include <pthread.h>
#include <semaphore.h>

#include <mm_error.h>
#include <mm_debug.h>

#include "include/mm_sound.h"
#include "include/mm_sound_msg.h"
#include "include/mm_sound_client.h"
#include "include/mm_sound_common.h"
#include "include/mm_sound_device.h"

#include <mm_session.h>
#include <mm_session_private.h>

#define __DIRECT_CALLBACK__
//#define __GIDLE_CALLBACK__

#include <glib.h>
#if defined(__GSOURCE_CALLBACK__)
#include <sys/poll.h>
#endif

#define MEMTYPE_SUPPORT_MAX (1024 * 1024) /* 1MB */
#define MEMTYPE_TRANS_PER_MAX (128 * 1024) /* 128K */

int g_msg_scsnd;	/* global msg queue id sound client snd */
int g_msg_scrcv;	/* global msg queue id sound client rcv */
int g_msg_sccb;		/* global msg queue id sound client callback */

/* global variables for device list */
static GList *g_device_list = NULL;
static mm_sound_device_list_t g_device_list_t;
static pthread_mutex_t g_device_list_mutex = PTHREAD_MUTEX_INITIALIZER;

/* callback */
struct __callback_param
{
	mm_sound_stop_callback_func     callback;
    void                    *data;
};

pthread_t g_thread;
static int g_exit_thread = 0;
int g_thread_id = -1;
int g_mutex_initted = -1;
pthread_mutex_t g_thread_mutex;

static void* callbackfunc(void *param);

/* manage IPC (msg contorl) */
static int __MMIpcCBSndMsg(mm_ipc_msg_t *msg);
static int __MMIpcRecvMsg(int msgtype, mm_ipc_msg_t *msg);
static int __MMIpcSndMsg(mm_ipc_msg_t *msg);
static int __MMIpcCBRecvMsg(int msgtype, mm_ipc_msg_t *msg);
static int __MMSoundGetMsg(void);

int MMSoundClientInit(void)
{
	int ret = MM_ERROR_NONE;
	debug_fenter();

	debug_fleave();
	return ret;
}

int MMSoundClientCallbackFini(void)
{
	mm_ipc_msg_t msgsnd={0,};
	int ret = MM_ERROR_NONE;

	debug_fenter();

	/* When the the callback thread is not created, do not wait destroy thread */
	/* g_thread_id is initialized : -1 */
	/* g_thread_id is set to 0, when the callback thread is created */
	if (g_thread_id != -1)
	{
		msgsnd.sound_msg.msgtype = MM_SOUND_MSG_INF_DESTROY_CB;
		msgsnd.sound_msg.msgid = getpid();
		ret = __MMIpcCBSndMsg(&msgsnd);
		if (ret != MM_ERROR_NONE)
		{
			debug_critical("[Client] Fail to send message\n");
		}
		pthread_join(g_thread, 0);
	}

	if (g_mutex_initted != -1)
	{
		pthread_mutex_destroy(&g_thread_mutex);
		g_mutex_initted = -1;
	}
	debug_fleave();
	return MM_ERROR_NONE;
}

#if defined(__GSOURCE_CALLBACK__)
gboolean sndcb_fd_check(GSource * source)
{
	GSList *fd_list;
	fd_list = source->poll_fds;
	GPollFD* temp;

	do
	{
		temp = (GPollFD*)fd_list->data;
		if (temp->revents & (POLLIN|POLLPRI))
			return TRUE;
		fd_list = fd_list->next;
	}while(fd_list);

	return FALSE; /* there is no change in any fd state */
}

gboolean sndcb_fd_prepare(GSource *source, gint *timeout)
{
	return FALSE;
}

gboolean sndcb_fd_dispatch(GSource *source,	GSourceFunc callback, gpointer user_data)
{
	callback(user_data);
	return TRUE;
}
#endif
gboolean RunCallback(gpointer data)
{
	mm_ipc_msg_t* msg = NULL;

	debug_msg("[Client] execute mm_sound stop callback function\n");

	msg = (mm_ipc_msg_t*)data;
	((mm_sound_stop_callback_func)msg->sound_msg.callback)(msg->sound_msg.cbdata, msg->sound_msg.handle);

	return FALSE;
}

gboolean _volume_change_cb(gpointer data)
{
	mm_ipc_msg_t* msg = NULL;

	if (!data) {
		debug_error("[Client] NULL param\n");
		return FALSE;
	}
	msg = (mm_ipc_msg_t*)data;

	((mm_sound_volume_changed_cb)msg->sound_msg.callback)(msg->sound_msg.type, msg->sound_msg.val, msg->sound_msg.cbdata);

	if(msg != NULL) {
		free(msg);
		msg = NULL;
	}
	return FALSE;
}

static void* callbackfunc(void *param)
{
	int ret = MM_ERROR_SOUND_INTERNAL;
	mm_ipc_msg_t *msgrcv = NULL;
	int run = 1;
	int instance;

	debug_fenter();

	instance = getpid();
	debug_msg("[Client] callback thread for [%d] is created\n", instance);

	msgrcv = (mm_ipc_msg_t*)malloc(sizeof(mm_ipc_msg_t));
	if(NULL == msgrcv)
	{
		debug_critical("[Client] Failed to memory allocation\n");
		return NULL;
	}

	while(run)
	{
#if defined(__GSOURCE_CALLBACK__)
		int eventFd = 0;
		gchar* eventFile = NULL;
		GSource* cmd_fd_gsrc = NULL;
		GSourceFuncs *src_funcs = NULL;		// handler function
		guint gsource_handle;
		GPollFD *g_fd_cmd = NULL;			// file descriptor
#endif

		debug_msg("[Client] Waiting message\n");
		ret = __MMIpcCBRecvMsg(instance, msgrcv);
		if (ret != MM_ERROR_NONE)
		{
			debug_error("[Client] Fail to receive msg in callback\n");
			continue;
		}

		debug_msg("[Client] Receive msgtype : [%d]\n", msgrcv->sound_msg.msgtype);

		switch (msgrcv->sound_msg.msgtype)
		{
		case MM_SOUND_MSG_INF_STOP_CB:
			debug_msg("[Client] callback : %p, data : %p\n", msgrcv->sound_msg.callback, msgrcv->sound_msg.cbdata);

			if (msgrcv->sound_msg.callback)
			{
#if defined(__DIRECT_CALLBACK__)
				((mm_sound_stop_callback_func)msgrcv->sound_msg.callback)(msgrcv->sound_msg.cbdata, msgrcv->sound_msg.handle);
#elif defined(__GIDLE_CALLBACK__)
				guint eventid = 0;
				eventid = g_idle_add((GSourceFunc)RunCallback, (gpointer)msgrcv);
				debug_msg("[Client] Event Source ID : %d\n", eventid);

#elif defined(__GSOURCE_CALLBACK__)
				char eventBuf[3]="OK";
				////////////////////////
				// 0. Make event source
				eventFile = g_strdup_printf("/tmp/%d_0x%08x_0x%08x", instance, (unsigned int)msgrcv->sound_msg.callback, (unsigned int)msgrcv->sound_msg.cbdata);
				eventFd = open(eventFile, O_RDWR|O_CREAT);
				if(eventFd == -1)
				{
					debug_critical("Event File creation failed\n");
					break;
				}

				// 1. make GSource Object
				src_funcs = (GSourceFuncs *)g_malloc(sizeof(GSourceFuncs));
				if(!src_funcs){
					debug_error("MMSoundCallback :  g_malloc failed on g_src_funcs");
					break;
				}
				src_funcs->prepare = sndcb_fd_prepare;
				src_funcs->check = sndcb_fd_check;
				src_funcs->dispatch = sndcb_fd_dispatch;
				src_funcs->finalize = NULL;
				cmd_fd_gsrc = g_source_new(src_funcs,sizeof(GSource));
			 	if(!cmd_fd_gsrc){
			 		debug_error("MMSoundCallback : g_malloc failed on m_readfd");
			 		break;
			 	}

			 	// 2. add file description which used in g_loop()
			 	g_fd_cmd = (GPollFD*)g_malloc(sizeof(GPollFD));
			 	g_fd_cmd->fd = eventFd;
			 	g_fd_cmd->events = POLLIN|POLLPRI;

			 	// 3. combine g_source object and file descriptor
			 	g_source_add_poll(cmd_fd_gsrc,g_fd_cmd);
			 	gsource_handle = g_source_attach(cmd_fd_gsrc, NULL);
			 	if(!gsource_handle){
			 		debug_error("MMSoundCallback : Error: Failed to attach the source to context");
			 		break;
			 	}

			 	// 4. set callback
			 	g_source_set_callback(cmd_fd_gsrc,RunCallback,(gpointer)g_fd_cmd,NULL);
			 	debug_msg("MMSoundCallback : g_source_set_callback() done\n")

			 	// 5. Set Event
			 	write(eventFd, eventBuf, sizeof(eventBuf));
			 	sleep(1);
			 	// 6. Cleanup
			 	close(eventFd);
			 	unlink(eventFile);
			 	g_source_remove_poll(cmd_fd_gsrc, g_fd_cmd);
			 	g_source_remove(gsource_handle);
			 	if(g_fd_cmd)
			 		free(g_fd_cmd);
			 	if(src_funcs)
			 		free(src_funcs);
			 	if(eventFile)
			 		g_free(eventFile);
				////////////////////////
#endif
			}
			break;
		case MM_SOUND_MSG_INF_DESTROY_CB:
			run = 0;
			break;

		case MM_SOUND_MSG_INF_DEVICE_CONNECTED_CB:
			debug_msg("[Client] device handle : %x, is_connected : %d, callback : %p, data : %p\n",
				&msgrcv->sound_msg.device_handle, msgrcv->sound_msg.is_connected, msgrcv->sound_msg.callback, msgrcv->sound_msg.cbdata);
			if (msgrcv->sound_msg.callback) {
				((mm_sound_device_connected_cb)msgrcv->sound_msg.callback)(&msgrcv->sound_msg.device_handle, msgrcv->sound_msg.is_connected, msgrcv->sound_msg.cbdata);
			}
			break;

		case MM_SOUND_MSG_INF_DEVICE_INFO_CHANGED_CB:
			debug_msg("[Client] device handle : %x, changed_info_type : %d, callback : %p, data : %p\n",
				&msgrcv->sound_msg.device_handle, msgrcv->sound_msg.changed_device_info_type, msgrcv->sound_msg.callback, msgrcv->sound_msg.cbdata);
			if (msgrcv->sound_msg.callback) {
				((mm_sound_device_info_changed_cb)msgrcv->sound_msg.callback)(&msgrcv->sound_msg.device_handle, msgrcv->sound_msg.changed_device_info_type, msgrcv->sound_msg.cbdata);
			}
			break;

		case MM_SOUND_MSG_INF_ACTIVE_DEVICE_CB:
			debug_msg("[Client] device_in : %d, device_out : %d\n", msgrcv->sound_msg.device_in, msgrcv->sound_msg.device_out);
			debug_log("[Client] callback : %p, data : %p\n", msgrcv->sound_msg.callback, msgrcv->sound_msg.cbdata);

			if (msgrcv->sound_msg.callback)
			{
				((mm_sound_active_device_changed_cb)msgrcv->sound_msg.callback)(msgrcv->sound_msg.device_in, msgrcv->sound_msg.device_out, msgrcv->sound_msg.cbdata);
			}
			break;
		case MM_SOUND_MSG_INF_AVAILABLE_ROUTE_CB:
			debug_log("[Client] callback : %p, data : %p\n", msgrcv->sound_msg.callback, msgrcv->sound_msg.cbdata);

			if (msgrcv->sound_msg.callback)
			{
				int route_index;
				mm_sound_route route;

				int list_count = sizeof(msgrcv->sound_msg.route_list) / sizeof(int);

				for (route_index = list_count-1; route_index >= 0; route_index--) {
					route = msgrcv->sound_msg.route_list[route_index];
					if (route == 0)
						continue;
					if (msgrcv->sound_msg.is_available) {
						debug_msg("[Client] available route : 0x%x\n", route);
					} else {
						debug_msg("[Client] unavailable route : 0x%x\n", route);
					}
					((mm_sound_available_route_changed_cb)msgrcv->sound_msg.callback)(route, msgrcv->sound_msg.is_available, msgrcv->sound_msg.cbdata);

					if (route == MM_SOUND_ROUTE_INOUT_HEADSET || route == MM_SOUND_ROUTE_IN_MIC_OUT_HEADPHONE) {
						debug_msg("[Client] no need to proceed further more....\n");
						break;
					}
				}
			}
			break;
		case MM_SOUND_MSG_INF_VOLUME_CB:
			debug_msg("[Client] type: %d, volume value : %d, callback : %p, data : %p\n", msgrcv->sound_msg.type, msgrcv->sound_msg.val, msgrcv->sound_msg.callback, msgrcv->sound_msg.cbdata);

			if (msgrcv->sound_msg.callback)
			{
				mm_ipc_msg_t* tmp = NULL;
				tmp = (mm_ipc_msg_t*)malloc(sizeof(mm_ipc_msg_t));
				if(tmp == NULL) {
					debug_critical("Fail malloc");
					break;
				}
				memcpy(tmp, msgrcv, sizeof(mm_ipc_msg_t));
				g_idle_add((GSourceFunc)_volume_change_cb, (gpointer)tmp);
			}
			break;

		default:
			/* Unexpected msg */
			debug_msg("Receive wrong msg in callback func\n");
			break;
		}
	}
	if(msgrcv) {
		free(msgrcv);
	}

	g_exit_thread = 1;
	debug_msg("[Client] callback [%d] is leaved\n", instance);
	debug_fleave();
	return NULL;
}

static int __mm_sound_client_get_msg_queue(void)
{
	int ret = MM_ERROR_NONE;

	if (g_mutex_initted == -1)
	{
		if(pthread_mutex_init(&g_thread_mutex, NULL)) {
			debug_error("pthread_mutex_init failed\n");
			return MM_ERROR_SOUND_INTERNAL;
		}
		debug_log("[Client] mutex initialized. \n");
		g_mutex_initted = 1;

		/* Get msg queue id */
		ret = __MMSoundGetMsg();
		if(ret != MM_ERROR_NONE)
		{
			debug_error("[Client] Fail to get message queue id. sound_server is not initialized.\n");
			pthread_mutex_destroy(&g_thread_mutex);
			g_mutex_initted = -1;
		}
	}

	return ret;
}

int MMSoundClientPlayTone(int number, int volume_config, double volume, int time, int *handle, bool enable_session)
{
	mm_ipc_msg_t msgrcv = {0,};
	mm_ipc_msg_t msgsnd = {0,};

	int ret = MM_ERROR_NONE;
	int instance = -1; 	/* instance is unique to communicate with server : client message queue filter type */

	debug_fenter();

	ret = __mm_sound_client_get_msg_queue();
	if (ret  != MM_ERROR_NONE) {
		return ret;
	}
	/* read session information */
	int session_type = MM_SESSION_TYPE_MEDIA;
	int session_options = 0;
	if (enable_session)
	{
		if (MM_ERROR_NONE != _mm_session_util_read_information(-1, &session_type, &session_options))
		{
			debug_warning("[Client] Read Session Information failed. use default \"media\" type\n");
			session_type = MM_SESSION_TYPE_MEDIA;

			if(MM_ERROR_NONE != mm_session_init(session_type))
			{
				debug_critical("[Client] MMSessionInit() failed\n");
				return MM_ERROR_POLICY_INTERNAL;
			}
		}
	}

	instance = getpid();
	debug_log("[Client] pid for client ::: [%d]\n", instance);

	pthread_mutex_lock(&g_thread_mutex);

	/* Send msg */
	debug_msg("[Client] Input number : %d\n", number);
	/* Send req memory */
	msgsnd.sound_msg.msgtype = MM_SOUND_MSG_REQ_DTMF;
	msgsnd.sound_msg.msgid = instance;
	msgsnd.sound_msg.session_type = session_type;       //session type
	msgsnd.sound_msg.session_options = session_options; //session options
	msgsnd.sound_msg.volume = volume;                   //This does not effect anymore
	msgsnd.sound_msg.volume_config = volume_config;
	msgsnd.sound_msg.tone = number;
	msgsnd.sound_msg.handle = -1;
	msgsnd.sound_msg.repeat = time;
	msgsnd.sound_msg.enable_session = enable_session;

	ret = __MMIpcSndMsg(&msgsnd);
	if (ret != MM_ERROR_NONE)
	{
		debug_error("[Client] Fail to send msg\n");
		goto cleanup;
	}

	/* Receive */
	ret = __MMIpcRecvMsg(instance, &msgrcv);
	if (ret != MM_ERROR_NONE)
	{
		debug_error("[Client] Fail to recieve msg\n");
		goto cleanup;
	}

	switch (msgrcv.sound_msg.msgtype)
	{
	case MM_SOUND_MSG_RES_DTMF:
		*handle = msgrcv.sound_msg.handle;
		if(*handle == -1) {
			debug_error("[Client] The handle is not get\n");
		} else {
			debug_msg("[Client] Success to play sound sound handle : [%d]\n", *handle);
		}
		break;

	case MM_SOUND_MSG_RES_ERROR:
		debug_error("[Client] Error occurred \n");
		ret = msgrcv.sound_msg.code;
		goto cleanup;
		break;

	default:
		debug_critical("[Client] Unexpected state with communication \n");
		ret = msgrcv.sound_msg.code;
		goto cleanup;
		break;
	}
cleanup:
	pthread_mutex_unlock(&g_thread_mutex);

	debug_fleave();
	return ret;
}


int MMSoundClientPlaySound(MMSoundPlayParam *param, int tone, int keytone, int *handle)
{
	mm_ipc_msg_t msgrcv = {0,};
	mm_ipc_msg_t msgsnd = {0,};
	unsigned char* sharedmem = NULL;

	int ret = MM_ERROR_NONE;
	int instance = -1; 	/* instance is unique to communicate with server : client message queue filter type */

	char shm_name[512];
	void *mmap_buf = NULL;
	static int keybase = 0;
	int shm_fd = -1;

	debug_fenter();
	memset(shm_name, 0, sizeof(shm_name));

	ret = __mm_sound_client_get_msg_queue();
	if (ret  != MM_ERROR_NONE) {
		return ret;
	}
	/* read session information */
	int session_type = MM_SESSION_TYPE_MEDIA;
	int session_options = 0;

	if (param->skip_session == false) {
		if(MM_ERROR_NONE != _mm_session_util_read_information(-1, &session_type, &session_options))
		{
			debug_warning("[Client] Read MMSession Type failed. use default \"media\" type\n");
			session_type = MM_SESSION_TYPE_MEDIA;

			if(MM_ERROR_NONE != mm_session_init(session_type))
			{
				debug_critical("[Client] MMSessionInit() failed\n");
				return MM_ERROR_POLICY_INTERNAL;
			}
		}
	}

	instance = getpid();
	debug_msg("[Client] pid for client ::: [%d]\n", instance);

	/* callback */
	/* callback thread is created just once & when the callback is exist */
	if (param->callback)
	{
		if (g_thread_id == -1)
		{
			g_thread_id = pthread_create(&g_thread, NULL, callbackfunc, NULL);
			if (g_thread_id == -1)
			{
				debug_critical("[Client] Fail to create thread %s\n", strerror(errno));
				return MM_ERROR_SOUND_INTERNAL;
			}
		}
	}

	pthread_mutex_lock(&g_thread_mutex);

	/* Send msg */
	if ((param->mem_ptr && param->mem_size))
	{
		debug_msg("The memptr : [%p]\n", param->mem_ptr);
		debug_msg("The memptr : [%d]\n", param->mem_size);
		/* Limited memory size */
		if (param->mem_ptr && param->mem_size > MEMTYPE_SUPPORT_MAX)
		{
			debug_msg("[Client] Memory size is too big. We support size of media to 1MB\n");
			goto cleanup;
		}

		debug_msg("[Client] memory size : %d\n", param->mem_size);
		snprintf(shm_name, sizeof(shm_name)-1, "%d_%d", instance, keybase);
		debug_msg("[Client] The shm_path : [%s]\n", shm_name);
		keybase++;

		shm_fd = shm_open(shm_name, O_RDWR |O_CREAT, 0666);
		if(shm_fd < 0)
		{
			perror("[Client] Fail create shm_open\n");
			debug_error("[Client] Fail to create shm_open\n");
			goto cleanup;
		}

		if(ftruncate(shm_fd, param->mem_size) == -1)
		{
			debug_error("[Client] Fail to ftruncate\n");
			goto cleanup;
		}

		mmap_buf = mmap (0, MEMTYPE_SUPPORT_MAX, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
		if (mmap_buf == MAP_FAILED)
		{
			debug_error("[Client] MMAP failed (%s)\n", strerror(errno));
			goto cleanup;
		}

		sharedmem = mmap_buf;

		debug_msg("[Client] Sheared mem ptr : %p\n", sharedmem);
		debug_msg("[Client] Sheared key : [%d]\n", 1);
		/* Send req memory */
		msgsnd.sound_msg.msgtype = MM_SOUND_MSG_REQ_MEMORY;
		msgsnd.sound_msg.msgid = instance;
		msgsnd.sound_msg.callback = (void*)(param->callback);
		msgsnd.sound_msg.cbdata = (void*)(param->data);
		msgsnd.sound_msg.memptr = (int)sharedmem;
		msgsnd.sound_msg.sharedkey = 1;		/* In case of shared memory file name */
		msgsnd.sound_msg.session_type = session_type;       //session type
		msgsnd.sound_msg.session_options = session_options; //session options
		msgsnd.sound_msg.priority = param->priority;

		MMSOUND_STRNCPY(msgsnd.sound_msg.filename, shm_name, FILE_PATH);
		debug_msg("[Client] shm_name : %s\n", msgsnd.sound_msg.filename);

		msgsnd.sound_msg.memsize = param->mem_size;
		msgsnd.sound_msg.volume = param->volume;
		msgsnd.sound_msg.tone = tone;
		msgsnd.sound_msg.handle = -1;
		msgsnd.sound_msg.repeat = param->loop;
		msgsnd.sound_msg.volume_config = param->volume_config;
		msgsnd.sound_msg.keytone = keytone;
		msgsnd.sound_msg.handle_route = param->handle_route;
		msgsnd.sound_msg.enable_session = !param->skip_session;

		/* Send req memory */
		debug_msg("[Client] Shared mem ptr %p, %p, size %d\n", sharedmem, param->mem_ptr, param->mem_size);
		memcpy(sharedmem, param->mem_ptr, param->mem_size);


		if (close(shm_fd) == -1)
		{
			debug_error("[Client] Fail to close file\n");
			ret = MM_ERROR_SOUND_INTERNAL;
			goto cleanup;
		}

		ret = __MMIpcSndMsg(&msgsnd);
		if (ret != MM_ERROR_NONE)
		{
			debug_error("[Client] Fail to send msg\n");
			goto cleanup;
		}
	}
	else
	{
		/* File type set for send msg */
		msgsnd.sound_msg.msgtype = MM_SOUND_MSG_REQ_FILE;
		msgsnd.sound_msg.msgid = instance;
		msgsnd.sound_msg.callback = (void*)(param->callback);
		msgsnd.sound_msg.cbdata = (void*)(param->data);
		msgsnd.sound_msg.volume = param->volume;
		msgsnd.sound_msg.tone = tone;
		msgsnd.sound_msg.handle = -1;
		msgsnd.sound_msg.repeat = param->loop;
		msgsnd.sound_msg.volume_config = param->volume_config;
		msgsnd.sound_msg.session_type = session_type;       //session type
		msgsnd.sound_msg.session_options = session_options; //session options
		msgsnd.sound_msg.priority = param->priority;
		msgsnd.sound_msg.handle_route = param->handle_route;
		msgsnd.sound_msg.enable_session = !param->skip_session;

		if((strlen(param->filename)) < FILE_PATH)
		{
			MMSOUND_STRNCPY(msgsnd.sound_msg.filename, param->filename, FILE_PATH);
		}
		else
		{
			debug_error("File name is over count\n");
			ret = MM_ERROR_SOUND_INVALID_PATH;
		}

		msgsnd.sound_msg.keytone = keytone;

		debug_msg("[Client] callback : %p\n", msgsnd.sound_msg.callback);
		debug_msg("[Client] cbdata : %p\n", msgsnd.sound_msg.cbdata);

		ret = __MMIpcSndMsg(&msgsnd);
		if (ret != MM_ERROR_NONE)
		{
			debug_error("[Client] Fail to send msg\n");
			goto cleanup;
		}
	}


	/* Receive */
	ret = __MMIpcRecvMsg(instance, &msgrcv);
	if (ret != MM_ERROR_NONE)
	{
		debug_error("[Client] Fail to recieve msg\n");
		goto cleanup;
	}

	switch (msgrcv.sound_msg.msgtype)
	{
	case MM_SOUND_MSG_RES_FILE:
		*handle = msgrcv.sound_msg.handle;
		debug_msg("[Client] Success to play sound sound handle : [%d]\n", *handle);
		break;
	case MM_SOUND_MSG_RES_MEMORY:
		*handle = msgrcv.sound_msg.handle;
		if(*handle == -1) {
			debug_error("[Client] The handle is not get\n");
		}
		int r = shm_unlink(shm_name);
		if (r == -1)
		{
			debug_critical("[Client] Fail to remove shared memory, must be checked\n");
			goto cleanup;
		}
		debug_msg("[Client] Success to play sound sound handle : [%d]\n", *handle);
		break;
	case MM_SOUND_MSG_RES_ERROR:
		debug_error("[Client] Error occurred \n");
		ret = msgrcv.sound_msg.code;
		goto cleanup;
		break;
	default:
		debug_critical("[Client] Unexpected state with communication \n");
		ret = msgrcv.sound_msg.code;
		goto cleanup;
		break;
	}
cleanup:
	pthread_mutex_unlock(&g_thread_mutex);

	debug_fleave();
	return ret;
}

int MMSoundClientStopSound(int handle)
{
	mm_ipc_msg_t msgrcv = {0,};
	mm_ipc_msg_t msgsnd = {0,};
	int ret = MM_ERROR_NONE;
	int instance;

	debug_fenter();
	debug_msg("[Client] The stop audio handle ::: [%d]\n", handle);

	instance = getpid();

	if (handle < 0)
	{
		ret = MM_ERROR_INVALID_ARGUMENT;
		return ret;
	}

	ret = __mm_sound_client_get_msg_queue();
	if (ret  != MM_ERROR_NONE) {
		return ret;
	}

	pthread_mutex_lock(&g_thread_mutex);

	/* Send req STOP */
	msgsnd.sound_msg.msgtype = MM_SOUND_MSG_REQ_STOP;
	msgsnd.sound_msg.msgid = instance;
	msgsnd.sound_msg.handle = handle;		/* handle means audio handle slot id */

	ret = __MMIpcSndMsg(&msgsnd);
	if (ret != MM_ERROR_NONE)
	{
		debug_error("Fail to send msg\n");
		goto cleanup;
	}

	/* Receive */
	ret = __MMIpcRecvMsg(instance, &msgrcv);
	if (ret != MM_ERROR_NONE)
	{
		debug_error("[Client] Fail to recieve msg\n");
		goto cleanup;
	}
	switch (msgrcv.sound_msg.msgtype)
	{
	case MM_SOUND_MSG_RES_STOP:
		debug_msg("[Client] Success to stop sound\n");
		break;
	case MM_SOUND_MSG_RES_ERROR:
		debug_error("[Client] Error occurred \n");
		ret = msgrcv.sound_msg.code;
		goto cleanup;
		break;
	default:
		debug_critical("[Client] Unexpected state with communication \n");
		ret = msgrcv.sound_msg.code;
		goto cleanup;
		break;
	}

cleanup:
	pthread_mutex_unlock(&g_thread_mutex);


	debug_fleave();
	return ret;
}

static int __mm_sound_device_check_flags_to_append (int device_flags, mm_sound_device_t *device_h, bool *is_good_to_append)
{
	bool need_to_append = false;
	int need_to_check_for_io_direction = device_flags & DEVICE_IO_DIRECTION_FLAGS;
	int need_to_check_for_state = device_flags & DEVICE_STATE_FLAGS;
	int need_to_check_for_type = device_flags & DEVICE_TYPE_FLAGS;

	debug_warning("device_h[0x%x], device_flags[0x%x], need_to_check(io_direction[0x%x],state[0x%x],type[0x%x])\n",
			device_h, device_flags, need_to_check_for_io_direction, need_to_check_for_state, need_to_check_for_type);

	if(!device_h) {
		return MM_ERROR_INVALID_ARGUMENT;
	}
	if (device_flags == DEVICE_ALL_FLAG) {
		*is_good_to_append = true;
		return MM_ERROR_NONE;
	}

	if (need_to_check_for_io_direction) {
		if ((device_h->io_direction == DEVICE_IO_DIRECTION_IN) && (device_flags & DEVICE_IO_DIRECTION_IN_FLAG)) {
			need_to_append = true;
		} else if ((device_h->io_direction == DEVICE_IO_DIRECTION_OUT) && (device_flags & DEVICE_IO_DIRECTION_OUT_FLAG)) {
			need_to_append = true;
		} else if ((device_h->io_direction == DEVICE_IO_DIRECTION_BOTH) && (device_flags & DEVICE_IO_DIRECTION_BOTH_FLAG)) {
			need_to_append = true;
		}
		if (need_to_append) {
			if (!need_to_check_for_state && !need_to_check_for_type) {
				*is_good_to_append = true;
				return MM_ERROR_NONE;
			}
		} else {
			*is_good_to_append = false;
			return MM_ERROR_NONE;
		}
	}
	if (need_to_check_for_state) {
		need_to_append = false;
		if ((device_h->state == DEVICE_STATE_DEACTIVATED) && (device_flags & DEVICE_STATE_DEACTIVATED_FLAG)) {
			need_to_append = true;
		} else if ((device_h->state == DEVICE_STATE_ACTIVATED) && (device_flags & DEVICE_STATE_ACTIVATED_FLAG)) {
			need_to_append = true;
		}
		if (need_to_append) {
			if (!need_to_check_for_type) {
				*is_good_to_append = true;
				return MM_ERROR_NONE;
			}
		} else {
			*is_good_to_append = false;
			return MM_ERROR_NONE;
		}
	}
	if (need_to_check_for_type) {
		need_to_append = false;
		bool is_internal_device = IS_INTERNAL_DEVICE(device_h->type);
		if (is_internal_device && (device_flags & DEVICE_TYPE_INTERNAL_FLAG)) {
			need_to_append = true;
		} else if (!is_internal_device && (device_flags & DEVICE_TYPE_EXTERNAL_FLAG)) {
			need_to_append = true;
		}
		if (need_to_append) {
			*is_good_to_append = true;
			return MM_ERROR_NONE;
		} else {
			*is_good_to_append = false;
			return MM_ERROR_NONE;
		}
	}
	return MM_ERROR_NONE;
}

int __mm_sound_client_device_list_clear ()
{
	int ret = MM_ERROR_NONE;

	MMSOUND_ENTER_CRITICAL_SECTION_WITH_RETURN(&g_device_list_mutex, MM_ERROR_SOUND_INTERNAL);

	if (g_device_list) {
		g_list_free_full(g_device_list, g_free);
		g_device_list = NULL;
	}

	MMSOUND_LEAVE_CRITICAL_SECTION(&g_device_list_mutex);

	return ret;
}

int __mm_sound_client_device_list_append_item (mm_sound_device_t *device_h)
{
	int ret = MM_ERROR_NONE;
	mm_sound_device_t *device_node = g_malloc0(sizeof(mm_sound_device_t));
	memcpy(device_node, device_h, sizeof(mm_sound_device_t));

	MMSOUND_ENTER_CRITICAL_SECTION_WITH_RETURN(&g_device_list_mutex, MM_ERROR_SOUND_INTERNAL);

	g_device_list = g_list_append(g_device_list, device_node);
	debug_log("[Client] g_device_list[0x%x], new device_node[0x%x] is appended, type[%d], id[%d]\n", g_device_list, device_node, device_node->type, device_node->id);

	MMSOUND_LEAVE_CRITICAL_SECTION(&g_device_list_mutex);

	return ret;
}

int _mm_sound_client_device_list_dump (GList *device_list)
{
	int ret = MM_ERROR_NONE;
	GList *list = NULL;
	mm_sound_device_t *device_node = NULL;
	int count = 0;

	debug_log("======================== device list : start ==========================\n");
	for (list = device_list; list != NULL; list = list->next) {
		device_node = (mm_sound_device_t *)list->data;
		if (device_node) {
			debug_log(" list idx[%d]: type[%02d], id[%02d], io_direction[%d], state[%d], name[%s]\n",
						count++, device_node->type, device_node->id, device_node->io_direction, device_node->state, device_node->name);
		}
	}
	debug_log("======================== device list : end ============================\n");

	return ret;
}

int _mm_sound_client_get_current_connected_device_list(int device_flags, mm_sound_device_list_t **device_list)
{
	mm_ipc_msg_t msgrcv = {0,};
	mm_ipc_msg_t msgsnd = {0,};
	int ret = MM_ERROR_NONE;
	int instance;

	debug_fenter();

	ret = __mm_sound_client_get_msg_queue();
	if (ret  != MM_ERROR_NONE) {
		return ret;
	}
	pthread_mutex_lock(&g_thread_mutex);

	instance = getpid();

	/* Send REQ_ADD_ACTIVE_DEVICE_CB */
	msgsnd.sound_msg.msgtype = MM_SOUND_MSG_REQ_GET_CONNECTED_DEVICE_LIST;
	msgsnd.sound_msg.msgid = instance;
	msgsnd.sound_msg.device_flags = device_flags;

	if (__MMIpcSndMsg(&msgsnd) != MM_ERROR_NONE) {
		goto cleanup;
	}

	if (__MMIpcRecvMsg(instance, &msgrcv) != MM_ERROR_NONE) {
		goto cleanup;
	}

	switch (msgrcv.sound_msg.msgtype)
	{
	case MM_SOUND_MSG_RES_GET_CONNECTED_DEVICE_LIST:
	{
		int i = 0;
		int ret = MM_ERROR_NONE;
		int total_device_num = msgrcv.sound_msg.total_device_num;
		bool is_good_to_append = false;
		mm_sound_device_t* device_h = &msgrcv.sound_msg.device_handle;

		ret = __mm_sound_client_device_list_clear();
		if (ret) {
			debug_error("[Client] failed to __mm_sound_client_device_list_clear(), ret[0x%x]\n", ret);
			goto cleanup;
		}

		debug_msg("[Client] supposed to receive %d messages\n", total_device_num);
		for (i = 0; i < total_device_num; i++) {
			/* check if this device_handle is suitable according to flags */
			ret = __mm_sound_device_check_flags_to_append (device_flags, device_h, &is_good_to_append);
			if (is_good_to_append) {
				ret = __mm_sound_client_device_list_append_item(device_h);
				if (ret) {
					debug_error("[Client] failed to __mm_sound_client_device_list_append_item(), ret[0x%x]\n", ret);
				}
			}
			if (total_device_num-i > 1) {
				if (__MMIpcRecvMsg(instance, &msgrcv) != MM_ERROR_NONE) {
					debug_error("[Client] failed to [%d]th of __MMIpcRecvMsg()\n", i);
					goto cleanup;
				}
				switch (msgrcv.sound_msg.msgtype)
				{
				case MM_SOUND_MSG_RES_GET_CONNECTED_DEVICE_LIST:
					break;
				default:
					debug_error("[Client] failed to [%d]th of __MMIpcRecvMsg(), msgtype[%d] is not expected\n", msgrcv.sound_msg.msgtype);
					goto cleanup;
					break;
				}
			}
		}
		g_device_list_t.list = g_device_list;
		*device_list = &g_device_list_t;
		debug_msg("[Client] Success to get connected device list, g_device_list_t[0x%x]->list[0x%x], device_list[0x%x]\n", &g_device_list_t, g_device_list_t.list, *device_list);
		_mm_sound_client_device_list_dump((*device_list)->list);
	}
		break;
	case MM_SOUND_MSG_RES_ERROR:
		debug_error("[Client] Error occurred \n");
		ret = msgrcv.sound_msg.code; // no data is possible
		goto cleanup;
		break;
	default:
		debug_error("[Client] Unexpected state with communication \n");
		ret = msgrcv.sound_msg.code;
		goto cleanup;
		break;
	}

cleanup:
	pthread_mutex_unlock(&g_thread_mutex);

	debug_fleave();
	return ret;
}

int _mm_sound_client_add_device_connected_callback(int device_flags, mm_sound_device_connected_cb func, void* user_data)
{
	mm_ipc_msg_t msgrcv = {0,};
	mm_ipc_msg_t msgsnd = {0,};
	int ret = MM_ERROR_NONE;
	int instance;

	debug_fenter();

	ret = __mm_sound_client_get_msg_queue();
	if (ret  != MM_ERROR_NONE) {
		return ret;
	}

	pthread_mutex_lock(&g_thread_mutex);

	instance = getpid();
	/* Send REQ_ADD_ACTIVE_DEVICE_CB */
	msgsnd.sound_msg.msgtype = MM_SOUND_MSG_REQ_ADD_DEVICE_CONNECTED_CB;
	msgsnd.sound_msg.msgid = instance;
	msgsnd.sound_msg.device_flags = device_flags;
	msgsnd.sound_msg.callback = func;
	msgsnd.sound_msg.cbdata = user_data;

	if (__MMIpcSndMsg(&msgsnd) != MM_ERROR_NONE) {
		goto cleanup;
	}

	/* Recieve */
	if (__MMIpcRecvMsg(instance, &msgrcv) != MM_ERROR_NONE) {
		goto cleanup;
	}

	switch (msgrcv.sound_msg.msgtype)
	{
	case MM_SOUND_MSG_RES_ADD_DEVICE_CONNECTED_CB:
		debug_msg("[Client] Success to add device connected callback\n");
		if (g_thread_id == -1)
		{
			g_thread_id = pthread_create(&g_thread, NULL, callbackfunc, NULL);
			if (g_thread_id == -1)
			{
				debug_critical("[Client] Fail to create thread %s\n", strerror(errno));
				ret = MM_ERROR_SOUND_INTERNAL;
				goto cleanup;
			}
		}
		break;
	case MM_SOUND_MSG_RES_ERROR:
		debug_error("[Client] Error occurred \n");
		ret = msgrcv.sound_msg.code;
		goto cleanup;
		break;
	default:
		debug_critical("[Client] Unexpected state with communication \n");
		ret = msgrcv.sound_msg.code;
		goto cleanup;
		break;
	}

cleanup:
	pthread_mutex_unlock(&g_thread_mutex);

	debug_fleave();
	return ret;
}

int _mm_sound_client_remove_device_connected_callback(void)
{
	mm_ipc_msg_t msgrcv = {0,};
	mm_ipc_msg_t msgsnd = {0,};
	int ret = MM_ERROR_NONE;
	int instance;

	debug_fenter();

	ret = __mm_sound_client_get_msg_queue();
	if (ret  != MM_ERROR_NONE) {
		return ret;
	}

	pthread_mutex_lock(&g_thread_mutex);

	instance = getpid();
	/* Send REQ_REMOVE_ACTIVE_DEVICE_CB */
	msgsnd.sound_msg.msgtype = MM_SOUND_MSG_REQ_REMOVE_DEVICE_CONNECTED_CB;
	msgsnd.sound_msg.msgid = instance;

	if (__MMIpcSndMsg(&msgsnd) != MM_ERROR_NONE) {
		goto cleanup;
	}
	/* Recieve */
	if (__MMIpcRecvMsg(instance, &msgrcv) != MM_ERROR_NONE) {
		goto cleanup;
	}
	switch (msgrcv.sound_msg.msgtype)
	{
	case MM_SOUND_MSG_RES_REMOVE_DEVICE_CONNECTED_CB:
		debug_msg("[Client] Success to remove device connected callback\n");
		break;
	case MM_SOUND_MSG_RES_ERROR:
		debug_error("[Client] Error occurred \n");
		ret = msgrcv.sound_msg.code;
		goto cleanup;
		break;
	default:
		debug_critical("[Client] Unexpected state with communication \n");
		ret = msgrcv.sound_msg.code;
		goto cleanup;
		break;
	}

cleanup:
	pthread_mutex_unlock(&g_thread_mutex);

	debug_fleave();
	return ret;
}

int _mm_sound_client_add_device_info_changed_callback(int device_flags, mm_sound_device_info_changed_cb func, void* user_data)
{
	mm_ipc_msg_t msgrcv = {0,};
	mm_ipc_msg_t msgsnd = {0,};
	int ret = MM_ERROR_NONE;
	int instance;

	debug_fenter();

	ret = __mm_sound_client_get_msg_queue();
	if (ret  != MM_ERROR_NONE) {
		return ret;
	}

	pthread_mutex_lock(&g_thread_mutex);

	instance = getpid();
	/* Send REQ_ADD_ACTIVE_DEVICE_CB */
	msgsnd.sound_msg.msgtype = MM_SOUND_MSG_REQ_ADD_DEVICE_INFO_CHANGED_CB;
	msgsnd.sound_msg.msgid = instance;
	msgsnd.sound_msg.device_flags = device_flags;
	msgsnd.sound_msg.callback = func;
	msgsnd.sound_msg.cbdata = user_data;

	if (__MMIpcSndMsg(&msgsnd) != MM_ERROR_NONE) {
		goto cleanup;
	}

	/* Recieve */
	if (__MMIpcRecvMsg(instance, &msgrcv) != MM_ERROR_NONE) {
		goto cleanup;
	}

	switch (msgrcv.sound_msg.msgtype)
	{
	case MM_SOUND_MSG_RES_ADD_DEVICE_INFO_CHANGED_CB:
		debug_msg("[Client] Success to add device connected callback\n");
		if (g_thread_id == -1)
		{
			g_thread_id = pthread_create(&g_thread, NULL, callbackfunc, NULL);
			if (g_thread_id == -1)
			{
				debug_critical("[Client] Fail to create thread %s\n", strerror(errno));
				ret = MM_ERROR_SOUND_INTERNAL;
				goto cleanup;
			}
		}
		break;
	case MM_SOUND_MSG_RES_ERROR:
		debug_error("[Client] Error occurred \n");
		ret = msgrcv.sound_msg.code;
		goto cleanup;
		break;
	default:
		debug_critical("[Client] Unexpected state with communication \n");
		ret = msgrcv.sound_msg.code;
		goto cleanup;
		break;
	}

cleanup:
	pthread_mutex_unlock(&g_thread_mutex);

	debug_fleave();
	return ret;
}

int _mm_sound_client_remove_device_info_changed_callback(void)
{
	mm_ipc_msg_t msgrcv = {0,};
	mm_ipc_msg_t msgsnd = {0,};
	int ret = MM_ERROR_NONE;
	int instance;

	debug_fenter();

	ret = __mm_sound_client_get_msg_queue();
	if (ret  != MM_ERROR_NONE) {
		return ret;
	}

	pthread_mutex_lock(&g_thread_mutex);

	instance = getpid();
	/* Send REQ_REMOVE_ACTIVE_DEVICE_CB */
	msgsnd.sound_msg.msgtype = MM_SOUND_MSG_REQ_REMOVE_DEVICE_INFO_CHANGED_CB;
	msgsnd.sound_msg.msgid = instance;

	if (__MMIpcSndMsg(&msgsnd) != MM_ERROR_NONE) {
		goto cleanup;
	}

	/* Recieve */
	if (__MMIpcRecvMsg(instance, &msgrcv) != MM_ERROR_NONE) {
		goto cleanup;
	}

	switch (msgrcv.sound_msg.msgtype)
	{
	case MM_SOUND_MSG_RES_REMOVE_DEVICE_INFO_CHANGED_CB:
		debug_msg("[Client] Success to remove device info changed callback\n");
		break;
	case MM_SOUND_MSG_RES_ERROR:
		debug_error("[Client] Error occurred \n");
		ret = msgrcv.sound_msg.code;
		goto cleanup;
		break;
	default:
		debug_critical("[Client] Unexpected state with communication \n");
		ret = msgrcv.sound_msg.code;
		goto cleanup;
		break;
	}

cleanup:
	pthread_mutex_unlock(&g_thread_mutex);

	debug_fleave();
	return ret;
}

int _mm_sound_client_is_route_available(mm_sound_route route, bool *is_available)
{
	mm_ipc_msg_t msgrcv = {0,};
	mm_ipc_msg_t msgsnd = {0,};
	int ret = MM_ERROR_NONE;
	int instance;

	debug_fenter();

	*is_available = FALSE;

	ret = __mm_sound_client_get_msg_queue();
	if (ret  != MM_ERROR_NONE) {
		return ret;
	}

	pthread_mutex_lock(&g_thread_mutex);

	instance = getpid();
	/* Send REQ_IS_ROUTE_AVAILABLE */
	msgsnd.sound_msg.msgtype = MM_SOUND_MSG_REQ_IS_ROUTE_AVAILABLE;
	msgsnd.sound_msg.msgid = instance;
	msgsnd.sound_msg.route = route;

	if (__MMIpcSndMsg(&msgsnd) != MM_ERROR_NONE) {
		goto cleanup;
	}

	/* Receive */
	if (__MMIpcRecvMsg(instance, &msgrcv) != MM_ERROR_NONE) {
		goto cleanup;
	}

	switch (msgrcv.sound_msg.msgtype)
	{
	case MM_SOUND_MSG_RES_IS_ROUTE_AVAILABLE:
		*is_available = msgrcv.sound_msg.is_available;
		debug_msg("[Client] Success to check given route is available %d\n", *is_available);
		break;
	case MM_SOUND_MSG_RES_ERROR:
		debug_error("[Client] Error occurred \n");
		ret = msgrcv.sound_msg.code;
		goto cleanup;
		break;
	default:
		debug_critical("[Client] Unexpected state with communication \n");
		ret = msgrcv.sound_msg.code;
		goto cleanup;
		break;
	}

cleanup:
	pthread_mutex_unlock(&g_thread_mutex);

	debug_fleave();
	return ret;
}

static int _handle_foreach_callback(mm_ipc_msg_t *msg)
{
	int route_index;
	mm_sound_route route;

	debug_fenter();

	if (msg->sound_msg.callback == NULL) {
		debug_error ("[Client] Foreach callback is [%p], cbdata = %p => exit",
				msg->sound_msg.callback, msg->sound_msg.cbdata);
		return MM_ERROR_SOUND_INTERNAL;
	}

	for (route_index = 0; route_index < MM_SOUND_ROUTE_NUM; route_index++) {
		route = msg->sound_msg.route_list[route_index];
		if (route == 0) {
			break;
		}
		debug_msg("[Client] available route : %d\n", route);
		if (((mm_sound_available_route_cb)msg->sound_msg.callback)(route, msg->sound_msg.cbdata) == false) {
			debug_msg ("[Client] user doesn't want anymore. quit loop!!\n");
			break;
		}
	}

	debug_fleave();

	return MM_ERROR_NONE;
}

int _mm_sound_client_foreach_available_route_cb(mm_sound_available_route_cb available_route_cb, void *user_data)
{
	mm_ipc_msg_t msgrcv = {0,};
	mm_ipc_msg_t msgsnd = {0,};
	int ret = MM_ERROR_NONE;
	int instance;

	debug_fenter();

	ret = __mm_sound_client_get_msg_queue();
	if (ret  != MM_ERROR_NONE) {
		return ret;
	}

	pthread_mutex_lock(&g_thread_mutex);

	instance = getpid();
	/* Send REQ_FOREACH_AVAILABLE_ROUTE_CB */
	msgsnd.sound_msg.msgtype = MM_SOUND_MSG_REQ_FOREACH_AVAILABLE_ROUTE_CB;
	msgsnd.sound_msg.msgid = instance;
	msgsnd.sound_msg.callback = (void *)available_route_cb;
	msgsnd.sound_msg.cbdata = (void *)user_data;

	if (__MMIpcSndMsg(&msgsnd) != MM_ERROR_NONE) {
		goto cleanup;
	}

	/* Recieve */
	if (__MMIpcRecvMsg(instance, &msgrcv) != MM_ERROR_NONE) {
		goto cleanup;
	}

	switch (msgrcv.sound_msg.msgtype)
	{
	case MM_SOUND_MSG_RES_FOREACH_AVAILABLE_ROUTE_CB:
		debug_msg("[Client] Success to set foreach available route callback\n");
		msgrcv.sound_msg.callback = (void *)available_route_cb;
		msgrcv.sound_msg.cbdata = (void *)user_data;
		ret = _handle_foreach_callback (&msgrcv);
		break;
	case MM_SOUND_MSG_RES_ERROR:
		debug_error("[Client] Error occurred \n");
		ret = msgrcv.sound_msg.code;
		goto cleanup;
		break;
	default:
		debug_critical("[Client] Unexpected state with communication \n");
		ret = msgrcv.sound_msg.code;
		goto cleanup;
		break;
	}

cleanup:
	pthread_mutex_unlock(&g_thread_mutex);

	debug_fleave();
	return ret;
}

int _mm_sound_client_set_active_route(mm_sound_route route, bool need_broadcast)
{
	mm_ipc_msg_t msgrcv = {0,};
	mm_ipc_msg_t msgsnd = {0,};
	int ret = MM_ERROR_NONE;
	int instance;

	debug_fenter();

	ret = __mm_sound_client_get_msg_queue();
	if (ret  != MM_ERROR_NONE) {
		return ret;
	}

	pthread_mutex_lock(&g_thread_mutex);

	instance = getpid();
	/* Send REQ_SET_ACTIVE_ROUTE */
	msgsnd.sound_msg.msgtype = MM_SOUND_MSG_REQ_SET_ACTIVE_ROUTE;
	msgsnd.sound_msg.msgid = instance;
	msgsnd.sound_msg.route = route;
	msgsnd.sound_msg.need_broadcast = need_broadcast;

	if (__MMIpcSndMsg(&msgsnd) != MM_ERROR_NONE)
		goto cleanup;

	/* Recieve */
	if (__MMIpcRecvMsg(instance, &msgrcv) != MM_ERROR_NONE) {
		goto cleanup;
	}

	switch (msgrcv.sound_msg.msgtype)
	{
	case MM_SOUND_MSG_RES_SET_ACTIVE_ROUTE:
		debug_msg("[Client] Success to add active device callback\n");
		break;
	case MM_SOUND_MSG_RES_ERROR:
		debug_error("[Client] Error occurred \n");
		ret = msgrcv.sound_msg.code;
		goto cleanup;
		break;
	default:
		debug_critical("[Client] Unexpected state with communication \n");
		ret = msgrcv.sound_msg.code;
		goto cleanup;
		break;
	}

cleanup:
	pthread_mutex_unlock(&g_thread_mutex);

	debug_fleave();
	return ret;
}

int _mm_sound_client_set_active_route_auto(void)
{
	mm_ipc_msg_t msgrcv = {0,};
	mm_ipc_msg_t msgsnd = {0,};
	int ret = MM_ERROR_NONE;
	int instance;

	debug_fenter();

	ret = __mm_sound_client_get_msg_queue();
	if (ret  != MM_ERROR_NONE) {
		return ret;
	}

	pthread_mutex_lock(&g_thread_mutex);

	instance = getpid();
	/* Send REQ_SET_ACTIVE_ROUTE */
	msgsnd.sound_msg.msgtype = MM_SOUND_MSG_REQ_SET_ACTIVE_ROUTE_AUTO;
	msgsnd.sound_msg.msgid = instance;

	if (__MMIpcSndMsg(&msgsnd) != MM_ERROR_NONE) {
		goto cleanup;
	}

	/* Recieve */
	if (__MMIpcRecvMsg(instance, &msgrcv) != MM_ERROR_NONE) {
		goto cleanup;
	}

	switch (msgrcv.sound_msg.msgtype)
	{
	case MM_SOUND_MSG_RES_SET_ACTIVE_ROUTE_AUTO:
		debug_msg("[Client] Success to set active device auto\n");
		break;
	case MM_SOUND_MSG_RES_ERROR:
		debug_error("[Client] Error occurred \n");
		ret = msgrcv.sound_msg.code;
		goto cleanup;
		break;
	default:
		debug_critical("[Client] Unexpected state with communication \n");
		ret = msgrcv.sound_msg.code;
		goto cleanup;
		break;
	}

cleanup:
	pthread_mutex_unlock(&g_thread_mutex);

	debug_fleave();
	return ret;
}

int _mm_sound_client_get_active_device(mm_sound_device_in *device_in, mm_sound_device_out *device_out)
{
	mm_ipc_msg_t msgrcv = {0,};
	mm_ipc_msg_t msgsnd = {0,};
	int ret = MM_ERROR_NONE;
	int instance;

	debug_fenter();

	ret = __mm_sound_client_get_msg_queue();
	if (ret  != MM_ERROR_NONE) {
		return ret;
	}

	pthread_mutex_lock(&g_thread_mutex);

	instance = getpid();
	/* Send REQ_GET_ACTIVE_DEVICE */
	msgsnd.sound_msg.msgtype = MM_SOUND_MSG_REQ_GET_ACTIVE_DEVICE;
	msgsnd.sound_msg.msgid = instance;

	if (__MMIpcSndMsg(&msgsnd) != MM_ERROR_NONE) {
		goto cleanup;
	}

	/* Recieve */
	if (__MMIpcRecvMsg(instance, &msgrcv) != MM_ERROR_NONE) {
		goto cleanup;
	}

	switch (msgrcv.sound_msg.msgtype)
	{
	case MM_SOUND_MSG_RES_GET_ACTIVE_DEVICE:
		*device_in = msgrcv.sound_msg.device_in;
		*device_out = msgrcv.sound_msg.device_out;
		debug_log("[Client] Success to get active device in=[%x], out=[%x]\n", *device_in, *device_out);
		break;
	case MM_SOUND_MSG_RES_ERROR:
		debug_error("[Client] Error occurred \n");
		ret = msgrcv.sound_msg.code;
		goto cleanup;
		break;
	default:
		debug_critical("[Client] Unexpected state with communication \n");
		ret = msgrcv.sound_msg.code;
		goto cleanup;
		break;
	}

cleanup:
	pthread_mutex_unlock(&g_thread_mutex);

	debug_fleave();
	return ret;
}

int _mm_sound_client_get_audio_path(mm_sound_device_in *device_in, mm_sound_device_out *device_out)
{
	mm_ipc_msg_t msgrcv = {0,};
	mm_ipc_msg_t msgsnd = {0,};
	int ret = MM_ERROR_NONE;
	int instance;

	debug_fenter();

	ret = __mm_sound_client_get_msg_queue();
	if (ret  != MM_ERROR_NONE) {
		return ret;
	}

	pthread_mutex_lock(&g_thread_mutex);

	instance = getpid();
	/* Send REQ_GET_AUDIO_PATH */
	msgsnd.sound_msg.msgtype = MM_SOUND_MSG_REQ_GET_AUDIO_PATH;
	msgsnd.sound_msg.msgid = instance;

	if (__MMIpcSndMsg(&msgsnd) != MM_ERROR_NONE) {
		goto cleanup;
	}

	/* Recieve */
	if (__MMIpcRecvMsg(instance, &msgrcv) != MM_ERROR_NONE) {
		goto cleanup;
	}

	switch (msgrcv.sound_msg.msgtype)
	{
	case MM_SOUND_MSG_RES_GET_AUDIO_PATH:
		*device_in = msgrcv.sound_msg.device_in;
		*device_out = msgrcv.sound_msg.device_out;
		debug_log("[Client] Success to get active device in=[%x], out=[%x]\n", *device_in, *device_out);
		break;
	case MM_SOUND_MSG_RES_ERROR:
		debug_error("[Client] Error occurred \n");
		ret = msgrcv.sound_msg.code;
		goto cleanup;
		break;
	default:
		debug_critical("[Client] Unexpected state with communication \n");
		ret = msgrcv.sound_msg.code;
		goto cleanup;
		break;
	}

cleanup:
	pthread_mutex_unlock(&g_thread_mutex);

	debug_fleave();
	return ret;
}

int _mm_sound_client_add_active_device_changed_callback(const char *name, mm_sound_active_device_changed_cb func, void* user_data)
{
	mm_ipc_msg_t msgrcv = {0,};
	mm_ipc_msg_t msgsnd = {0,};
	int ret = MM_ERROR_NONE;
	int instance;

	debug_fenter();

	ret = __mm_sound_client_get_msg_queue();
	if (ret  != MM_ERROR_NONE) {
		return ret;
	}

	pthread_mutex_lock(&g_thread_mutex);

	instance = getpid();
	/* Send REQ_ADD_ACTIVE_DEVICE_CB */
	msgsnd.sound_msg.msgtype = MM_SOUND_MSG_REQ_ADD_ACTIVE_DEVICE_CB;
	msgsnd.sound_msg.msgid = instance;
	msgsnd.sound_msg.callback = func;
	msgsnd.sound_msg.cbdata = user_data;
	MMSOUND_STRNCPY(msgsnd.sound_msg.name, name, MM_SOUND_NAME_NUM);

	if (__MMIpcSndMsg(&msgsnd) != MM_ERROR_NONE) {
		goto cleanup;
	}

	/* Recieve */
	if (__MMIpcRecvMsg(instance, &msgrcv) != MM_ERROR_NONE) {
		goto cleanup;
	}

	switch (msgrcv.sound_msg.msgtype)
	{
	case MM_SOUND_MSG_RES_ADD_ACTIVE_DEVICE_CB:
		debug_msg("[Client] Success to add active device callback\n");
		if (g_thread_id == -1)
		{
			g_thread_id = pthread_create(&g_thread, NULL, callbackfunc, NULL);
			if (g_thread_id == -1)
			{
				debug_critical("[Client] Fail to create thread %s\n", strerror(errno));
				ret = MM_ERROR_SOUND_INTERNAL;
				goto cleanup;
			}
		}
		break;
	case MM_SOUND_MSG_RES_ERROR:
		debug_error("[Client] Error occurred \n");
		ret = msgrcv.sound_msg.code;
		goto cleanup;
		break;
	default:
		debug_critical("[Client] Unexpected state with communication \n");
		ret = msgrcv.sound_msg.code;
		goto cleanup;
		break;
	}

cleanup:
	pthread_mutex_unlock(&g_thread_mutex);

	debug_fleave();
	return ret;
}

int _mm_sound_client_remove_active_device_changed_callback(const char *name)
{
	mm_ipc_msg_t msgrcv = {0,};
	mm_ipc_msg_t msgsnd = {0,};
	int ret = MM_ERROR_NONE;
	int instance;

	debug_fenter();

	ret = __mm_sound_client_get_msg_queue();
	if (ret  != MM_ERROR_NONE) {
		return ret;
	}

	pthread_mutex_lock(&g_thread_mutex);

	instance = getpid();
	/* Send REQ_REMOVE_ACTIVE_DEVICE_CB */
	msgsnd.sound_msg.msgtype = MM_SOUND_MSG_REQ_REMOVE_ACTIVE_DEVICE_CB;
	msgsnd.sound_msg.msgid = instance;
	MMSOUND_STRNCPY(msgsnd.sound_msg.name, name, MM_SOUND_NAME_NUM);

	if (__MMIpcSndMsg(&msgsnd) != MM_ERROR_NONE) {
		goto cleanup;
	}

	/* Recieve */
	if (__MMIpcRecvMsg(instance, &msgrcv) != MM_ERROR_NONE) {
		goto cleanup;
	}

	switch (msgrcv.sound_msg.msgtype)
	{
	case MM_SOUND_MSG_RES_REMOVE_ACTIVE_DEVICE_CB:
		debug_msg("[Client] Success to remove active device callback\n");
		break;
	case MM_SOUND_MSG_RES_ERROR:
		debug_error("[Client] Error occurred \n");
		ret = msgrcv.sound_msg.code;
		goto cleanup;
		break;
	default:
		debug_critical("[Client] Unexpected state with communication \n");
		ret = msgrcv.sound_msg.code;
		goto cleanup;
		break;
	}

cleanup:
	pthread_mutex_unlock(&g_thread_mutex);

	debug_fleave();
	return ret;
}

int _mm_sound_client_add_volume_changed_callback(mm_sound_volume_changed_cb func, void* user_data)
{
	mm_ipc_msg_t msgrcv = {0,};
	mm_ipc_msg_t msgsnd = {0,};
	int ret = MM_ERROR_NONE;
	int instance;

	debug_fenter();

	ret = __mm_sound_client_get_msg_queue();
	if (ret  != MM_ERROR_NONE) {
		return ret;
	}

	pthread_mutex_lock(&g_thread_mutex);

	instance = getpid();
	/* Send REQ_ADD_VOLUME_CB */
	msgsnd.sound_msg.msgtype = MM_SOUND_MSG_REQ_ADD_VOLUME_CB;
	msgsnd.sound_msg.msgid = instance;
	msgsnd.sound_msg.callback = func;
	msgsnd.sound_msg.cbdata = user_data;

	if (__MMIpcSndMsg(&msgsnd) != MM_ERROR_NONE) {
		goto cleanup;
	}

	/* Recieve */
	if (__MMIpcRecvMsg(instance, &msgrcv) != MM_ERROR_NONE) {
		goto cleanup;
	}

	switch (msgrcv.sound_msg.msgtype)
	{
	case MM_SOUND_MSG_RES_ADD_VOLUME_CB:
		debug_msg("[Client] Success to add volume callback\n");
		if (g_thread_id == -1)
		{
			g_thread_id = pthread_create(&g_thread, NULL, callbackfunc, NULL);
			if (g_thread_id == -1)
			{
				debug_critical("[Client] Fail to create thread %s\n", strerror(errno));
				ret = MM_ERROR_SOUND_INTERNAL;
				goto cleanup;
			}
		}
		break;
	case MM_SOUND_MSG_RES_ERROR:
		debug_error("[Client] Error occurred \n");
		ret = msgrcv.sound_msg.code;
		goto cleanup;
		break;
	default:
		debug_critical("[Client] Unexpected state with communication \n");
		ret = msgrcv.sound_msg.code;
		goto cleanup;
		break;
	}

cleanup:
	pthread_mutex_unlock(&g_thread_mutex);

	debug_fleave();
	return ret;
}

int _mm_sound_client_remove_volume_changed_callback(void)
{
	mm_ipc_msg_t msgrcv = {0,};
	mm_ipc_msg_t msgsnd = {0,};
	int ret = MM_ERROR_NONE;
	int instance;

	debug_fenter();

	ret = __mm_sound_client_get_msg_queue();
	if (ret  != MM_ERROR_NONE) {
		return ret;
	}

	pthread_mutex_lock(&g_thread_mutex);

	instance = getpid();
	/* Send REQ_REMOVE_VOLUME_CB */
	msgsnd.sound_msg.msgtype = MM_SOUND_MSG_REQ_REMOVE_VOLUME_CB;
	msgsnd.sound_msg.msgid = instance;

	if (__MMIpcSndMsg(&msgsnd) != MM_ERROR_NONE) {
		goto cleanup;
	}

	/* Recieve */
	if (__MMIpcRecvMsg(instance, &msgrcv) != MM_ERROR_NONE) {
		goto cleanup;
	}

	switch (msgrcv.sound_msg.msgtype)
	{
	case MM_SOUND_MSG_RES_REMOVE_VOLUME_CB:
		debug_msg("[Client] Success to remove volume callback\n");
		break;
	case MM_SOUND_MSG_RES_ERROR:
		debug_error("[Client] Error occurred \n");
		ret = msgrcv.sound_msg.code;
		goto cleanup;
		break;
	default:
		debug_critical("[Client] Unexpected state with communication \n");
		ret = msgrcv.sound_msg.code;
		goto cleanup;
		break;
	}

cleanup:
	pthread_mutex_unlock(&g_thread_mutex);

	debug_fleave();
	return ret;
}

int _mm_sound_client_add_available_route_changed_callback(mm_sound_available_route_changed_cb func, void* user_data)
{
	mm_ipc_msg_t msgrcv = {0,};
	mm_ipc_msg_t msgsnd = {0,};
	int ret = MM_ERROR_NONE;
	int instance;

	debug_fenter();

	ret = __mm_sound_client_get_msg_queue();
	if (ret  != MM_ERROR_NONE) {
		return ret;
	}

	pthread_mutex_lock(&g_thread_mutex);

	instance = getpid();
	/* Send REQ_ADD_AVAILABLE_ROUTE_CB */
	msgsnd.sound_msg.msgtype = MM_SOUND_MSG_REQ_ADD_AVAILABLE_ROUTE_CB;
	msgsnd.sound_msg.msgid = instance;
	msgsnd.sound_msg.callback = func;
	msgsnd.sound_msg.cbdata = user_data;

	if (__MMIpcSndMsg(&msgsnd) != MM_ERROR_NONE) {
		goto cleanup;
	}

	/* Recieve */
	if (__MMIpcRecvMsg(instance, &msgrcv) != MM_ERROR_NONE) {
		goto cleanup;
	}

	switch (msgrcv.sound_msg.msgtype)
	{
	case MM_SOUND_MSG_RES_ADD_AVAILABLE_ROUTE_CB:
		debug_msg("[Client] Success to add available route callback\n");
		if (g_thread_id == -1)
		{
			g_thread_id = pthread_create(&g_thread, NULL, callbackfunc, NULL);
			if (g_thread_id == -1)
			{
				debug_critical("[Client] Fail to create thread %s\n", strerror(errno));
				ret = MM_ERROR_SOUND_INTERNAL;
				goto cleanup;
			}
		}
		break;
	case MM_SOUND_MSG_RES_ERROR:
		debug_error("[Client] Error occurred \n");
		ret = msgrcv.sound_msg.code;
		goto cleanup;
		break;
	default:
		debug_critical("[Client] Unexpected state with communication \n");
		ret = msgrcv.sound_msg.code;
		goto cleanup;
		break;
	}

cleanup:
	pthread_mutex_unlock(&g_thread_mutex);

	debug_fleave();
	return ret;
}

int _mm_sound_client_remove_available_route_changed_callback(void)
{
	mm_ipc_msg_t msgrcv = {0,};
	mm_ipc_msg_t msgsnd = {0,};
	int ret = MM_ERROR_NONE;
	int instance;

	debug_fenter();

	ret = __mm_sound_client_get_msg_queue();
	if (ret  != MM_ERROR_NONE) {
		return ret;
	}

	pthread_mutex_lock(&g_thread_mutex);

	instance = getpid();
	/* Send REQ_REMOVE_AVAILABLE_ROUTE_CB */
	msgsnd.sound_msg.msgtype = MM_SOUND_MSG_REQ_REMOVE_AVAILABLE_ROUTE_CB;
	msgsnd.sound_msg.msgid = instance;

	if (__MMIpcSndMsg(&msgsnd) != MM_ERROR_NONE) {
		goto cleanup;
	}

	/* Recieve */
	if (__MMIpcRecvMsg(instance, &msgrcv) != MM_ERROR_NONE) {
		goto cleanup;
	}

	switch (msgrcv.sound_msg.msgtype)
	{
	case MM_SOUND_MSG_RES_REMOVE_AVAILABLE_ROUTE_CB:
		debug_msg("[Client] Success to remove available route callback\n");
		break;
	case MM_SOUND_MSG_RES_ERROR:
		debug_error("[Client] Error occurred \n");
		ret = msgrcv.sound_msg.code;
		goto cleanup;
		break;
	default:
		debug_critical("[Client] Unexpected state with communication \n");
		ret = msgrcv.sound_msg.code;
		goto cleanup;
		break;
	}

cleanup:
	pthread_mutex_unlock(&g_thread_mutex);

	debug_fleave();
	return ret;
}

static int __MMIpcCBSndMsg(mm_ipc_msg_t *msg)
{
	/* rcv message */
	msg->msg_type = msg->sound_msg.msgid;
	if (msgsnd(g_msg_sccb, msg,DSIZE, 0)== -1)
	{
		debug_error("[Client] Fail to callback send message msgid : [%d], [%d][%s]", g_msg_sccb, errno, strerror(errno));
		return MM_ERROR_COMMON_UNKNOWN;
	}
	return MM_ERROR_NONE;
}

static int __MMIpcCBRecvMsg(int msgtype, mm_ipc_msg_t *msg)
{
	/* rcv message */
	if(msgrcv(g_msg_sccb, msg, DSIZE, msgtype, 0) == -1)
	{
		debug_error("[Client] Fail to callback receive message msgid : [%d], [%d][%s]", g_msg_sccb, errno, strerror(errno));
		return MM_ERROR_COMMON_UNKNOWN;
	}
	return MM_ERROR_NONE;
}

#define MAX_RCV_RETRY 20000

static int __MMIpcRecvMsg(int msgtype, mm_ipc_msg_t *msg)
{
	int retry_count = 0;

	/* rcv message */
	while (msgrcv(g_msg_scrcv, msg, DSIZE, msgtype, IPC_NOWAIT) == -1) {
		if (errno == ENOMSG) {
			if (retry_count < MAX_RCV_RETRY) { /* usec is 10^-6 sec so, 5ms * 20000 = 10sec. */
				usleep(5000);
				retry_count++;
				continue;
			} else {
				debug_error("[Client] retry(%d) is over : [%d] \n", MAX_RCV_RETRY, g_msg_scrcv);
				return MM_ERROR_SOUND_INTERNAL;
			}
		} else if (errno == EINTR) {
			debug_warning("[Client] Interrupted by signal, continue loop");
			continue;
		}

		debug_error("[Client] Fail to receive msgid : [%d], [%d][%s]", g_msg_scrcv, errno, strerror(errno));

		return MM_ERROR_COMMON_UNKNOWN;
	}
	debug_log("[Client] Retry %d times when receive msg\n", retry_count);
	return MM_ERROR_NONE;
}

static int __MMIpcSndMsg(mm_ipc_msg_t *msg)
{
	/* rcv message */
	int try_again = 0;

	msg->msg_type = msg->sound_msg.msgid;
	while (msgsnd(g_msg_scsnd, msg,DSIZE, IPC_NOWAIT) == -1)
	{
		if(errno == EACCES) {
			debug_warning("Not acces.\n");
		} else if(errno == EAGAIN || errno == ENOMEM) {
			mm_ipc_msg_t msgdata = {0,};
			debug_warning("Blocked process [msgflag & IPC_NOWAIT != 0]\n");
			debug_warning("The system does not have enough memory to make a copy of the message pointed to by msgp\n");
			/* wait 10 msec ,then it will try again */
			usleep(10000);
			/*  it will try 5 times, after 5 times ,if it still fail ,then it will clear the message queue */
			if (try_again <= 5) {
				try_again ++;
				continue;
			}
			/* message queue is full ,it need to clear the queue */
			while( msgrcv(g_msg_scsnd, &msgdata, DSIZE, 0, IPC_NOWAIT) != -1 ) {
				debug_warning("msg queue is full ,remove msgtype:[%d] from the queue",msgdata.sound_msg.msgtype);
			}
			try_again++;
			continue;
		} else if(errno == EIDRM) {
			debug_warning("Removed msgid from system\n");
		} else if(errno == EINTR) {
			debug_warning("Iterrrupted by singnal\n");
		} else if(errno == EINVAL) {
			debug_warning("Invalid msgid or msgtype < 1 or out of data size \n");
		} else if(errno == EFAULT) {
			debug_warning("The address pointed to by msgp isn't accessible \n");
		}
		debug_error("[Client] Fail to send message msgid : [%d], [%d][%s]", g_msg_scsnd, errno, strerror(errno));
		return MM_ERROR_SOUND_INTERNAL;
	}
	return MM_ERROR_NONE;
}

static int __MMSoundGetMsg(void)
{
	/* Init message queue, generate msgid for communication to server */
	/* The key value to get msgid is defined "mm_sound_msg.h". Shared with server */
	int i = 0;

	debug_fenter();

	/* get msg queue rcv, snd, cb */
	g_msg_scsnd = msgget(ftok(KEY_BASE_PATH, RCV_MSG), 0666);
	g_msg_scrcv = msgget(ftok(KEY_BASE_PATH, SND_MSG), 0666);
	g_msg_sccb = msgget(ftok(KEY_BASE_PATH, CB_MSG), 0666);

	if ((g_msg_scsnd == -1 || g_msg_scrcv == -1 || g_msg_sccb == -1) != MM_ERROR_NONE) {
		if (errno == EACCES) {
			debug_warning("Require ROOT permission.\n");
		} else if (errno == ENOMEM) {
			debug_warning("System memory is empty.\n");
		} else if(errno == ENOSPC) {
			debug_warning("Resource is empty.\n");
		}
		/* Some app would start before Sound Server IPC ready. */
		/* Let's try it again in 50ms later by 10 times */
		for (i=0;i<10;i++) {
			usleep(50000);
			g_msg_scsnd = msgget(ftok(KEY_BASE_PATH, RCV_MSG), 0666);
			g_msg_scrcv = msgget(ftok(KEY_BASE_PATH, SND_MSG), 0666);
			g_msg_sccb = msgget(ftok(KEY_BASE_PATH, CB_MSG), 0666);
			if ((g_msg_scsnd == -1 || g_msg_scrcv == -1 || g_msg_sccb == -1) != MM_ERROR_NONE) {
				debug_error("Fail to GET msgid by retrying %d times\n", i+1);
			} else
				break;
		}
		if ((g_msg_scsnd == -1 || g_msg_scrcv == -1 || g_msg_sccb == -1) != MM_ERROR_NONE) {
			debug_error("Fail to GET msgid finally, just return internal error.\n");
			return MM_ERROR_SOUND_INTERNAL;
		}
	}

	debug_log("Get msg queue id from server : snd[%d], rcv[%d], cb[%d]\n", g_msg_scsnd, g_msg_scrcv, g_msg_sccb);

	debug_fleave();
	return MM_ERROR_NONE;
}

#ifdef PULSE_CLIENT

int MMSoundClientIsBtA2dpOn (bool *connected, char** bt_name)
{
	mm_ipc_msg_t msgrcv = {0,};
	mm_ipc_msg_t msgsnd = {0,};
	int ret = MM_ERROR_NONE;
	int instance;

	debug_fenter();

	instance = getpid();

	ret = __mm_sound_client_get_msg_queue();
	if (ret  != MM_ERROR_NONE)
		return ret;

	pthread_mutex_lock(&g_thread_mutex);

	/* Send req  */
	msgsnd.sound_msg.msgtype = MM_SOUND_MSG_REQ_IS_BT_A2DP_ON;
	msgsnd.sound_msg.msgid = instance;

	ret = __MMIpcSndMsg(&msgsnd);
	if (ret != MM_ERROR_NONE)
	{
		debug_error("Fail to send msg\n");
		goto cleanup;
	}

	/* Receive */
	ret = __MMIpcRecvMsg(instance, &msgrcv);
	if (ret != MM_ERROR_NONE)
	{
		debug_error("Fail to recieve msg\n");
		goto cleanup;
	}
	switch (msgrcv.sound_msg.msgtype)
	{
		case MM_SOUND_MSG_RES_IS_BT_A2DP_ON:
			debug_msg("Success to get IS_BT_A2DP_ON [%d][%s]\n", msgrcv.sound_msg.code, msgrcv.sound_msg.filename);
			*connected  = (bool)msgrcv.sound_msg.code;
			if (*connected)
				*bt_name = strdup (msgrcv.sound_msg.filename);
			else
				*bt_name = NULL;
			break;
		case MM_SOUND_MSG_RES_ERROR:
			debug_error("Error occurred \n");
			ret = msgrcv.sound_msg.code;
			goto cleanup;
			break;
		default:
			debug_critical("Unexpected state with communication \n");
			ret = msgrcv.sound_msg.code;
			goto cleanup;
			break;
	}

cleanup:
		pthread_mutex_unlock(&g_thread_mutex);

	debug_fleave();
	return ret;
}

int _mm_sound_client_set_sound_path_for_active_device(mm_sound_device_out device_out, mm_sound_device_in device_in)
{
	mm_ipc_msg_t msgrcv = {0,};
	mm_ipc_msg_t msgsnd = {0,};
	int ret = MM_ERROR_NONE;
	int instance;

	debug_fenter();

	ret = __mm_sound_client_get_msg_queue();
	if (ret  != MM_ERROR_NONE)
		return ret;

	pthread_mutex_lock(&g_thread_mutex);

	instance = getpid();
	/* Send MM_SOUND_MSG_REQ_SET_PATH_FOR_ACTIVE_DEVICE */
	msgsnd.sound_msg.msgtype = MM_SOUND_MSG_REQ_SET_PATH_FOR_ACTIVE_DEVICE;
	msgsnd.sound_msg.msgid = instance;
	msgsnd.sound_msg.device_in =  device_in;
	msgsnd.sound_msg.device_out = device_out;

	if (__MMIpcSndMsg(&msgsnd) != MM_ERROR_NONE)
		goto cleanup;

	/* Recieve */
	if (__MMIpcRecvMsg(instance, &msgrcv) != MM_ERROR_NONE)
		goto cleanup;

	switch (msgrcv.sound_msg.msgtype)
	{
	case MM_SOUND_MSG_RES_SET_PATH_FOR_ACTIVE_DEVICE:
		debug_msg("[Client] Success to setsound path for active device\n");
		break;
	case MM_SOUND_MSG_RES_ERROR:
		debug_error("[Client] Error occurred \n");
		ret = msgrcv.sound_msg.code;
		goto cleanup;
		break;
	default:
		debug_critical("[Client] Unexpected state with communication \n");
		ret = msgrcv.sound_msg.code;
		goto cleanup;
		break;
	}

cleanup:
	pthread_mutex_unlock(&g_thread_mutex);

	debug_fleave();
	return ret;
}


#endif // PULSE_CLIENT
