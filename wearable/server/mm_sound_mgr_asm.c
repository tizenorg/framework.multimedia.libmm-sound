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
#include "include/mm_sound_thread_pool.h"
#include "../include/mm_sound_common.h"

#include <mm_error.h>
#include <mm_debug.h>


#include <avsys-audio.h>

#include "include/mm_sound_mgr_asm.h"
#include "include/mm_sound_mgr_session.h"
#include "include/mm_sound_mgr_ipc.h"
#include "../include/mm_sound_utils.h"

#define USE_IO_PRIORITY
#ifdef USE_IO_PRIORITY /* IO Priority */
#include <sys/syscall.h>
#endif

pthread_mutex_t g_mutex_asm = PTHREAD_MUTEX_INITIALIZER;




/******************************* ASM Code **********************************/

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/msg.h>
#include <glib.h>
#include <glib/gprintf.h>
#include <poll.h>

#include <vconf.h>
#include <audio-session-manager.h>
#include <string.h>
#include <errno.h>

#ifdef USE_SECURITY
#include <security-server.h>
#endif

#include <avsys-audio.h>

#define USE_SYSTEM_SERVER_PROCESS_MONITORING

#define SUPPORT_GCF /* currently in use */

#define ROW_NUM_OF_SUB_EVENT		16 /* it should be exactly same with number of ASM_CASE_SUB_EVENT in ASM_sound_case table */
#define NUM_OF_ADV_ASM_EVENT		3

#define SERVER_HANDLE_MAX_COUNT	256

#define WILL_BE_DEPRECATED

/* check whether if it is advanced event type */
#define ASM_IS_ADVANCED_ASM_EVENT(x_sound_event) (x_sound_event == ASM_EVENT_VOICE_RECOGNITION || x_sound_event == ASM_EVENT_MMCAMCORDER_AUDIO || x_sound_event == ASM_EVENT_MMCAMCORDER_VIDEO)

static const ASM_sound_cases_t ASM_sound_case[ASM_EVENT_MAX][ASM_EVENT_MAX] =
{
	/*
		0 == ASM_CASE_NONE
		1 == ASM_CASE_1PLAY_2STOP
		5 == ASM_CASE_1STOP_2PLAY
		6 == ASM_CASE_1PAUSE_2PLAY
		8 == ASM_CASE_1PLAY_2PLAY_MIX
		9 == ASM_CASE_RESOURCE_CHECK
		7 == ASM_CASE_SUB_EVENT
	*/
	/*   SP SC SS SO SA   EP EC ES EO EA  NT CL SF EF EU  AL VC VO MT RC  EM ER VR CA CV */
		{ 8, 8, 8, 8, 8,  6, 6, 6, 6, 6,  6, 6, 8, 6, 6,  6, 6, 6, 8, 6,  6, 9, 7, 7, 7},     /* 00 Shared MMPlayer */
		{ 8, 8, 8, 8, 8,  5, 5, 5, 5, 5,  6, 5, 8, 5, 8,  5, 5, 5, 8, 5,  8, 9, 7, 7, 7},     /* 01 Shared MMCamcorder */
		{ 8, 8, 8, 8, 8,  6, 6, 6, 6, 6,  6, 6, 8, 6, 5,  6, 6, 6, 8, 5,  6, 9, 7, 7, 7},     /* 02 Shared MMSound */
		{ 8, 8, 8, 8, 8,  6, 6, 6, 6, 6,  6, 6, 8, 6, 6,  6, 6, 6, 8, 6,  6, 9, 7, 7, 7},     /* 03 Shared OpenAL */
		{ 8, 8, 8, 8, 8,  5, 5, 5, 5, 5,  6, 5, 8, 5, 8,  5, 5, 5, 8, 5,  5, 9, 7, 7, 7},     /* 04 Shared AVsystem */

		{ 6, 6, 6, 6, 6,  6, 6, 6, 6, 6,  6, 6, 6, 6, 6,  6, 6, 6, 8, 6,  6, 9, 6, 6, 6},     /* 05 Exclusive MMPlayer */
		{ 5, 5, 5, 5, 5,  5, 5, 5, 5, 5,  8, 5, 5, 5, 8,  5, 5, 5, 8, 5,  8, 9, 5, 5, 5},     /* 06 Exclusive MMCamcorder */
		{ 6, 6, 6, 6, 6,  6, 6, 6, 6, 6,  6, 6, 6, 6, 6,  6, 6, 6, 8, 6,  6, 9, 6, 6, 6},     /* 07 Exclusive MMSound */
		{ 6, 6, 6, 6, 6,  6, 6, 6, 6, 6,  6, 6, 6, 6, 6,  6, 5, 6, 8, 6,  6, 9, 6, 6, 6},     /* 08 Exclusive OpenAL */
		{ 5, 5, 5, 5, 5,  5, 5, 5, 5, 5,  6, 5, 5, 5, 8,  5, 5, 5, 8, 5,  5, 9, 5, 5, 5},     /* 09 Exclusive AVsystem */

		{ 8, 8, 8, 8, 8,  8, 8, 8, 8, 8,  8, 5, 8, 8, 8,  5, 5, 5, 8, 5,  5, 9, 8, 8, 8},     /* 10 Notify */
		{ 1, 8, 1, 1, 1,  1, 1, 1, 1, 1,  8, 0, 1, 1, 8,  8, 1, 1, 8, 0,  1, 9, 1, 7, 7},     /* 11 Call */
		{ 8, 8, 8, 8, 8,  5, 5, 5, 5, 5,  8, 5, 5, 5, 5,  5, 5, 5, 8, 5,  5, 9, 7, 7, 7},     /* 12 Shared FMradio */
		{ 5, 5, 5, 5, 5,  5, 5, 5, 5, 5,  8, 5, 5, 5, 5,  5, 5, 5, 8, 5,  5, 9, 5, 5, 5},     /* 13 Exclusive FMradio */
		{ 8, 8, 8, 8, 8,  8, 8, 8, 8, 8,  8, 8, 8, 8, 8,  8, 8, 8, 8, 8,  8, 9, 8, 8, 8},     /* 14 Earjack Unplug */

		{ 1, 1, 1, 1, 1,  1, 1, 1, 1, 1,  8, 5, 1, 1, 8,  8, 5, 5, 8, 5,  5, 9, 1, 1, 1},     /* 15 Alarm */
		{ 1, 1, 1, 1, 1,  1, 1, 1, 1, 1,  8, 1, 1, 1, 8,  8, 1, 0, 8, 1,  1, 9, 1, 7, 1},     /* 16 Video Call */
		{ 1, 1, 1, 1, 1,  1, 1, 1, 1, 1,  8, 1, 1, 1, 8,  8, 1, 0, 8, 1,  1, 9, 1, 7, 1},     /* 17 VOIP */
		{ 8, 8, 8, 8, 8,  8, 8, 8, 8, 8,  8, 8, 8, 8, 8,  8, 8, 8, 8, 8,  8, 9, 8, 8, 8},     /* 18 Monitor */
		{ 1, 8, 1, 1, 1,  1, 1, 1, 1, 1,  8, 0, 1, 1, 8,  8, 1, 1, 8, 0,  1, 9, 1, 7, 7},     /* 19 Rich Call */ // not used

		{ 5, 5, 5, 5, 5,  5, 5, 5, 5, 5,  5, 5, 5, 5, 8,  5, 5, 5, 8, 5,  5, 9, 5, 5, 5},     /* 20 Emergency */
		{ 9, 9, 9, 9, 9,  9, 9, 9, 9, 9,  9, 9, 9, 9, 9,  9, 9, 9, 9, 9,  9, 9, 9, 9, 9},     /* 21 Exclusive Resource */
		{ 7, 7, 7, 7, 7,  7, 5, 5, 5, 5,  8, 5, 7, 5, 8,  5, 5, 5, 8, 5,  5, 9, 7, 7, 7},     /* 22 Voice Recognition */
		{ 7, 7, 7, 7, 7,  5, 5, 5, 5, 5,  7, 6, 7, 5, 8,  5, 5, 5, 8, 5,  5, 9, 7, 7, 7},     /* 23 MMCamcorder Audio */
		{ 7, 7, 7, 7, 7,  5, 5, 5, 5, 5,  7, 5, 7, 5, 8,  5, 5, 5, 8, 5,  5, 9, 7, 7, 5},     /* 24 MMCamcorder Video */
};

static const ASM_sound_cases_t ASM_sound_case_sub[NUM_OF_ADV_ASM_EVENT][ASM_SUB_EVENT_MAX][ROW_NUM_OF_SUB_EVENT] =
{
	/*
		0 == ASM_CASE_NONE
		1 == ASM_CASE_1PLAY_2STOP
		5 == ASM_CASE_1STOP_2PLAY
		6 == ASM_CASE_1PAUSE_2PLAY
		7 == ASM_CASE_SUB_EVENT
		8 == ASM_CASE_1PLAY_2PLAY_MIX
	*/
	/*    SP SC SS SO SA  EP EC ES EO NT  CL SF ER VR CA  CV  <== Current playing event */
		{{ 8, 8, 8, 8, 8,  8, 5, 6, 8, 8,  8, 8, 9, 5, 7,  7 }, /* Voice Recognition - SH */
		 { 6, 5, 6, 6, 6,  6, 5, 6, 6, 8,  1, 5, 9, 5, 5,  5 }},/* Voice Recognition - EX */

		{{ 8, 8, 8, 8, 8,  6, 5, 6, 6, 8,  8, 8, 9, 5, 5,  5 }, /* MMCamcorder Audio - SH */
		 { 6, 5, 6, 6, 6,  6, 5, 6, 6, 8,  1, 5, 9, 5, 5,  5 }},/* MMCamcorder Audio - EX */

		{{ 8, 8, 8, 8, 8,  6, 5, 6, 6, 8,  8, 8, 9, 7, 5,  5 }, /* MMCamcorder Video - SH */
		 { 6, 5, 6, 6, 6,  6, 5, 6, 6, 8,  1, 5, 9, 5, 5,  5 }} /* MMCamcorder Video - EX */
};

typedef struct _paused_by_id
{
	int pid;
	int sound_handle;
	ASM_event_sources_t eventsrc;
} asm_paused_by_id_t;

#ifdef ASM_FOR_PRODUCT
typedef struct asm_subsession_option
{
	int pid;
	ASM_resource_t resource;
} asm_subsession_option_t;
#endif

typedef struct _list
{
	int 				instance_id;
	int 					sound_handle;
	ASM_sound_events_t 		sound_event;
	ASM_sound_sub_events_t	sound_sub_event;
	ASM_sound_states_t 		sound_state;
	ASM_sound_states_t 		prev_sound_state;
	ASM_resume_states_t		need_resume;
	ASM_resource_t			mm_resource;
	asm_paused_by_id_t		paused_by_id;
#ifdef ASM_FOR_PRODUCT
	asm_subsession_option_t option;
#endif
	unsigned short			monitor_active;
	unsigned short			monitor_dirty;
	bool					is_registered_for_watching;
	struct _list 			*next;
} asm_instance_list_t;

asm_instance_list_t *head_list, *tail_list;

typedef struct asm_compare_result
{
	int previous_num_of_changes;
	int incoming_num_of_changes;
	asm_instance_list_t* previous_asm_handle[SERVER_HANDLE_MAX_COUNT];
	asm_instance_list_t* incoming_asm_handle[SERVER_HANDLE_MAX_COUNT];
} asm_compare_result_t;

int asm_snd_msgid;
int asm_rcv_msgid;
int asm_cb_msgid;

bool asm_is_send_msg_to_cb = false;
ASM_sound_blocked_reason_t g_blocked_reason = ASM_SOUND_BLOCKED_BY_TABLE;

#ifdef SUPPORT_GCF
#define GCF_DEFAULT	0
int is_gcf = GCF_DEFAULT;
#endif

unsigned int g_sound_status_pause = 0;
unsigned int g_sound_status_playing = 0;
unsigned int g_sound_status_waiting = 0;
int g_session_ref_count = 0;

static const char* ASM_sound_event_str[] =
{
	"SHARE_MMPLAYER",
	"SHARE_MMCAMCORDER",
	"SHARE_MMSOUND",
	"SHARE_OPENAL",
	"SHARE_AVSYSTEM",
	"EXCLUSIVE_MMPLAYER",
	"EXCLUSIVE_MMCAMCORDER",
	"EXCLUSIVE_MMSOUND",
	"EXCLUSIVE_OPENAL",
	"EXCLUSIVE_AVSYSTEM",
	"NOTIFY",
	"CALL",
	"SHARE_FMRADIO",
	"EXCLUSIVE_FMRADIO",
	"EARJACK_UNPLUG",
	"ALARM",
	"VIDEOCALL",
	"VOIP",
	"MONITOR",
	"RICH_CALL",
	"EMERGENCY",
	"EXCLUSIVE_RESOURCE",
	"VOICE_RECOGNITION",
	"MMCAMCORDER_AUDIO",
	"MMCAMCORDER_VIDEO"
};

static const char* ASM_sound_sub_event_str[] =
{
	"SUB_EVENT_NONE",
	"SUB_EVENT_SHARE",
	"SUB_EVENT_EXCLUSIVE"
};

static const char* ASM_sound_state_str[] =
{
	"STATE_NONE",
	"STATE_PLAYING",
	"STATE_WAITING",
	"STATE_STOP",
	"STATE_PAUSE",
	"STATE_PAUSE_BY_APP",
	"STATE_ALTER_PLAYING"
};


static const char* ASM_sound_request_str[] =
{
	"REQUEST_REGISTER",
	"REQUEST_UNREGISTER",
	"REQUEST_GETSTATE",
	"REQUEST_GETMYSTATE",
	"REQUEST_SETSTATE",
	"REQUEST_EMEGENT_EXIT",
	"REQUEST_DUMP",
	"REQUEST_SET_SUBSESSION",
	"REQUEST_GET_SUBSESSION",
	"REQUEST_REGISTER_WATCHER",
	"REQUEST_UNREGISTER_WATCHER",
	"REQUEST_SET_SUBEVENT",
	"REQUEST_GET_SUBEVENT",
	"REQUEST_RESET_RESUME_TAG"
};


static const char* ASM_sound_case_str[] =
{
	"CASE_NONE",
	"CASE_1PLAY_2STOP",
	"CASE_1PLAY_2ALTER_PLAY",
	"CASE_1PLAY_2WAIT",
	"CASE_1ALTER_PLAY_2PLAY",
	"CASE_1STOP_2PLAY",
	"CASE_1PAUSE_2PLAY",
	"CASE_SUB_EVENT",
	"CASE_1PLAY_2PLAY_MIX",
	"CASE_RESOURCE_CHECK",
	"CASE_1VIRTUAL_2PLAY"
};

static const char* ASM_sound_resume_str[] =
{
	"NO-RESUME",
	"RESUME"
};


static const char* ASM_sound_command_str[] =
{
	"CMD_NONE",
	"CMD_WAIT",
	"CMD_PLAY",
	"CMD_STOP",
	"CMD_PAUSE",
	"CMD_RESUME"
};

static char *subsession_str[] =
{
	"VOICE",
	"RINGTONE",
	"MEDIA",
	"VOICE_ANSWER_PLAY",
	"VOICE_ANSWER_REC",
	"INIT",
	"VR_NORMAL",
	"VR_DRIVE",
	"RECORD_STEREO",
	"RECORD_STEREO_INTERVIEW",
	"RECORD_STEREO_CONVERSATION",
	"RECORD_MONO",
};

static char *subevent_str[] =
{
	"SUB_EVENT_NONE",
	"SUB_EVENT_SHARE",
	"SUB_EVENT_EXCLUSIVE"
};

#define ASM_SND_MSG_SET_DEFAULT(asm_snd_msg, x_instance_id, x_alloc_handle, x_cmd_handle, x_source_request_id) \
do { \
	asm_snd_msg.instance_id               = x_instance_id; \
	asm_snd_msg.data.alloc_handle         = x_alloc_handle; \
	asm_snd_msg.data.cmd_handle           = x_cmd_handle; \
	asm_snd_msg.data.source_request_id    = x_source_request_id; \
} while (0)

#define ASM_SND_MSG_SET_RESULT(asm_snd_msg, x_result_sound_command, x_result_sound_state, x_previous_sound_event) \
do { \
	asm_snd_msg->data.result_sound_command = x_result_sound_command; \
	asm_snd_msg->data.result_sound_state   = x_result_sound_state; \
	asm_snd_msg->data.previous_sound_event = x_previous_sound_event; \
	asm_snd_msg->data.reason_of_blocking   = g_blocked_reason; \
	g_blocked_reason = ASM_SOUND_BLOCKED_BY_TABLE; \
} while (0)

/* reference count of session */
#define SESSION_REF(x_cur_session) \
do { \
	g_session_ref_count++; \
	debug_log("session ref.count is increased:%d (by cur_session(%d))", g_session_ref_count, x_cur_session); \
} while (0)

#define SESSION_UNREF(x_cur_session) \
do { \
	g_session_ref_count--; \
	debug_log("session ref.count is decreased:%d (by cur_session(%d))", g_session_ref_count, x_cur_session); \
} while (0)

#define SESSION_REF_INIT() \
do { \
	g_session_ref_count = 0; \
	debug_log("session ref.count is initialized:%d", g_session_ref_count); \
} while (0)

#define IS_ON_GOING_SESSION() (g_session_ref_count > 0)

/* execute watch callback */
#define ASM_DO_WATCH_CALLBACK(x_interest_event, x_interest_state) \
do { \
	if (__do_watch_callback(x_interest_event, x_interest_state)) { \
		debug_error(" oops! failed to _do_watch_callback(%d, %d)\n", x_interest_event, x_interest_state); \
	} \
} while (0)

/* execute monitor callback */
#define ASM_DO_MONITOR_CALLBACK(x_current_instance_id, x_monitor_handle, x_asm_command, x_event_src) \
do { \
	int cb_res = 0; \
	if (__find_clean_monitor_handle(x_current_instance_id, &x_monitor_handle)) { \
		cb_res = __do_callback(x_current_instance_id, x_monitor_handle, x_asm_command, x_event_src); \
		debug_warning(" send stop callback for monitor handle of pid %d\n", x_current_instance_id); \
		if (cb_res != ASM_CB_RES_NONE && cb_res != ASM_CB_RES_STOP) { \
			debug_error(" oops! not suspected callback result %d\n", cb_res); \
		} \
		__set_monitor_dirty(x_current_instance_id); \
	} \
} while (0)

/* execute watch callback from result structure */
#define ASM_DO_WATCH_CALLBACK_FROM_RESULT(x_compare_result) \
do { \
	while(x_compare_result.incoming_num_of_changes--) { \
		ASM_DO_WATCH_CALLBACK(x_compare_result.incoming_asm_handle[x_compare_result.incoming_num_of_changes]->sound_event, x_compare_result.incoming_asm_handle[x_compare_result.incoming_num_of_changes]->sound_state); \
	} \
	while(x_compare_result.previous_num_of_changes--) { \
		ASM_DO_WATCH_CALLBACK(x_compare_result.previous_asm_handle[x_compare_result.previous_num_of_changes]->sound_event, x_compare_result.previous_asm_handle[x_compare_result.previous_num_of_changes]->sound_state); \
	} \
} while (0)

