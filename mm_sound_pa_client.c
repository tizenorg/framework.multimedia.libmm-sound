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

#include <mm_error.h>
#include <mm_debug.h>

#include <mm_session.h>
#include <mm_session_private.h>
#include <mm_sound_pa_client.h>

#include <glib.h>
#include <pulse/simple.h>
#include <pulse/proplist.h>

#include "include/mm_sound.h"
#include "include/mm_sound_private.h"
#include "include/mm_sound_utils.h"
#include "include/mm_sound_common.h"

#define MM_SOUND_CHANNEL_MIN                1
#define MM_SOUND_CHANNEL_MAX                6

#define MEDIA_POLICY_AUTO                   "auto"
#define MEDIA_POLICY_PHONE                  "phone"
#define MEDIA_POLICY_ALL                    "all"
#define MEDIA_POLICY_VOIP                   "voip"
#define MEDIA_POLICY_MIRRORING              "mirroring"
#define MEDIA_POLICY_HIGH_LATENCY           "high-latency"

typedef struct _mm_sound_handle_t {
    uint32_t handle;

    int mode;
    int policy;
    int volume_type;
    int gain_type;
    int rate;
    int channels;
    pa_simple* s;

    int period; /* open api retrun value.*/

    int stream_idx;
    int source_type;
} mm_sound_handle_t;

#define MM_SOUND_HANDLE_MAX                 32
static struct {
    uint32_t handle_count; /* use amotic operations */
    GList* handles;
    pthread_mutex_t lock;

    int state;
    pa_threaded_mainloop *mainloop;
    pa_context *context;
} mm_sound_handle_mgr;

#define CHECK_HANDLE_RANGE(x) \
    do { \
        if(x == 0) { \
            debug_msg("invalid handle(%d)", x); \
            return MM_ERROR_INVALID_ARGUMENT; \
        } \
    } while(0);

#define CHECK_VOLUME_TYPE_RANGE(x) \
    do { \
        if(x < VOLUME_TYPE_SYSTEM || x >= VOLUME_TYPE_MAX) { \
            debug_msg("invalid volume type(%d)", x); \
            return MM_ERROR_INVALID_ARGUMENT; \
        } \
    } while(0);

#define ATOMIC_INC(l, x) \
    do { \
        pthread_mutex_lock(l); \
        x = x + 1; \
        if(x == 0) \
            x = x + 1; \
        pthread_mutex_unlock(l); \
    } while(0);

// phandle(ret), GList, userdata, coimpare func
#define GET_HANDLE_DATA(p, l, u, func) \
    do { \
        GList* list = 0; \
        list = g_list_find_custom(l, u, func); \
        if(list != 0) \
            p = (mm_sound_handle_t*)list->data; \
        else \
            p = NULL; \
    } while(0);


#define CHECK_CONNECT_TO_PULSEAUDIO()   __mm_sound_pa_connect_to_pa()

// should be call after pa_ext function.
#define WAIT_PULSEAUDIO_OPERATION(x, y) \
    do { \
        while (pa_operation_get_state(y) == PA_OPERATION_RUNNING) { \
            debug_msg("waiting.................."); \
            pa_threaded_mainloop_wait(x.mainloop); \
            debug_msg("waiting DONE"); \
        } \
    } while(0);

static const pa_tizen_volume_type_t mm_sound_volume_type_to_pa[VOLUME_TYPE_MAX] = {
	[VOLUME_TYPE_SYSTEM] = PA_TIZEN_VOLUME_TYPE_SYSTEM,
	[VOLUME_TYPE_NOTIFICATION] = PA_TIZEN_VOLUME_TYPE_NOTIFICATION,
	[VOLUME_TYPE_ALARM] = PA_TIZEN_VOLUME_TYPE_ALARM,
	[VOLUME_TYPE_RINGTONE] = PA_TIZEN_VOLUME_TYPE_RINGTONE,
	[VOLUME_TYPE_MEDIA] = PA_TIZEN_VOLUME_TYPE_MEDIA,
	[VOLUME_TYPE_CALL] = PA_TIZEN_VOLUME_TYPE_CALL,
	[VOLUME_TYPE_VOIP] = PA_TIZEN_VOLUME_TYPE_VOIP,
	[VOLUME_TYPE_VOICE] = PA_TIZEN_VOLUME_TYPE_VOICE,
	[VOLUME_TYPE_FIXED] = PA_TIZEN_VOLUME_TYPE_FIXED,
//	[VOLUME_TYPE_EXT_SYSTEM_JAVA] = PA_TIZEN_VOLUME_TYPE_EXT_JAVA,
};

#define PA_SIMPLE_FADE_INTERVAL_USEC						20000

#define PA_SIMPLE_SAMPLES_PER_PERIOD_DEFAULT				1536	/* frames */
#define PA_SIMPLE_PERIODS_PER_BUFFER_FASTMODE				4
#define PA_SIMPLE_PERIODS_PER_BUFFER_DEFAULT				6
#define PA_SIMPLE_PERIODS_PER_BUFFER_VOIP					2
#define PA_SIMPLE_PERIODS_PER_BUFFER_PLAYBACK				8
#define PA_SIMPLE_PERIODS_PER_BUFFER_CAPTURE				12
#define PA_SIMPLE_PERIODS_PER_BUFFER_VIDEO					10

