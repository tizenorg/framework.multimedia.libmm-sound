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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <error.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <getopt.h>

#include <vconf.h>
#include <avsys-audio.h>
#include <mm_error.h>
#include <mm_debug.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <time.h>
#include <errno.h>


#include "../include/mm_sound_common.h"
#include "../include/mm_sound_utils.h"
#include "include/mm_sound_thread_pool.h"
#include "include/mm_sound_mgr_run.h"
#include "include/mm_sound_mgr_codec.h"
#include "include/mm_sound_mgr_ipc.h"
#include "include/mm_sound_mgr_pulse.h"
#include "include/mm_sound_mgr_asm.h"
#include "include/mm_sound_mgr_session.h"
#include "include/mm_sound_mgr_device.h"
#include "include/mm_sound_mgr_headset.h"
#include "include/mm_sound_mgr_dock.h"
#include "include/mm_sound_mgr_wfd.h"
#include <audio-session-manager.h>

#include <glib.h>

#define PLUGIN_ENV "MM_SOUND_PLUGIN_PATH"
#define PLUGIN_DIR "/usr/lib/soundplugins/"
#define PLUGIN_MAX 30

#define HIBERNATION_SOUND_CHECK_PATH	"/tmp/hibernation/sound_ready"
#define USE_SYSTEM_SERVER_PROCESS_MONITORING

#define	ASM_CHECK_INTERVAL	10000

#define MAX_PLUGIN_DIR_PATH_LEN	256

typedef struct {
    char plugdir[MAX_PLUGIN_DIR_PATH_LEN];
    int startserver;
    int printlist;
    int testmode;
    int poweroff;
    int updatescn;
    int soundreset;
    int avsysinit;
    int avsysuninit;
    int avsysreset;
    int avsysdump;
} server_arg;

static char *str_errormsg[] = {
    "Operation is success.",
    "Handle Init Fail",
    "Path Init Fail",
    "Handle Fini Fail",
    "Path Fini Fail",
    "Handle Reset Fail",
    "Path Reset Fail",
    "Handle Dump Fail",
    "Path Dump Fail",
    "Sync Dump Fail",
};

static int getOption(int argc, char **argv, server_arg *arg);
static int usgae(int argc, char **argv);

static struct sigaction sigint_action;  /* Backup pointer of SIGINT handler */
static struct sigaction sigabrt_action; /* Backup pointer of SIGABRT signal handler */
static struct sigaction sigsegv_action; /* Backup pointer of SIGSEGV fault signal handler */
static struct sigaction sigterm_action; /* Backup pointer of SIGTERM signal handler */
static struct sigaction sigsys_action;  /* Backup pointer of SIGSYS signal handler */
static void _exit_handler(int sig);

/* remove build warning : to be removed later */
extern int avsys_audio_path_set_single_ascn(char *str);
extern int avsys_audio_handle_init(void);
extern int avsys_audio_handle_fini(void);
extern int avsys_audio_handle_reset(int *volume_value);
extern int avsys_audio_handle_dump(void);
extern int avsys_audio_path_ex_init(void);
extern int avsys_audio_path_ex_fini(void);
extern int avsys_audio_path_ex_reset(int forced);
extern int avsys_audio_path_ex_dump(void);
extern int avsys_audio_dump_sync(void);


GMainLoop *g_mainloop;

void* pulse_handle;

gpointer event_loop_thread(gpointer data)
{
	g_mainloop = g_main_loop_new(NULL, TRUE);
	if(g_mainloop == NULL) {
		debug_error("g_main_loop_new() failed\n");
	}
	g_main_loop_run(g_mainloop);
	return NULL;
}

#ifdef USE_HIBERNATION
static void __hibernation_leave_cb()
{
	int volumes[VOLUME_TYPE_MAX] = {0, };

	MMSoundMgrPulseHandleRegisterMonoAudio(pulse_handle);
	MMSoundMgrPulseHandleRegisterBluetoothStatus (pulse_handle);

#if 0
	_mm_sound_volume_get_values_on_bootup(volumes);
#endif
	if (avsys_audio_hibernation_reset(volumes)) {
		debug_error("Audio reset failed\n");
	} else {
		debug_msg("Audio reset success\n");
	}
}
#endif