/* check whether if it needs to be resumed later */
#define ASM_CHECK_RESUME(x_sound_event, x_resource, x_current_sound_event, x_current_instance_id, x_current_state, x_current_handle, x_paused_by_pid, x_paused_by_sound_handle, x_paused_by_eventsrc) \
do { \
	if ( __is_need_resume(x_sound_event, x_resource, x_current_sound_event) && (x_current_state == ASM_STATE_PLAYING || x_current_state == ASM_STATE_WAITING) ) { \
		__asm_change_need_resume_list(x_current_instance_id, x_current_handle, ASM_NEED_RESUME, x_paused_by_pid, x_paused_by_sound_handle, x_paused_by_eventsrc); \
	} \
} while (0)

/* defines for special cases */
subsession_t g_previous_subsession = SUBSESSION_VOICE;

#define MAX_WATCH_CALLBACK_CALCUL_NUM 128

#ifdef USE_IO_PRIORITY /* IO Priority */
enum {
	IOPRIO_WHO_PROCESS = 1,
	IOPRIO_WHO_PRGP,
	IOPRIO_WHO_USER
};

enum {
	IOPRIO_CLASS_NONE = 0,
	IOPRIO_CLASS_RT,
	IOPRIO_CLASS_BE,
	IOPRIO_CLASS_IDLE
};
#define IOPRIO_CLASS_SHIFT      13

static int _ioprio_set(int which, int who, int ioprio)
{
	return syscall(SYS_ioprio_set, which, who, ioprio);
}

static int _ioprio_get(int which, int who)
{
	return syscall(SYS_ioprio_get, which, who);
}

static int _proc_set_io_pri(pid_t pid)
{
	int ioprio, ret;

	ioprio = _ioprio_get(IOPRIO_WHO_PROCESS, pid);
	ret = _ioprio_set(IOPRIO_WHO_PROCESS, pid, ioprio | IOPRIO_CLASS_RT << IOPRIO_CLASS_SHIFT);
	debug_msg("set_io_pri : pid = %d, ret = %d", pid, ret);

	return ret;
}
#endif /* USE_IO_PRIORITY */

void ___select_sleep(int secs)
{
	struct timeval timeout;
	timeout.tv_sec = (secs < 1 || secs > 10) ? 3 : secs;
	timeout.tv_usec = 0;
	select(0, NULL, NULL, NULL, &timeout);
	return;
}

int __get_adv_event_idx_for_subtable(ASM_sound_events_t sound_event, int *index)
{
	int ret = MM_ERROR_NONE;
	if (!index) {
		debug_error("index is null");
		return MM_ERROR_INVALID_ARGUMENT;
	}

	switch (sound_event) {
	case ASM_EVENT_VOICE_RECOGNITION:
		*index = 0;
		break;
	case ASM_EVENT_MMCAMCORDER_AUDIO:
		*index = 1;
		break;
	case ASM_EVENT_MMCAMCORDER_VIDEO:
		*index = 2;
		break;
	default:
		debug_error("Not supported advanced sound_type(%d) for subtable", sound_event);
		ret = MM_ERROR_SOUND_INTERNAL;
		break;
	}

	return ret;
}

int __get_sub_case_table_idx(ASM_sound_events_t sound_event, int *index)
{
	int ret = MM_ERROR_NONE;
	if (!index) {
		debug_error("index is null");
		return MM_ERROR_INVALID_ARGUMENT;
	}

	switch (sound_event) {
	case ASM_EVENT_SHARE_MMPLAYER:
	case ASM_EVENT_SHARE_MMCAMCORDER:
	case ASM_EVENT_SHARE_MMSOUND:
	case ASM_EVENT_SHARE_OPENAL:
	case ASM_EVENT_SHARE_AVSYSTEM:
	case ASM_EVENT_EXCLUSIVE_MMPLAYER:
	case ASM_EVENT_EXCLUSIVE_MMCAMCORDER:
	case ASM_EVENT_EXCLUSIVE_MMSOUND:
	case ASM_EVENT_EXCLUSIVE_OPENAL:
		*index = sound_event;
		break;
	case ASM_EVENT_NOTIFY:
		*index = ASM_EVENT_EXCLUSIVE_OPENAL + 1;
		break;
	case ASM_EVENT_CALL:
	case ASM_EVENT_VIDEOCALL:
	case ASM_EVENT_VOIP:
	case ASM_EVENT_RICH_CALL:
		*index = ASM_EVENT_EXCLUSIVE_OPENAL + 2;
		break;
	case ASM_EVENT_SHARE_FMRADIO:
		*index = ASM_EVENT_EXCLUSIVE_OPENAL + 3;
		break;
	case ASM_EVENT_EXCLUSIVE_RESOURCE:
		*index = ASM_EVENT_EXCLUSIVE_OPENAL + 4;
		break;
	case ASM_EVENT_VOICE_RECOGNITION:
		*index = ASM_EVENT_EXCLUSIVE_OPENAL + 5;
		break;
	case ASM_EVENT_MMCAMCORDER_AUDIO:
		*index = ASM_EVENT_EXCLUSIVE_OPENAL + 6;
		break;
	case ASM_EVENT_MMCAMCORDER_VIDEO:
		*index = ASM_EVENT_EXCLUSIVE_OPENAL + 7;
		break;
	default:
		debug_error("Not supported sound_type(%d) for subtable index", sound_event);
		ret = MM_ERROR_SOUND_INTERNAL;
		break;
	}

	return ret;
}

ASM_event_sources_t __mapping_sound_event_to_event_src(ASM_sound_events_t sound_event, ASM_resource_t resource, ASM_sound_cases_t sound_case)
{
	ASM_event_sources_t event_src = ASM_EVENT_SOURCE_MEDIA;

	switch (sound_event) {
	case ASM_EVENT_CALL:
	case ASM_EVENT_RICH_CALL:
	case ASM_EVENT_VIDEOCALL:
	case ASM_EVENT_VOIP:
		event_src = ASM_EVENT_SOURCE_CALL_START;
		break;

	case ASM_EVENT_EARJACK_UNPLUG:
		event_src = ASM_EVENT_SOURCE_EARJACK_UNPLUG;
		break;

	case ASM_EVENT_ALARM:
		event_src = ASM_EVENT_SOURCE_ALARM_START;
		break;

	case ASM_EVENT_EMERGENCY:
		event_src = ASM_EVENT_SOURCE_EMERGENCY_START;
		break;

	case ASM_EVENT_NOTIFY:
		event_src = ASM_EVENT_SOURCE_NOTIFY_START;
		break;

	case ASM_EVENT_MMCAMCORDER_AUDIO:
	case ASM_EVENT_MMCAMCORDER_VIDEO:
	case ASM_EVENT_EXCLUSIVE_MMCAMCORDER:
		event_src = ASM_EVENT_SOURCE_RESUMABLE_MEDIA;
		break;

	case ASM_EVENT_SHARE_MMPLAYER:
	case ASM_EVENT_EXCLUSIVE_MMPLAYER:
		if (sound_case == ASM_CASE_1PAUSE_2PLAY) {
			if ( sound_event == ASM_EVENT_SHARE_MMPLAYER || sound_event == ASM_EVENT_EXCLUSIVE_MMPLAYER ) {
				if (resource & ASM_RESOURCE_EAR_SPK_PATH || /* if it is pre-listening mode */
					resource & ASM_RESOURCE_RESUMPTION_TAG) {
					event_src = ASM_EVENT_SOURCE_RESUMABLE_MEDIA;
				} else {
					event_src = ASM_EVENT_SOURCE_OTHER_PLAYER_APP;
				}
				break;
			}
		}

	default:
		event_src = ASM_EVENT_SOURCE_MEDIA;
		break;
	}

	return event_src;
}

ASM_event_sources_t __convert_eventsrc_interrupted_to_completed(ASM_event_sources_t eventsrc)
{
	ASM_event_sources_t completed_eventsrc = eventsrc;

	switch(eventsrc) {
	case ASM_EVENT_SOURCE_ALARM_START:
		completed_eventsrc = ASM_EVENT_SOURCE_ALARM_END;
		break;
	case ASM_EVENT_SOURCE_EMERGENCY_START:
		completed_eventsrc = ASM_EVENT_SOURCE_EMERGENCY_START;
		break;
	case ASM_EVENT_SOURCE_NOTIFY_START:
		completed_eventsrc = ASM_EVENT_SOURCE_NOTIFY_END;
		break;
	case ASM_EVENT_SOURCE_VR_START:
		completed_eventsrc = ASM_EVENT_SOURCE_VR_END;
		break;
	default:
		break;
	}
	debug_warning(" completed EventSrc[%d]", completed_eventsrc);
	return completed_eventsrc;
}

gboolean __is_need_resume (ASM_sound_events_t incoming_sound_event, ASM_resource_t incoming_resource, ASM_sound_events_t current_sound_event)
{
	gboolean result = FALSE;
	switch (incoming_sound_event) {
	case ASM_EVENT_NOTIFY:
	case ASM_EVENT_ALARM:
	case ASM_EVENT_EMERGENCY:
	case ASM_EVENT_EXCLUSIVE_MMCAMCORDER:
	case ASM_EVENT_VOICE_RECOGNITION:
	case ASM_EVENT_MMCAMCORDER_AUDIO:
	case ASM_EVENT_MMCAMCORDER_VIDEO:
		result = TRUE;
		break;
	case ASM_EVENT_EXCLUSIVE_MMPLAYER:
		if (incoming_resource & ASM_RESOURCE_EAR_SPK_PATH || /* for pre-listening mode */
			incoming_resource & ASM_RESOURCE_RESUMPTION_TAG) {
			result = TRUE;
		}
		break;
	default:
		result = FALSE;
		break;
	}

	/* exception case : if current playing sound event is one of below list, check false*/
	switch (current_sound_event) {
	case ASM_EVENT_VOICE_RECOGNITION:
	case ASM_EVENT_MMCAMCORDER_VIDEO:
		result = FALSE;
		break;
	default:
		break;
	}

	debug_warning(" resume check(%d)", result);
	return result;
}

gboolean __is_media_session_type (ASM_sound_events_t sound_event)
{
	gboolean result = FALSE;
	switch (sound_event) {
	case ASM_EVENT_SHARE_MMPLAYER:
	case ASM_EVENT_SHARE_MMCAMCORDER:
	case ASM_EVENT_SHARE_MMSOUND:
	case ASM_EVENT_SHARE_OPENAL:
	case ASM_EVENT_SHARE_AVSYSTEM:
	case ASM_EVENT_EXCLUSIVE_MMPLAYER:
	case ASM_EVENT_EXCLUSIVE_MMCAMCORDER:
	case ASM_EVENT_EXCLUSIVE_MMSOUND:
	case ASM_EVENT_EXCLUSIVE_OPENAL:
	case ASM_EVENT_EXCLUSIVE_AVSYSTEM:
	case ASM_EVENT_SHARE_FMRADIO:
	case ASM_EVENT_EXCLUSIVE_FMRADIO:
	case ASM_EVENT_MMCAMCORDER_AUDIO:
	case ASM_EVENT_MMCAMCORDER_VIDEO:
	case ASM_EVENT_VOICE_RECOGNITION:
		debug_log("it's a media session type (%d)", sound_event);
		result = TRUE;
		break;
	default:
		break;
	}
	return result;
}

gboolean __find_clean_monitor_handle(int instance_id, int *handle)
{
	asm_instance_list_t *temp_list = head_list;
	int lhandle = ASM_HANDLE_INIT_VAL;

	while (temp_list->next != tail_list) {
		if (temp_list->instance_id == instance_id && temp_list->sound_event == ASM_EVENT_MONITOR) {
			if (temp_list->monitor_dirty == 0) {
				lhandle = temp_list->sound_handle;
			}
			break;
		}
		temp_list = temp_list->next;
	}
	if (lhandle == ASM_HANDLE_INIT_VAL) {
		debug_warning(" could not find a clean monitor handle");
		return FALSE;
	} else {
		*handle = lhandle;
		debug_log(" found a clean monitor handle(%d)", *handle);
		return TRUE;
	}
}

void __update_monitor_active(int instance_id)
{
	asm_instance_list_t *temp_list = head_list;
	asm_instance_list_t *monitor_list = NULL;
	unsigned short active = 0;
	debug_log(" \n");

	while (temp_list->next != tail_list) {
		if (temp_list->instance_id == instance_id && temp_list->sound_event == ASM_EVENT_MONITOR) {
			/* backup monitor pointer */
			monitor_list = temp_list;
			break;
		}
		temp_list = temp_list->next;
	}
	if (NULL == monitor_list) {
		debug_warning(" No monitor instance for %d\n", instance_id);
		return;
	}

	temp_list = head_list;
	while (temp_list->next != tail_list) {
		if (temp_list->instance_id == instance_id && temp_list->sound_event != ASM_EVENT_MONITOR) {
			if (ASM_STATE_PLAYING == temp_list->sound_state) {
				active = 1;
				break;
			}
		}
		temp_list = temp_list->next;
	}

	monitor_list->monitor_active = active;
}

void __set_all_monitor_clean()
{
	asm_instance_list_t *temp_list = head_list;

	while (temp_list->next != tail_list) {
		if (temp_list->sound_event == ASM_EVENT_MONITOR) {
			temp_list->monitor_dirty = 0;
		}
		temp_list = temp_list->next;
	}
}

void __set_monitor_dirty(int instance_id)
{
	asm_instance_list_t *temp_list = head_list;

	while (temp_list->next != tail_list) {
		if (temp_list->instance_id == instance_id && temp_list->sound_event == ASM_EVENT_MONITOR) {
			temp_list->monitor_dirty = 1;
			break;
		}
		temp_list = temp_list->next;
	}
}

/* callback without retcb */
void __do_callback_wo_retcb(int instance_id,int handle,int command)
{
	int fd_ASM = -1, cur_handle = 0;
	char *filename = g_strdup_printf("/tmp/ASM.%d.%d", instance_id, handle);

	if ((fd_ASM = open(filename,O_WRONLY|O_NONBLOCK)) < 0) {
		debug_log("[CallCB] %s open error",filename);
		g_free(filename);
		return;
	}
	cur_handle = (unsigned int)(handle |(command << 4));
	if (write(fd_ASM, (void *)&cur_handle, sizeof(cur_handle)) < 0) {
		debug_log("[CallCB] %s write error",filename);
		g_free(filename);
		if (fd_ASM != -1) {
			close(fd_ASM);
		}
		return;
	}
	close(fd_ASM);
	g_free(filename);
	___select_sleep(2); /* if return immediately bad sound occur */
}

int __do_callback(int instance_id,int handle,int command, ASM_event_sources_t event_src)
{
	char *filename = NULL;
	char *filename2 = NULL;
	struct timeval time;
	int starttime = 0;
	int endtime = 0;
	int fd=-1,nread = 0;
	int fd_ASM = -1, cur_handle = 0;
	int buf = 0;
	struct pollfd pfd;
	int pret = 0;
	int pollingTimeout = 2500; /* NOTE : This is temporary code, because of Deadlock issues. If you fix that issue, remove this comment */

	debug_log(" __do_callback for pid(%d) handle(%d)\n", instance_id, handle);

	/* Set start time */
	gettimeofday(&time, NULL);
	starttime = time.tv_sec * 1000000 + time.tv_usec;

	/**************************************
	 *
	 * Open callback cmd pipe
	 *
	 **************************************/
	filename = g_strdup_printf("/tmp/ASM.%d.%d", instance_id, handle);
	if ((fd_ASM = open(filename, O_WRONLY|O_NONBLOCK)) < 0) {
		debug_error("[CallCB] %s open error\n", filename);
		goto fail;
	}

	/******************************************
	 *
	 * Open callback result pipe
	 * before writing callback cmd to pipe
	 *
	 ******************************************/
	filename2 = g_strdup_printf("/tmp/ASM.%d.%dr", instance_id, handle);
	if ((fd=open(filename2,O_RDONLY|O_NONBLOCK))== -1) {
		char str_error[256];
		strerror_r (errno, str_error, sizeof(str_error));
		debug_error("[RETCB] Fail to open fifo (%s)\n", str_error);
		goto fail;
	}
	debug_log(" open return cb %s\n", filename2);


	/*******************************************
	 * Write Callback msg
	 *******************************************/
	cur_handle = (unsigned int)((0x0000ffff & handle) |(command << 16) | (event_src << 24));
	if (write(fd_ASM, (void *)&cur_handle, sizeof(cur_handle)) < 0) {
		debug_error("[CallCB] %s write error\n", filename);
		goto fail;
	}
	/**************************************
	 *
	 * Close callback cmd pipe
	 *
	 **************************************/
	if (fd_ASM != -1) {
		close(fd_ASM);
		fd_ASM = -1;
	}
	g_free(filename);
	filename = NULL;

	/* If command is RESUME, then do not wait for client return */
	if (command == ASM_COMMAND_RESUME) {
		debug_log("[RETCB] No need to wait return from client. \n");
		if (fd != -1) {
			close (fd);
			fd = -1;
		}
		return ASM_CB_RES_NONE;
	}

	pfd.fd = fd;
	pfd.events = POLLIN;

	/*********************************************
	 *
	 * Wait callback result msg
	 *
	 ********************************************/
	debug_log("[RETCB]wait callback(tid=%d, handle=%d, cmd=%d, timeout=%d)\n", instance_id, handle, command, pollingTimeout);
	pret = poll(&pfd, 1, pollingTimeout); //timeout 7sec
	if (pret < 0) {
		debug_error("[RETCB]poll failed (%d)\n", pret);
		goto fail;
	}
	if (pfd.revents & POLLIN) {
		nread=read(fd, (void *)&buf, sizeof(buf));
	}
	g_free(filename2);
	filename2 = NULL;

	/* Calculate endtime and display*/
	gettimeofday(&time, NULL);
	endtime = time.tv_sec * 1000000 + time.tv_usec;
	debug_log("[RETCB] ASM_CB_END cbtimelab=%3.3f(second), timeout=%d(milli second) (reciever=%d)\n", ((endtime-starttime)/1000000.), pollingTimeout, instance_id);

	/**************************************
	 *
	 * Close callback result pipe
	 *
	 **************************************/
	if (fd != -1) {
		close(fd);
		fd = -1;
	}
	debug_log("[RETCB] Return value 0x%x\n", buf);
	return buf;

fail:
	if (filename) {
		g_free (filename);
		filename = NULL;
	}
	if (filename2) {
		g_free (filename2);
		filename2 = NULL;
	}
	if (fd_ASM != -1) {
		close(fd_ASM);
		fd_ASM = -1;
	}
	if (fd != -1) {
		close (fd);
		fd = -1;
	}

	return -1;
}

