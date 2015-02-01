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
 #ifndef __MM_SOUND_PA_CLIENT__
 #define __MM_SOUND_PA_CLIENT__

#include <pulse/simple.h>
#include <pulse/error.h>
#include <pulse/proplist.h>
#include <pulse/channelmap.h>
#include <pulse/pulseaudio.h>
#include <pulse/ext-policy.h>
#include "include/mm_sound.h"

typedef enum {
	HANDLE_ROUTE_POLICY_DEFAULT,
	HANDLE_ROUTE_POLICY_OUT_AUTO,
	HANDLE_ROUTE_POLICY_OUT_HANDSET,
	HANDLE_ROUTE_POLICY_OUT_ALL,
	HANDLE_ROUTE_POLICY_IN_MIRRORING,
	HANDLE_ROUTE_POLICY_IN_VOIP,
}MMSoundHandleRoutePolicy; /* custom route policy per handle */

typedef enum {
	HANDLE_PRIORITY_NORMAL,
	HANDLE_PRIORITY_SOLO,
	HANDLE_PRIORITY_MAX,
}MMSoundHandlePriority;

typedef enum {
	HANDLE_MODE_OUTPUT,				/**< Output mode of handle */
	HANDLE_MODE_OUTPUT_CLOCK,			/**< Output mode of gst audio only mode */
	HANDLE_MODE_OUTPUT_VIDEO,			/**< Output mode of gst video mode */
	HANDLE_MODE_OUTPUT_LOW_LATENCY,	/**< Output mode for low latency play mode. typically for game */
	HANDLE_MODE_INPUT,					/**< Input mode of handle */
	HANDLE_MODE_INPUT_HIGH_LATENCY,	/**< Input mode for high latency capture mode. */
	HANDLE_MODE_INPUT_LOW_LATENCY,		/**< Input mode for low latency capture mode. typically for VoIP */
	HANDLE_MODE_CALL_OUT,				/**< for voice call establish */
	HANDLE_MODE_CALL_IN,				/**< for voice call establish */
	HANDLE_MODE_OUTPUT_AP_CALL,		/**< for VT call on thin modem */
	HANDLE_MODE_INPUT_AP_CALL,			/**< for VT call on thin modem */
	HANDLE_MODE_NUM,					/**< Number of mode */
}MMSoundHandleMode;

enum mm_sound_handle_direction_t {
	HANDLE_DIRECTION_NONE,
	HANDLE_DIRECTION_IN,
	HANDLE_DIRECTION_OUT,
};

typedef struct mm_sound_handle_route_info{
	MMSoundHandleRoutePolicy policy;
	mm_sound_device_in device_in;
	mm_sound_device_out device_out;
	bool do_not_restore;
}mm_sound_handle_route_info;

int mm_sound_pa_open(MMSoundHandleMode mode, mm_sound_handle_route_info *route_info, MMSoundHandlePriority priority, int volume_config, pa_sample_spec* ss, pa_channel_map* channel_map, int* size);
int mm_sound_pa_read(const int handle, void* buf, const int size);
int mm_sound_pa_write(const int handle, void* buf, const int size);
int mm_sound_pa_close(const int handle);
int mm_sound_pa_cork(const int handle, const int cork);
int mm_sound_pa_drain(const int handle);
int mm_sound_pa_set_volume_by_type(const int type, const int value);
int mm_sound_pa_get_latency(const int handle, int* latency);
int mm_sound_pa_set_call_mute(const int type, const int mute, int direction);
int mm_sound_pa_get_volume_max(const int type, int* step);
int mm_sound_pa_get_volume_level(const int handle, const int type, int* level);
int mm_sound_pa_set_volume_level(const int handle, const int type, int level);
int mm_sound_pa_set_mute(const int handle, const int type, int direction, int mute);
int mm_sound_pa_get_mute(const int handle, const int type, int direction, int* mute);
int mm_sound_pa_corkall(int cork);

#endif