static void __wait_for_asm_ready ()
{
	int retry_count = 0;
	int asm_ready = 0;
	while (!asm_ready) {
		debug_msg("Checking ASM ready....[%d]\n", retry_count++);
		if (vconf_get_int(ASM_READY_KEY, &asm_ready)) {
			debug_warning("vconf_get_int for ASM_READY_KEY (%s) failed\n", ASM_READY_KEY);
		}
		usleep (ASM_CHECK_INTERVAL);
	}
	debug_msg("ASM is now ready...clear the key!!!\n");
	vconf_set_int(ASM_READY_KEY, 0);
}

static int _handle_power_off ()
{
	int handle = 0;
	int asm_error = 0;

	if (ASM_register_sound (-1, &handle, ASM_EVENT_EXCLUSIVE_MMPLAYER, ASM_STATE_PLAYING, NULL, NULL, ASM_RESOURCE_NONE, &asm_error)) {
		if (ASM_unregister_sound (handle, ASM_EVENT_EXCLUSIVE_MMPLAYER, &asm_error)) {
			debug_log ("asm register/unregister success!!!\n");
			return 0;
		} else {
			debug_error ("asm unregister failed...0x%x\n", asm_error);
		}
	} else {
		debug_error ("asm register failed...0x%x\n", asm_error);
	}

	return -1;
}

static int _handle_sound_reset ()
{
	int ret = 0;
	ret = avsys_audio_path_set_single_ascn("reset");
	if (AVSYS_FAIL(ret)) {
		debug_error ("avsys_audio_path_set_single_ascn() failed [%x]\n", ret);
		return -1;
	}
	return 0;
}

static int _handle_update_scn ()
{
	int ret = 0;
	ret = avsys_audio_update_scn();
	if (AVSYS_FAIL(ret)) {
		debug_error ("avsys_audio_pa_ctrl_update_scn() failed [%x]\n", ret);
		return -1;
	}
	return 0;
}

void _avsys_init()
{
	int result = 0;
	mode_t old_umask = umask(0);
	fprintf(stderr, "old umask was [%o]\n", old_umask);
	result = avsys_audio_handle_init();
	if (AVSYS_FAIL(result)) {
		result = 1;
		umask(old_umask);
		debug_log("set umask to old value\n");
		goto END;
	}
	result = avsys_audio_path_ex_init();
	if (AVSYS_FAIL(result)) {
		result = 2;
		umask(old_umask);
		debug_log("set umask to old value\n");
		goto END;
	}
	umask(old_umask);
	debug_log("set umask to old value\n");

END:
	if (result != 0) {
		debug_error("%s\n", str_errormsg[result]);
	}
}

void _avsys_uninit()
{
	int result = 0;
	result = avsys_audio_handle_fini();
	if (AVSYS_FAIL(result)) {
		result = 3;
		goto END;
	}
	result = avsys_audio_path_ex_fini();
	if (AVSYS_FAIL(result)) {
		result = 4;
		goto END;
	}
END:
	if (result != 0) {
		debug_error("%s\n", str_errormsg[result]);
	}
}

void _avsys_reset()
{
	int result = 0;
	result = avsys_audio_handle_reset(NULL);
	if (AVSYS_FAIL(result)) {
		result = 5;
		goto END;
	}

	result = avsys_audio_path_ex_reset(0);
	if (AVSYS_FAIL(result)) {
		result = 6;
		goto END;
	}
END:
	if (result != 0) {
		debug_error("%s\n", str_errormsg[result]);
	}
}

