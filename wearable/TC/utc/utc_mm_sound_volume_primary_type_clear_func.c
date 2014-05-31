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
* @addtogroup	UTS_MMF_SOUND_VOLUME_GET_STEP Uts_Mmf_Sound_Volume_Get_Step
* @{
*/

/**
* @file uts_mm_sound_volume_get_step_func.c
* @brief This is a suit of unit test cases to test mm_sound_volume_get_step API
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
#define API_NAME "mm_sound_volume_primary_type_clear"

struct tet_testlist tet_testlist[] = {
	{utc_mm_sound_volume_primary_type_clear_func_01, 1},
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

void utc_mm_sound_volume_primary_type_clear_func_01()
{
	int ret = 0;
	volume_type_t set_type = VOLUME_TYPE_RINGTONE ;
	volume_type_t read_type = VOLUME_TYPE_RINGTONE;

	mm_sound_volume_primary_type_set(set_type);

	ret = mm_sound_volume_primary_type_clear();

	dts_check_eq(API_NAME, ret, MM_ERROR_NONE);


	ret = mm_sound_volume_get_current_playing_type(&read_type);

	if(ret == MM_ERROR_NONE)
	{
		if(read_type == set_type) {
			//char buffer[128] = {0,};
			//snprintf(buffer, sizeof(buffer)-1,"ret 0x%x, set type %d, read type %d", ret, set_type, read_type);
			//tet_infoline( buffer );
			dts_fail(API_NAME,"double check failed");
		}
		else {
			dts_pass(API_NAME, "double check success");
		}
	}
	else
	{
		dts_pass(API_NAME, "double check success");
	}

	return;
}


/** @} */




