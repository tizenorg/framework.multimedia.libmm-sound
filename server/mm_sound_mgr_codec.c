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

#include <mm_source.h>
#include <mm_error.h>
#include <mm_types.h>
#include <mm_debug.h>
#include <mm_ipc.h>

#include "include/mm_sound_mgr_common.h"
#include "include/mm_sound_mgr_codec.h"
#include "include/mm_sound_plugin_codec.h"
#include "include/mm_sound_thread_pool.h"
#include "include/mm_sound_pa_client.h"
#include "include/mm_sound_mgr_asm.h"



#define _ENABLE_KEYTONE	/* Temporal test code */

typedef struct {
	int (*callback)(int, void *, void *, int);	/* msg_type(pid) client callback & client data info */
	void *param;
	int pid;
	void *msgcallback;
	void *msgdata;
	MMHandleType plughandle;

	int pluginid;
	int status;
	int session_type;
	int session_options;
	int session_handle;

	bool enable_session;
 } __mmsound_mgr_codec_handle_t;

static MMSoundPluginType *g_codec_plugins = NULL;
static __mmsound_mgr_codec_handle_t g_slots[MANAGER_HANDLE_MAX];
static mmsound_codec_interface_t g_plugins[MM_SOUND_SUPPORTED_CODEC_NUM];
static pthread_mutex_t g_slot_mutex;
static pthread_mutex_t codec_wave_mutex;
static int _MMSoundMgrCodecStopCallback(int param);
static int _MMSoundMgrCodecFindKeytoneSlot(int *slotid);
static int _MMSoundMgrCodecGetEmptySlot(int *slotid);
static int _MMSoundMgrCodecFindLocaleSlot(int *slotid);
static int _MMSoundMgrCodecRegisterInterface(MMSoundPluginType *plugin);

#define STATUS_IDLE 0
#define STATUS_KEYTONE 1
#define STATUS_LOCALE 2
#define STATUS_SOUND 3

#define SOUND_SLOT_START 0



ASM_cb_result_t
sound_codec_asm_callback(int handle, ASM_event_sources_t event_src, ASM_sound_commands_t command, unsigned int sound_status, void* cb_data)
{
	int slotid = (int)cb_data;
	int result = MM_ERROR_NONE;
	ASM_cb_result_t	cb_res = ASM_CB_RES_NONE;

	debug_log("Got audio session callback msg for session_handle %d\n", handle);

	switch(command)
	{
	case ASM_COMMAND_STOP:
	case ASM_COMMAND_PAUSE:
		debug_log("Got msg from asm to Stop or Pause %d\n", command);
		result = MMSoundMgrCodecStop(slotid);
		if (result != MM_ERROR_NONE) {
			debug_log("result error %d\n", result);
		}
		cb_res = ASM_CB_RES_STOP;
		break;
	case ASM_COMMAND_RESUME:
	case ASM_COMMAND_PLAY:
		debug_log("Got msg from asm to Play or Resume %d\n", command);
		cb_res = ASM_CB_RES_NONE;;
	default:
		break;
	}
	return cb_res;
}


int MMSoundMgrCodecInit(const char *targetdir)
{
	int loop = 0;
	int count = 0;

	debug_enter("\n");

	memset (g_slots, 0, sizeof(g_slots));

	if(pthread_mutex_init(&g_slot_mutex, NULL)) {
		debug_error("pthread_mutex_init failed\n");
		return MM_ERROR_SOUND_INTERNAL;
	}

	if(pthread_mutex_init(&codec_wave_mutex, NULL)) {
		debug_error("pthread_mutex_init failed\n");
		return MM_ERROR_SOUND_INTERNAL;
	}

	for (count = 0; count < MANAGER_HANDLE_MAX; count++) {
		g_slots[count].status = STATUS_IDLE;
		g_slots[count].plughandle = 0;
	}

	if (g_codec_plugins) {
		debug_warning("Please Check Init twice\n");
		MMSoundPluginRelease(g_codec_plugins);
	}

	MMSoundPluginScan(targetdir, MM_SOUND_PLUGIN_TYPE_CODEC, &g_codec_plugins);

	while (g_codec_plugins[loop].type != MM_SOUND_PLUGIN_TYPE_NONE) {
		_MMSoundMgrCodecRegisterInterface(&g_codec_plugins[loop++]);
	}

	debug_leave("\n");
	return MM_ERROR_NONE;
}

