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

#ifndef __MM_SOUND_COMMON_H__
#define __MM_SOUND_COMMON_H__

///////////////////////////////////
////     MMSOUND VOLUME APIs
///////////////////////////////////
#define VCONF_KEY_VOLUME_PREFIX				"file/private/sound/volume"
#define VCONF_KEY_VOLUME_TYPE_SYSTEM		VCONF_KEY_VOLUME_PREFIX"/system"
#define VCONF_KEY_VOLUME_TYPE_NOTIFICATION	VCONF_KEY_VOLUME_PREFIX"/notification"
#define VCONF_KEY_VOLUME_TYPE_ALARM			VCONF_KEY_VOLUME_PREFIX"/alarm"
#define VCONF_KEY_VOLUME_TYPE_RINGTONE		VCONF_KEY_VOLUME_PREFIX"/ringtone"
#define VCONF_KEY_VOLUME_TYPE_MEDIA			VCONF_KEY_VOLUME_PREFIX"/media"
#define VCONF_KEY_VOLUME_TYPE_CALL			VCONF_KEY_VOLUME_PREFIX"/call"
#define VCONF_KEY_VOLUME_TYPE_VOIP			VCONF_KEY_VOLUME_PREFIX"/voip"
#define VCONF_KEY_VOLUME_TYPE_ANDROID		VCONF_KEY_VOLUME_PREFIX"/fixed"
#define VCONF_KEY_VOLUME_TYPE_JAVA			VCONF_KEY_VOLUME_PREFIX"/java"

#define ASM_READY_KEY		"memory/Sound/ASMReady"

#define MMSOUND_ENTER_CRITICAL_SECTION(x_mutex) \
switch ( pthread_mutex_lock( x_mutex ) ) \
{ \
case EINVAL: \
	debug_warning("try mutex init..\n"); \
	if( 0 > pthread_mutex_init( x_mutex, NULL) ) { \
		return; \
	} else { \
		break; \
	} \
	return; \
case 0: \
	break; \
default: \
	debug_error("mutex lock failed\n"); \
	return; \
}

#define MMSOUND_ENTER_CRITICAL_SECTION_WITH_RETURN(x_mutex,x_return) \
switch ( pthread_mutex_lock( x_mutex ) ) \
{ \
case EINVAL: \
	debug_warning("try mutex init..\n"); \
	if( 0 > pthread_mutex_init( x_mutex, NULL) ) { \
		return x_return; \
	} else { \
		break; \
	} \
	return x_return; \
case 0: \
	break; \
default: \
	debug_error("mutex lock failed\n"); \
	return x_return; \
}

#define MMSOUND_LEAVE_CRITICAL_SECTION(x_mutex) \
if( pthread_mutex_unlock( x_mutex ) ) { \
	debug_error("mutex unlock failed\n"); \
}

#endif /* __MM_SOUND_COMMON_H__ */