gboolean __is_it_redundant_request(int instance_id, ASM_sound_events_t sound_event, ASM_sound_states_t sound_state)
{
	asm_instance_list_t *temp_list = head_list;
	while (temp_list->next != tail_list) {
		if (temp_list->is_registered_for_watching) {
			if (temp_list->instance_id == instance_id && temp_list->sound_event == sound_event && temp_list->sound_state == sound_state) {
					return TRUE;
			}
		}
		temp_list = temp_list->next;
	}
	return FALSE;
}

int __get_watcher_list (ASM_sound_events_t interest_sound_event, ASM_sound_states_t interest_sound_state, int *instance_id_list, int *handle_list)
{
	asm_instance_list_t *temp_list = head_list;
	int num = 0;

	while (temp_list->next != tail_list) {
		if (temp_list->is_registered_for_watching) {
			if (temp_list->sound_event == interest_sound_event && temp_list->sound_state == interest_sound_state) {
				instance_id_list[num] = temp_list->instance_id;
				handle_list[num] = temp_list->sound_handle;
				num++;
				debug_warning(" found a watcher[pid:%d] for :: INTEREST event(%s) state(%s)", temp_list->instance_id, ASM_sound_event_str[interest_sound_event], ASM_sound_state_str[interest_sound_state]);
				if (__mm_sound_mgr_ipc_freeze_send(FREEZE_COMMAND_WAKEUP, temp_list->instance_id)) {
					debug_warning("failed to send wake-up msg to the client pid(%d)",temp_list->instance_id);
				}
			}
		}
		if (num >= MAX_WATCH_CALLBACK_CALCUL_NUM) {
			debug_warning(" out of bound for checking registered watch handles, num(%d)");
			break;
		}
		temp_list = temp_list->next;
	}

	return num;
}

int __do_watch_callback(ASM_sound_events_t sound_event, ASM_sound_states_t updated_sound_state)
{
	char *filename = NULL;
	char *filename2 = NULL;
	struct timeval time;
	int starttime = 0;
	int endtime = 0;
	int fd = -1;
	int nread = 0;
	int fd_ASM = -1;
	int cur_handle = 0;
	int buf = 0;
	struct pollfd pfd;
	int pret = 0;
	int pollingTimeout = 1500; /* NOTE : This is temporary code, because of Deadlock issues. If you fix that issue, remove this comment */
	int instance_id_list[MAX_WATCH_CALLBACK_CALCUL_NUM] = {0};
	int handle_list[MAX_WATCH_CALLBACK_CALCUL_NUM] = {0};
	int num = 0;

	debug_fenter();

	/* find candidates' instance_id and handle for providing session-changed notification, get number of candidates */
	num = __get_watcher_list (sound_event, updated_sound_state, instance_id_list, handle_list);

	while (num--) {

		debug_log(" pid(%d), handle(%d), sound_event(%d), sound_state(%d)\n", instance_id_list[num], handle_list[num], sound_event, updated_sound_state);

		/* Set start time */
		gettimeofday(&time, NULL);
		starttime = time.tv_sec * 1000000 + time.tv_usec;

		/**************************************
		 *
		 * Open callback cmd pipe
		 *
		 **************************************/
		filename = g_strdup_printf("/tmp/ASM.%d.%d", instance_id_list[num], handle_list[num]);
		if ((fd_ASM = open(filename, O_WRONLY|O_NONBLOCK)) < 0) {
			debug_error("[CallCB] %s open error\n", filename);
			goto fail;
		}

		/******************************************
		 *
		 * Open callback result pipe
		 * before writing callback cmd to pipe
		 *
		 ******************************************/
		filename2 = g_strdup_printf("/tmp/ASM.%d.%dr", instance_id_list[num], handle_list[num]);
		if ((fd=open(filename2,O_RDONLY|O_NONBLOCK))== -1) {
			char str_error[256];
			strerror_r (errno, str_error, sizeof(str_error));
			debug_error("[RETCB] Fail to open fifo (%s)\n", str_error);
			goto fail;
		}
		debug_log(" open return cb %s\n", filename2);


		/*******************************************
		 * Write Callback msg
		 *******************************************/
		//cur_handle = (unsigned int)((0x0000ffff & handle_list[num]));
		cur_handle = (unsigned int)((0x0000ffff & handle_list[num]) | (sound_event << 16) | (updated_sound_state << 24));
		if (write(fd_ASM, (void *)&cur_handle, sizeof(cur_handle)) < 0) {
			debug_error("[CallCB] %s write error\n", filename);
			goto fail;
		}
		/**************************************
		 *
		 * Close callback cmd pipe
		 *
		 **************************************/
		if (fd_ASM != -1) {
			close(fd_ASM);
			fd_ASM = -1;
		}
		g_free(filename);
		filename = NULL;

		pfd.fd = fd;
		pfd.events = POLLIN;

		/*********************************************
		 *
		 * Wait callback result msg
		 *
		 ********************************************/
		debug_log("[RETCB]wait callback(tid=%d, handle=%d, timeout=%d)\n",
					instance_id_list[num], handle_list[num], pollingTimeout);
		pret = poll(&pfd, 1, pollingTimeout); //timeout 7sec
		if (pret < 0) {
			debug_error("[RETCB]poll failed (%d)\n", pret);
			goto fail;
		}
		if (pfd.revents & POLLIN) {
			nread=read(fd, (void *)&buf, sizeof(buf));
		}
		g_free(filename2);
		filename2 = NULL;

		/* Calculate endtime and display*/
		gettimeofday(&time, NULL);
		endtime = time.tv_sec * 1000000 + time.tv_usec;
		debug_log("[RETCB] ASM_CB_END cbtimelab=%3.3f(second), timeout=%d(milli second) (reciever=%d)\n",
					((endtime-starttime)/1000000.), pollingTimeout, instance_id_list[num]);

		/**************************************
		 *
		 * Close callback result pipe
		 *
		 **************************************/
		if (fd != -1) {
			close(fd);
			fd = -1;
		}
		debug_log("[RETCB] Return value 0x%x\n", buf);
		//return buf;

	}

	debug_fleave();

	return 0;

fail:
	if (filename) {
		g_free (filename);
		filename = NULL;
	}
	if (filename2) {
		g_free (filename2);
		filename2 = NULL;
	}
	if (fd_ASM != -1) {
		close(fd_ASM);
		fd_ASM = -1;
	}
	if (fd != -1) {
		close (fd);
		fd = -1;
	}

	return -1;
}

gboolean __is_it_playing_now(int instance_id, int handle)
{
	asm_instance_list_t *temp_list = head_list;
	while (temp_list->next != tail_list) {
		if (temp_list->instance_id == instance_id && temp_list->sound_handle == handle) {
			if (temp_list->sound_state == ASM_STATE_PLAYING) {
				return TRUE;
			}
		}

		temp_list = temp_list->next;
	}
	return FALSE;
}

void __temp_print_list(char * msg)
{
	asm_instance_list_t *temp_list = head_list;
	int i = 0;

	if (NULL != msg) {
		debug_warning(" => MSG: %s\n", msg);
	}
	while (temp_list->next != tail_list) {
		if (!temp_list->is_registered_for_watching) {
			if (!ASM_IS_ADVANCED_ASM_EVENT(temp_list->sound_event)) {
				debug_msg(" List[%02d] ( %5d, %2d, %-22s, %-15s, %9s checked by%5d/%d, 0x%04x)\n" , i,
												temp_list->instance_id, temp_list->sound_handle,
												ASM_sound_event_str[temp_list->sound_event],
												ASM_sound_state_str[temp_list->sound_state],
												ASM_sound_resume_str[temp_list->need_resume],
												temp_list->paused_by_id.pid,
												temp_list->paused_by_id.sound_handle,
												temp_list->mm_resource);
			} else {
				debug_msg(" List[%02d] ( %5d, %2d, %-22s, %-15s, %-15s, 0x%04x)\n", i,
												temp_list->instance_id,
												temp_list->sound_handle,
												ASM_sound_event_str[temp_list->sound_event],
												ASM_sound_state_str[temp_list->sound_state],
												ASM_sound_sub_event_str[temp_list->sound_sub_event],
												temp_list->mm_resource);
			}
			i++;
		}
		temp_list = temp_list->next;
	}
	temp_list = head_list;
	debug_log(" listed below are requests for watching session\n");
	while (temp_list->next != tail_list) {
		if (temp_list->is_registered_for_watching) {
			debug_msg(" List[%02d] ( %5d, %2d, %-22s, %-15s, %5s)\n", i, temp_list->instance_id, temp_list->sound_handle,
												ASM_sound_event_str[temp_list->sound_event],
												ASM_sound_state_str[temp_list->sound_state],
												"WATCHING");
			i++;
		}
		temp_list = temp_list->next;
	}
}

void ___update_phone_status()
{
	asm_instance_list_t *temp_list = head_list;
	int error = 0;

	g_sound_status_pause = 0;
	g_sound_status_playing = 0;
	g_sound_status_waiting = 0;

	while (temp_list->next != tail_list) {
		if (!temp_list->is_registered_for_watching) {
			if (temp_list->sound_state == ASM_STATE_PLAYING) {
				if (temp_list->sound_event >= ASM_EVENT_SHARE_MMPLAYER && temp_list->sound_event  < ASM_EVENT_MAX) {
					g_sound_status_playing |= ASM_sound_type[(temp_list->sound_event) + 1].sound_status;
				}
			} else if (temp_list->sound_state == ASM_STATE_PAUSE ) {
				if (temp_list->sound_event >= ASM_EVENT_SHARE_MMPLAYER && temp_list->sound_event < ASM_EVENT_MAX) {
					g_sound_status_pause |= ASM_sound_type[(temp_list->sound_event) + 1].sound_status;
				}
			} else if (temp_list->sound_state == ASM_STATE_WAITING ) {
				if (temp_list->sound_event >= ASM_EVENT_SHARE_MMPLAYER && temp_list->sound_event < ASM_EVENT_MAX) {
					g_sound_status_waiting |= ASM_sound_type[(temp_list->sound_event) + 1].sound_status;
				}
			}
		}
		temp_list = temp_list->next;
	}

	if (vconf_set_int(SOUND_STATUS_KEY, g_sound_status_playing)) {
		debug_error("[ASM_Server[Error = %d][1st try] phonestatus_set \n", error);
		if (vconf_set_int(SOUND_STATUS_KEY, g_sound_status_playing)) {
			debug_error("[Error = %d][2nd try]  phonestatus_set \n", error);
		}
	}

	//debug_log(" soundstatus : playing(0x%08x), pause(0x%08x), waiting(0x%08x)\n", g_sound_status_playing, g_sound_status_pause, g_sound_status_waiting);
}


asm_instance_list_t* __asm_register_list(int instance_id, int handle, ASM_sound_events_t sound_event, ASM_sound_states_t sound_state, ASM_resource_t mm_resource, bool is_requested_for_watching)
{
	asm_instance_list_t *temp_list;
	temp_list = (asm_instance_list_t *)malloc(sizeof(asm_instance_list_t));
	if (!temp_list) {
		debug_error("could not allocate memory for asm_instance_list_t\n");
		return NULL;
	}
	temp_list->instance_id = instance_id;
	temp_list->sound_handle = handle;
	temp_list->sound_event = sound_event;
	temp_list->sound_sub_event = ASM_SUB_EVENT_NONE;
	temp_list->sound_state = sound_state;
	temp_list->need_resume = 0;
	temp_list->mm_resource = mm_resource;
	temp_list->paused_by_id.pid = 0;
	temp_list->paused_by_id.sound_handle = ASM_HANDLE_INIT_VAL;
	temp_list->paused_by_id.eventsrc = ASM_EVENT_SOURCE_MEDIA;
	temp_list->monitor_active = 0;
	temp_list->monitor_dirty = 0;
	temp_list->is_registered_for_watching = is_requested_for_watching;
	temp_list->next = head_list;
	head_list = temp_list;

	__temp_print_list("Register List");
	___update_phone_status();

	return temp_list;
}

int __asm_unregister_list(int handle)
{
	asm_instance_list_t *temp_list = head_list;
	asm_instance_list_t *temp_list2 = head_list;
	int instance_id = ASM_HANDLE_INIT_VAL;

	debug_log(" __asm_unregister_list \n");

	while (temp_list->next != tail_list) {
		if (temp_list->sound_handle == handle) {
			instance_id = temp_list->instance_id;
			if (temp_list == head_list) {
				head_list = temp_list->next;
			} else {
				temp_list2->next = temp_list->next;
			}
			free(temp_list);
			break;
		}
		temp_list2 = temp_list;
		temp_list = temp_list->next;
	}

	__temp_print_list("Unregister List for handle");
	___update_phone_status();
	return instance_id;
}

/* -------------------------
 * if PID exist return true, else return false
 */
gboolean ___is_pid_exist(int pid)
{
	if (pid > 999999 || pid < 2)
		return FALSE;
	gchar *tmp = g_malloc0(25);
	g_sprintf(tmp, "/proc/%d", pid);
	if (access(tmp, R_OK)==0) {
		g_free(tmp);
		return TRUE;
	}
	g_free(tmp);
	return FALSE;
}

/* -------------------------
 *
 */
void __check_dead_process()
{
	asm_instance_list_t *temp_list = head_list;
	asm_instance_list_t *temp_list2 = head_list;
	while (temp_list->next != tail_list) {
		if (!___is_pid_exist(temp_list->instance_id)) {
			debug_warning(" PID(%d) not exist! -> ASM_Server resource of pid(%d) will be cleared \n", temp_list->instance_id, temp_list->instance_id);

			if (temp_list == head_list) {
				head_list = temp_list->next;
			}
			temp_list2->next = temp_list->next;
			free(temp_list);
		} else {
			if (temp_list->paused_by_id.pid) {
				if (!___is_pid_exist(temp_list->paused_by_id.pid)) {
					temp_list->need_resume = ASM_NEED_NOT_RESUME;
					temp_list->paused_by_id.pid = 0;
					temp_list->paused_by_id.sound_handle = ASM_HANDLE_INIT_VAL;
					temp_list->paused_by_id.eventsrc = ASM_EVENT_SOURCE_MEDIA;
				}
			}
			temp_list2 = temp_list;
		}
		temp_list = temp_list2->next;
	}
	___update_phone_status();
}

void __reset_resume_check(int instance_id, int handle)
{
	asm_instance_list_t *temp_list = head_list;
	while (temp_list->next != tail_list) {
		if (handle == ASM_HANDLE_INIT_VAL) {
			if (temp_list->instance_id == instance_id && temp_list->paused_by_id.pid) {
				temp_list->need_resume = ASM_NEED_NOT_RESUME;
				temp_list->paused_by_id.pid = 0;
				temp_list->paused_by_id.sound_handle = ASM_HANDLE_INIT_VAL;
				temp_list->paused_by_id.eventsrc = ASM_EVENT_SOURCE_MEDIA;
			}
		} else {
			if (temp_list->instance_id == instance_id && temp_list->paused_by_id.sound_handle == handle) {
				temp_list->need_resume = ASM_NEED_NOT_RESUME;
				temp_list->paused_by_id.pid = 0;
				temp_list->paused_by_id.sound_handle = ASM_HANDLE_INIT_VAL;
				temp_list->paused_by_id.eventsrc = ASM_EVENT_SOURCE_MEDIA;
			}
		}
		temp_list = temp_list->next;
	}
}

void __emergent_exit(int exit_pid)
{
	asm_instance_list_t *temp_list = head_list;
	int handle = ASM_HANDLE_INIT_VAL;
	int instance_id = -1;

	while (temp_list->next != tail_list) {
		if (temp_list->instance_id == exit_pid) {
			handle = temp_list->sound_handle;

			instance_id = __asm_unregister_list(handle);

			if (instance_id != -1) {
				char str_error[256];
				char* filename = g_strdup_printf("/tmp/ASM.%d.%d", instance_id, handle);
				char* filename2 = g_strdup_printf("/tmp/ASM.%d.%dr", instance_id, handle);
				if (!remove(filename)) {
					debug_log(" remove %s success\n", filename);
				} else {
					strerror_r (errno, str_error, sizeof (str_error));
					debug_error(" remove %s failed with %s\n", filename, str_error);
				}

				if (!remove(filename2)) {
					debug_log(" remove %s success\n", filename2);
				} else {
					strerror_r (errno, str_error, sizeof (str_error));
					debug_error(" remove %s failed with %s\n", filename2, str_error);
				}

				g_free(filename);
				g_free(filename2);
			}
			temp_list = head_list;
		} else {
			temp_list = temp_list->next;
		}
	}

	debug_warning("[EMERGENT_EXIT] complete\n");
	return;
}

int ___reorder_state(ASM_sound_states_t input)
{
	int res = 0;

	switch (input) {
	case ASM_STATE_IGNORE:
	case ASM_STATE_NONE:
		res = 0;
		break;
	case ASM_STATE_WAITING:
	case ASM_STATE_STOP:
		res = 1;
		break;
	case ASM_STATE_PAUSE:
	case ASM_STATE_PAUSE_BY_APP:
		res = 2;
		break;
	case ASM_STATE_PLAYING:
		res = 3;
		break;
	}
	return res;
}

ASM_sound_states_t __asm_find_process_status(int pid)
{
	asm_instance_list_t *temp_list = head_list;
	ASM_sound_states_t result_state = ASM_STATE_NONE;

	debug_log(" __asm_find_process_status for pid %d\n", pid);

	while (temp_list->next != tail_list) {
		if (temp_list->instance_id == pid && !temp_list->is_registered_for_watching) {
			if ( ___reorder_state(temp_list->sound_state) >= ___reorder_state(result_state)) {
				result_state = temp_list->sound_state;
			}
		}
		temp_list = temp_list->next;
	}

	return result_state;
}

asm_instance_list_t* __asm_find_list(int instance_id, int handle)
{
	asm_instance_list_t* temp_list = head_list;

	debug_fenter();

	while (temp_list->next != tail_list) {
		if (temp_list->instance_id == instance_id && temp_list->sound_handle == handle) {
			debug_fleave();
			return temp_list;
		} else {
			temp_list = temp_list->next;
		}
	}

	debug_warning("could not find handle for pid(%d), handle(%d)", instance_id, handle);

	return NULL;
}

ASM_sound_states_t __asm_find_state_of_handle(int instance_id, int handle)
{
	asm_instance_list_t *temp_list = head_list;

	debug_fenter();

	while (temp_list->next != tail_list) {
		if (temp_list->instance_id == instance_id && temp_list->sound_handle == handle) {
			return temp_list->sound_state;
		}
		temp_list = temp_list->next;
	}

	debug_fleave();

	return ASM_STATE_NONE;
}

ASM_sound_events_t __asm_find_event_of_handle(int instance_id, int handle)
{
	asm_instance_list_t *temp_list = head_list;

	debug_fenter();

	while (temp_list->next != tail_list) {
		if (temp_list->instance_id == instance_id && temp_list->sound_handle == handle) {
			return temp_list->sound_event;
		}
		temp_list = temp_list->next;
	}

	debug_fleave();

	return ASM_EVENT_NONE;
}