void _avsys_dump()
{
	int result = 0;
	result = avsys_audio_handle_dump();
	if (AVSYS_FAIL(result)) {
		result = 7;
		goto END;
	}
	result = avsys_audio_path_ex_dump();
	if (AVSYS_FAIL(result)) {
		result = 8;
		goto END;
	}

	result = avsys_audio_dump_sync();
	if (AVSYS_FAIL(result)) {
		result = 9;
		goto END;
	}
END:
	if (result != 0) {
		debug_error("%s\n", str_errormsg[result]);
	}
}

static sem_t* sem_create_n_wait()
{
	sem_t* sem = NULL;

	if ((sem = sem_open ("booting-sound", O_CREAT, 0660, 0))== SEM_FAILED) {
		debug_error ("error creating sem : %d", errno);
		return NULL;
	}

	debug_msg ("returning sem [%p]", sem);
	return sem;
}

int main(int argc, char **argv)
{
	sem_t* sem = NULL;
	server_arg serveropt;
	struct sigaction action;
#ifdef USE_HIBERNATION
	int heynotifd = -1;
#endif
	int volumes[VOLUME_TYPE_MAX] = {0, };
#if !defined(USE_SYSTEM_SERVER_PROCESS_MONITORING)
	int pid;
#endif

	action.sa_handler = _exit_handler;
	action.sa_flags = 0;
	sigemptyset(&action.sa_mask);

	if (getOption(argc, argv, &serveropt))
		return 1;

	debug_warning("sound_server [%d] init \n", getpid());

	if (serveropt.startserver || serveropt.avsysinit) {
		sem = sem_create_n_wait();
		_avsys_init();
	}
	/* Daemon process create */
	if (!serveropt.testmode && serveropt.startserver) {
#if !defined(USE_SYSTEM_SERVER_PROCESS_MONITORING)
		daemon(0,0); //chdir to ("/"), and close stdio
#endif
	}
	if (serveropt.avsysdump) {
		_avsys_dump();
	}
	if (serveropt.avsysreset) {
		_avsys_reset();
	}
	if (serveropt.avsysuninit) {
		_avsys_uninit();
	}
	if (serveropt.poweroff) {
		if (_handle_power_off() == 0) {
			debug_log("_handle_power_off success!!\n");
		} else {
			debug_error("_handle_power_off failed..\n");
		}
		return 0;
	}

	if (serveropt.soundreset) {
		if (_handle_sound_reset() == 0) {
			debug_log("_handle_sound_reset success!!\n");
		} else {
			debug_error("_handle_sound_reset failed..\n");
		}
		return 0;
	}

	if (serveropt.updatescn) {
		if (_handle_update_scn() == 0) {
			debug_log("_handle_update_scn success!!\n");
		} else {
			debug_error("_handle_update_scn failed..\n");
		}
		return 0;
	}

	/* Sound Server Starts!!!*/
	debug_warning("sound_server [%d] start \n", getpid());

	signal(SIGPIPE, SIG_IGN); //ignore SIGPIPE

#if 0
	_mm_sound_volume_get_values_on_bootup(volumes);
#endif
	if (avsys_audio_hibernation_reset(volumes)) {
		debug_error("Audio reset failed\n");
	} else {
		debug_msg("Audio reset success\n");
	}

#ifdef USE_HIBERNATION
	heynotifd = heynoti_init();
	if(heynoti_subscribe(heynotifd, "HIBERNATION_LEAVE", __hibernation_leave_cb, NULL)) {
		debug_error("heynoti_subscribe failed...\n");
	} else {
		debug_msg("heynoti_subscribe() success\n");
	}

	if(heynoti_attach_handler(heynotifd)) {
		debug_error("heynoti_attach_handler() failed\n");
	} else {
		debug_msg("heynoti_attach_handler() success\n");
	}
#endif

#if !defined(USE_SYSTEM_SERVER_PROCESS_MONITORING)
	while(1)
	{
		if ((pid = fork()) < 0)
		{
			fprintf(stderr, "Sub Fork Error\n");
			return 2;
		}
		else if(pid == 0)
		{
			break;
		}
		else if(pid > 0)
		{
			wait(&ret);
			fprintf(stderr, "Killed by signal [%05X]\n", ret);
			fprintf(stderr, "Daemon is run againg\n");
		}
	}
#endif
	sigaction(SIGABRT, &action, &sigabrt_action);
	sigaction(SIGSEGV, &action, &sigsegv_action);
	sigaction(SIGTERM, &action, &sigterm_action);
	sigaction(SIGSYS, &action, &sigsys_action);

	if (!g_thread_supported ())
		g_thread_init (NULL);

	if(NULL == g_thread_create(event_loop_thread, NULL, FALSE, NULL)) {
		fprintf(stderr,"event loop thread create failed\n");
		return 3;
	}

	if (serveropt.startserver || serveropt.printlist) {
		MMSoundThreadPoolInit();
		MMSoundMgrRunInit(serveropt.plugdir);
		MMSoundMgrCodecInit(serveropt.plugdir);
		if (!serveropt.testmode)
			MMSoundMgrIpcInit();

		pulse_handle = MMSoundMgrPulseInit();
		MMSoundMgrASMInit();
		/* Wait for ASM Ready */
		__wait_for_asm_ready();
		debug_warning("sound_server [%d] asm ready...now, initialize devices!!!\n", getpid());

		_mm_sound_mgr_device_init();
		MMSoundMgrHeadsetInit();
		MMSoundMgrDockInit();
		MMSoundMgrWfdInit();
		MMSoundMgrSessionInit();
	}

	debug_warning("sound_server [%d] initialization complete...now, start running!!\n", getpid());

	if (serveropt.startserver) {
		/* Start Run types */
		MMSoundMgrRunRunAll();

#ifdef USE_HIBERNATION
		/* set hibernation check */
		_mm_sound_check_hibernation (HIBERNATION_SOUND_CHECK_PATH);
#endif

		unlink(PA_READY); // remove pa_ready file after sound-server init.

		if (sem_post(sem) == -1) {
			debug_error ("error sem post : %d", errno);
		} else {
			debug_msg ("Ready to play booting sound!!!!");
		}
		/* Start Ipc mgr */
		MMSoundMgrIpcReady();
	}

	debug_warning("sound_server [%d] terminating \n", getpid());

	if (serveropt.startserver || serveropt.printlist) {
		MMSoundMgrRunStopAll();
		if (!serveropt.testmode)
			MMSoundMgrIpcFini();

		MMSoundMgrCodecFini();
		MMSoundMgrRunFini();
		MMSoundThreadPoolFini();

		MMSoundMgrWfdFini();
		MMSoundMgrDockFini();
		MMSoundMgrHeadsetFini();
		MMSoundMgrSessionFini();
		_mm_sound_mgr_device_fini();
		MMSoundMgrASMFini();
		MMSoundMgrPulseFini(pulse_handle);
#ifdef USE_HIBERNATION
		if(heynoti_unsubscribe(heynotifd, "HIBERNATION_LEAVE", NULL)) {
			debug_error("heynoti_unsubscribe failed..\n");
		}
		heynoti_close(heynotifd);
#endif
	}

	debug_warning("sound_server [%d] exit \n", getpid());

	return 0;
}