int MMSoundMgrCodecFini(void)
{
	debug_enter("\n");

	memset(g_plugins, 0, sizeof(mmsound_codec_interface_t) * MM_SOUND_SUPPORTED_CODEC_NUM);
	MMSoundPluginRelease(g_codec_plugins);
	g_codec_plugins = NULL;
	pthread_mutex_destroy(&g_slot_mutex);
	pthread_mutex_destroy(&codec_wave_mutex);
	debug_leave("\n");
	return MM_ERROR_NONE;
}


int MMSoundMgrCodecPlay(int *slotid, const mmsound_mgr_codec_param_t *param)
{
	int count = 0;
	mmsound_codec_info_t info;
	mmsound_codec_param_t codec_param;
	int err = MM_ERROR_NONE;
	int errorcode = 0;
	int need_asm_unregister = 0;

#ifdef DEBUG_DETAIL
	debug_enter("\n");
#endif

	for (count = 0; g_plugins[count].GetSupportTypes; count++) {
		/* Find codec */
		if (g_plugins[count].Parse(param->source, &info) == MM_ERROR_NONE)
			break;
	}

	/*The count num means codec type WAV, MP3 */
	debug_msg("DTMF[%d] Repeat[%d] Volume[%f] plugin_codec[%d]\n", param->tone, param->repeat_count, param->volume, count);

	if (g_plugins[count].GetSupportTypes == NULL) {	/* Codec not found */
		debug_error("unsupported file type %d\n", count);
		err = MM_ERROR_SOUND_UNSUPPORTED_MEDIA_TYPE;
		goto cleanup;
	}

	/* KeyTone */
	if (param->keytone == 1) {
		/* Find keytone slot */
		err = _MMSoundMgrCodecFindKeytoneSlot(slotid);
		/* Not First connect */
		if (err == MM_ERROR_NONE) {
			if(g_slots[*slotid].status != STATUS_IDLE) {
				MMSoundMgrCodecStop(*slotid);
			}
			debug_msg("Key tone : Stop to Play !!!\n");
		}
		codec_param.keytone = param->keytone;
	} else if (param->keytone == 2) {
		/* Find keytone slot */
		err = _MMSoundMgrCodecFindLocaleSlot(slotid);
		/* Not First connect */
		if (err == MM_ERROR_NONE) {
			if(g_slots[*slotid].status != STATUS_IDLE) {
				MMSoundMgrCodecStop(*slotid);
			}
			debug_msg("Key tone : Stop to Play !!!\n");
		}
		codec_param.keytone = param->keytone;
	} else {
#ifdef DEBUG_DETAIL
		debug_msg("Get New handle\n");
#endif
		codec_param.keytone = 0;
	}

	err = _MMSoundMgrCodecGetEmptySlot(slotid);
	if (err != MM_ERROR_NONE) {
		debug_error("Empty g_slot is not found\n");
		goto cleanup;
	}

	codec_param.tone = param->tone;
	codec_param.volume_config = param->volume_config;
	codec_param.repeat_count = param->repeat_count;
	codec_param.volume = param->volume;
	codec_param.source = param->source;
	codec_param.priority = param->priority;
	codec_param.stop_cb = _MMSoundMgrCodecStopCallback;
	codec_param.param = *slotid;
	codec_param.pid = (int)param->param;
	codec_param.handle_route = param->handle_route;
	codec_param.codec_wave_mutex = &codec_wave_mutex;
	pthread_mutex_lock(&g_slot_mutex);
#ifdef DEBUG_DETAIL
	debug_msg("After Slot_mutex LOCK\n");
#endif

	/* In case of KEYTONE */
	if (param->keytone == 1)
		g_slots[*slotid].status = STATUS_KEYTONE;

	/* In case of LOCALE */
	if (param->keytone == 2) /* KeyTone */
		g_slots[*slotid].status = STATUS_LOCALE;

	/*
	 * Register ASM here
	 */
	if (param->session_type != ASM_EVENT_CALL &&
		param->session_type != ASM_EVENT_VIDEOCALL &&
		param->session_type != ASM_EVENT_VOIP &&
		param->session_type != ASM_EVENT_VOICE_RECOGNITION &&
		param->priority != HANDLE_PRIORITY_SOLO &&
		param->enable_session) {
		if(!ASM_register_sound_ex((int)param->param, (int *)(&param->session_handle), param->session_type, ASM_STATE_NONE,
								sound_codec_asm_callback, (void*)*slotid, ASM_RESOURCE_NONE, &errorcode, __asm_process_message)) {
			debug_critical("ASM_register_sound_ex() failed 0x%X\n", errorcode);
			pthread_mutex_unlock(&g_slot_mutex);
			return MM_ERROR_POLICY_INTERNAL;
		}
		if(param->session_options) {
			if(!ASM_set_session_option(param->session_handle, param->session_options, &errorcode)) {
				debug_error("ASM_set_session_option() failed 0x%x\n", errorcode);
			}
		}
		if(!ASM_set_sound_state_ex(param->session_handle, param->session_type, ASM_STATE_PLAYING, ASM_RESOURCE_NONE, &errorcode, __asm_process_message)) {
			debug_critical("ASM_set_sound_state_ex() failed 0x%X\n", errorcode);
			pthread_mutex_unlock(&g_slot_mutex);
			if(!ASM_unregister_sound_ex(param->session_handle, param->session_type, &errorcode,__asm_process_message)) {
				debug_error("ASM_unregister_sound_ex() failed 0x%X\n", errorcode);
			}
			return MM_ERROR_POLICY_INTERNAL;
		}
	}
	//

	/* Codec id WAV or MP3 */
	g_slots[*slotid].pluginid = count;
	g_slots[*slotid].callback = param->callback;
	g_slots[*slotid].msgcallback = param->msgcallback;
	g_slots[*slotid].msgdata = param->msgdata;
	g_slots[*slotid].param    = param->param;		/* This arg is used callback data */
	g_slots[*slotid].session_type = param->session_type;
	g_slots[*slotid].session_options = param->session_options;
	g_slots[*slotid].session_handle = param->session_handle;
	g_slots[*slotid].enable_session = true;

	debug_msg("Using Slotid : [%d] Slot Status : [%d]\n", *slotid, g_slots[*slotid].status);

	err = g_plugins[g_slots[*slotid].pluginid].Create(&codec_param, &info, &(g_slots[*slotid].plughandle));
	debug_msg("Created audio handle : [%d]\n", g_slots[*slotid].plughandle);
	if (err != MM_ERROR_NONE) {
		debug_error("Plugin create fail : 0x%08X\n", err);
		g_slots[*slotid].status = STATUS_IDLE;
		pthread_mutex_unlock(&g_slot_mutex);
		debug_warning("After Slot_mutex UNLOCK\n");
		if (param->session_handle) {
			need_asm_unregister = 1;
		}
		goto cleanup;
	}

	err = g_plugins[g_slots[*slotid].pluginid].Play(g_slots[*slotid].plughandle);
	if (err != MM_ERROR_NONE) {
		debug_error("Fail to play : 0x%08X\n", err);
		g_plugins[g_slots[*slotid].pluginid].Destroy(g_slots[*slotid].plughandle);
		if (param->session_handle) {
			need_asm_unregister = 1;
		}
	}

	pthread_mutex_unlock(&g_slot_mutex);
#ifdef DEBUG_DETAIL
	debug_msg("After Slot_mutex UNLOCK\n");
#endif

cleanup:
	if(param->session_type != ASM_EVENT_CALL &&
		param->session_type != ASM_EVENT_VIDEOCALL &&
		param->session_type != ASM_EVENT_VOIP &&
		param->session_type != ASM_EVENT_VOICE_RECOGNITION &&
		param->enable_session &&
		need_asm_unregister == 1) {
		if(!ASM_set_sound_state_ex(param->session_handle, param->session_type, ASM_STATE_STOP, ASM_RESOURCE_NONE, &errorcode, __asm_process_message)) {
			debug_critical("ASM_set_sound_state_ex() failed 0x%X\n", errorcode);
		}
		if(!ASM_unregister_sound_ex(param->session_handle, param->session_type, &errorcode,__asm_process_message)) {
			debug_error("ASM_unregister_sound_ex() failed 0x%X\n", errorcode);
			return MM_ERROR_POLICY_INTERNAL;
		}
	}

#ifdef DEBUG_DETAIL
	debug_leave("\n");
#endif

	return err;
}