int __asm_find_pid_of_resume_tagged(int paused_by_handle)
{
	asm_instance_list_t *temp_list = head_list;

	debug_fenter();

	while (temp_list->next != tail_list) {
		if (temp_list->need_resume == ASM_NEED_RESUME && temp_list->paused_by_id.sound_handle == paused_by_handle) {
			return temp_list->instance_id;
		}
		temp_list = temp_list->next;
	}

	debug_fleave();

	return -1;
}

asm_instance_list_t* __asm_change_state_list(int instance_id, int handle, ASM_sound_states_t sound_state, ASM_resource_t mm_resource)
{
	asm_instance_list_t *temp_list = head_list;
	debug_log(" __asm_change_state_list\n");
	if (sound_state == ASM_STATE_IGNORE) {
		debug_log(" skip update state list %d-%d\n", instance_id, handle);
		return NULL;
	}

	while (temp_list->next != tail_list) {
		if (temp_list->instance_id == instance_id && temp_list->sound_handle == handle) {
			temp_list->prev_sound_state = temp_list->sound_state;
			temp_list->sound_state = sound_state;
			temp_list->mm_resource = mm_resource;
			__update_monitor_active(instance_id);
			___update_phone_status();
			return temp_list;
		}
		temp_list = temp_list->next;
	}

	debug_warning("could not find handle for pid(%d), handle(%d)", instance_id, handle);

	return NULL;
}

void __asm_change_sub_event_list(int instance_id, int handle, ASM_sound_sub_events_t sub_event)
{
	asm_instance_list_t *temp_list = head_list;
	debug_log("[ASM_Server] __asm_change_sub_event_list\n");

	while (temp_list->next != tail_list) {
		if (temp_list->instance_id == instance_id && temp_list->sound_handle == handle) {
			temp_list->sound_sub_event = sub_event;
			break;
		}
		temp_list = temp_list->next;
	}
}

void __asm_change_need_resume_list(int instance_id, int handle, ASM_resume_states_t need_resume, int paused_by_pid, int paused_by_sound_handle, ASM_event_sources_t paused_by_eventsrc)
{
	asm_instance_list_t *temp_list = head_list;
	debug_log(" __asm_change_need_resume_list\n");
	while (temp_list->next != tail_list) {
		if (temp_list->instance_id == instance_id && temp_list->sound_handle == handle) {
			temp_list->need_resume = need_resume;
			temp_list->paused_by_id.pid = paused_by_pid;
			temp_list->paused_by_id.sound_handle = paused_by_sound_handle;
			temp_list->paused_by_id.eventsrc = paused_by_eventsrc;
			break;
		}
		temp_list = temp_list->next;
	}
}

void __asm_create_message_queue()
{
	asm_rcv_msgid = msgget((key_t)2014, 0666 | IPC_CREAT);
	asm_snd_msgid = msgget((key_t)4102, 0666 | IPC_CREAT);
	asm_cb_msgid = msgget((key_t)4103, 0666 | IPC_CREAT);

	if (asm_snd_msgid == -1 || asm_rcv_msgid == -1 || asm_cb_msgid == -1) {
		debug_error(" msgget failed with error(%d,%s) \n", errno, strerror(errno));
		exit(EXIT_FAILURE);
	}
}

void __asm_snd_message(ASM_msg_asm_to_lib_t *asm_snd_msg)
{
	if (msgsnd(asm_snd_msgid, (void *)asm_snd_msg, sizeof(asm_snd_msg->data), 0) == -1) {
		debug_error(" msgsnd failed with error %d\n", errno);
		exit(EXIT_FAILURE);
	} else {
		debug_warning(" success : handle(%d), pid(%d), result_state(%s), result_command(%s), source_request(%s)",
			asm_snd_msg->data.alloc_handle, asm_snd_msg->instance_id,
			ASM_sound_state_str[asm_snd_msg->data.result_sound_state],
			ASM_sound_command_str[asm_snd_msg->data.result_sound_command],
			ASM_sound_request_str[asm_snd_msg->data.source_request_id]);
		asm_snd_msg->data.result_sound_state = ASM_STATE_NONE;
		asm_snd_msg->data.result_sound_command = ASM_COMMAND_NONE;
	}
}

void __asm_rcv_message(ASM_msg_lib_to_asm_t *asm_rcv_msg)
{
	if (msgrcv(asm_rcv_msgid, (void *)asm_rcv_msg, sizeof(asm_rcv_msg->data), 0, 0) == -1) {
		debug_error(" msgrcv failed with error %d\n", errno);
		exit(EXIT_FAILURE);
	} else {
		debug_log(" success");
	}
}

void __asm_get_empty_handle(int instance_id, int *handle)
{
	asm_instance_list_t *temp_list = head_list;
	unsigned int i = 0, find_empty = 0;
	char handle_info[SERVER_HANDLE_MAX_COUNT];

	debug_log(" __asm_make_handle for %d\n", instance_id);
	//__temp_print_list("current list before get new handle");

	memset(handle_info, 0, sizeof(char) * SERVER_HANDLE_MAX_COUNT);

	while (temp_list->next != tail_list) {
		handle_info[temp_list->sound_handle] = 1;
		temp_list = temp_list->next;
	}

	for (i = 0; i < ASM_SERVER_HANDLE_MAX; i++) {
		if (handle_info[i] == 0) {
			find_empty = 1;
			break;
		}
	}
	if (find_empty && (i != ASM_SERVER_HANDLE_MAX)) {
		debug_log(" New handle for %d is %d\n", instance_id, i);
		*handle = i;
	} else {
		debug_error(" Handle is full for pid %d\n", instance_id);
		*handle = ASM_HANDLE_INIT_VAL;
	}
}

void __print_resource(unsigned short resource_status)
{
	if (resource_status == ASM_RESOURCE_NONE)
		debug_warning(" resource NONE\n");
	if (resource_status & ASM_RESOURCE_CAMERA)
		debug_warning(" resource CAMERA\n");
	if (resource_status & ASM_RESOURCE_VIDEO_OVERLAY)
		debug_warning(" resource VIDEO OVERLAY\n");
	if (resource_status & ASM_RESOURCE_STREAMING)
		debug_warning(" resource STREAMING\n");
	if (resource_status & ASM_RESOURCE_HW_ENCODER)
		debug_warning(" resource HW ENCODER\n");
	if (resource_status & ASM_RESOURCE_HW_DECODER)
		debug_warning(" resource HW DECODER\n");
	if (resource_status & ASM_RESOURCE_RADIO_TUNNER)
		debug_warning(" resource RADIO TUNNER\n");
	if (resource_status & ASM_RESOURCE_TV_TUNNER)
		debug_warning(" resource TV TUNNER\n");
	if (resource_status & ASM_RESOURCE_EAR_SPK_PATH)
		debug_warning(" resource for EAR_SPK_PATH\n");
	if (resource_status & ASM_RESOURCE_SVR)
		debug_warning(" resource for SVR\n");
	if (resource_status & ASM_RESOURCE_VOICECONTROL)
		debug_warning(" resource for VOICECONTROL\n");
}