#define PA_SIMPLE_PERIOD_TIME_FOR_ULOW_LATENCY_MSEC			20
#define PA_SIMPLE_PERIOD_TIME_FOR_LOW_LATENCY_MSEC			25
#define PA_SIMPLE_PERIOD_TIME_FOR_MID_LATENCY_MSEC			50
#define PA_SIMPLE_PERIOD_TIME_FOR_HIGH_LATENCY_MSEC			75
#define PA_SIMPLE_PERIOD_TIME_FOR_VERY_HIGH_LATENCY_MSEC		150
#define PA_SIMPLE_PERIOD_TIME_FOR_VOIP_LATENCY_MSEC			20

#define IS_INPUT_HANDLE(x) \
    if( x == HANDLE_MODE_INPUT || x == HANDLE_MODE_INPUT_HIGH_LATENCY || \
        x == HANDLE_MODE_INPUT_LOW_LATENCY || x == HANDLE_MODE_INPUT_AP_CALL )

__attribute__ ((constructor)) void __mm_sound_pa_init(void)
{
    memset(&mm_sound_handle_mgr, 0, sizeof(mm_sound_handle_mgr));
    mm_sound_handle_mgr.state = FALSE;

    mm_sound_handle_mgr.handles = g_list_alloc();
    mm_sound_handle_mgr.handle_count = 1;
    pthread_mutex_init(&mm_sound_handle_mgr.lock, NULL);
}

__attribute__ ((destructor)) void __mm_sound_pa_deinit(void)
{
     g_list_free(mm_sound_handle_mgr.handles);
     pthread_mutex_destroy(&mm_sound_handle_mgr.lock);
     mm_sound_handle_mgr.handle_count = 0;

    if(mm_sound_handle_mgr.state) {
        debug_msg("mainloop(%x),  context(%x)", mm_sound_handle_mgr.mainloop, mm_sound_handle_mgr.context);
        pa_threaded_mainloop_stop(mm_sound_handle_mgr.mainloop);
        pa_context_disconnect(mm_sound_handle_mgr.context);
        pa_context_unref(mm_sound_handle_mgr.context);
        pa_threaded_mainloop_free(mm_sound_handle_mgr.mainloop);
    }
}

gint __mm_sound_handle_comparefunc(gconstpointer a, gconstpointer b)
{
    mm_sound_handle_t* phandle = (mm_sound_handle_t*)a;
    int* handle = (int*)b;

    if(phandle == NULL)
        return -1;

    if(phandle->handle == *handle)
        return 0;
    else
        return -1;
}