#define DTMF_PLUGIN_COUNT 2
int MMSoundMgrCodecPlayDtmf(int *slotid, const mmsound_mgr_codec_param_t *param)
{
	int count = 0;
	int *codec_type;
	mmsound_codec_info_t info;
	mmsound_codec_param_t codec_param;
	int err = MM_ERROR_NONE;
	int errorcode = 0;
	int need_asm_unregister = 0;

#ifdef DEBUG_DETAIL
	debug_enter("\n");
#endif

	for (count = 0; g_plugins[count].GetSupportTypes; count++) {
		/* Find codec */
		codec_type = g_plugins[count].GetSupportTypes();
		if(codec_type && (MM_SOUND_SUPPORTED_CODEC_DTMF == codec_type[0]))
			break;
	}

	/*The count num means codec type DTMF */
	debug_msg("DTMF[%d] Repeat[%d] Volume[%f] plugin_codec[%d]\n", param->tone, param->repeat_count, param->volume, count);

	if (g_plugins[count].GetSupportTypes == NULL) {	/* Codec not found */
		debug_error("unsupported file type %d\n", count);
		printf("unsupported file type %d\n", count);
		err = MM_ERROR_SOUND_UNSUPPORTED_MEDIA_TYPE;
		goto cleanup;
	}

#ifdef DEBUG_DETAIL
	debug_msg("Get New handle\n");
#endif
	codec_param.keytone = 0;

	err = _MMSoundMgrCodecGetEmptySlot(slotid);
	if(err != MM_ERROR_NONE)
	{
		debug_error("Empty g_slot is not found\n");
		goto cleanup;
	}

	codec_param.tone = param->tone;
	codec_param.priority = 0;
	codec_param.volume_config = param->volume_config;
	codec_param.repeat_count = param->repeat_count;
	codec_param.volume = param->volume;
	codec_param.stop_cb = _MMSoundMgrCodecStopCallback;
	codec_param.param = *slotid;
	codec_param.pid = (int)param->param;

	pthread_mutex_lock(&g_slot_mutex);
#ifdef DEBUG_DETAIL
	debug_msg("After Slot_mutex LOCK\n");
#endif

	//
	/*
	 * Register ASM here
	 */

	if (param->session_type != ASM_EVENT_CALL &&
		param->session_type != ASM_EVENT_VIDEOCALL &&
		param->session_type != ASM_EVENT_VOIP &&
		param->session_type != ASM_EVENT_VOICE_RECOGNITION &&
		param->enable_session)	{
		if(!ASM_register_sound_ex((int)param->param, (int *)(&param->session_handle), param->session_type, ASM_STATE_NONE,
								sound_codec_asm_callback, (void*)*slotid, ASM_RESOURCE_NONE, &errorcode, __asm_process_message)) {
			debug_critical("ASM_register_sound_ex() failed 0x%X\n", errorcode);
			pthread_mutex_unlock(&g_slot_mutex);
			return MM_ERROR_POLICY_INTERNAL;
		}
		if(param->session_options) {
			if(!ASM_set_session_option(param->session_handle, param->session_options, &errorcode)) {
				debug_error("ASM_set_session_option() failed 0x%x\n", errorcode);
			}
		}
		if(!ASM_set_sound_state_ex(param->session_handle, param->session_type, ASM_STATE_PLAYING, ASM_RESOURCE_NONE, &errorcode, __asm_process_message)) {
			debug_critical("ASM_set_sound_state_ex() failed 0x%X\n", errorcode);
			pthread_mutex_unlock(&g_slot_mutex);
			if(!ASM_unregister_sound_ex(param->session_handle, param->session_type, &errorcode,__asm_process_message)) {
				debug_error("ASM_unregister_sound_ex() failed 0x%X\n", errorcode);
			}
			return MM_ERROR_POLICY_INTERNAL;
		}
	}

	g_slots[*slotid].pluginid = count;
	g_slots[*slotid].callback = param->callback;
	g_slots[*slotid].param    = param->param;		/* This arg is used callback data */
	g_slots[*slotid].session_type = param->session_type;
	g_slots[*slotid].session_options = param->session_options;
	g_slots[*slotid].session_handle = param->session_handle;
	g_slots[*slotid].enable_session = param->enable_session;

#ifdef DEBUG_DETAIL
	debug_msg("Using Slotid : [%d] Slot Status : [%d]\n", *slotid, g_slots[*slotid].status);
#endif

	err = g_plugins[g_slots[*slotid].pluginid].Create(&codec_param, &info, &(g_slots[*slotid].plughandle));
	debug_msg("Created audio handle : [%d]\n", g_slots[*slotid].plughandle);
	if (err != MM_ERROR_NONE) {
		debug_error("Plugin create fail : 0x%08X\n", err);
		g_slots[*slotid].status = STATUS_IDLE;
		pthread_mutex_unlock(&g_slot_mutex);
		debug_warning("After Slot_mutex UNLOCK\n");
		need_asm_unregister = 1;
		goto cleanup;
	}

	err = g_plugins[g_slots[*slotid].pluginid].Play(g_slots[*slotid].plughandle);
	if (err != MM_ERROR_NONE) {
		debug_error("Fail to play : 0x%08X\n", err);
		g_plugins[g_slots[*slotid].pluginid].Destroy(g_slots[*slotid].plughandle);
		need_asm_unregister = 1;
	}

	pthread_mutex_unlock(&g_slot_mutex);

	debug_msg("Using Slotid : [%d] Slot Status : [%d]\n", *slotid, g_slots[*slotid].status);
#ifdef DEBUG_DETAIL
	debug_msg("After Slot_mutex UNLOCK\n")
#endif

cleanup:
	if (param->session_type != ASM_EVENT_CALL &&
		param->session_type != ASM_EVENT_VIDEOCALL &&
		param->session_type != ASM_EVENT_VOIP &&
		param->session_type != ASM_EVENT_VOICE_RECOGNITION &&
		param->enable_session &&
		need_asm_unregister == 1) {
		if(!ASM_unregister_sound_ex(param->session_handle, param->session_type, &errorcode,__asm_process_message)) {
			debug_error("Unregister sound failed 0x%X\n", errorcode);
			return MM_ERROR_POLICY_INTERNAL;
		}
	}

#ifdef DEBUG_DETAIL
	debug_leave("\n");
#endif

	return err;
}