asm_compare_result_t __asm_compare_priority_matrix(ASM_msg_asm_to_lib_t *asm_snd_msg, ASM_msg_asm_to_lib_t *asm_ret_msg,
								int instance_id, int handle, ASM_requests_t request_id,
								ASM_sound_events_t sound_event,ASM_sound_states_t sound_state, ASM_resource_t mm_resource)
{
	int no_conflict_flag = 0;
	asm_instance_list_t *incoming_list = NULL;
	asm_compare_result_t compare_result;
	memset(&compare_result, 0, sizeof(asm_compare_result_t));

	debug_log(" ENTER >>>>>> \n");

	/* If nobody is playing and waiting now, this means no conflict */
	if (ASM_STATUS_NONE == g_sound_status_playing && ASM_STATUS_NONE == g_sound_status_waiting) {
		debug_log(" No conflict ( No existing Sound )\n");

		ASM_SND_MSG_SET_RESULT(asm_snd_msg, ASM_COMMAND_NONE, sound_state, ASM_EVENT_NONE);

		no_conflict_flag = 1;
	} else { /* Somebody is playing */
		asm_instance_list_t *temp_list = head_list;
		int is_already_updated = 0;
		int cb_res = 0;
		int update_state = ASM_STATE_NONE;
		ASM_sound_sub_events_t incoming_sound_sub_event = sound_event;

		while (temp_list->next != tail_list) {

			/* Find who's playing now (include waiting state) */
			if ((temp_list->sound_state != ASM_STATE_PLAYING && temp_list->sound_state != ASM_STATE_WAITING)
					|| temp_list->is_registered_for_watching) {
				temp_list = temp_list->next;
				continue;
			}

			debug_warning("( %5d, %2d, %-20s, %-20s, %9s, 0x%04x) ..... [%s]\n",
					temp_list->instance_id, temp_list->sound_handle,
					ASM_sound_event_str[temp_list->sound_event],
					ASM_sound_state_str[temp_list->sound_state],
					ASM_sound_resume_str[temp_list->need_resume],
					temp_list->mm_resource,
					(temp_list->sound_state != ASM_STATE_PLAYING)? "PASS" : "CHECK");


			/* Found it */
			ASM_sound_states_t current_playing_state = temp_list->sound_state;
			ASM_sound_events_t current_playing_sound_event = temp_list->sound_event;
			int current_playing_instance_id = temp_list->instance_id;
			int current_playing_handle = temp_list->sound_handle;
			ASM_resource_t current_using_resource = temp_list->mm_resource;

			if ((current_playing_instance_id == instance_id) && (current_playing_handle == handle)) {
				debug_warning(" This is my handle. skip %d %d\n", instance_id, handle);
				temp_list = temp_list->next;
				if (current_playing_state != sound_state) {
					__asm_change_state_list(instance_id, handle, sound_state, mm_resource);
				}
				continue;
			}

			/* Request is PLAYING */
			if (sound_state == ASM_STATE_PLAYING) {
				ASM_sound_cases_t sound_case = ASM_CASE_NONE;
				ASM_sound_sub_events_t current_playing_sound_sub_event = ASM_SUB_EVENT_NONE;

				if (request_id ==  ASM_REQUEST_SET_SUBEVENT) {
					int adv_asm_idx = 0;
					int subtable_row_idx = 0;
					int ret = MM_ERROR_NONE;
					current_playing_sound_sub_event = temp_list->sound_sub_event;
					/* Determine sound policy using SubTable */
					ret = __get_sub_case_table_idx(current_playing_sound_event, &subtable_row_idx);
					if (ret) {
						temp_list = temp_list->next;
						continue;
					}

					sound_event = __asm_find_event_of_handle(instance_id, handle);
					ret = __get_adv_event_idx_for_subtable(sound_event, &adv_asm_idx);
					if (ret) {
						temp_list = temp_list->next;
						continue;
					}

					sound_case = ASM_sound_case_sub[adv_asm_idx][incoming_sound_sub_event][subtable_row_idx];
					if (sound_case == ASM_CASE_SUB_EVENT) {
						switch (incoming_sound_sub_event) {
							case ASM_SUB_EVENT_NONE:
							case ASM_SUB_EVENT_SHARE:
								if (current_playing_sound_sub_event == ASM_SUB_EVENT_NONE ||
									current_playing_sound_sub_event == ASM_SUB_EVENT_SHARE) {
									sound_case = ASM_CASE_1PLAY_2PLAY_MIX;
								}
								break;
							case ASM_SUB_EVENT_EXCLUSIVE:
								sound_case = ASM_CASE_1STOP_2PLAY;
								break;
							default:
								sound_case = ASM_CASE_SUB_EVENT;
								debug_warning("incoming_sound_sub_event is not valid: %d", incoming_sound_sub_event);
								break;
						}
					}
					debug_warning(" SubTable result:%s, sub-event:%s, adv. event type:%s",
							ASM_sound_case_str[sound_case], ASM_sound_sub_event_str[incoming_sound_sub_event+1], ASM_sound_event_str[sound_event]);
				} else {
					/* Determine sound policy using MainTable */
					sound_case = ASM_sound_case[current_playing_sound_event][sound_event];
				}

#ifdef SUPPORT_GCF
				/* GCF case is exception case */
				/* NOTE : GCF exception case only */
				if ((is_gcf) && (sound_case != ASM_CASE_1PLAY_2PLAY_MIX)) {
					sound_case = ASM_CASE_1PLAY_2PLAY_MIX;;
				}
#endif
				debug_warning(" Conflict policy[0x%x][0x%x]: %s\n", current_playing_sound_event,sound_event,ASM_sound_case_str[sound_case]);
				switch (sound_case) {
				case ASM_CASE_1PLAY_2STOP:
				{
					if (current_playing_instance_id == instance_id) {
						/* PID is policy group.*/
						debug_log(" Do not send Stop callback in same pid %d\n", instance_id);

						/* Prepare msg to send */
						ASM_SND_MSG_SET_RESULT(asm_snd_msg, ASM_COMMAND_PLAY, sound_state, current_playing_sound_event);

						if (!is_already_updated && request_id == ASM_REQUEST_REGISTER){
							__asm_register_list(instance_id, handle, sound_event, sound_state, mm_resource, false);
							is_already_updated = 1;
						} else {
							__asm_change_state_list(instance_id, handle, sound_state, mm_resource);
						}
						break;
					} else {
						ASM_SND_MSG_SET_RESULT(asm_snd_msg, ASM_COMMAND_STOP, ASM_STATE_STOP, current_playing_sound_event);
						temp_list = tail_list;	/* skip all remain list */
						__asm_change_state_list(instance_id, handle, ASM_STATE_STOP, mm_resource);
						break;
					}
				}

				case ASM_CASE_1STOP_2PLAY:
				{
					if (current_playing_instance_id == instance_id) {
						/* PID is policy group. */
						debug_warning(" Do not send Stop callback in same pid %d\n", instance_id);

					} else {
						ASM_event_sources_t event_src;

						/* Determine root cause of conflict */
						event_src = __mapping_sound_event_to_event_src(sound_event, mm_resource, ASM_CASE_1STOP_2PLAY);

						/* Execute callback function for monitor handle */
						int monitor_handle = -1;
						ASM_DO_MONITOR_CALLBACK(current_playing_instance_id, monitor_handle, ASM_COMMAND_STOP, event_src);

						/* Execute callback function for worker handle */
						cb_res = __do_callback(current_playing_instance_id,current_playing_handle, ASM_COMMAND_STOP, event_src);
						if (cb_res != ASM_CB_RES_NONE && cb_res != ASM_CB_RES_STOP) {
							debug_error(" oops! not suspected result %d\n", cb_res);
						}
						debug_warning(" 1STOP_2PLAY : __do_callback Complete, TID=%d, handle=%d",
								current_playing_instance_id,current_playing_handle );

						/* Set state to STOP or NONE */
						__asm_change_state_list(current_playing_instance_id, current_playing_handle,
								(cb_res == ASM_CB_RES_STOP)? ASM_STATE_STOP : ASM_STATE_NONE,
								current_using_resource);

						compare_result.previous_asm_handle[compare_result.previous_num_of_changes++] = temp_list;
					}

					/* Prepare msg to send */
					ASM_SND_MSG_SET_RESULT(asm_snd_msg, ASM_COMMAND_PLAY, sound_state, current_playing_sound_event);

					if (!is_already_updated && request_id == ASM_REQUEST_REGISTER) {
						incoming_list = __asm_register_list(instance_id, handle, sound_event, sound_state, mm_resource, false);
						is_already_updated = 1;
					} else {
						incoming_list = __asm_change_state_list(instance_id, handle, sound_state, mm_resource);
					}
					if (incoming_list) {
						compare_result.incoming_asm_handle[compare_result.incoming_num_of_changes++] = incoming_list;
					}

					break;
				}

				case ASM_CASE_1PAUSE_2PLAY:
				{
					ASM_resource_t	update_resource = current_using_resource;
					if (current_playing_instance_id == instance_id)	{
						debug_warning(" Do not send Pause callback in same pid %d\n", instance_id);
					} else {
						ASM_event_sources_t event_src;
						ASM_sound_commands_t command;

						unsigned short resource_status = current_using_resource & mm_resource;
						if (resource_status != ASM_RESOURCE_NONE) {
							debug_log(" resource conflict found 0x%x\n", resource_status);
							event_src = ASM_EVENT_SOURCE_RESOURCE_CONFLICT;
							command = ASM_COMMAND_STOP;
						} else {
							event_src = __mapping_sound_event_to_event_src(sound_event, mm_resource, ASM_CASE_1PAUSE_2PLAY);
							command = ASM_COMMAND_PAUSE;
						}

						/* Execute callback function for monitor handle */
						int monitor_handle = -1;
						ASM_DO_MONITOR_CALLBACK(current_playing_instance_id, monitor_handle, command, event_src);
						ASM_CHECK_RESUME (sound_event, mm_resource, current_playing_sound_event, current_playing_instance_id, current_playing_state, monitor_handle,
											instance_id, handle, event_src);

						/* Execute callback function for worker handle */
						cb_res = __do_callback(current_playing_instance_id,current_playing_handle, command, event_src);
						debug_warning(" 1PAUSE_2PLAY : Callback of %s, TID(%d), result(%d)",
									ASM_sound_command_str[command], current_playing_instance_id, cb_res);
						/*Change current sound' state when it is in 1Pause_2Play case */
						switch (cb_res) {
						case ASM_CB_RES_PAUSE:
							update_state = ASM_STATE_PAUSE;
							break;
						case ASM_CB_RES_NONE:
							update_state = ASM_STATE_NONE;
							break;
						case ASM_CB_RES_STOP:
							update_state = ASM_STATE_STOP;
							break;
						case ASM_CB_RES_IGNORE:
							update_state = ASM_STATE_IGNORE;
							break;
						default:
							debug_error(" oops! not suspected result %d\n", cb_res);
							update_state = ASM_STATE_NONE;
							break;
						}

						ASM_CHECK_RESUME (sound_event, mm_resource, current_playing_sound_event, current_playing_instance_id, current_playing_state, current_playing_handle,
											instance_id, handle, event_src);

						__asm_change_state_list(current_playing_instance_id, current_playing_handle, update_state, update_resource);

						compare_result.previous_asm_handle[compare_result.previous_num_of_changes++] = temp_list;
					}

					/* Prepare msg to send */
					ASM_SND_MSG_SET_RESULT(asm_snd_msg, ASM_COMMAND_PLAY, sound_state, current_playing_sound_event);

					if (!is_already_updated && request_id == ASM_REQUEST_REGISTER) {
						incoming_list = __asm_register_list(instance_id, handle, sound_event, sound_state, mm_resource, false);
						is_already_updated = 1;
					} else {
						incoming_list = __asm_change_state_list(instance_id, handle, sound_state, mm_resource);
					}
					if (incoming_list) {
						compare_result.incoming_asm_handle[compare_result.incoming_num_of_changes++] = incoming_list;
					}
					break;
				}

				case ASM_CASE_1PLAY_2PLAY_MIX:
				{
					if (current_playing_instance_id == instance_id) {
						debug_warning(" Do not send check resource conflict in same pid %d\n", instance_id);
					} else {
						/* MIX but need to check resource conflict */
						debug_warning(" 1PLAY_2PLAY_MIX\n");

						ASM_resource_t update_resource = current_using_resource;
						unsigned short resource_status = current_using_resource & mm_resource;
						if (resource_status) { /* Resouce conflict */
							debug_warning(" there is system resource conflict 0x%x\n", resource_status);
							__print_resource(resource_status);

							/* Execute callback function for monitor handle */
							int monitor_handle = -1;
							ASM_DO_MONITOR_CALLBACK(current_playing_instance_id, monitor_handle, ASM_COMMAND_STOP, ASM_EVENT_SOURCE_RESOURCE_CONFLICT);

							/* Execute callback function for worker handle */
							/* Stop current resource holding instance */
							cb_res = __do_callback(current_playing_instance_id, current_playing_handle, ASM_COMMAND_STOP, ASM_EVENT_SOURCE_RESOURCE_CONFLICT);
							debug_warning(" 1PLAY_2PLAY_MIX : Resource Conflict, TID(%d)\n", current_playing_instance_id);

							/* Change current sound */
							switch (cb_res) {
							case ASM_CB_RES_NONE:
								update_state = ASM_STATE_NONE;
								break;
							case ASM_CB_RES_STOP:
								update_state = ASM_STATE_STOP;
								break;
							case ASM_CB_RES_IGNORE:
								update_state = ASM_STATE_IGNORE;
								break;
							default:
								debug_error(" oops! not suspected result %d\n", cb_res);
								update_state = ASM_STATE_NONE;
								break;
							}

							__asm_change_state_list(current_playing_instance_id, current_playing_handle, update_state, update_resource);

							compare_result.previous_asm_handle[compare_result.previous_num_of_changes++] = temp_list;
						}
					}

					/* Prepare msg to send */
					ASM_SND_MSG_SET_RESULT(asm_snd_msg, ASM_COMMAND_PLAY, sound_state, current_playing_sound_event);

					if (!is_already_updated && request_id == ASM_REQUEST_REGISTER) {
						incoming_list = __asm_register_list(instance_id, handle, sound_event, sound_state, mm_resource, false);
						is_already_updated = 1;
					} else {
						incoming_list = __asm_change_state_list(instance_id, handle, sound_state, mm_resource);
					}
					if (incoming_list) {
						compare_result.incoming_asm_handle[compare_result.incoming_num_of_changes++] = incoming_list;
					}

					break;
				}

				case ASM_CASE_RESOURCE_CHECK:
				{
					unsigned short resource_status = current_using_resource & mm_resource;
					if (resource_status!= ASM_RESOURCE_NONE) {
						debug_log(" ASM_CASE_RESOURCE_CHECK : resource conflict found 0x%x\n", resource_status);

						switch (resource_status){
						case ASM_RESOURCE_CAMERA:
							if (current_playing_sound_event == ASM_EVENT_MMCAMCORDER_VIDEO ||
#ifdef WILL_BE_DEPRECATED
								current_playing_sound_event == ASM_EVENT_SHARE_MMCAMCORDER ||
								current_playing_sound_event == ASM_EVENT_EXCLUSIVE_MMCAMCORDER ||
#endif
								current_playing_sound_event == ASM_EVENT_MMCAMCORDER_VIDEO ||
								current_playing_sound_event == ASM_EVENT_VIDEOCALL ||
								current_playing_sound_event == ASM_EVENT_RICH_CALL ) {
								/* 1PLAY,2STOP */
								debug_log(" ASM_CASE_RESOURCE_CHECK : 1PLAY_2STOP");

								ASM_SND_MSG_SET_RESULT(asm_snd_msg, ASM_COMMAND_STOP, ASM_STATE_STOP, current_playing_sound_event);
								temp_list = tail_list;	/* skip all remain list */
								__asm_change_state_list(instance_id, handle, ASM_STATE_STOP, mm_resource);

							} else if (current_playing_sound_event == ASM_EVENT_EXCLUSIVE_RESOURCE){
								/* 1STOP,2PLAY */
								ASM_event_sources_t event_src;
								event_src = ASM_EVENT_SOURCE_RESOURCE_CONFLICT;

								/* Execute callback function for worker handle */
								cb_res = __do_callback(current_playing_instance_id,current_playing_handle,ASM_COMMAND_STOP, event_src);
								if (cb_res != ASM_CB_RES_NONE && cb_res != ASM_CB_RES_STOP) {
									debug_error(" oops! not suspected result %d\n", cb_res);
								}
								debug_warning(" ASM_CASE_RESOURCE_CHECK : 1STOP_2PLAY, __do_callback Complete : TID=%d, handle=%d",
										current_playing_instance_id,current_playing_handle );

								/* Set state to STOP */
								__asm_change_state_list(current_playing_instance_id, current_playing_handle,
										(cb_res == ASM_CB_RES_STOP)? ASM_STATE_STOP : ASM_STATE_NONE,
										ASM_RESOURCE_NONE);

								/* Prepare msg to send */
								ASM_SND_MSG_SET_RESULT(asm_snd_msg, ASM_COMMAND_PLAY, sound_state, current_playing_sound_event);

								if (!is_already_updated && request_id == ASM_REQUEST_REGISTER) {
									incoming_list = __asm_register_list(instance_id, handle, sound_event, sound_state, mm_resource, false);
									is_already_updated = 1;
								} else {
									incoming_list = __asm_change_state_list(instance_id, handle, sound_state, mm_resource);
								}
								if (incoming_list) {
									compare_result.incoming_asm_handle[compare_result.incoming_num_of_changes++] = incoming_list;
								}
							}
							break;
						default:
							debug_warning(" ASM_CASE_RESOURCE_CHECK : Not support it(0x%x)", resource_status);
							break;
						}
					} else {
						debug_log(" ASM_CASE_RESOURCE_CHECK : do MIX");
						/* DO MIX */
						/* Prepare msg to send */
						ASM_SND_MSG_SET_RESULT(asm_snd_msg, ASM_COMMAND_PLAY, sound_state, current_playing_sound_event);

						if (!is_already_updated && request_id == ASM_REQUEST_REGISTER) {
							incoming_list = __asm_register_list(instance_id, handle, sound_event, sound_state, mm_resource, false);
							is_already_updated = 1;
						} else {
							incoming_list = __asm_change_state_list(instance_id, handle, sound_state, mm_resource);
						}
						if (incoming_list) {
							compare_result.incoming_asm_handle[compare_result.incoming_num_of_changes++] = incoming_list;
						}
					}
					break;
				}

				case ASM_CASE_SUB_EVENT:
				{
					ASM_sound_commands_t result_command = ASM_COMMAND_PLAY;
					ASM_sound_states_t result_sound_state = sound_state;
					if (current_playing_instance_id == instance_id) {
						/* PID is policy group. */
						debug_warning(" Do not compare with same pid %d in case of ASM_CASE_SUB_EVENT\n", instance_id);

					} else if (!ASM_IS_ADVANCED_ASM_EVENT(sound_event)) {
						/* (current)advanced event type VS. (incoming)common event type */
						/* get previous sub-event type */
						asm_instance_list_t *asm_instance_h = NULL;
						asm_instance_h = __asm_find_list(current_playing_instance_id, current_playing_handle);
						if (!asm_instance_h) {
							debug_error(" Something is wrong, skip this request..");
						} else {
							debug_warning("current playing sub-event type : %s", subevent_str[asm_instance_h->sound_sub_event]);
							/* get sub event type of current */
							if (asm_instance_h->sound_sub_event == ASM_SUB_EVENT_NONE || asm_instance_h->sound_sub_event == ASM_SUB_EVENT_SHARE) {
								/* MIX but need to check resource conflict */
								debug_warning(" ASM_CASE_SUB_EVENT : 1PLAY_2PLAY_MIX\n");
								ASM_resource_t update_resource = current_using_resource;
								unsigned short resource_status = current_using_resource & mm_resource;
								if (resource_status) {
									/* Resource conflict */
									debug_warning(" there is system resource conflict 0x%x\n", resource_status);
									__print_resource(resource_status);

									/* Execute callback function for worker handle */
									/* Stop current resource holding instance */
									cb_res = __do_callback(current_playing_instance_id, current_playing_handle, ASM_COMMAND_STOP, ASM_EVENT_SOURCE_RESOURCE_CONFLICT);
									debug_warning(" Resource Conflict : TID(%d)\n", current_playing_instance_id);

									/* Change current sound */
									switch (cb_res) {
									case ASM_CB_RES_NONE:
										update_state = ASM_STATE_NONE;
										break;
									case ASM_CB_RES_STOP:
										update_state = ASM_STATE_STOP;
										break;
									case ASM_CB_RES_IGNORE:
										update_state = ASM_STATE_IGNORE;
										break;
									default:
										debug_error(" oops! not suspected result %d\n", cb_res);
										update_state = ASM_STATE_NONE;
										break;
									}

									__asm_change_state_list(current_playing_instance_id, current_playing_handle, update_state, update_resource);

									compare_result.previous_asm_handle[compare_result.previous_num_of_changes++] = temp_list;
								}
							} else {
								/* define exception case */
								bool is_exception_case = false;

#ifdef ASM_FOR_PRODUCT
								int svoice_state = 0;
#if 0
								vconf_get_int(VCONFKEY_SVOICE_STATE, &svoice_state);
								if (svoice_state == 1) {
									debug_warning("EXCEPTION case for svoice in ASM_CASE_SUB_EVENT => 1PLAY_2STOP\n");
									g_blocked_reason = ASM_SOUND_BLOCKED_BY_CUSTOM;
								}
#endif
#endif

								is_exception_case =
#ifdef ASM_FOR_PRODUCT
										((current_using_resource & ASM_RESOURCE_SVR) && svoice_state && (sound_event == ASM_EVENT_EXCLUSIVE_MMPLAYER && (mm_resource & ASM_RESOURCE_VIDEO_OVERLAY))) ||
#endif
										 (sound_event == ASM_EVENT_NOTIFY);
								if (!is_exception_case) {
									/* do competition */
									ASM_event_sources_t event_src;
									unsigned short resource_status = current_using_resource & mm_resource;

									/* Determine root cause of conflict */
									if (resource_status != ASM_RESOURCE_NONE) {
										event_src = ASM_EVENT_SOURCE_RESOURCE_CONFLICT;
									} else {
										event_src = __mapping_sound_event_to_event_src(sound_event, mm_resource, ASM_CASE_1STOP_2PLAY);
									}

									/* Execute callback function for monitor handle */
									int monitor_handle = -1;
									ASM_DO_MONITOR_CALLBACK(current_playing_instance_id, monitor_handle, ASM_COMMAND_STOP, event_src);

									/* Execute callback function for worker handle */
									cb_res = __do_callback(current_playing_instance_id,current_playing_handle,ASM_COMMAND_STOP, event_src);
									if (cb_res != ASM_CB_RES_NONE && cb_res != ASM_CB_RES_STOP) {
										debug_error(" oops! not suspected result %d\n", cb_res);
									}
									debug_warning(" ASM_CASE_SUB_EVENT : 1STOP_2PLAY, __do_callback Complete : TID=%d, handle=%d",
											current_playing_instance_id,current_playing_handle );

									__asm_change_state_list(current_playing_instance_id, current_playing_handle, ASM_STATE_STOP, ASM_RESOURCE_NONE);

									compare_result.previous_asm_handle[compare_result.previous_num_of_changes++] = temp_list;

								} else {
									/* exception case : do not start it */
									debug_warning(" ASM_CASE_SUB_EVENT : Do not start it");
									result_command = ASM_COMMAND_PAUSE;
									result_sound_state = ASM_STATE_PAUSE;
								}
							}
						}
					}

					/* Prepare msg to send */
					ASM_SND_MSG_SET_RESULT(asm_snd_msg, result_command, result_sound_state, current_playing_sound_event);

					if (!is_already_updated && request_id == ASM_REQUEST_REGISTER) {
						incoming_list = __asm_register_list(instance_id, handle, sound_event, result_sound_state, mm_resource, false);
						is_already_updated = 1;
					} else {
						incoming_list = __asm_change_state_list(instance_id, handle, result_sound_state, mm_resource);
					}
					if (incoming_list) {
						compare_result.incoming_asm_handle[compare_result.incoming_num_of_changes++] = incoming_list;
					}
					break;
				}

				default:
				{
					ASM_SND_MSG_SET_RESULT(asm_snd_msg, ASM_COMMAND_NONE, sound_state, current_playing_sound_event);
					debug_warning(" ASM_CASE_NONE [It should not be seen] !!!\n");
					break;
				}
				} /* switch (sound_case) */
			} else {
				/* Request was not PLAYING, this means no conflict, just do set */
				debug_log(" No Conflict (Just Register or Set State) !!!\n");
				ASM_SND_MSG_SET_RESULT(asm_snd_msg, ASM_COMMAND_NONE, sound_state, current_playing_sound_event);

				if (sound_state == ASM_STATE_NONE) {
					debug_log(" 1PLAY_2NONE : No Conflict !!!\n");
				} else if (sound_state == ASM_STATE_WAITING) {
					/* Request is WAITING */
					if (current_playing_instance_id == instance_id)	{
						debug_warning(" Do not check in same pid %d\n", instance_id);
					} else {
						ASM_resource_t update_resource = current_using_resource;
						unsigned short resource_status = current_using_resource & mm_resource;
						if (resource_status) { /* Resouce conflict */
							debug_warning(" there is system resource conflict 0x%x\n", resource_status);
							__print_resource(resource_status);

							/* Execute callback function for monitor handle */
							int monitor_handle = -1;
							ASM_DO_MONITOR_CALLBACK(current_playing_instance_id, monitor_handle, ASM_COMMAND_STOP, ASM_EVENT_SOURCE_RESOURCE_CONFLICT);

							/* Execute callback function for worker handle */
							/* Stop current resource holding instance */
							cb_res = __do_callback(current_playing_instance_id, current_playing_handle, ASM_COMMAND_STOP, ASM_EVENT_SOURCE_RESOURCE_CONFLICT);
							debug_warning(" Resource Conflict, TID(%d)\n", current_playing_instance_id);

							/* Change current sound */
							switch (cb_res) {
							case ASM_CB_RES_NONE:
								update_state = ASM_STATE_NONE;
								break;
							case ASM_CB_RES_STOP:
								update_state = ASM_STATE_STOP;
								break;
							case ASM_CB_RES_IGNORE:
								update_state = ASM_STATE_IGNORE;
								break;
							default:
								debug_error(" oops! not suspected result %d\n", cb_res);
								update_state = ASM_STATE_NONE;
								break;
							}
							__asm_change_state_list(current_playing_instance_id, current_playing_handle, update_state, update_resource);
							compare_result.previous_asm_handle[compare_result.previous_num_of_changes++] = temp_list;
						}
					}
				}

				if (!is_already_updated && request_id == ASM_REQUEST_REGISTER) {
					incoming_list = __asm_register_list(instance_id, handle, sound_event, sound_state, mm_resource, false);
					is_already_updated = 1;
				} else {
					incoming_list = __asm_change_state_list(instance_id, handle, sound_state, mm_resource);
				}
				if (incoming_list) {
					compare_result.incoming_asm_handle[compare_result.incoming_num_of_changes++] = incoming_list;
				}
			}

			temp_list = temp_list->next;

		} /* while (temp_list->next != tail_list) */

		/* Make all monitor handle dirty flag clean. */
		__set_all_monitor_clean();
	}

	/* Find if resource conflict exists in case of 1Pause 2Play or 1Stop 2Play */
	if (ASM_STATUS_NONE != g_sound_status_pause && mm_resource != ASM_RESOURCE_NONE &&
		(asm_snd_msg->data.result_sound_command == ASM_COMMAND_PLAY || no_conflict_flag)) {
		asm_instance_list_t *temp_list = head_list;
		int cb_res = 0;

		while (temp_list->next != tail_list) {
			/* Who is in PAUSE state? */
			if (temp_list->sound_state == ASM_STATE_PAUSE) {
				/* Found PAUSE state */
				debug_warning(" Now list's state is pause. %d %d\n", temp_list->instance_id, temp_list->sound_handle);
				ASM_sound_events_t current_playing_sound_event = temp_list->sound_event;
				int current_playing_instance_id = temp_list->instance_id;
				int current_playing_handle = temp_list->sound_handle;
				ASM_resource_t current_using_resource = temp_list->mm_resource;

				if ((current_playing_instance_id == instance_id) && (current_playing_handle == handle)) {
					if (request_id == ASM_REQUEST_SETSTATE) {
						debug_warning(" Own handle. Pause state change to play. %d %d\n", instance_id, handle);
						__asm_change_state_list(instance_id, handle, sound_state, mm_resource);
					} else {
						debug_warning(" This is my handle. skip %d %d\n", instance_id, handle);
					}
					temp_list = temp_list->next;
					continue;
				}

				if (sound_state == ASM_STATE_PLAYING) {
					ASM_sound_cases_t sound_case = ASM_sound_case[current_playing_sound_event][sound_event];

					debug_log(" Conflict policy[0x%x][0x%x]: %s\n", current_playing_sound_event, sound_event, ASM_sound_case_str[sound_case]);
					switch (sound_case) {
					case ASM_CASE_1PAUSE_2PLAY:
					case ASM_CASE_1STOP_2PLAY:
					{
						if (current_playing_instance_id == instance_id) {
							//PID is policy group.
							debug_log(" Do not send Stop callback in same pid %d\n", instance_id);
						} else {
							unsigned short resource_status = current_using_resource & mm_resource;

							/* Check conflict with paused instance */
							if (resource_status != ASM_RESOURCE_NONE) {
								debug_warning(" there is system resource conflict with paused instance 0x%x\n", resource_status);
								__print_resource(resource_status);
							} else {
								debug_log(" no resource conflict with paused instance\n");
								break;
							}

							/* Execute callback function for monitor handle */
							int monitor_handle = -1;
							ASM_DO_MONITOR_CALLBACK(current_playing_instance_id, monitor_handle, ASM_COMMAND_STOP, ASM_EVENT_SOURCE_RESOURCE_CONFLICT);

							/* Execute callback function for worker handle */
							cb_res = __do_callback(current_playing_instance_id,current_playing_handle, ASM_COMMAND_STOP, ASM_EVENT_SOURCE_RESOURCE_CONFLICT);
							if (cb_res != ASM_CB_RES_NONE && cb_res != ASM_CB_RES_STOP) {
								debug_error(" oops! not suspected result %d\n", cb_res);
							}
							debug_warning("  1STOP_2PLAY cause RESOURCE, __do_callback Complete : TID=%d, handle=%d",
									current_playing_instance_id,current_playing_handle );

							__asm_change_state_list(current_playing_instance_id, current_playing_handle, ASM_STATE_NONE, ASM_RESOURCE_NONE);

							compare_result.previous_asm_handle[compare_result.previous_num_of_changes++] = temp_list;
						}

						debug_log(" 1STOP_2PLAY cause RESOURCE : msg sent and  then received msg !!!\n");
						break;
					}

					default:
						/* debug_warning(" >>>> __asm_compare_priority_matrix : ASM_CASE_NONE [do not anything] !!!\n"); */
						break;

					} /* switch (sound_case) */
				} else {
					/* debug_warning(" >>>> __asm_compare_priority_matrix : ASM_CASE_NONE [do not anything] !!!\n"); */
				}
			} /* if (temp_list->sound_state == ASM_STATE_PAUSE) */

			temp_list = temp_list->next;
		} /* while (temp_list->next != tail_list) */
	}

	/* Finally, no conflict */
	if (no_conflict_flag) {
		if (request_id == ASM_REQUEST_REGISTER) {
			incoming_list = __asm_register_list(instance_id, handle, sound_event, sound_state, mm_resource, false);
		} else {
			incoming_list = __asm_change_state_list(instance_id, handle, sound_state, mm_resource);
		}

		if (incoming_list) {
			compare_result.incoming_asm_handle[compare_result.incoming_num_of_changes++] = incoming_list;
		}
	}

	if (asm_ret_msg) {
		*asm_ret_msg = *asm_snd_msg;
	}

	debug_log(" LEAVE <<<<<< \n");

	return compare_result;
}