EXPORT_API
int mm_sound_pa_open(MMSoundHandleMode mode, mm_sound_handle_route_info *route_info, MMSoundHandlePriority priority, int volume_config, pa_sample_spec* ss, pa_channel_map* channel_map, int* size)
{
    pa_simple *s = NULL;
    pa_channel_map maps;
    pa_buffer_attr attr;

    int vol_conf_type, vol_conf_gain;
    int prop_vol_type, prop_gain_type;

    int err = MM_ERROR_SOUND_INTERNAL;
    int period_time = PA_SIMPLE_PERIOD_TIME_FOR_MID_LATENCY_MSEC;
    int samples_per_period = PA_SIMPLE_SAMPLES_PER_PERIOD_DEFAULT;
    int periods_per_buffer = PA_SIMPLE_PERIODS_PER_BUFFER_DEFAULT;

    const char *prop_policy = NULL;

    unsigned long latency = 0;
    int handle_mode = mode;
    int handle_inout = HANDLE_DIRECTION_NONE;
    int sample_size = 0;
    pa_proplist *proplist = pa_proplist_new();

    mm_sound_handle_t* handle = NULL;
    MMSoundHandleRoutePolicy policy = HANDLE_ROUTE_POLICY_DEFAULT;

    if(route_info) {
        policy = route_info->policy;
    }

    if (ss->channels < MM_SOUND_CHANNEL_MIN || ss->channels > MM_SOUND_CHANNEL_MAX)
        return MM_ERROR_INVALID_ARGUMENT;

    if(channel_map == NULL) {
        pa_channel_map_init_auto(&maps, ss->channels, PA_CHANNEL_MAP_ALSA);
        channel_map = &maps;
    }

    switch(ss->format) {
        case PA_SAMPLE_U8:
            sample_size = 1 * ss->channels;
            break;
        case PA_SAMPLE_S16LE:
            sample_size = 2 * ss->channels;
            break;
        default :
            sample_size = 0;
            debug_error("Invalid sample size (%d)", sample_size);
            break;
    }

    /* Set volume type of stream */
    vol_conf_type = volume_config & 0x000000FF;
    prop_vol_type = mm_sound_volume_type_to_pa[vol_conf_type];
    pa_proplist_setf(proplist, PA_PROP_MEDIA_TIZEN_VOLUME_TYPE, "%d", prop_vol_type);

    /* Set gain type of stream */
    prop_gain_type = (volume_config >> 8) & 0x000000FF;

    pa_proplist_setf(proplist, PA_PROP_MEDIA_TIZEN_GAIN_TYPE, "%d", prop_gain_type);

    IS_INPUT_HANDLE(handle_mode) {
        handle_inout = HANDLE_DIRECTION_IN;

        if (policy == HANDLE_ROUTE_POLICY_IN_MIRRORING) {
            prop_policy = MEDIA_POLICY_MIRRORING;
        } else if (policy == HANDLE_ROUTE_POLICY_IN_VOIP) {
            prop_policy = MEDIA_POLICY_VOIP;
            handle_mode = HANDLE_MODE_INPUT_AP_CALL;
        }
    } else {
        handle_inout = HANDLE_DIRECTION_OUT;

        /* Set policy property*/
        if (policy == HANDLE_ROUTE_POLICY_OUT_HANDSET) {
            prop_policy = MEDIA_POLICY_PHONE;

        } else if (policy == HANDLE_ROUTE_POLICY_OUT_ALL) {
            prop_policy = MEDIA_POLICY_ALL;
        }
    }

    /* If not set any yet, set based on volume type */
    if (prop_policy == NULL) {
        /* check stream type (vol type) */
        switch (vol_conf_type)
        {
        case VOLUME_TYPE_NOTIFICATION:
        case VOLUME_TYPE_ALARM:
            prop_policy = MEDIA_POLICY_ALL;
            break;

        case VOLUME_TYPE_MEDIA:
            /* Set High-Latency for Music stream */
            if (handle_mode == HANDLE_MODE_OUTPUT_CLOCK) {
                latency = PA_SIMPLE_PERIOD_TIME_FOR_VERY_HIGH_LATENCY_MSEC;
                prop_policy = MEDIA_POLICY_HIGH_LATENCY;
            } else {
                prop_policy = MEDIA_POLICY_AUTO;
            }
            break;

        case VOLUME_TYPE_CALL:
        case VOLUME_TYPE_RINGTONE:
        case VOLUME_TYPE_FIXED: /* Used for Emergency */
            prop_policy = MEDIA_POLICY_PHONE;
            break;

        case VOLUME_TYPE_VOIP:
            prop_policy = MEDIA_POLICY_VOIP;

            if(handle_inout == HANDLE_DIRECTION_OUT)
                handle_mode = HANDLE_MODE_OUTPUT_AP_CALL; /* need to move to session */
            else
                debug_error("VOLUME_TYPE_VOIP unknown handle_inout(%d)", handle_inout);

            break;

        default:
            prop_policy = MEDIA_POLICY_AUTO;
            break;
        }
    }
    pa_proplist_sets(proplist, PA_PROP_MEDIA_POLICY, prop_policy);

    if (priority) {
        debug_msg("Set HIGH priority [%d]", priority);
        pa_proplist_sets(proplist, PA_PROP_MEDIA_ROLE, "solo");
    }

    memset(&attr, '\0', sizeof(attr));

    switch (handle_mode) {
    case HANDLE_MODE_INPUT:
        period_time = PA_SIMPLE_PERIOD_TIME_FOR_MID_LATENCY_MSEC;
        samples_per_period = (ss->rate * period_time) / 1000;
        periods_per_buffer = PA_SIMPLE_PERIODS_PER_BUFFER_DEFAULT;
        attr.prebuf = 0;
        attr.minreq = -1;
        attr.tlength = -1;
        attr.maxlength = -1;
        attr.fragsize = samples_per_period * pa_sample_size(ss);

        s = pa_simple_new_proplist(NULL, "MM_SOUND_PA_CLIENT", PA_STREAM_RECORD, NULL, "CAPTURE", ss, channel_map, &attr, proplist, &err);
        break;

    case HANDLE_MODE_INPUT_LOW_LATENCY:
        period_time = PA_SIMPLE_PERIOD_TIME_FOR_ULOW_LATENCY_MSEC;
        samples_per_period = (ss->rate * period_time) / 1000;
        periods_per_buffer = PA_SIMPLE_PERIODS_PER_BUFFER_FASTMODE;
        attr.prebuf = 0;
        attr.minreq = -1;
        attr.tlength = -1;
        attr.maxlength = -1;
        attr.fragsize = samples_per_period * pa_sample_size(ss);

        s = pa_simple_new_proplist(NULL, "MM_SOUND_PA_CLIENT", PA_STREAM_RECORD, NULL, "LOW LATENCY CAPTURE", ss, channel_map, &attr, proplist, &err);
        break;

    case HANDLE_MODE_INPUT_HIGH_LATENCY:
        period_time = PA_SIMPLE_PERIOD_TIME_FOR_HIGH_LATENCY_MSEC;
        samples_per_period = (ss->rate * period_time) / 1000;
        periods_per_buffer = PA_SIMPLE_PERIODS_PER_BUFFER_CAPTURE;
        attr.prebuf = 0;
        attr.minreq = -1;
        attr.tlength = -1;
        attr.maxlength = -1;
        attr.fragsize = samples_per_period * pa_sample_size(ss);

        s = pa_simple_new_proplist(NULL, "MM_SOUND_PA_CLIENT", PA_STREAM_RECORD, NULL, "HIGH LATENCY CAPTURE", ss, channel_map, &attr, proplist, &err);
        break;

    case HANDLE_MODE_OUTPUT:
        period_time = PA_SIMPLE_PERIOD_TIME_FOR_MID_LATENCY_MSEC;
        samples_per_period = (ss->rate * period_time) / 1000;
        periods_per_buffer = PA_SIMPLE_PERIODS_PER_BUFFER_DEFAULT;
        attr.prebuf = -1;
        attr.minreq = -1;
        attr.tlength = (ss->rate / 10) * pa_sample_size(ss) * ss->channels;
        attr.maxlength = -1;
        attr.fragsize = 0;

        s = pa_simple_new_proplist(NULL, "MM_SOUND_PA_CLIENT", PA_STREAM_PLAYBACK, NULL, "PLAYBACK", ss, channel_map, &attr, proplist, &err);
        break;

    case HANDLE_MODE_OUTPUT_LOW_LATENCY:
        period_time = PA_SIMPLE_PERIOD_TIME_FOR_LOW_LATENCY_MSEC;
        samples_per_period = (ss->rate * period_time) / 1000;
        periods_per_buffer = PA_SIMPLE_PERIODS_PER_BUFFER_FASTMODE;
        attr.prebuf = (ss->rate / 100) * pa_sample_size(ss) * ss->channels;
        attr.minreq = -1;
        attr.tlength = (ss->rate / 10) * pa_sample_size(ss) * ss->channels;
        attr.maxlength = -1;
        attr.fragsize = 0;
        debug_msg("rate(%d), samplesize(%d), ch(%d) format(%d)", ss->rate, pa_sample_size(ss), ss->channels, ss->format);

        debug_msg("prebuf(%d), minreq(%d), tlength(%d), maxlength(%d), fragsize(%d)", attr.prebuf, attr.minreq, attr.tlength, attr.maxlength, attr.fragsize);

        s = pa_simple_new_proplist(NULL,"MM_SOUND_PA_CLIENT", PA_STREAM_PLAYBACK, NULL, "LOW LATENCY PLAYBACK", ss, channel_map, &attr, proplist, &err);
        break;

    case HANDLE_MODE_OUTPUT_CLOCK:
        period_time = (!latency) ? PA_SIMPLE_PERIOD_TIME_FOR_HIGH_LATENCY_MSEC : latency;
        samples_per_period = (ss->rate * period_time) / 1000;
        periods_per_buffer = PA_SIMPLE_PERIODS_PER_BUFFER_PLAYBACK;
        attr.prebuf = -1;
        attr.minreq = -1;
        attr.tlength = (!latency) ? (uint32_t)-1 : periods_per_buffer * samples_per_period * pa_sample_size(ss);
        attr.maxlength = -1;
        attr.fragsize = 0;

        s = pa_simple_new_proplist(NULL, "MM_SOUND_PA_CLIENT", PA_STREAM_PLAYBACK, NULL, "HIGH LATENCY PLAYBACK", ss, channel_map, &attr, proplist, &err);
        break;

    case HANDLE_MODE_OUTPUT_VIDEO: /* low latency playback */
        period_time = PA_SIMPLE_PERIOD_TIME_FOR_LOW_LATENCY_MSEC;
        samples_per_period = (ss->rate * period_time) / 1000;
        periods_per_buffer = PA_SIMPLE_PERIODS_PER_BUFFER_VIDEO;
        attr.prebuf = 4*(samples_per_period * pa_sample_size(ss));
        attr.minreq = samples_per_period * pa_sample_size(ss);
        attr.tlength = periods_per_buffer * samples_per_period * pa_sample_size(ss);
        attr.maxlength = -1;
        attr.fragsize = 0;

        s = pa_simple_new_proplist(NULL, "MM_SOUND_PA_CLIENT", PA_STREAM_PLAYBACK, NULL, "LOW LATENCY PLAYBACK", ss, channel_map, &attr, proplist, &err);
        break;

    case HANDLE_MODE_OUTPUT_AP_CALL:
#if defined(_MMFW_I386_ALL_SIMULATOR)
        debug_msg("Does not support AP call mode at i386 simulator\n");
        s = NULL;
#else
        period_time = PA_SIMPLE_PERIOD_TIME_FOR_VOIP_LATENCY_MSEC;
        samples_per_period = (ss->rate * period_time) / 1000;
        periods_per_buffer = PA_SIMPLE_PERIODS_PER_BUFFER_VOIP;
        attr.prebuf = -1;
        attr.minreq = pa_usec_to_bytes(20*PA_USEC_PER_MSEC, &ss);
        attr.tlength = pa_usec_to_bytes(100*PA_USEC_PER_MSEC, &ss);
        attr.maxlength = -1;
        attr.fragsize = 0;

        s = pa_simple_new_proplist(NULL, "MM_SOUND_PA_CLIENT", PA_STREAM_PLAYBACK, NULL, "VoIP PLAYBACK", ss, channel_map, &attr, proplist, &err);
#endif
        break;
    case HANDLE_MODE_INPUT_AP_CALL:
#if defined(_MMFW_I386_ALL_SIMULATOR)
        debug_msg("Does not support AP call mode at i386 simulator\n");
        s = NULL;
#else
        period_time = PA_SIMPLE_PERIOD_TIME_FOR_VOIP_LATENCY_MSEC;
        samples_per_period = (ss->rate * period_time) / 1000;
        periods_per_buffer = PA_SIMPLE_PERIODS_PER_BUFFER_VOIP;
        attr.prebuf = 0;
        attr.minreq = -1;
        attr.tlength = -1;
        attr.maxlength = -1;
        attr.fragsize = samples_per_period * pa_sample_size(ss);

        s = pa_simple_new_proplist(NULL, "MM_SOUND_PA_CLIENT", PA_STREAM_RECORD, NULL, "VoIP CAPTURE", ss, channel_map, &attr, proplist, &err);
#endif
        break;
    case HANDLE_MODE_CALL_OUT:
    case HANDLE_MODE_CALL_IN:
        debug_error("Does not support call device handling\n");
        assert(0);
        break;
    default:
        return MM_ERROR_SOUND_INTERNAL;
        assert(0);
        break;
    }

    if (!s) {
        debug_error("Open pulseaudio handle has failed - %s\n", pa_strerror(err));
        if (!strncmp(pa_strerror(err), "Access denied by security check",strlen(pa_strerror(err)))) {
            err = MM_ERROR_SOUND_PERMISSION_DENIED;
        } else {
            err = MM_ERROR_SOUND_INTERNAL;
        }
        goto fail;
    }

    handle = (mm_sound_handle_t*)malloc(sizeof(mm_sound_handle_t));

    handle->mode = mode;
    handle->policy = policy;
    handle->volume_type = prop_vol_type;
    handle->gain_type = prop_gain_type;
    handle->rate = ss->rate;
    handle->channels = ss->channels;
    handle->s = s;
    handle->period = samples_per_period * sample_size;
    *size = handle->period;
    handle->handle = mm_sound_handle_mgr.handle_count;
    ATOMIC_INC(&mm_sound_handle_mgr.lock, mm_sound_handle_mgr.handle_count); // 0 is not used

    if (0 > pa_simple_get_stream_index(s, &handle->stream_idx, &err)) {
        debug_msg("Can not get stream index %s\n", pa_strerror(err));
        err = MM_ERROR_SOUND_INTERNAL;
        goto fail;
    }
    g_list_append(mm_sound_handle_mgr.handles, handle);

    if(handle->handle == 0) {
        debug_msg("out of range. handle(%d)\n", handle->handle);
        goto fail;
    }

    mm_sound_pa_set_mute(handle->handle, prop_vol_type, handle_inout, 0);

    debug_msg("created handle[%d]. mode(%d), policy(%d), volumetype(%d), gain(%d), rate(%d), channels(%d), format(%d), stream_idx(%d), s(%x), source_type(%d) handle_inout(%d)",
        handle->handle, handle->mode, handle->policy, handle->volume_type, handle->gain_type,
        handle->rate, handle->channels, ss->format, handle->stream_idx, handle->s, handle->source_type, handle_inout);

    return handle->handle;

fail:
    if (proplist)
        pa_proplist_free(proplist);

    if(handle)
        free(handle);

    return err;

}