static int getOption(int argc, char **argv, server_arg *arg)
{
	int c;
	char *plugin_env_dir = NULL;
	static struct option long_options[] = {
		{"start", 0, 0, 'S'},
		{"poweroff", 0, 0, 'F'},
		{"soundreset", 0, 0, 'R'},
		{"updatescn", 0, 0, 'U'},
		{"list", 0, 0, 'L'},
		{"help", 0, 0, 'H'},
		{"plugdir", 1, 0, 'P'},
		{"testmode", 0, 0, 'T'},
		{"avsysinit", 0, 0, 'i'},
		{"avsysuninit", 0, 0, 'u'},
		{"avsysreset", 0, 0, 'r'},
		{"avsysdump", 0, 0, 'd'},
		{0, 0, 0, 0}
	};
	memset(arg, 0, sizeof(server_arg));

	plugin_env_dir = getenv(PLUGIN_ENV);
	if (plugin_env_dir) {
		strncpy (arg->plugdir, plugin_env_dir, sizeof(arg->plugdir)-1);
	} else {
		strncpy (arg->plugdir, PLUGIN_DIR, sizeof(arg->plugdir)-1);
	}

	arg->testmode = 0;

	while (1)
	{
		int opt_idx = 0;

		c = getopt_long (argc, argv, "SFLHRUP:Tiurd", long_options, &opt_idx);
		if (c == -1)
			break;
		switch (c)
		{
		case 'S': /* Start daemon */
			arg->startserver = 1;
			break;
		case 'F': /* Poweroff */
			arg->poweroff = 1;
			break;
		case 'R': /* SoundReset */
			arg->soundreset = 1;
			break;
		case 'U': /* UpdateSCN */
			arg->updatescn = 1;
			break;
		case 'L': /* list of plugins */
			arg->printlist = 1;
			break;
		case 'P': /* Custom plugindir */
			strncpy (arg->plugdir, optarg, sizeof(arg->plugdir)-1);
			break;
		case 'T': /* Test mode */
			arg->testmode = 1;
			break;
		case 'i':
			arg->avsysinit = 1;
			break;
		case 'u':
			arg->avsysuninit = 1;
			break;
		case 'r':
			arg->avsysreset = 1;
			break;
		case 'd':
			arg->avsysdump = 1;
			break;
		case 'H': /* help msg */
		default:
		return usgae(argc, argv);
		}
	}
	if (argc == 1)
		return usgae(argc, argv);
	return 0;
}