int MMSoundMgrCodecStop(const int slotid)
{
	int err = MM_ERROR_NONE;

	debug_enter("(Slotid : [%d])\n", slotid);

	if (slotid < 0 || MANAGER_HANDLE_MAX <= slotid) {
		return MM_ERROR_INVALID_ARGUMENT;
	}

	pthread_mutex_lock (&g_slot_mutex);
#ifdef DEBUG_DETAIL
	debug_msg("After Slot_mutex LOCK\n");
#endif
	if (g_slots[slotid].status == STATUS_IDLE) {
		err = MM_ERROR_SOUND_INVALID_STATE;
		debug_warning("The playing slots is not found, Slot ID : [%d]\n", slotid);
		goto cleanup;
	}
#ifdef DEBUG_DETAIL
	debug_msg("Found slot, Slotid [%d] State [%d]\n", slotid, g_slots[slotid].status);
#endif

	err = g_plugins[g_slots[slotid].pluginid].Stop(g_slots[slotid].plughandle);
	if (err != MM_ERROR_NONE) {
		debug_error("Fail to STOP Code : 0x%08X\n", err);
	}
	debug_msg("Found slot, Slotid [%d] State [%d]\n", slotid, g_slots[slotid].status);
cleanup:
	pthread_mutex_unlock(&g_slot_mutex);
#ifdef DEBUG_DETAIL
	debug_msg("After Slot_mutex UNLOCK\n");
#endif
	debug_leave("(err : 0x%08X)\n", err);

	return err;
}