EXPORT_API
int mm_sound_pa_read(const int handle, void* buf, const int size)
{
    mm_sound_handle_t* phandle = NULL;
    int err = MM_ERROR_NONE;

#ifdef __STREAM_DEBUG__
    debug_msg("handle(%d), buf(%p), size(%d)", handle, buf, size);
#endif
    if (buf == NULL)
        return MM_ERROR_INVALID_ARGUMENT;

    if (size < 0)
        return MM_ERROR_INVALID_ARGUMENT;
    else if (size == 0)
        return size;

    CHECK_HANDLE_RANGE(handle);
    GET_HANDLE_DATA(phandle, mm_sound_handle_mgr.handles, &handle, __mm_sound_handle_comparefunc);
    if(phandle == NULL) {
        debug_msg("phandle is null");
        return MM_ERROR_SOUND_INTERNAL;
    }

    if (0 > pa_simple_read(phandle->s, buf, size, &err)) {
        debug_error("pa_simple_read() failed with %s", pa_strerror(err));
        return MM_ERROR_SOUND_INTERNAL;
    }

    return size;
}

EXPORT_API
int mm_sound_pa_write(const int handle, void* buf, const int size)
{
    mm_sound_handle_t* phandle = NULL;
    int err = MM_ERROR_NONE;

    if (buf == NULL)
        return MM_ERROR_INVALID_ARGUMENT;

    if (size < 0)
        return MM_ERROR_INVALID_ARGUMENT;
    else if (size == 0)
        return MM_ERROR_NONE;

    CHECK_HANDLE_RANGE(handle);
    GET_HANDLE_DATA(phandle, mm_sound_handle_mgr.handles, &handle, __mm_sound_handle_comparefunc);

#ifdef __STREAM_DEBUG__
    debug_msg("phandle(%x) s(%x), handle(%d), rate(%d), ch(%d) stream_idx(%d), buf(%p), size(%d)",
        phandle, phandle->s, phandle->handle, phandle->rate, phandle->channels, phandle->stream_idx. buf, size);
#endif

    if(phandle == NULL) {
        debug_msg("phandle is null");
        return MM_ERROR_SOUND_INTERNAL;
    }

    if (0 > pa_simple_write(phandle->s, buf, size, &err)) {
        debug_error("pa_simple_write() failed with %s\n", pa_strerror(err));
        return MM_ERROR_SOUND_INTERNAL;
    }

    return size;

}

