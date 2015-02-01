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

#include "include/mm_sound_plugin_run.h"
#include "include/mm_sound_mgr_run.h"
#include "include/mm_sound_thread_pool.h"

#include <mm_error.h>
#include <mm_debug.h>

static void _MMsoundMgrRunRunInternal(void *param);

static MMSoundPluginType *g_run_plugins = NULL;

int MMSoundMgrRunInit(const char *targetdir)
{
    debug_fenter();

    if (g_run_plugins) {
        debug_error("Please Check Init twice\n");
        return MM_ERROR_SOUND_INTERNAL;
    }
    MMSoundPluginScan(targetdir, MM_SOUND_PLUGIN_TYPE_RUN, &g_run_plugins);

    debug_fleave();
    return MM_ERROR_NONE;
}

int MMSoundMgrRunFini(void)
{
	debug_fenter();

    MMSoundPluginRelease(g_run_plugins);
    g_run_plugins = NULL;

    debug_fleave();
    return MM_ERROR_NONE;
}

int MMSoundMgrRunRunAll(void)
{
    int loop = 0;

    debug_fenter();

    while (g_run_plugins[loop].type != MM_SOUND_PLUGIN_TYPE_NONE) {
        MMSoundThreadPoolRun((void*)loop, _MMsoundMgrRunRunInternal);
        loop++;
    }

    debug_fleave();
    return MM_ERROR_NONE;
}

int MMSoundMgrRunStopAll(void)
{
	mmsound_run_interface_t intface;
	void *func = NULL;
	int loop = 0;

	debug_fenter();

	while (g_run_plugins[loop].type != MM_SOUND_PLUGIN_TYPE_NONE) {
		debug_msg("loop : %d\n", loop);	
		MMSoundPluginGetSymbol(&g_run_plugins[loop], RUN_GET_INTERFACE_FUNC_NAME, &func);
		MMSoundPlugRunCastGetInterface(func)(&intface);
		intface.stop();
		loop++;
	}

	debug_fleave();
    return MM_ERROR_NONE;
}

static void _MMsoundMgrRunRunInternal(void *param)
{
	int err = MM_ERROR_NONE;
	mmsound_run_interface_t intface;
	void* func = NULL;

	debug_enter("plugin number %d\n", (int)param);

	err = MMSoundPluginGetSymbol(&g_run_plugins[(int)param], RUN_GET_INTERFACE_FUNC_NAME, &func);
	if (err  != MM_ERROR_NONE) {
		debug_error("Get Symbol RUN_GET_INTERFACE_FUNC_NAME is fail : %x\n", err);
	}
	err = MMSoundPlugRunCastGetInterface(func)(&intface);
	if (err != MM_ERROR_NONE) {
		debug_error("Get interface fail : %x\n", err);
		/* If error occur, clean interface */
		//memset(&g_run_plugins[(int)param], 0, sizeof(mmsound_run_interface_t));
	}
	if(intface.SetThreadPool)
		intface.SetThreadPool(MMSoundThreadPoolRun);
	intface.run();
	debug_msg("Trace\n");
	debug_msg("Trace\n");

	debug_fleave();
}