static int _MMSoundMgrCodecStopCallback(int param)
{
	int err = MM_ERROR_NONE;

	debug_enter("(Slot : %d)\n", param);

	pthread_mutex_lock(&g_slot_mutex);
	debug_msg("[CODEC MGR] Slot_mutex lock done\n");


	/*
	 * Unregister ASM here
	 */

	int errorcode = 0;
	debug_msg("[CODEC MGR] enable_session %d ",g_slots[param].enable_session);

	if (g_slots[param].session_handle) {
		if(g_slots[param].session_type != ASM_EVENT_CALL &&
			g_slots[param].session_type != ASM_EVENT_VIDEOCALL &&
			g_slots[param].session_type != ASM_EVENT_VOIP &&
			g_slots[param].session_type != ASM_EVENT_VOICE_RECOGNITION &&
			g_slots[param].enable_session ) {
			if(!ASM_set_sound_state_ex(g_slots[param].session_handle, g_slots[param].session_type, ASM_STATE_STOP, ASM_RESOURCE_NONE, &errorcode, __asm_process_message)) {
				debug_error("[CODEC MGR] ASM_set_sound_state_ex() failed 0x%X\n", errorcode);
			}
			debug_msg("[CODEC MGR] ASM unregister\n");
			if(!ASM_unregister_sound_ex(g_slots[param].session_handle, g_slots[param].session_type, &errorcode, __asm_process_message)) {
				debug_error("[CODEC MGR] ASM_unregister_sound_ex() failed 0x%X\n", errorcode);
			}
		}
	}

	if (g_slots[param].msgcallback) {
		debug_msg("[CODEC MGR] msgcallback : %p\n", g_slots[param].msgcallback);
		debug_msg("[CODEC MGR] msg data : %p\n", g_slots[param].msgdata);
		debug_msg("[CODEC MGR] mgr codec callback : %p\n", g_slots[param].callback);
		g_slots[param].callback((int)g_slots[param].param, g_slots[param].msgcallback, g_slots[param].msgdata, param);		/*param means client msg_type */
	}
	debug_msg("Client callback msg_type (instance) : [%d]\n", (int)g_slots[param].param);
	debug_msg("Handle allocated handle : [0x%08X]\n", g_slots[param].plughandle);
	err = g_plugins[g_slots[param].pluginid].Destroy(g_slots[param].plughandle);
	if (err < 0 ) {
		debug_critical("[CODEC MGR] Fail to destroy slot number : [%d] err [0x%x]\n", param, err);
	}
	memset(&g_slots[param], 0, sizeof(__mmsound_mgr_codec_handle_t));
	g_slots[param].status = STATUS_IDLE;
	pthread_mutex_unlock(&g_slot_mutex);
	debug_msg("[CODEC MGR] Slot_mutex done\n");

	return err;
}

