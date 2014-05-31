/*
 *  libmm-sound
 *
 *  Copyright (c) 2000 - 2011 Samsung Electronics Co., Ltd. All rights reserved.
 *
 *  Contact: Seungbae Shin <seungbae.shin@samsung.com>
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <mm_error.h>
#include <mm_debug.h>
#include <avsys-audio.h>

#include "mm_sound_hal.h"

#include "include/mm_sound_plugin_hal.h"
#include "include/mm_sound_mgr_pulse.h"

static MMSoundPluginType* g_hal_plugin = NULL;
static mmsound_hal_interface_t g_plugin_interface;

#define DEVICE_API_BLUETOOTH    "bluez"
#define DEVICE_API_ALSA "alsa"
#ifdef USE_PULSE_WFD /* Not enabled yet */
#define DEVICE_API_WFD  "wfd"
#endif

#define DEVICE_BUS_BLUETOOTH "bluetooth"
#define DEVICE_BUS_USB "usb"
#define DEVICE_BUS_BUILTIN "builtin"

int audio_hal_pulse_sink_route(int device)
{
    debug_fenter();
    if (g_plugin_interface.pulse_sink_route) {
        return g_plugin_interface.pulse_sink_route(device);
    }

    debug_log("g_plugin_interface.pulse_sink_route is null!");
    if (device & MM_SOUND_DEVICE_OUT_BT_A2DP) {
        debug_log("BT A2DP is active, Set default sink to BLUEZ");
        MMSoundMgrPulseSetDefaultSink (DEVICE_API_BLUETOOTH, DEVICE_BUS_BLUETOOTH);
        return AUDIO_HAL_ROUTE_SUCCESS_AND_GOTOEND;
    } else if (device & MM_SOUND_DEVICE_OUT_WFD) {
#ifdef USE_PULSE_WFD /* Not enabled yet */
        debug_log("WFD is active, Set default sink to WFD");
        MMSoundMgrPulseSetDefaultSink (DEVICE_API_WFD, DEVICE_BUS_BUILTIN);
        return AUDIO_HAL_ROUTE_SUCCESS_AND_GOTOEND;
#endif
    } else if (device & MM_SOUND_DEVICE_OUT_USB_AUDIO) {
        debug_log("USB Audio is active, Set default sink to USB Audio");
        MMSoundMgrPulseSetDefaultSink (DEVICE_API_ALSA, DEVICE_BUS_USB);
        return AUDIO_HAL_ROUTE_SUCCESS_AND_GOTOEND;
    }
    debug_log("Set default sink to ALSA with BUILTIN");
    MMSoundMgrPulseSetDefaultSink (DEVICE_API_ALSA, DEVICE_BUS_BUILTIN);
    debug_fleave();
    return AUDIO_HAL_STATE_SUCCESS;
}

int audio_hal_pulse_source_route(int device)
{
    debug_fenter();
    if (g_plugin_interface.pulse_source_route) {
        return g_plugin_interface.pulse_source_route(device);
    }
    debug_log("g_plugin_interface.pulse_source_route is null!");
    debug_fleave();
    return AUDIO_HAL_STATE_SUCCESS;
}

EXPORT_API
int audio_hal_set_sound_path(int gain, int output, int input, int option)
{
    int err = MM_ERROR_NONE;

    debug_fenter();
    if (g_plugin_interface.set_sound_path) {
        err = g_plugin_interface.set_sound_path(gain, output, input, option);
    }
    else {
        err = avsys_audio_set_path_ex(gain, output, input, option);
    }
    debug_fleave();
    return err;
}

int audio_hal_init()
{
    debug_fenter();
    if (g_plugin_interface.init) {
        return g_plugin_interface.init();
    }
    debug_fleave();
    return AUDIO_HAL_STATE_SUCCESS;
}

int audio_hal_fini()
{
    debug_fenter();
    if (g_plugin_interface.fini) {
        return g_plugin_interface.fini();
    }
    debug_fleave();
    return AUDIO_HAL_STATE_SUCCESS;
}

int MMSoundMgrHALInit(const char *targetdir)
{
    int err = MM_ERROR_NONE;

    debug_fenter();
    if (g_hal_plugin) {
        debug_error("Please Check Init\n");
        return MM_ERROR_SOUND_INTERNAL;
    }
    err = MMSoundPluginScan(targetdir, MM_SOUND_PLUGIN_TYPE_HAL, &g_hal_plugin);
    debug_log("MMSoundPluginScan return %d", err);

    if (g_hal_plugin) {
        void* func = NULL;

        err = MMSoundPluginGetSymbol(g_hal_plugin, HAL_GET_INTERFACE_FUNC_NAME, &func);
        if (err  != MM_ERROR_NONE) {
            debug_error("Get Symbol %s fail : %x\n", HAL_GET_INTERFACE_FUNC_NAME, err);
            goto err_out_and_free;
        }
        err = MMSoundPlugHALCastGetInterface(func)(&g_plugin_interface);
        if (err != AUDIO_HAL_STATE_SUCCESS) {
            debug_error("Get interface fail : %x\n", err);
            goto err_out_and_free;
        }

        debug_log("g_plugin_interface.pulse_sink_route=%p", g_plugin_interface.pulse_sink_route);
        debug_log("g_plugin_interface.pulse_source_route=%p", g_plugin_interface.pulse_source_route);
        debug_log("g_plugin_interface.set_sound_path=%p", g_plugin_interface.set_sound_path);
        debug_log("g_plugin_interface.init=%p", g_plugin_interface.init);
        debug_log("g_plugin_interface.fini=%p", g_plugin_interface.fini);

        err = audio_hal_init();
        if (err != AUDIO_HAL_STATE_SUCCESS) {
            debug_error("audio_hal_init failed : %x\n", err);
            goto err_out_and_free;
        }
    }

    debug_fleave();
    return AUDIO_HAL_STATE_SUCCESS;

err_out_and_free:
    free(g_hal_plugin);
    g_hal_plugin = NULL;
    debug_fleave();
    return AUDIO_HAL_STATE_ERROR_INTERNAL;
}

int MMSoundMgrHALFini(void)
{
    int err = MM_ERROR_NONE;

    debug_fenter();
    err = audio_hal_fini();
    if (err != AUDIO_HAL_STATE_SUCCESS) {
        debug_error("audio_hal_fini failed");
        return err;
    }
    debug_fleave();
    return AUDIO_HAL_STATE_SUCCESS;
}