void __asm_do_all_resume_callback(asm_paused_by_id_t paused_by_id)
{
	asm_instance_list_t *temp_list = head_list;
	int cb_res = 0;

	debug_log(" >>>>>>>>>> ENTER\n");

	while (temp_list->next != tail_list) {
		if (temp_list->need_resume == ASM_NEED_RESUME
			&& temp_list->paused_by_id.pid == paused_by_id.pid
			&& temp_list->paused_by_id.sound_handle == paused_by_id.sound_handle) {
			if (__mm_sound_mgr_ipc_freeze_send (FREEZE_COMMAND_WAKEUP, temp_list->instance_id)) {
				debug_warning("failed to send wake-up msg to the client pid(%d)",temp_list->instance_id);
			}
			debug_warning("EXECUTE RESUME CALLBACKS with interrupted EventSrc[%d], paused_by_pid[%d]/handle[%d], resumption pid[%d]/handle[%d]\n",
					temp_list->paused_by_id.eventsrc, paused_by_id.pid, paused_by_id.sound_handle, temp_list->instance_id, temp_list->sound_handle);
			cb_res = __do_callback(temp_list->instance_id, temp_list->sound_handle, ASM_COMMAND_RESUME, __convert_eventsrc_interrupted_to_completed(temp_list->paused_by_id.eventsrc));
			debug_warning("RESUME CALLBACKS END, cb_res(%d)\n", cb_res);
			switch (cb_res) {
			case ASM_CB_RES_PLAYING:
				temp_list->sound_state = ASM_STATE_PLAYING;
				break;
			case ASM_CB_RES_IGNORE:
			case ASM_CB_RES_NONE:
			case ASM_CB_RES_STOP:
			case ASM_CB_RES_PAUSE:
			default:
				/* do nothing */
				break;
			}
			temp_list->need_resume = ASM_NEED_NOT_RESUME;
			temp_list->paused_by_id.pid = 0;
			temp_list->paused_by_id.sound_handle = ASM_HANDLE_INIT_VAL;
			temp_list->paused_by_id.eventsrc = ASM_EVENT_SOURCE_MEDIA;;
		}
		temp_list = temp_list->next;
	}

	debug_log(" <<<<<<<<<< LEAVE\n");
}

#ifdef USE_SECURITY
gboolean __asm_check_check_privilege (unsigned char* cookie)
{
	int asm_gid = -1;
	int retval = 0;

	/* Get ASM server group id */
	asm_gid = security_server_get_gid("asm");
	debug_log ("[Security] asm server gid = [%d]\n", asm_gid);
	if (asm_gid < 0) {
		debug_error ("[Security] security_server_get_gid() failed. error=[%d]\n", asm_gid);
		return false;
	}

	/* Check privilege with valid group id */
	retval = security_server_check_privilege((char *)cookie, asm_gid);
	if (retval == SECURITY_SERVER_API_SUCCESS) {
		debug_log("[Security] security_server_check_privilege() returns [%d]\n", retval);
		return true;
	} else {
		debug_error("[Security] security_server_check_privilege() returns [%d]\n", retval);
		return false;
	}
}
#endif /* USE_SECURITY */


int __asm_change_session (ASM_requests_t rcv_request, ASM_sound_events_t rcv_sound_event, ASM_sound_states_t rcv_sound_state, ASM_resource_t rcv_resource, bool is_for_recovery, bool *need_to_resume)
{
	int ret = MM_ERROR_NONE;
	session_t cur_session;

	MMSoundMgrSessionGetSession(&cur_session);
	debug_warning (" cur_session[%d] (0:MEDIA 1:VC 2:VT 3:VOIP 4:FM 5:NOTI 6:ALARM 7:EMER 8:VR)\n",cur_session);
	if (is_for_recovery) {
		if (need_to_resume) {
			*need_to_resume = false;
		} else {
			debug_error (" need_to_resume(%x) is null..", need_to_resume);
			return MM_ERROR_SOUND_INTERNAL;
		}
	}

	if (!is_for_recovery) {
	/* change session setting for new sound event */
		if (rcv_request == ASM_REQUEST_REGISTER) {

			switch (rcv_sound_event) {
			case ASM_EVENT_CALL:
			case ASM_EVENT_RICH_CALL:
				debug_warning (" ****** SESSION_VOICECALL start ******\n");
				ret = MMSoundMgrSessionSetSession(SESSION_VOICECALL, SESSION_START);
				if (ret) {
					goto ERROR_CASE;
				}
				break;
			case ASM_EVENT_VIDEOCALL:
				debug_warning (" ****** SESSION_VIDEOCALL start ******\n");
				ret = MMSoundMgrSessionSetSession(SESSION_VIDEOCALL, SESSION_START);
				if (ret) {
					goto ERROR_CASE;
				}
				break;
			case ASM_EVENT_VOIP:
				debug_warning (" ****** SESSION_VOIP start ******\n");
				ret = MMSoundMgrSessionSetSession(SESSION_VOIP, SESSION_START);
				if (ret) {
					goto ERROR_CASE;
				}
				break;
			case ASM_EVENT_EXCLUSIVE_RESOURCE:
				if ( rcv_resource & ASM_RESOURCE_VOICECONTROL ) {
					debug_warning (" ****** ASM_RESOURCE_VOICECONTROL START ******\n");
					MMSoundMgrSessionSetVoiceControlState(true);
				}
				break;
#ifdef TIZEN_MICRO
			case ASM_EVENT_EXCLUSIVE_MMPLAYER:
				/* pre-listening mode */
				if ( rcv_resource & ASM_RESOURCE_EAR_SPK_PATH ) {
					if (cur_session == SESSION_ALARM) {
						debug_warning (" ****** SESSION_ALARM ongoing ******\n");
						SESSION_REF(cur_session);
					} else {
						debug_warning (" ****** SESSION_ALARM for pre-listening mode start ******\n");
						ret = MMSoundMgrSessionSetSession(SESSION_ALARM, SESSION_START);
						if (ret) {
							goto ERROR_CASE;
						}
						SESSION_REF_INIT();
						SESSION_REF(cur_session);
					}
				} else {
					if (cur_session != SESSION_MEDIA) {
						if (cur_session == SESSION_VOICE_RECOGNITION) {
							/* Media and Voice Recognition can be mixed */
							debug_warning (" SESSION_VOICE_RECOGNITION exception case, no change\n");
						} else {
							goto RECOVERY_CASE;
						}
					}
				}
				break;
#endif
			default:
				break;
			}

		} else if (rcv_request == ASM_REQUEST_SETSTATE) {

			switch (rcv_sound_event) {
			case ASM_EVENT_NOTIFY:
				if (cur_session == SESSION_NOTIFICATION) {
					debug_warning (" ****** SESSION_NOTIFICATION ongoing ******\n");
					SESSION_REF(cur_session);
				} else if (cur_session == SESSION_ALARM) {
					debug_warning (" ****** SESSION_ALARM ongoing ******\n");
					SESSION_REF(cur_session);
				} else if (cur_session == SESSION_VOICECALL || cur_session == SESSION_VIDEOCALL || cur_session == SESSION_VOIP) {
					debug_warning (" ****** Notify starts on Call/Voip session ******\n");
				} else {
					debug_warning (" ****** SESSION_NOTIFICATION start ******\n");
					ret = MMSoundMgrSessionSetSession(SESSION_NOTIFICATION, SESSION_START);
					if (ret) {
						goto ERROR_CASE;
					}
					SESSION_REF_INIT();
					SESSION_REF(cur_session);
				}
				break;
			case ASM_EVENT_ALARM:
				if (cur_session == SESSION_ALARM) {
					debug_warning (" ****** SESSION_ALARM ongoing ******\n");
					SESSION_REF(cur_session);
				} else if (cur_session == SESSION_VOICECALL || cur_session == SESSION_VIDEOCALL || cur_session == SESSION_VOIP) {
					debug_warning (" ****** Alarm starts on Call/Voip session ******\n");
				} else {
					debug_warning (" ****** SESSION_ALARM start ******\n");
					ret = MMSoundMgrSessionSetSession(SESSION_ALARM, SESSION_START);
					if (ret) {
						goto ERROR_CASE;
					}
					SESSION_REF_INIT();
					SESSION_REF(cur_session);
				}
				break;
			case ASM_EVENT_EMERGENCY:
				if (cur_session == SESSION_EMERGENCY) {
					debug_warning (" ****** SESSION_EMERGENCY ongoing ******\n");
					SESSION_REF(cur_session);
				} else {
					debug_warning (" ****** SESSION_EMERGENCY start ******\n");
					ret = MMSoundMgrSessionSetSession(SESSION_EMERGENCY, SESSION_START);
					if (ret) {
						goto ERROR_CASE;
					}
					SESSION_REF_INIT();
					SESSION_REF(cur_session);
				}
				break;
			case ASM_EVENT_SHARE_FMRADIO:
			case ASM_EVENT_EXCLUSIVE_FMRADIO:
				if (cur_session == SESSION_FMRADIO) {
					debug_warning (" ****** SESSION_FMRADIO ongoing ******\n");
					SESSION_REF(cur_session);
				} else {
					debug_warning (" ****** SESSION_FMRADIO start ******\n");
					ret = MMSoundMgrSessionSetSession(SESSION_FMRADIO, SESSION_START);
					if (ret) {
						goto ERROR_CASE;
					}
					SESSION_REF_INIT();
					SESSION_REF(cur_session);
				}
				break;
			case ASM_EVENT_EXCLUSIVE_MMPLAYER:
				/* pre-listening mode */
				if ( rcv_resource & ASM_RESOURCE_EAR_SPK_PATH ) {
#ifndef TIZEN_MICRO
					if (cur_session == SESSION_ALARM) {
						debug_warning (" ****** SESSION_ALARM ongoing ******\n");
						SESSION_REF(cur_session);
					} else {
						debug_warning (" ****** SESSION_ALARM for pre-listening mode start ******\n");
						ret = MMSoundMgrSessionSetSession(SESSION_ALARM, SESSION_START);
						if (ret) {
							goto ERROR_CASE;
						}
						SESSION_REF_INIT();
						SESSION_REF(cur_session);
					}
				} else {
					if (cur_session != SESSION_MEDIA) {
						if (cur_session == SESSION_VOICE_RECOGNITION) {
							/* Media and Voice Recognition can be mixed */
							debug_warning (" SESSION_VOICE_RECOGNITION exception case, no change\n");
						} else {
							goto RECOVERY_CASE;
						}
					}
#endif
				}
				break;
			case ASM_EVENT_MMCAMCORDER_AUDIO:
			case ASM_EVENT_MMCAMCORDER_VIDEO:
				if (cur_session != SESSION_MEDIA) {
					if (cur_session == SESSION_VOICECALL || cur_session == SESSION_VIDEOCALL || cur_session == SESSION_VOIP) {
						debug_warning (" ****** Voice recording starts on Call/Voip session ******\n");
					} else if (cur_session == SESSION_MEDIA) {
						debug_warning (" ****** SESSION_MEDIA ongoing ******\n");
					} else {
						goto RECOVERY_CASE;
					}
				}
				break;
#ifdef WILL_BE_DEPRECATED
			case ASM_EVENT_EXCLUSIVE_MMCAMCORDER:
				if (cur_session != SESSION_MEDIA) {
					if (cur_session == SESSION_VOICECALL || cur_session == SESSION_VIDEOCALL || cur_session == SESSION_VOIP) {
						debug_warning (" ****** Voice recording starts on Call/Voip session ******\n");
					} else if (cur_session == SESSION_MEDIA) {
						debug_warning (" ****** SESSION_MEDIA ongoing ******\n");
					} else {
						goto RECOVERY_CASE;
					}
				}
				break;
#endif
			case ASM_EVENT_EARJACK_UNPLUG:
			case ASM_EVENT_CALL:
			case ASM_EVENT_RICH_CALL:
			case ASM_EVENT_VIDEOCALL:
			case ASM_EVENT_VOIP:
			case ASM_EVENT_EXCLUSIVE_RESOURCE:
				break;
			case ASM_EVENT_VOICE_RECOGNITION:
				if (cur_session == SESSION_VOICE_RECOGNITION) {
					debug_warning (" ****** SESSION_VOICE_RECOGNITION ongoing ******\n");
					SESSION_REF(cur_session);
				} else {
					debug_warning (" ****** SESSION_VOICE_RECOGNITION start ******\n");
					ret = MMSoundMgrSessionSetSession(SESSION_VOICE_RECOGNITION, SESSION_START);
					if (ret) {
						goto ERROR_CASE;
					}
					SESSION_REF_INIT();
					SESSION_REF(cur_session);
				}
				break;

			default:
				/* recovery path for normal ASM event type */
				if (cur_session != SESSION_MEDIA) {
					if (cur_session == SESSION_VOICE_RECOGNITION) {
						/* Media and Voice Recognition can be mixed */
						debug_warning (" SESSION_VOICE_RECOGNITION exception case, no change\n");
					} else if (cur_session == SESSION_VOICECALL || cur_session == SESSION_VIDEOCALL || cur_session == SESSION_VOIP) {
						debug_warning (" SESSION_VOICECALL/VIDEOCALL/VOIP exception case, no change\n");
					} else {
						goto RECOVERY_CASE;
					}
				}
				break;
			}
		} else if (rcv_request == ASM_REQUEST_SET_SUBEVENT) {

			switch (rcv_sound_event) {
			case ASM_SUB_EVENT_SHARE:
				/* wakeup mode */
				if (cur_session == SESSION_VOICE_RECOGNITION) {
					subsession_t subsession = 0;
					MMSoundMgrSessionGetSubSession(&subsession);
					if (subsession == SUBSESSION_VR_NORMAL || subsession == SUBSESSION_VR_DRIVE) {
						if (rcv_resource == ASM_RESOURCE_SVR) {
							MMSoundMgrSessionSetSubSession(subsession, MM_SUBSESSION_OPTION_SVOICE | MM_SUBSESSION_OPTION_PRIV_SVOICE_WAKEUP);
						}
					}
				}
				break;
			case ASM_SUB_EVENT_EXCLUSIVE:
				/* command mode */
				if (cur_session == SESSION_VOICE_RECOGNITION) {
					subsession_t subsession = 0;
					MMSoundMgrSessionGetSubSession(&subsession);
					if (subsession == SUBSESSION_VR_NORMAL || subsession == SUBSESSION_VR_DRIVE) {
						if (rcv_resource == ASM_RESOURCE_SVR) {
							MMSoundMgrSessionSetSubSession(subsession, MM_SUBSESSION_OPTION_SVOICE | MM_SUBSESSION_OPTION_PRIV_SVOICE_COMMAND);
						}
					}
				}
				break;
			default:
				/* recovery path for normal ASM event type */
				if (cur_session != SESSION_MEDIA) {
					goto RECOVERY_CASE;
				}
				break;
			}
		}
	} else {
	/* change session setting for recovery */
		if (rcv_request == ASM_REQUEST_UNREGISTER) {

			switch (rcv_sound_event) {
			case ASM_EVENT_CALL:
			case ASM_EVENT_RICH_CALL:
				debug_warning (" ****** SESSION_VOICECALL end ******\n");
				ret = MMSoundMgrSessionSetSession(SESSION_VOICECALL, SESSION_END);
				if (ret) {
					goto ERROR_CASE;
				}
				break;
			case ASM_EVENT_VIDEOCALL:
				debug_warning (" ****** SESSION_VIDEOCALL end ******\n");
				ret = MMSoundMgrSessionSetSession(SESSION_VIDEOCALL, SESSION_END);
				if (ret) {
					goto ERROR_CASE;
				}
#ifndef TIZEN_MICRO
				*need_to_resume = true;
#endif
				break;
			case ASM_EVENT_VOIP:
				debug_warning (" ****** SESSION_VOIP end ******\n");
				ret = MMSoundMgrSessionSetSession(SESSION_VOIP, SESSION_END);
				if (ret) {
					goto ERROR_CASE;
				}
#ifndef TIZEN_MICRO
				*need_to_resume = true;
#endif
				break;
			case ASM_EVENT_NOTIFY:
				*need_to_resume = true;
				break;
			case ASM_EVENT_ALARM:
				*need_to_resume = true;
				break;
			case ASM_EVENT_VOICE_RECOGNITION:
				if (cur_session == SESSION_VOICE_RECOGNITION) {
					SESSION_UNREF(cur_session);
					if (IS_ON_GOING_SESSION()) {
						debug_warning (" ****** KEEP GOING SESSION_VOICE_RECOGNITION ****** current ref.count(%d)\n", g_session_ref_count);
					} else {
						debug_warning (" ****** SESSION_VOICE_RECOGNITION end ******\n");
						ret = MMSoundMgrSessionSetSession(SESSION_VOICE_RECOGNITION, SESSION_END);
						if (ret) {
							goto ERROR_CASE;
						}
					}
				}
				*need_to_resume = true;
				break;
			case ASM_EVENT_MMCAMCORDER_AUDIO:
			case ASM_EVENT_MMCAMCORDER_VIDEO:
				if (cur_session == SESSION_MEDIA) {
					debug_warning (" ****** SESSION_MEDIA for MMCAMCORDER end ******\n");
					ret = MMSoundMgrSessionSetSession(SESSION_MEDIA, SESSION_END);
					if (ret) {
						goto ERROR_CASE;
					}
				}
				break;
			case ASM_EVENT_EXCLUSIVE_RESOURCE:
				if ( rcv_resource & ASM_RESOURCE_VOICECONTROL ) {
					debug_warning (" ****** ASM_RESOURCE_VOICECONTROL END ******\n");
					MMSoundMgrSessionSetVoiceControlState(false);
				}
				break;
#ifdef TIZEN_MICRO
			case  ASM_EVENT_EXCLUSIVE_MMPLAYER:
				if ( rcv_resource & ASM_RESOURCE_EAR_SPK_PATH || /* for pre-listening mode */
					rcv_resource & ASM_RESOURCE_RESUMPTION_TAG) {
					if (cur_session == SESSION_ALARM) {
						debug_warning (" ****** SESSION_ALARM for pre-listening mode end ******\n");
						ret = MMSoundMgrSessionSetSession(SESSION_ALARM, SESSION_END);
						if (ret) {
							goto ERROR_CASE;
						}
					} else {
						debug_warning (" ****** Not expected case : current session was [%d] ******\n", cur_session);
					}
					*need_to_resume = true;
				}
				break;
#endif
			default:
				break;
			}

		} else if (rcv_request == ASM_REQUEST_SETSTATE) {

			if (rcv_sound_state == ASM_STATE_STOP) {
				switch (rcv_sound_event) {
				case ASM_EVENT_NOTIFY:
					if (cur_session == SESSION_NOTIFICATION) {
						SESSION_UNREF(cur_session);
						if (IS_ON_GOING_SESSION()) {
							debug_warning (" ****** KEEP GOING SESSION_NOTIFICATION ****** current ref.count(%d)\n", g_session_ref_count);
						} else {
							debug_warning (" ****** SESSION_NOTIFICATON end ******\n");
							ret = MMSoundMgrSessionSetSession(SESSION_NOTIFICATION, SESSION_END);
							if (ret) {
								goto ERROR_CASE;
							}
						}
					} else if (cur_session == SESSION_ALARM) {
						SESSION_UNREF(cur_session);
						if (IS_ON_GOING_SESSION()) {
							debug_warning (" ****** KEEP GOING SESSION_ALARM ****** current ref.count(%d)\n", g_session_ref_count);
						} else {
							debug_warning (" ****** SESSION_ALARM end ******\n");
							ret = MMSoundMgrSessionSetSession(SESSION_ALARM, SESSION_END);
							if (ret) {
								goto ERROR_CASE;
							}
						}
					} else {
						debug_warning (" ****** No need to end session : current session was [%d] ******\n", cur_session);
					}
					*need_to_resume = true;
					break;
				case ASM_EVENT_ALARM:
					if (cur_session == SESSION_ALARM) {
						SESSION_UNREF(cur_session);
						if (IS_ON_GOING_SESSION()) {
							debug_warning (" ****** KEEP GOING SESSION_ALARM ****** current ref.count(%d)\n", g_session_ref_count);
						} else {
							debug_warning (" ****** SESSION_ALARM end ******\n");
							ret = MMSoundMgrSessionSetSession(SESSION_ALARM, SESSION_END);
							if (ret) {
								goto ERROR_CASE;
							}
						}
					} else if (cur_session == SESSION_VOICECALL || cur_session == SESSION_VIDEOCALL || cur_session == SESSION_VOIP) {
						debug_warning (" ****** Alarm ends on Call/Voip session ******\n");
					} else {
						debug_warning (" ****** Not expected case : current session was [%d] ******\n", cur_session);
					}
					*need_to_resume = true;
					break;
				case ASM_EVENT_EMERGENCY:
					if (cur_session == SESSION_EMERGENCY) {
						SESSION_UNREF(cur_session);
						if (IS_ON_GOING_SESSION()) {
							debug_warning (" ****** KEEP GOING SESSION_EMERGENCY ****** current ref.count(%d)\n", g_session_ref_count);
						} else {
							debug_warning (" ****** SESSION_EMERGENCY end ******\n");
							ret = MMSoundMgrSessionSetSession(SESSION_EMERGENCY, SESSION_END);
							if (ret) {
								goto ERROR_CASE;
							}
						}
					} else {
						debug_warning (" ****** Not expected case : current session was [%d] ******\n", cur_session);
					}
					*need_to_resume = true;
					break;
				case ASM_EVENT_SHARE_FMRADIO:
				case ASM_EVENT_EXCLUSIVE_FMRADIO:
					if (cur_session == SESSION_FMRADIO) {
						SESSION_UNREF(cur_session);
						if (IS_ON_GOING_SESSION()) {
							debug_warning (" ****** KEEP GOING SESSION_FMRADIO ****** current ref.count(%d)\n", g_session_ref_count);
						} else {
							debug_warning (" ****** SESSION_FMRADIO end ******");
							ret = MMSoundMgrSessionSetSession(SESSION_FMRADIO, SESSION_END);
							if (ret) {
								goto ERROR_CASE;
							}
						}
					} else {
						debug_warning (" Session is not SESSION_FMRADIO");
					}
					break;
				case  ASM_EVENT_EXCLUSIVE_MMPLAYER:
					if ( rcv_resource & ASM_RESOURCE_EAR_SPK_PATH || /* for pre-listening mode */
						rcv_resource & ASM_RESOURCE_RESUMPTION_TAG) {
#ifndef TIZEN_MICRO
						if (cur_session == SESSION_ALARM) {
							debug_warning (" ****** SESSION_ALARM for pre-listening mode end ******\n");
							ret = MMSoundMgrSessionSetSession(SESSION_ALARM, SESSION_END);
							if (ret) {
								goto ERROR_CASE;
							}
						} else {
							debug_warning (" ****** Not expected case : current session was [%d] ******\n", cur_session);
						}
						*need_to_resume = true;
#endif
					}
					break;
				case ASM_EVENT_VOICE_RECOGNITION:
					if (cur_session == SESSION_VOICE_RECOGNITION) {
						SESSION_UNREF(cur_session);
						if (IS_ON_GOING_SESSION()) {
							debug_warning (" ****** KEEP GOING SESSION_VOICE_RECOGNITION ****** current ref.count(%d)\n", g_session_ref_count);
						} else {
							debug_warning (" ****** SESSION_VOICE_RECOGNITION end ******\n");
							ret = MMSoundMgrSessionSetSession(SESSION_VOICE_RECOGNITION, SESSION_END);
							if (ret) {
								goto ERROR_CASE;
							}
						}
					} else {
						debug_warning (" ****** Not expected case : current session was [%d] ******\n", cur_session);
					}
					break;
				default:
					break;
				}
			} else if (rcv_sound_state == ASM_STATE_PAUSE)  {
				if (cur_session == SESSION_NOTIFICATION ||
					cur_session == SESSION_ALARM ||
					cur_session == SESSION_VOICE_RECOGNITION) {
					if (IS_ON_GOING_SESSION()) {
						SESSION_UNREF(cur_session);
					}
				}
			}
		}
		debug_warning (" need_to_resume[%d]", *need_to_resume);
	}

	MMSoundMgrSessionGetSession(&cur_session);
	debug_msg (" cur_session[%d] : leave", cur_session);

	return ret;

RECOVERY_CASE:
	debug_warning (" ****** recovery case : current session(%d) end ******\n", cur_session);
	ret = MMSoundMgrSessionSetSession(cur_session, SESSION_END);
	if (ret) {
		goto ERROR_CASE;
	}
	SESSION_REF_INIT();
	return ret;

ERROR_CASE:
	debug_error (" failed to MMSoundMgrSessionSetSession()");
	return ret;
}