static int _MMSoundMgrCodecFindKeytoneSlot(int *slotid)
{
	int count = 0;
	int err = MM_ERROR_NONE;

#ifdef DEBUG_DETAIL
	debug_enter("\n");
#endif

	pthread_mutex_lock(&g_slot_mutex);
#ifdef DEBUG_DETAIL
	debug_warning("After Slot_mutex LOCK\n");
#endif

	for (count = SOUND_SLOT_START; count < MANAGER_HANDLE_MAX ; count++) {
		if (g_slots[count].status == STATUS_KEYTONE) {
			break;
		}
	}
	pthread_mutex_unlock(&g_slot_mutex);
#ifdef DEBUG_DETAIL
	debug_warning("After Slot_mutex UNLOCK\n");
#endif
	if (count < MANAGER_HANDLE_MAX) {
		debug_msg("Found keytone handle allocated (Slot : [%d])\n", count);
		*slotid = count;
		err =  MM_ERROR_NONE;
	} else {
		debug_warning("Handle is full handle [KEY TONE] : [%d]\n", count);
		err =  MM_ERROR_SOUND_INTERNAL;
	}

#ifdef DEBUG_DETAIL
	debug_leave("\n");
#endif

	return err;
}

static int _MMSoundMgrCodecFindLocaleSlot(int *slotid)
{
	int count = 0;
	int err = MM_ERROR_NONE;

#ifdef DEBUG_DETAIL
	debug_enter("\n");
#endif

	pthread_mutex_lock(&g_slot_mutex);
#ifdef DEBUG_DETAIL
	debug_warning("After Slot_mutex LOCK\n");
#endif

	for (count = SOUND_SLOT_START; count < MANAGER_HANDLE_MAX ; count++) {
		if (g_slots[count].status == STATUS_LOCALE) {
			break;
		}
	}
	pthread_mutex_unlock(&g_slot_mutex);

#ifdef DEBUG_DETAIL
	debug_warning("After Slot_mutex UNLOCK\n");
#endif
	if (count < MANAGER_HANDLE_MAX) {
		debug_msg("Found locale handle allocated (Slot : [%d])\n", count);
		*slotid = count;
		err =  MM_ERROR_NONE;
	} else {
		debug_warning("Handle is full handle [KEY TONE] \n");
		err =  MM_ERROR_SOUND_INTERNAL;
	}

#ifdef DEBUG_DETAIL
	debug_leave("\n");
#endif

	return err;
}