EXPORT_API
int mm_sound_pa_close(const int handle)
{
    mm_sound_handle_t* phandle = NULL;
    int err = MM_ERROR_NONE;

    CHECK_HANDLE_RANGE(handle);
    GET_HANDLE_DATA(phandle, mm_sound_handle_mgr.handles, &handle, __mm_sound_handle_comparefunc);
    if(phandle == NULL) {
        debug_msg("phandle is null");
        return MM_ERROR_SOUND_INTERNAL;
    }

    debug_msg("phandle(%x) s(%x), handle(%d), rate(%d), ch(%d) stream_idx(%d)",
            phandle, phandle->s, phandle->handle, phandle->rate, phandle->channels, phandle->stream_idx);

	switch (phandle->mode) {
	case HANDLE_MODE_OUTPUT:
	case HANDLE_MODE_OUTPUT_CLOCK:
	case HANDLE_MODE_OUTPUT_LOW_LATENCY:
	case HANDLE_MODE_OUTPUT_AP_CALL:
	case HANDLE_MODE_OUTPUT_VIDEO:
		if (0 > pa_simple_flush(phandle->s, &err)) {
            err = MM_ERROR_SOUND_INTERNAL;
			debug_msg("pa_simple_flush() failed with %s\n", pa_strerror(err));
		}
		break;
	default:
		break;
	}

	pa_simple_free(phandle->s);
    phandle->s = NULL;

    debug_msg("leave: handle[%d] stream_index[%d]\n", handle, phandle->stream_idx);

    g_list_remove(mm_sound_handle_mgr.handles, phandle);
    if(phandle != NULL) {
        free(phandle);
        phandle = NULL;
    }

    return err;

}

