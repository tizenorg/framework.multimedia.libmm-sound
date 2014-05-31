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
* @ingroup	MMF_SOUND_API
* @addtogroup	SOUND
*/

/**
* @ingroup	SOUND
* @addtogroup	UTS_MMF_SOUND Unit
*/

/**
* @ingroup	UTS_MMF_SOUND Unit
* @addtogroup	UTS_MMF_SOUND_VOLUME_ADD_CALLBACK Uts_Mmf_Sound_Volume_Add_Callback
* @{
*/

/**
* @file uts_mm_sound_volume_add_callback_func.c
* @brief This is a suit of unit test cases to test mm_sound_volume_add_callback API
* @author Kwanghui Cho (kwanghui.cho@samsung.com)
* @version Initial Creation Version 0.1
* @date 2010.10.05
*/


#include "utc_mm_sound_common.h"


///////////////////////////////////////////////////////////////////////////////////////////////////
//-------------------------------------------------------------------------------------------------
///////////////////////////////////////////////////////////////////////////////////////////////////
// Declare the global variables and registers and Internal Funntions
//-------------------------------------------------------------------------------------------------
#define API_NAME "mm_sound_volume_remove_callback"

struct tet_testlist tet_testlist[] = {
	{utc_mm_sound_volume_remove_callback_func_01, 1},
	{utc_mm_sound_volume_remove_callback_func_02, 2},
	{NULL, 0}
};

///////////////////////////////////////////////////////////////////////////////////////////////////
/* Initialize TCM data structures */

/* Start up function for each test purpose */
void
startup ()
{
}

/* Clean up function for each test purpose */
void
cleanup ()
{
}

volume_type_t g_vol_type = VOLUME_TYPE_MEDIA;
volatile int callback_done = 0;

void _volume_callback(void *data)
{
	callback_done = 1;
}

void utc_mm_sound_volume_remove_callback_func_01()
{
	int ret = 0;
	unsigned int value = 0;
	int step = 0;
	int wait=0;


	mm_sound_volume_add_callback(g_vol_type, _volume_callback, (void*)&g_vol_type);

	ret = mm_sound_volume_remove_callback(g_vol_type);
	dts_check_eq(API_NAME, ret, MM_ERROR_NONE);

    mm_sound_volume_get_step(g_vol_type, &step);
	mm_sound_volume_get_value(g_vol_type, &value);
	if(value == 0)
		value++;
	else if(value == (step-1))
		value--;
	else
		value++;

	mm_sound_volume_set_value(g_vol_type, value);
	sleep(1);

	dts_check_eq(API_NAME, callback_done, 0);

	return;
}


void utc_mm_sound_volume_remove_callback_func_02()
{
	int ret = 0;

	mm_sound_volume_add_callback(g_vol_type, _volume_callback, (void*)&g_vol_type);

	ret = mm_sound_volume_remove_callback(VOLUME_TYPE_MAX);

	dts_check_ne(API_NAME, ret, MM_ERROR_NONE);

	mm_sound_volume_remove_callback(g_vol_type);

	return;
}


/** @} */