static int _MMSoundMgrCodecGetEmptySlot(int *slot)
{
	int count = 0;
	int err = MM_ERROR_NONE;

#ifdef DEBUG_DETAIL
	debug_enter("\n");
#endif
	debug_msg("Codec slot ID : [%d]\n", *slot);
	pthread_mutex_lock(&g_slot_mutex);
#ifdef DEBUG_DETAIL
	debug_msg("After Slot_mutex LOCK\n");
#endif

	for (count = SOUND_SLOT_START; count < MANAGER_HANDLE_MAX ; count++) {
		if (g_slots[count].status == STATUS_IDLE) {
			g_slots[count].status = STATUS_SOUND;
			break;
		}
	}
	pthread_mutex_unlock(&g_slot_mutex);
#ifdef DEBUG_DETAIL
	debug_msg("After Slot_mutex UNLOCK\n");
#endif

	if (count < MANAGER_HANDLE_MAX)	{
		debug_msg("New handle allocated (codec slot ID : [%d])\n", count);
		*slot = count;
		err =  MM_ERROR_NONE;
	} else {
		debug_warning("Handle is full handle : [%d]\n", count);
		*slot = -1;
		/* Temporal code for reset */
		while(count--) {
			g_slots[count].status = STATUS_IDLE;
		}
		err =  MM_ERROR_SOUND_INTERNAL;
	}

#ifdef DEBUG_DETAIL
	debug_leave("\n");
#endif

	return err;
}

static int _MMSoundMgrCodecRegisterInterface(MMSoundPluginType *plugin)
{
	int err = MM_ERROR_NONE;
	int count = 0;
	void *getinterface = NULL;

#ifdef DEBUG_DETAIL
	debug_enter("\n");
#endif

	/* find emptry slot */
	for (count = 0; count < MM_SOUND_SUPPORTED_CODEC_NUM; count++) {
		if (g_plugins[count].GetSupportTypes == NULL)
			break;
	}

	if (count == MM_SOUND_SUPPORTED_CODEC_NUM) {
		debug_critical("The plugin support type is not valid\n");
		return MM_ERROR_COMMON_OUT_OF_RANGE;
	}

	err = MMSoundPluginGetSymbol(plugin, CODEC_GET_INTERFACE_FUNC_NAME, &getinterface);
	if (err != MM_ERROR_NONE) {
		debug_error("Get Symbol CODEC_GET_INTERFACE_FUNC_NAME is fail : %x\n", err);
		goto cleanup;
	}
	debug_msg("interface[%s] empty_slot[%d]\n", (char*)getinterface, count);

	err = MMSoundPlugCodecCastGetInterface(getinterface)(&g_plugins[count]);
	if (err != MM_ERROR_NONE) {
		debug_error("Get interface fail : %x\n", err);

cleanup:
		/* If error occur, clean interface */
		memset(&g_plugins[count], 0, sizeof(mmsound_codec_interface_t));
	} else {
		if (g_plugins[count].SetThreadPool)
			g_plugins[count].SetThreadPool(MMSoundThreadPoolRun);
	}

#ifdef DEBUG_DETAIL
	debug_leave("\n");
#endif

	return err;
}