int __asm_process_message (void *rcv_msg, void *ret_msg)
{
	int rcv_instance_id;
	ASM_requests_t rcv_request_id;
	ASM_sound_events_t rcv_sound_event;
	ASM_sound_states_t rcv_sound_state;
	ASM_resource_t rcv_resource;
	int rcv_sound_handle;
	ASM_msg_asm_to_lib_t asm_snd_msg;
	ASM_msg_lib_to_asm_t *asm_rcv_msg = (ASM_msg_lib_to_asm_t *)rcv_msg;
	ASM_msg_asm_to_lib_t *asm_ret_msg = (ASM_msg_asm_to_lib_t *)ret_msg;
	int ret = MM_ERROR_NONE;
	asm_instance_list_t *asm_instance_h = NULL;

	pthread_mutex_lock(&g_mutex_asm);
	debug_log (" ===================================================================== Started!!! (LOCKED) ");

	rcv_instance_id = asm_rcv_msg->instance_id;
	rcv_sound_handle = asm_rcv_msg->data.handle;
	rcv_request_id = asm_rcv_msg->data.request_id;
	rcv_sound_event = asm_rcv_msg->data.sound_event;
	rcv_sound_state = asm_rcv_msg->data.sound_state;
	rcv_resource = asm_rcv_msg->data.system_resource;

	/*******************************************************************/
	debug_warning(" received msg (tid=%d,handle=%d,req=%d[%s],event=%d,state=%d[%s],resource=0x%x)\n",
			rcv_instance_id, rcv_sound_handle, rcv_request_id, ASM_sound_request_str[rcv_request_id],
			rcv_sound_event, rcv_sound_state, ASM_sound_state_str[rcv_sound_state], rcv_resource);
	if (rcv_request_id != ASM_REQUEST_EMERGENT_EXIT) {
		if (rcv_request_id == ASM_REQUEST_SET_SUBSESSION) {
			debug_warning("     sub-session : %s\n", subsession_str[rcv_sound_event]);
		} else if (rcv_request_id == ASM_REQUEST_SET_SUBEVENT) {
			debug_warning("     sub-event : %s\n", subevent_str[rcv_sound_event]);
		} else if (rcv_request_id == ASM_REQUEST_GET_SUBSESSION || rcv_request_id == ASM_REQUEST_GET_SUBEVENT) {
			/* no log */
		} else {
			debug_warning("     sound_event : %s\n", ASM_sound_event_str[rcv_sound_event]);
		}
	}
	/*******************************************************************/

	memset(&asm_snd_msg, 0, sizeof(ASM_msg_asm_to_lib_t));

	debug_log (" +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++ ");

	switch (rcv_request_id) {
	case ASM_REQUEST_REGISTER:
#ifdef USE_SECURITY
		/* do security check */
		if (__asm_check_check_privilege(asm_rcv_msg->data.cookie) == 0) {
			debug_error ("[Security] __asm_check_check_privilege() failed....\n");
			ASM_SND_MSG_SET_DEFAULT(asm_snd_msg, rcv_instance_id, ASM_HANDLE_INIT_VAL, ASM_HANDLE_INIT_VAL, rcv_request_id);
			asm_snd_msg.data.check_privilege = 0;
			if (asm_ret_msg == NULL) {
				__asm_snd_message(&asm_snd_msg);
			}
			break;
		}
		debug_log ("[Security] __asm_check_check_privilege() success\n");
		asm_snd_msg.data.check_privilege = 1;
#endif /* USE_SECURITY */
		__check_dead_process();

		__asm_get_empty_handle(rcv_instance_id, &rcv_sound_handle);
		if (rcv_sound_handle == ASM_HANDLE_INIT_VAL) {
			ASM_SND_MSG_SET_DEFAULT(asm_snd_msg, rcv_instance_id, ASM_HANDLE_INIT_VAL, ASM_HANDLE_INIT_VAL, rcv_request_id);
			if (asm_ret_msg == NULL) {
				__asm_snd_message(&asm_snd_msg);
			}
		} else {
			asm_compare_result_t compare_result = __asm_compare_priority_matrix(&asm_snd_msg, asm_ret_msg,
									rcv_instance_id, rcv_sound_handle, rcv_request_id, rcv_sound_event, rcv_sound_state, rcv_resource);

			if (asm_snd_msg.data.result_sound_command != ASM_COMMAND_STOP) {
				/* do not execute below when the result is ASM_COMMAND_STOP */
				ret = __asm_change_session (rcv_request_id, rcv_sound_event, rcv_sound_state, rcv_resource, false, NULL);
				if (ret) {
					debug_error (" failed to __asm_change_session(), error(0x%x)", ret);
				}
			}
			ASM_SND_MSG_SET_DEFAULT(asm_snd_msg, rcv_instance_id, rcv_sound_handle, rcv_sound_handle, rcv_request_id);
			if (asm_ret_msg == NULL) {
				__asm_snd_message(&asm_snd_msg);
			}
			ASM_DO_WATCH_CALLBACK_FROM_RESULT(compare_result);
		}

#ifdef USE_IO_PRIORITY
		if ( rcv_resource & ASM_RESOURCE_IOPRIORITY ) {
			_proc_set_io_pri(rcv_instance_id);
		}
#endif /* USE_IO_PRIORITY */
		break;

	case ASM_REQUEST_UNREGISTER:
		asm_instance_h = __asm_find_list(rcv_instance_id, rcv_sound_handle);

		bool need_to_resume = false;
		ret = __asm_change_session (rcv_request_id, rcv_sound_event, rcv_sound_state,
								(asm_instance_h)? asm_instance_h->mm_resource : rcv_resource,
								true, &need_to_resume);

		/* enforce calling watch callback when current state of rcv_instance_id is still ASM_STATE_PLAYING */
		/* need to call the watch callback of Call event(STOP) because Call session is changed in ASM_REQUEST_UNREGISTER */
		if (__asm_find_state_of_handle(rcv_instance_id, rcv_sound_handle) == ASM_STATE_PLAYING ||
				rcv_sound_event == ASM_EVENT_CALL) {
			ASM_DO_WATCH_CALLBACK(rcv_sound_event, ASM_STATE_STOP);
		}
		__asm_unregister_list(rcv_sound_handle);

		if (ret) {
			debug_error (" failed to __asm_change_session(), error(0x%x)", ret);
		} else {
			if (need_to_resume) {
				asm_paused_by_id_t paused_by_id;
				paused_by_id.pid = rcv_instance_id;
				paused_by_id.sound_handle = rcv_sound_handle;
				__asm_do_all_resume_callback(paused_by_id);
			}
		}
		break;

	case ASM_REQUEST_SETSTATE:
		__check_dead_process();
		ASM_SND_MSG_SET_DEFAULT(asm_snd_msg, rcv_instance_id, rcv_sound_handle, rcv_sound_handle, rcv_request_id);
		if (rcv_sound_state == ASM_STATE_PLAYING || rcv_sound_state == ASM_STATE_WAITING) {
			if ( __is_it_playing_now(rcv_instance_id, rcv_sound_handle)) {
				__asm_change_state_list(rcv_instance_id, rcv_sound_handle, rcv_sound_state, rcv_resource);
				asm_snd_msg.data.result_sound_command = ASM_COMMAND_NONE;
				asm_snd_msg.data.result_sound_state = rcv_sound_state;
				if (asm_ret_msg == NULL) {
					__asm_snd_message(&asm_snd_msg);
				}
			} else {
				__reset_resume_check(rcv_instance_id, ASM_HANDLE_INIT_VAL);
				asm_compare_result_t compare_result = __asm_compare_priority_matrix(&asm_snd_msg, asm_ret_msg,
										rcv_instance_id, rcv_sound_handle, rcv_request_id, rcv_sound_event, rcv_sound_state, rcv_resource);

				if (asm_snd_msg.data.result_sound_command == ASM_COMMAND_PLAY ||
					asm_snd_msg.data.result_sound_command == ASM_COMMAND_NONE) {

					ret = __asm_change_session (rcv_request_id, rcv_sound_event, rcv_sound_state, rcv_resource, false, NULL);
					if (ret) {
						debug_error (" failed to __asm_change_session(), error(0x%x)", ret);
					}
				}
				if (asm_ret_msg == NULL) {
					__asm_snd_message(&asm_snd_msg);
				}

				ASM_DO_WATCH_CALLBACK_FROM_RESULT(compare_result);
			}
			__temp_print_list("Set State (Play)");
		} else {
			asm_instance_h = __asm_change_state_list(rcv_instance_id, rcv_sound_handle, rcv_sound_state, rcv_resource);
			if(asm_instance_h) {
				bool need_to_resume = false;
				if (asm_instance_h->prev_sound_state == ASM_STATE_PLAYING) {
					if (rcv_sound_state == ASM_STATE_STOP || rcv_sound_state == ASM_STATE_PAUSE) {
						ret = __asm_change_session (rcv_request_id, rcv_sound_event, rcv_sound_state, rcv_resource, true, &need_to_resume);
						if (ret) {
							debug_error (" failed to __asm_change_session(), error(0x%x)", ret);
						} else {
							if (need_to_resume) {
								asm_paused_by_id_t paused_by_id;
								paused_by_id.pid = rcv_instance_id;
								paused_by_id.sound_handle = rcv_sound_handle;
								__asm_do_all_resume_callback(paused_by_id);
							}
						}
						/* Call session is changed in ASM_REQUEST_UNREGISTER */
						if (rcv_sound_event != ASM_EVENT_CALL) { // exception case for the watch callback(STOP)
							ASM_DO_WATCH_CALLBACK(rcv_sound_event, rcv_sound_state);
						}
					}
				} else if (asm_instance_h->prev_sound_state == ASM_STATE_PAUSE) {
					if (rcv_sound_state == ASM_STATE_STOP) {
						ret = __asm_change_session (rcv_request_id, rcv_sound_event, rcv_sound_state, rcv_resource, true, &need_to_resume);
						if (ret) {
							debug_error (" failed to __asm_change_session(), error(0x%x)", ret);
						} else {
							if (need_to_resume) {
								asm_paused_by_id_t paused_by_id;
								paused_by_id.pid = rcv_instance_id;
								paused_by_id.sound_handle = rcv_sound_handle;
								__asm_do_all_resume_callback(paused_by_id);
							}
						}
						/* Call session is changed in ASM_REQUEST_UNREGISTER */
						if (rcv_sound_event != ASM_EVENT_CALL) { // exception case for the watch callback(STOP)
							ASM_DO_WATCH_CALLBACK(rcv_sound_event, rcv_sound_state);
						}
					}
				}
			}
			__temp_print_list("Set State (Not Play)");
		}
		break;

	case ASM_REQUEST_GETSTATE:
	{
		asm_instance_list_t *asm_instance_h = NULL;
		asm_instance_h = __asm_find_list(rcv_instance_id, rcv_sound_handle);
		if (asm_instance_h) {
			asm_snd_msg.data.result_sound_state = asm_instance_h->sound_state;
			asm_snd_msg.data.source_request_id = rcv_request_id;
		}
		ASM_SND_MSG_SET_DEFAULT(asm_snd_msg, rcv_instance_id, rcv_sound_handle, rcv_sound_handle, rcv_request_id);
		if (asm_ret_msg == NULL) {
			__asm_snd_message(&asm_snd_msg);
		}
		break;
	}

	case ASM_REQUEST_GETMYSTATE:
		__check_dead_process();
		ASM_SND_MSG_SET_DEFAULT(asm_snd_msg, rcv_instance_id, rcv_sound_handle, rcv_sound_handle, rcv_request_id);
		asm_snd_msg.data.result_sound_state = __asm_find_process_status(rcv_instance_id);
		if (asm_ret_msg == NULL) {
			__asm_snd_message(&asm_snd_msg);
		}
		break;

	case ASM_REQUEST_DUMP:
		__temp_print_list("DUMP");
		break;

	case ASM_REQUEST_SET_SUBSESSION:
		{
			subsession_t rcv_subsession = rcv_sound_event; //latest rcv_subsession is needed
			bool do_set_subsession = true;

			if (rcv_subsession < SUBSESSION_VOICE || rcv_subsession >= SUBSESSION_NUM) {
				debug_error (" Invalid subsession [%d] to set\n", rcv_subsession);
				break;
			}

			debug_warning (" ****** SUB-SESSION [%s] ******\n", subsession_str[rcv_subsession]);

			ASM_sound_events_t sound_event = __asm_find_event_of_handle(rcv_instance_id, rcv_sound_handle);
			switch (sound_event) {
			case ASM_EVENT_CALL:
			case ASM_EVENT_VIDEOCALL:
			case ASM_EVENT_VOIP:
			case ASM_EVENT_RICH_CALL:
				break;
			case ASM_EVENT_VOICE_RECOGNITION:
			{
				int ret = 0;
				int session_order = -1;

				session_t cur_session;
				MMSoundMgrSessionGetSession(&cur_session);
				debug_warning (" cur_session[%d] (0:MEDIA 1:VC 2:VT 3:VOIP 4:FM 5:NOTI 6:ALARM 7:EMER 8:VR)\n",cur_session);
				if (cur_session == SESSION_VOICECALL ||
					cur_session == SESSION_VIDEOCALL ||
					cur_session == SESSION_VOIP ) {
					debug_warning (" skip REQUEST_SET_SUBSESSION\n");
					do_set_subsession = false;
					break;
				}
				if (cur_session != SESSION_VOICE_RECOGNITION &&
					(rcv_subsession == SUBSESSION_VR_NORMAL || rcv_subsession == SUBSESSION_VR_DRIVE)) {
					/* if current session is not for VR, so we need to set it first */
					debug_warning (" ****** SESSION_VOICE_RECOGNITION start in REQUEST_SET_SUBSESSION ******\n");
					session_order = SESSION_START;
				} else	if (cur_session == SESSION_VOICE_RECOGNITION && rcv_subsession == SUBSESSION_INIT) {
					/* if current session is for VR, but user request set sub-session to NONE, so we need to end it */
					debug_warning (" ****** SESSION_VOICE_RECOGNITION end in REQUEST_SET_SUBSESSION ******\n");
					session_order = SESSION_END;
				}
				if (session_order != -1) {
					ret = MMSoundMgrSessionSetSession(SESSION_VOICE_RECOGNITION, session_order);
					if (ret) {
						debug_error (" failed to MMSoundMgrSessionSetSession() for VOICE_RECOGNITION\n");
						do_set_subsession = false;
					} else {
						if (session_order == SESSION_START) {
							SESSION_REF_INIT();
							SESSION_REF(SESSION_VOICE_RECOGNITION);
						} else {
							SESSION_UNREF(SESSION_VOICE_RECOGNITION);
						}
					}
				}
				MMSoundMgrSessionGetSession(&cur_session);
				debug_msg (" cur_session[%d] : leave", cur_session);
				break;
			}
			case ASM_EVENT_MMCAMCORDER_AUDIO:
			case ASM_EVENT_MMCAMCORDER_VIDEO:
			{
				int ret = 0;
				int session_order = -1;
				session_t cur_session;
				MMSoundMgrSessionGetSession(&cur_session);
				debug_warning (" cur_session[%d] (0:MEDIA 1:VC 2:VT 3:VOIP 4:FM 5:NOTI 6:ALARM 7:EMER 8:VR)\n",cur_session);
				if (cur_session == SESSION_VOICECALL ||
					cur_session == SESSION_VIDEOCALL ||
					cur_session == SESSION_VOIP ) {
					debug_warning (" skip REQUEST_SET_SUBSESSION\n");
					do_set_subsession = false;
					break;
				}
				if (cur_session != SESSION_MEDIA) {
					/* if current session is not for MMCAMCOARDER_AUDIO/VIDEO, so we need to set it first */
					debug_warning (" ****** SESSION_MEDIA start in REQUEST_SET_SUBSESSION ******\n");
					session_order = SESSION_START;
				} else	if (cur_session == SESSION_MEDIA && rcv_subsession == SUBSESSION_INIT) {
					/* if current session is for MMCAMCORDRE_AUDIO/VIDEO, but user request set sub-session to NONE, so we need to end it */
					debug_warning (" ****** SESSION_MEDIA end(initialization) in REQUEST_SET_SUBSESSION ******\n");
					session_order = SESSION_END;
				}
				if (session_order != -1) {
					ret = MMSoundMgrSessionSetSession(SESSION_MEDIA, session_order);
					if (ret) {
						debug_error (" failed to MMSoundMgrSessionSetSession() for MMCAMCORDER_AUDIO/VIDEO\n");
						do_set_subsession = false;
					}
				}
				MMSoundMgrSessionGetSession(&cur_session);
				debug_msg (" cur_session[%d] : leave", cur_session);

				break;
			}
			default:
				if (sound_event == ASM_EVENT_NONE) {
					debug_error (" Could not find event type of the handle(%d)\n", rcv_sound_handle);
				} else {
					debug_error (" Not supported ASM Event(%s) for this SUBSESSION(%s)\n", ASM_sound_event_str[sound_event], subsession_str[rcv_subsession]);
				}
				do_set_subsession = false;
				break;
			}

			if (do_set_subsession) {
				int result_resource = ASM_RESOURCE_NONE;
				ASM_sound_events_t sound_event = __asm_find_event_of_handle(rcv_instance_id, rcv_sound_handle);

				/* resource converting for sub-session */
				asm_instance_h = __asm_find_list(rcv_instance_id, rcv_sound_handle);
				if (asm_instance_h) {
					switch (sound_event) {
					case ASM_EVENT_VOICE_RECOGNITION:
#ifdef ASM_FOR_PRODUCT
						switch (rcv_resource) {
						case MM_SUBSESSION_OPTION_SVOICE:
							asm_instance_h->option.pid = rcv_instance_id;
							asm_instance_h->option.resource = ASM_RESOURCE_SVR;
							rcv_resource = ASM_RESOURCE_SVR;
							result_resource = MM_SUBSESSION_OPTION_NONE;
							break;
						default:
							asm_instance_h->option.resource = rcv_resource;
							break;
						}
#endif
						break;
					default:
						/* set resource of asm handle to rcv_resource forcedly */
						result_resource = rcv_resource = asm_instance_h->mm_resource;
						break;
					}
				}

				ret = MMSoundMgrSessionSetSubSession(rcv_subsession, result_resource);
				if (ret != MM_ERROR_NONE) {
					/* TODO : Error Handling */
					debug_error (" MMSoundMgrSessionSetSubSession failed....ret = [%x]\n", ret);
				} else {
					g_previous_subsession = rcv_subsession;
				}

				__asm_change_state_list(rcv_instance_id, rcv_sound_handle, rcv_sound_state, rcv_resource);
				__temp_print_list("Set Sub-session");
			}

			/* Return result msg */
			ASM_SND_MSG_SET_DEFAULT(asm_snd_msg, rcv_instance_id, rcv_sound_handle, rcv_sound_handle, rcv_request_id);
			if (asm_ret_msg == NULL) {
				__asm_snd_message(&asm_snd_msg);
			}
		}
		break;

	case ASM_REQUEST_GET_SUBSESSION:
		{
			subsession_t subsession = 0;

			/* FIXME: have to check only call instance with playing stsate can request this */
			debug_warning (" ****** GET SUB-SESSION ******\n");
			ret = MMSoundMgrSessionGetSubSession(&subsession);
			if (ret != MM_ERROR_NONE) {
				/* TODO : Error Handling */
				debug_error (" MMSoundMgrSessionGetSubSession failed....ret = [%x]\n", ret);
			}

			/* Return result msg */
			asm_snd_msg.data.result_sound_command = subsession;
			ASM_SND_MSG_SET_DEFAULT(asm_snd_msg, rcv_instance_id, rcv_sound_handle, rcv_sound_handle, rcv_request_id);
			if (asm_ret_msg == NULL) {
				__asm_snd_message(&asm_snd_msg);
			}
		}
		break;

	case ASM_REQUEST_SET_SUBEVENT:
	{
		ASM_sound_sub_events_t rcv_subevent = rcv_sound_event; //latest rcv_subsession is needed
		ASM_sound_events_t sound_event = ASM_EVENT_NONE;
		if (rcv_subevent < ASM_SUB_EVENT_NONE || rcv_subevent >= ASM_SUB_EVENT_MAX) {
			debug_error (" Invalid sub-event type[%d] to set\n", rcv_subevent);
		}
		sound_event = __asm_find_event_of_handle(rcv_instance_id, rcv_sound_handle);

		/* get previous sub-session option */
		asm_instance_list_t *asm_instance_h = NULL;
		asm_instance_h = __asm_find_list(rcv_instance_id, rcv_sound_handle);
		if (asm_instance_h) {
			switch (sound_event) {
			case ASM_EVENT_VOICE_RECOGNITION:
#ifdef ASM_FOR_PRODUCT
				if (asm_instance_h->option.pid == rcv_instance_id) {
					rcv_resource = asm_instance_h->option.resource;
				}
#endif
				break;
			default:
				/* set resource of asm handle to rcv_resource forcedly */
				rcv_resource = asm_instance_h->mm_resource;
				break;
			}
		}
		debug_warning (" ****** SUB-EVENT [%s] ******\n", ASM_sound_sub_event_str[rcv_subevent]);
		__asm_change_sub_event_list(rcv_instance_id, rcv_sound_handle, rcv_subevent);

		ASM_SND_MSG_SET_DEFAULT(asm_snd_msg, rcv_instance_id, rcv_sound_handle, rcv_sound_handle, rcv_request_id);

		if (rcv_subevent == ASM_SUB_EVENT_NONE) {
			/* NOTE: special case, resume paused list here */
			asm_paused_by_id_t paused_by_id;
			paused_by_id.pid = rcv_instance_id;
			paused_by_id.sound_handle = rcv_sound_handle;
			__asm_do_all_resume_callback(paused_by_id);

			__asm_change_state_list(rcv_instance_id, rcv_sound_handle, rcv_sound_state, rcv_resource);

		} else {
			__asm_compare_priority_matrix(&asm_snd_msg, asm_ret_msg,
										rcv_instance_id, rcv_sound_handle, rcv_request_id, rcv_subevent-1, rcv_sound_state, rcv_resource);

			if (asm_snd_msg.data.result_sound_command == ASM_COMMAND_PLAY ||
				asm_snd_msg.data.result_sound_command == ASM_COMMAND_NONE) {

				ret = __asm_change_session (rcv_request_id, rcv_subevent, rcv_sound_state, rcv_resource, false, NULL);
				if (ret) {
					debug_error (" failed to __asm_change_session(), error(0x%x)", ret);
				}
			}
			if (asm_ret_msg == NULL) {
				__asm_snd_message(&asm_snd_msg);
			}
		}

		__temp_print_list("Set Sub-event");
		break;
	}

	case ASM_REQUEST_GET_SUBEVENT:
		ASM_SND_MSG_SET_DEFAULT(asm_snd_msg, rcv_instance_id, rcv_sound_handle, rcv_sound_handle, rcv_request_id);
		/* Return result msg */
		asm_instance_h = __asm_find_list(rcv_instance_id, rcv_sound_handle);
		asm_snd_msg.data.result_sound_command = (asm_instance_h ? asm_instance_h->sound_sub_event : ASM_SUB_EVENT_NONE);
		if (asm_ret_msg == NULL) {
			__asm_snd_message(&asm_snd_msg);
		}
		break;

	case ASM_REQUEST_EMERGENT_EXIT:
	{
		asm_instance_h = __asm_find_list(rcv_instance_id, rcv_sound_handle);
		bool need_to_resume = false;

		/* change session forcedly */
		if (asm_instance_h && (asm_instance_h->sound_state == ASM_STATE_PLAYING || asm_instance_h->sound_state == ASM_STATE_PAUSE)) {
			ret = __asm_change_session (ASM_REQUEST_SETSTATE, rcv_sound_event, ASM_STATE_STOP,
							(asm_instance_h)? asm_instance_h->mm_resource : rcv_resource,
							true, &need_to_resume);
			if (ret) {
				debug_error (" failed to __asm_change_session(), error(0x%x)", ret);
			} else {
				if (need_to_resume) {
					asm_paused_by_id_t paused_by_id;
					paused_by_id.pid = rcv_instance_id;
					paused_by_id.sound_handle = rcv_sound_handle;
					__asm_do_all_resume_callback(paused_by_id);
				}
			}
		}
		ret = __asm_change_session (ASM_REQUEST_UNREGISTER, rcv_sound_event, rcv_sound_state,
								(asm_instance_h)? asm_instance_h->mm_resource : rcv_resource,
								true, &need_to_resume);
		if (ret) {
			debug_error (" failed to __asm_change_session(), error(0x%x)", ret);
		} else {
			if (need_to_resume) {
				asm_paused_by_id_t paused_by_id;
				paused_by_id.pid = rcv_instance_id;
				paused_by_id.sound_handle = rcv_sound_handle;
				__asm_do_all_resume_callback(paused_by_id);
			}
		}
		__asm_unregister_list(rcv_sound_handle);
		__emergent_exit(rcv_instance_id);
	}
		break;

	case ASM_REQUEST_REGISTER_WATCHER:
		debug_log (" ****** REQUEST_REGISTER_WATCHER ******\n");
#ifdef USE_SECURITY
		/* do security check */
		if (__asm_check_check_privilege(asm_rcv_msg->data.cookie) == 0) {
			debug_error ("[Security] __asm_check_check_privilege() failed....\n");
			ASM_SND_MSG_SET_DEFAULT(asm_snd_msg, rcv_instance_id, ASM_HANDLE_INIT_VAL, ASM_HANDLE_INIT_VAL, rcv_request_id);
			asm_snd_msg.data.check_privilege = 0;
			if (asm_ret_msg == NULL) {
				__asm_snd_message(&asm_snd_msg);
			}
			break;
		}
		debug_log ("[Security] __asm_check_check_privilege() success\n");
		asm_snd_msg.data.check_privilege = 1;
#endif /* USE_SECURITY */
		__check_dead_process();

		/* check if it is redundant */
		if (__is_it_redundant_request(rcv_instance_id, rcv_sound_event, rcv_sound_state)) {
			debug_error (" it is redundant request for adding watch list...");
			return false;
		}

		__asm_get_empty_handle(rcv_instance_id, &rcv_sound_handle);
		if (rcv_sound_handle == ASM_HANDLE_INIT_VAL) {
			ASM_SND_MSG_SET_DEFAULT(asm_snd_msg, rcv_instance_id, ASM_HANDLE_INIT_VAL, ASM_HANDLE_INIT_VAL, rcv_request_id);
			if (asm_ret_msg == NULL) {
				__asm_snd_message(&asm_snd_msg);
			}
		} else {
			ASM_SND_MSG_SET_DEFAULT(asm_snd_msg, rcv_instance_id, rcv_sound_handle, rcv_sound_handle, rcv_request_id);
			asm_snd_msg.data.result_sound_command = ASM_COMMAND_PLAY;
			asm_snd_msg.data.result_sound_state = rcv_sound_state;
			if (asm_ret_msg == NULL) {
				__asm_snd_message(&asm_snd_msg);
			}
			__asm_register_list(rcv_instance_id, rcv_sound_handle, rcv_sound_event, rcv_sound_state, 0, true);
		}
		break;

	case ASM_REQUEST_UNREGISTER_WATCHER:
		debug_log (" ****** REQUEST_UNREGISTER_WATCHER ******\n");
		__asm_unregister_list(rcv_sound_handle);
		break;

	case ASM_REQUEST_RESET_RESUME_TAG:
	{
		int found_pid = -1;
		debug_log (" ****** REQUEST_RESET_RESUME_TAG ******\n");
		found_pid = __asm_find_pid_of_resume_tagged(rcv_sound_handle);
		if (found_pid == -1) {
			debug_warning (" could not find any pid tagged for resumption");
		} else {
			__reset_resume_check(found_pid, rcv_sound_handle);
		}
		ASM_SND_MSG_SET_DEFAULT(asm_snd_msg, rcv_instance_id, rcv_sound_handle, rcv_sound_handle, rcv_request_id);
		asm_snd_msg.data.result_sound_command = ASM_COMMAND_PLAY;
		asm_snd_msg.data.result_sound_state = rcv_sound_state;
		if (asm_ret_msg == NULL) {
			__asm_snd_message(&asm_snd_msg);
		}
		break;
	}

	default:
		break;
	}

	if (asm_ret_msg) {
		*asm_ret_msg = asm_snd_msg;
	}

	pthread_mutex_unlock(&g_mutex_asm);
	debug_log (" ===================================================================== End (UNLOCKED) ");

	return 0;
}