EXPORT_API
int mm_sound_pa_cork(const int handle, const int cork)
{
    mm_sound_handle_t* phandle = NULL;
    int err = MM_ERROR_NONE;

	CHECK_HANDLE_RANGE(handle);
    GET_HANDLE_DATA(phandle, mm_sound_handle_mgr.handles, &handle, __mm_sound_handle_comparefunc);
    if(phandle == NULL) {
        debug_msg("phandle is null");
        return MM_ERROR_SOUND_INTERNAL;
    }

    if (0 > pa_simple_cork(phandle->s, cork, &err)) {
        debug_error("pa_simple_cork() failed with %s\n", pa_strerror(err));
        err = MM_ERROR_SOUND_INTERNAL;
    }

	return err;
}

EXPORT_API
int mm_sound_pa_drain(const int handle)
{
    mm_sound_handle_t* phandle = NULL;
    int err = MM_ERROR_NONE;

	CHECK_HANDLE_RANGE(handle);
    GET_HANDLE_DATA(phandle, mm_sound_handle_mgr.handles, &handle, __mm_sound_handle_comparefunc);
    if(phandle == NULL)
        return MM_ERROR_SOUND_INTERNAL;

    if (0 > pa_simple_drain(phandle->s, &err)) {
		debug_error("pa_simple_drain() failed with %s\n", pa_strerror(err));
		err = MM_ERROR_SOUND_INTERNAL;
	}

	return err;
}

EXPORT_API
int mm_sound_pa_get_latency(const int handle, int* latency)
{
    mm_sound_handle_t* phandle = NULL;
    int err = MM_ERROR_NONE;
    pa_usec_t latency_time = 0;

	CHECK_HANDLE_RANGE(handle);
    GET_HANDLE_DATA(phandle, mm_sound_handle_mgr.handles, &handle, __mm_sound_handle_comparefunc);
    if(phandle == NULL) {
        debug_msg("phandle is null");
        return MM_ERROR_SOUND_INTERNAL;
    }

    latency_time = pa_simple_get_final_latency(phandle->s, &err);
    if (err > 0 && latency_time == 0) {
        debug_error("pa_simple_get_latency() failed with %s\n", pa_strerror(err));
        err = MM_ERROR_SOUND_INTERNAL;
    }
    *latency = latency_time / 1000; // usec to msec

    return err;
}

static void __mm_sound_pa_state_cb(pa_context *c, void *userdata)
{
    pa_threaded_mainloop *mainloop = userdata;

    switch (pa_context_get_state(c)) {
    case PA_CONTEXT_READY:
        pa_threaded_mainloop_signal(mainloop, 0);
        break;
    case PA_CONTEXT_TERMINATED:
    case PA_CONTEXT_FAILED:
        pa_threaded_mainloop_signal(mainloop, 0);
        break;

    case PA_CONTEXT_UNCONNECTED:
    case PA_CONTEXT_CONNECTING:
    case PA_CONTEXT_AUTHORIZING:
    case PA_CONTEXT_SETTING_NAME:
        break;
    }
}

static void __mm_sound_pa_connect_to_pa()
{
    if(mm_sound_handle_mgr.state)
        return;

    if (!(mm_sound_handle_mgr.mainloop = pa_threaded_mainloop_new())) {
        debug_error("mainloop create failed");
    }

    if (!(mm_sound_handle_mgr.context = pa_context_new(pa_threaded_mainloop_get_api(mm_sound_handle_mgr.mainloop), NULL))) {
        debug_error("context create failed");
    }

    pa_threaded_mainloop_lock(mm_sound_handle_mgr.mainloop);
    pa_context_set_state_callback(mm_sound_handle_mgr.context, __mm_sound_pa_state_cb, (void *)mm_sound_handle_mgr.mainloop);

    if(pa_threaded_mainloop_start(mm_sound_handle_mgr.mainloop) < 0) {
        debug_error("mainloop start failed");
    }

    if (pa_context_connect(mm_sound_handle_mgr.context, NULL, 0, NULL) < 0) {
        debug_error("context connect failed");
    }

    while (TRUE) {
        pa_context_state_t state = pa_context_get_state(mm_sound_handle_mgr.context);

        if (!PA_CONTEXT_IS_GOOD (state)) {
            break;
        }

        if (state == PA_CONTEXT_READY) {
            break;
        }

        debug_msg("waiting..................");
        pa_threaded_mainloop_wait(mm_sound_handle_mgr.mainloop);
        debug_msg("waiting DONE. check again...");
    }

    mm_sound_handle_mgr.state = TRUE;

    pa_threaded_mainloop_unlock(mm_sound_handle_mgr.mainloop);

}

