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


/**
* @file		utc_mm_sound_common.h
* @brief	This is a suite of unit test cases for MMSound APIs.
* @author	Kwanghui Cho(kwanghui.cho@samsung.com)
* @version	Initial Creation V0.1
* @date		2010.10.05
*/


#ifndef UTC_MM_FRAMEWORK_SOUND_COMMON_H
#define UTC_MM_FRAMEWORK_SOUND_COMMON_H


#include <mm_sound_private.h>
#include <mm_error.h>
#include <mm_types.h>
#include <stdio.h>
#include <string.h>
#include <tet_api.h>
#include <unistd.h>
#include <glib.h>


void startup();
void cleanup();

void (*tet_startup)() = startup;
void (*tet_cleanup)() = cleanup;

void utc_mm_sound_volume_get_step_func_01 ();
void utc_mm_sound_volume_get_step_func_02 ();
void utc_mm_sound_volume_get_step_func_03 ();
void utc_mm_sound_volume_get_step_func_04 ();


void utc_mm_sound_volume_get_value_func_01();
void utc_mm_sound_volume_get_value_func_02();


void utc_mm_sound_volume_set_value_func_01();
void utc_mm_sound_volume_set_value_func_02();


void utc_mm_sound_volume_primary_type_set_func_01();
void utc_mm_sound_volume_primary_type_set_func_02();


void utc_mm_sound_volume_primary_type_clear_func_01();


void utc_mm_sound_volume_add_callback_func_01();
void utc_mm_sound_volume_add_callback_func_02();


void utc_mm_sound_volume_remove_callback_func_01();
void utc_mm_sound_volume_remove_callback_func_02();


void utc_mm_sound_volume_get_current_playing_type_func_01();
void utc_mm_sound_volume_get_current_playing_type_func_02();


void utc_mm_sound_pcm_capture_open_func_01();
void utc_mm_sound_pcm_capture_open_func_02();


void utc_mm_sound_pcm_capture_read_func_01();
void utc_mm_sound_pcm_capture_read_func_02();


void utc_mm_sound_pcm_capture_close_func_01();
void utc_mm_sound_pcm_capture_close_func_02();


void utc_mm_sound_pcm_play_open_func_01();
void utc_mm_sound_pcm_play_open_func_02();


void utc_mm_sound_pcm_play_write_func_01();
void utc_mm_sound_pcm_play_write_func_02();


void utc_mm_sound_pcm_play_close_func_01();
void utc_mm_sound_pcm_play_close_func_02();


void utc_mm_sound_play_sound_func_01();
void utc_mm_sound_play_sound_func_02();


void utc_mm_sound_stop_sound_func_01();
void utc_mm_sound_stop_sound_func_02();


void utc_mm_sound_play_tone_func_01();
void utc_mm_sound_play_tone_func_02();


void utc_mm_sound_play_keysound_func_01();
void utc_mm_sound_play_keysound_func_02();

#endif /* UTC_MM_FRAMEWORK_SOUND_COMMON_H */
