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
#define VCONF_KEY_VOLUME_TYPE_VOICE		VCONF_KEY_VOLUME_PREFIX"/voice"
#define VCONF_KEY_VOLUME_TYPE_ANDROID		VCONF_KEY_VOLUME_PREFIX"/fixed"

#define VCONF_KEY_VOLUME_BALANCE			VCONF_KEY_VOLUME_PREFIX"/balance"
#define VCONF_KEY_MUTE_ALL			        VCONF_KEY_VOLUME_PREFIX"/muteall"
#define VCONF_KEY_MONO_AUDIO				VCONFKEY_SETAPPL_ACCESSIBILITY_MONO_AUDIO

#define VCONFKEY_SOUND_PRIMARY_VOLUME_TYPE		"memory/private/sound/PrimaryVolumetype"
#define VCONFKEY_SOUND_PRIMARY_VOLUME_TYPE_FORCE	"memory/private/sound/PrimaryVolumetypeForce"


#ifndef _TIZEN_PUBLIC_
#define VCONF_KEY_NOISE_REDUCTION			VCONFKEY_CALL_NOISE_REDUCTION_STATE_BOOL
#define VCONF_KEY_EXTRA_VOLUME				VCONFKEY_CALL_EXTRA_VOLUME_STATE_BOOL
#define VCONF_KEY_WBAMR						VCONFKEY_CALL_WBAMR_STATE_BOOL
#define VCONF_KEY_VR_ENABLED				VCONFKEY_VOICE_CONTROL_ENABLED
#define VCONF_KEY_VR_RINGTONE_ENABLED		VCONFKEY_VOICE_CONTROL_INCOMING_CALL_ENABLED
#ifdef TIZEN_MICRO
#define VCONF_KEY_VR_LEFTHAND_ENABLED		VCONFKEY_SETAPPL_PERFERED_ARM_LEFT_BOOL
#endif
#endif
#define ASM_READY_KEY		"memory/private/Sound/ASMReady"
#define VCONF_KEY_FMRADIO_RECORDING	"memory/private/Sound/FMRadioRecording"

#define PA_READY "/tmp/.pa_ready"

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

#define MMSOUND_STRNCPY(dst,src,size)\
do { \
	if(src != NULL && dst != NULL && size > 0) {\
		strncpy(dst,src,size); \
		dst[size-1] = '\0';\
	} else if(dst == NULL) {       \
		debug_error("STRNCPY ERROR: Destination String is NULL\n"); \
	}	\
	else if(size <= 0) {      \
		debug_error("STRNCPY ERROR: Destination String is NULL\n"); \
	}	\
	else {    \
		debug_error("STRNCPY ERROR: Destination String is NULL\n"); \
	}	\
} while(0)

#endif /* __MM_SOUND_COMMON_H__ */