//__attribute__ ((destructor))
static void _exit_handler(int sig)
{
	int ret = MM_ERROR_NONE;

	ret = MMSoundMgrRunStopAll();
	if (ret != MM_ERROR_NONE) {
		debug_error("Fail to stop run-plugin\n");
	} else {
		debug_log("All run-type plugin stopped\n");
	}

	switch(sig)
	{
	case SIGINT:
		sigaction(SIGINT, &sigint_action, NULL);
		debug_error("signal(SIGINT) error");
		break;
	case SIGABRT:
		sigaction(SIGABRT, &sigabrt_action, NULL);
		debug_error("signal(SIGABRT) error");
		break;
	case SIGSEGV:
		sigaction(SIGSEGV, &sigsegv_action, NULL);
		debug_error("signal(SIGSEGV) error");
		break;
	case SIGTERM:
		sigaction(SIGTERM, &sigterm_action, NULL);
		debug_error("signal(SIGTERM) error");
		break;
	case SIGSYS:
		sigaction(SIGSYS, &sigsys_action, NULL);
		debug_error("signal(SIGSYS) error");
		break;
	default:
		break;
	}
	raise(sig);
}

static int usgae(int argc, char **argv)
{
	fprintf(stderr, "Usage: %s [Options]\n", argv[0]);
	fprintf(stderr, "\t%-20s: start sound server.\n", "--start,-S");
	fprintf(stderr, "\t%-20s: handle poweroff\n", "--poweroff,-F");
	fprintf(stderr, "\t%-20s: handle soundreset\n", "--soundreset,-R");
	fprintf(stderr, "\t%-20s: update scenario\n", "--updatescn,-U");
	fprintf(stderr, "\t%-20s: help message.\n", "--help,-H");
	fprintf(stderr, "\t%-20s: Initialize audio system\n","--avsysinit,-i");
	fprintf(stderr, "\t%-20s: Uninitialize audio system\n","--avsysuninit,-u");
	fprintf(stderr, "\t%-20s: Reset audio system\n","--avsysreset,-r");
	fprintf(stderr, "\t%-20s: Dump audio system\n","--avsysdump,-d");
#if 0 /* currently not in use */
	fprintf(stderr, "\t%-20s: print plugin list.\n", "--list,-L");
	fprintf(stderr, "\t%-20s: print this message.\n", "--plugdir,-P");
#endif

	return 1;
}

