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

#ifndef __MM_SOUND_IPC_H__
#define __MM_SOUND_IPC_H__

#include <stdio.h>
#include <unistd.h>

#include "mm_sound.h"
#include "mm_sound_device.h"


#define FILE_PATH 512

typedef enum {
    MM_IPC_SUCCESS,
    MM_IPC_WARNING,
    MM_IPC_ERROR,
    MM_IPC_PROCESS,
} mm_ipc_async_state;

typedef struct
{
	/* Recieve data */
	int msgid;
	int msgtype;
	int code;
	
	/* Send data */
	int keytone;
	int repeat;
	int tone;
	double volume;
	int memptr;
	int memsize;
	int sharedkey;
	char filename[FILE_PATH];

	/* Device */
	mm_sound_device_t device_handle;
	int total_device_num;
	int device_flags;
	bool is_connected;
	int changed_device_info_type;

	int route;
	int device_in;
	int device_out;
	int is_available;
	int route_list[MM_SOUND_ROUTE_NUM];

	/* Volume */
	int type;
	int val;

	/* Common data */
	int handle;
	void *callback;
	void *cbdata;
	int samplerate;
	int channels;
	int volume_config;
	int session_type;
	int session_options;
	int priority;
	int handle_route;

	bool enable_session;
	bool need_broadcast;

	char name[MM_SOUND_NAME_NUM];
} mmsound_ipc_t;

typedef struct
{
	long msg_type;
	mmsound_ipc_t sound_msg;
} mm_ipc_msg_t;

typedef void (*mm_ipc_callback_t)(int code, int size);

int MMSoundGetTime(char *position);
int MMIpcCreate(const int key);
int MMIpcDestroy(const int key);
int MMIpcSendMsg(const int key, mm_ipc_msg_t *msg);
int MMIpcRecvMsg(const int key, mm_ipc_msg_t *msg);

int MMIpcSendMsgAsync(const char *ipcname, mm_ipc_msg_t *msg, mm_ipc_callback_t callback);
int MMIpcRecvMsgAsync(const char *ipcname, mm_ipc_msg_t **msg, mm_ipc_callback_t callback);
int MMIpcRecvData(const char *ipcname, void *data, int *size);
int MMIpcSendDataAsync(const char *ipcname, void *data, int size, mm_ipc_callback_t callback);
int __mm_sound_lock(void);
int __mm_sound_unlock(void);

#endif  /* __MM_SOUND_IPC_H__ */