void __asm_main_run (void* param)
{
	ASM_msg_lib_to_asm_t asm_rcv_msg;

	signal(SIGPIPE, SIG_IGN);

	/* Init Msg Queue */
	__asm_create_message_queue();

	int temp_msgctl_id1 = msgctl(asm_snd_msgid, IPC_RMID, 0);
	int temp_msgctl_id2 = msgctl(asm_rcv_msgid, IPC_RMID, 0);
	int temp_msgctl_id3 = msgctl(asm_cb_msgid, IPC_RMID, 0);

	if (temp_msgctl_id1 == -1 || temp_msgctl_id2 == -1 || temp_msgctl_id3 == -1) {
		debug_error(" msgctl failed with error(%d,%s) \n", errno, strerror(errno));
		exit(EXIT_FAILURE);
	}
	//-------------------------------------------------------------------
	/*
		This is unnessasry finaly, but nessasary during implement.
	*/
	/* FIXME: Do we need to do this again ? */
	__asm_create_message_queue();

	/* Init List */
	head_list = (asm_instance_list_t *)malloc(sizeof(asm_instance_list_t));
	tail_list = (asm_instance_list_t *)malloc(sizeof(asm_instance_list_t));
	head_list->next = tail_list;
	tail_list->next = tail_list;


	/*
	 * Init Vconf
	 */
	if (vconf_set_int(SOUND_STATUS_KEY, 0)) {
		debug_error(" vconf_set_int fail\n");
		if (vconf_set_int(SOUND_STATUS_KEY, 0)) {
			debug_error(" vconf_set_int fail\n");
		}
	}

#ifdef SUPPORT_GCF
	if (vconf_get_int(VCONFKEY_ADMIN_GCF_TEST, &is_gcf)) {
		debug_warning(" vconf_get_int for VCONFKEY_ADMIN_GCF_TEST failed, set as default\n");
		is_gcf = GCF_DEFAULT;
	}
#endif

	/* Set READY flag */
	if (vconf_set_int(ASM_READY_KEY, 1)) {
		debug_error(" vconf_set_int fail\n");
	}

	/* Msg Loop */
	while (true) {
		debug_log(" asm_Server is waiting message(%d)!!!\n", asm_is_send_msg_to_cb);
		if (asm_is_send_msg_to_cb)
			continue;

		/* Receive Msg */
		__asm_rcv_message(&asm_rcv_msg);

		/* Do msg handling */
		__asm_process_message (&asm_rcv_msg, NULL);

		/* TODO : Error Handling */
	}
}


int MMSoundMgrASMInit(void)
{
	int ret = 0;
	debug_fenter();

	ret = MMSoundThreadPoolRun(NULL, __asm_main_run);
	if (ret != MM_ERROR_NONE) {
		/* TODO : Error Handling */
		debug_error ("MMSoundThreadPoolRun failed....ret = [%x]\n", ret);
	}

	debug_fleave();
	return ret;
}

int MMSoundMgrASMFini(void)
{
	debug_fenter();

	debug_fleave();
	return MM_ERROR_NONE;
}