static void __mm_sound_pa_success_cb(pa_context *c, int success, void *userdata)
{
	pa_threaded_mainloop *mainloop = (pa_threaded_mainloop *)userdata;

	if (!success) {
		debug_error("pa control failed: %s\n", pa_strerror(pa_context_errno(c)));
	} else {
		debug_msg("pa control success\n");
	}
	pa_threaded_mainloop_signal(mainloop, 0);
}

EXPORT_API
int mm_sound_pa_set_volume_by_type(const int type, const int value)
{
    pa_operation *o = NULL;

    CHECK_VOLUME_TYPE_RANGE(type);
    CHECK_CONNECT_TO_PULSEAUDIO();

    pa_threaded_mainloop_lock(mm_sound_handle_mgr.mainloop);

    o = pa_ext_policy_set_volume_level(mm_sound_handle_mgr.context, -1, type, value, __mm_sound_pa_success_cb, (void*)mm_sound_handle_mgr.mainloop);
    WAIT_PULSEAUDIO_OPERATION(mm_sound_handle_mgr, o);

    if(o)
        pa_operation_unref(o);

    pa_threaded_mainloop_unlock(mm_sound_handle_mgr.mainloop);

    return MM_ERROR_NONE;
}

EXPORT_API
int mm_sound_pa_set_call_mute(const int type, const int mute, int direction)
{
    pa_operation *o = NULL;

    CHECK_VOLUME_TYPE_RANGE(type);
    CHECK_CONNECT_TO_PULSEAUDIO();

    pa_threaded_mainloop_lock(mm_sound_handle_mgr.mainloop);

    o = pa_ext_policy_set_mute(mm_sound_handle_mgr.context, -1, type, direction, mute, __mm_sound_pa_success_cb, (void *)mm_sound_handle_mgr.mainloop);
    WAIT_PULSEAUDIO_OPERATION(mm_sound_handle_mgr, o);

    if(o)
        pa_operation_unref(o);

    pa_threaded_mainloop_unlock(mm_sound_handle_mgr.mainloop);

    return MM_ERROR_NONE;
}

typedef struct _get_volume_max_userdata_t
{
    pa_threaded_mainloop* mainloop;
    int value;
} get_volume_max_userdata_t;

static void __mm_sound_pa_get_cb(pa_context *c, uint32_t value, void *userdata)
{
    get_volume_max_userdata_t* u = (get_volume_max_userdata_t*)userdata;

    assert(c);
    assert(u);

    u->value = value;

    pa_threaded_mainloop_signal(u->mainloop, 0);
}


EXPORT_API
int mm_sound_pa_get_volume_max(const int type, int* step)
{
    get_volume_max_userdata_t userdata;
    pa_operation *o = NULL;

    CHECK_VOLUME_TYPE_RANGE(type);
    CHECK_CONNECT_TO_PULSEAUDIO();

    pa_threaded_mainloop_lock(mm_sound_handle_mgr.mainloop);

    userdata.mainloop = mm_sound_handle_mgr.mainloop;
    userdata.value = -1;

    o = pa_ext_policy_get_volume_level_max(mm_sound_handle_mgr.context, type, __mm_sound_pa_get_cb, (void *)&userdata);
    WAIT_PULSEAUDIO_OPERATION(mm_sound_handle_mgr, o);

    if(userdata.value < 0) {
        debug_error("pa_ext_policy_get_volume_level_max() failed, userdata.value(%d)", userdata.value);
        *step = -1;
        pa_threaded_mainloop_unlock(mm_sound_handle_mgr.mainloop);
        return MM_ERROR_SOUND_INTERNAL;
    } else
        pa_operation_unref(o);

    *step = userdata.value;

    pa_threaded_mainloop_unlock(mm_sound_handle_mgr.mainloop);

    return MM_ERROR_NONE;
}

EXPORT_API
int mm_sound_pa_get_volume_level(int handle, const int type, int* level)
{
    mm_sound_handle_t* phandle = NULL;
    get_volume_max_userdata_t userdata;
    pa_operation *o = NULL;

    CHECK_HANDLE_RANGE(handle);
    CHECK_VOLUME_TYPE_RANGE(type);
    CHECK_CONNECT_TO_PULSEAUDIO();

    GET_HANDLE_DATA(phandle, mm_sound_handle_mgr.handles, &handle, __mm_sound_handle_comparefunc);
    if(phandle == NULL) {
        debug_msg("phandle is null");
        return MM_ERROR_SOUND_INTERNAL;
    }

    pa_threaded_mainloop_lock(mm_sound_handle_mgr.mainloop);

    userdata.mainloop = mm_sound_handle_mgr.mainloop;
    userdata.value = -1;

    o = pa_ext_policy_get_volume_level(mm_sound_handle_mgr.context, phandle->stream_idx, type, __mm_sound_pa_get_cb, (void *)&userdata);
    WAIT_PULSEAUDIO_OPERATION(mm_sound_handle_mgr, o);

    if(userdata.value < 0) {
        debug_error("pa_ext_policy_get_volume_level() failed");
        *level = -1;
        pa_threaded_mainloop_unlock(mm_sound_handle_mgr.mainloop);
        return MM_ERROR_SOUND_INTERNAL;
    } else
        pa_operation_unref(o);


    *level = userdata.value;

    pa_threaded_mainloop_unlock(mm_sound_handle_mgr.mainloop);

}

EXPORT_API
int mm_sound_pa_set_volume_level(int handle, const int type, int level)
{
    mm_sound_handle_t* phandle = NULL;
    pa_operation *o = NULL;

    CHECK_HANDLE_RANGE(handle);
    CHECK_VOLUME_TYPE_RANGE(type);
    CHECK_CONNECT_TO_PULSEAUDIO();

    GET_HANDLE_DATA(phandle, mm_sound_handle_mgr.handles, &handle, __mm_sound_handle_comparefunc);
    if(phandle == NULL) {
        debug_msg("phandle is null");
        return MM_ERROR_SOUND_INTERNAL;
    }

    pa_threaded_mainloop_lock(mm_sound_handle_mgr.mainloop);

    o = pa_ext_policy_set_volume_level(mm_sound_handle_mgr.context, phandle->stream_idx, type, level, __mm_sound_pa_success_cb, (void *)mm_sound_handle_mgr.mainloop);
    WAIT_PULSEAUDIO_OPERATION(mm_sound_handle_mgr, o);

    if(o)
        pa_operation_unref(o);

    pa_threaded_mainloop_unlock(mm_sound_handle_mgr.mainloop);
}

EXPORT_API
int mm_sound_pa_get_mute(int handle, const int type, int direction, int* mute)
{
    mm_sound_handle_t* phandle = NULL;
    get_volume_max_userdata_t userdata;
    pa_operation *o = NULL;

    CHECK_HANDLE_RANGE(handle);
    CHECK_VOLUME_TYPE_RANGE(type);
    CHECK_CONNECT_TO_PULSEAUDIO();

    GET_HANDLE_DATA(phandle, mm_sound_handle_mgr.handles, &handle, __mm_sound_handle_comparefunc);
    if(phandle == NULL) {
        debug_msg("phandle is null");
        return MM_ERROR_SOUND_INTERNAL;
    }

    pa_threaded_mainloop_lock(mm_sound_handle_mgr.mainloop);

    userdata.mainloop = mm_sound_handle_mgr.mainloop;
    userdata.value = -1;

    o = pa_ext_policy_get_mute(mm_sound_handle_mgr.context, phandle->stream_idx, type, direction, __mm_sound_pa_get_cb, (void *)&userdata);
    WAIT_PULSEAUDIO_OPERATION(mm_sound_handle_mgr, o);

    if(userdata.value < 0) {
        debug_error("mm_sound_pa_get_mute() failed");
        *mute = -1;
        pa_threaded_mainloop_unlock(mm_sound_handle_mgr.mainloop);
        return MM_ERROR_SOUND_INTERNAL;
    } else
        pa_operation_unref(o);

    pa_threaded_mainloop_unlock(mm_sound_handle_mgr.mainloop);
}

EXPORT_API
int mm_sound_pa_set_mute(int handle, const int type, int direction, int mute)
{
    mm_sound_handle_t* phandle = NULL;
    pa_operation *o = NULL;

    CHECK_HANDLE_RANGE(handle);
    CHECK_VOLUME_TYPE_RANGE(type);
    CHECK_CONNECT_TO_PULSEAUDIO();

    GET_HANDLE_DATA(phandle, mm_sound_handle_mgr.handles, &handle, __mm_sound_handle_comparefunc);
    if(phandle == NULL) {
        debug_msg("phandle is null");
        return MM_ERROR_SOUND_INTERNAL;
    }

    pa_threaded_mainloop_lock(mm_sound_handle_mgr.mainloop);

    o = pa_ext_policy_set_mute(mm_sound_handle_mgr.context, phandle->stream_idx, type, direction, mute, __mm_sound_pa_success_cb, (void *)mm_sound_handle_mgr.mainloop);
    WAIT_PULSEAUDIO_OPERATION(mm_sound_handle_mgr, o);

    if(o)
        pa_operation_unref(o);

    pa_threaded_mainloop_unlock(mm_sound_handle_mgr.mainloop);

}

EXPORT_API
int mm_sound_pa_corkall(int cork)
{
    pa_operation *o = NULL;

    CHECK_CONNECT_TO_PULSEAUDIO();

    pa_threaded_mainloop_lock(mm_sound_handle_mgr.mainloop);

    o = pa_context_set_cork_all(mm_sound_handle_mgr.context, cork, __mm_sound_pa_success_cb, (void *)mm_sound_handle_mgr.mainloop);
    WAIT_PULSEAUDIO_OPERATION(mm_sound_handle_mgr, o);

    if(o)
        pa_operation_unref(o);

    pa_threaded_mainloop_unlock(mm_sound_handle_mgr.mainloop);

    return MM_ERROR_NONE;
}

