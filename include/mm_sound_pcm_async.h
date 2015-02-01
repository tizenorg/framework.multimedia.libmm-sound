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
#ifndef __MM_SOUND_PCM_ASYNC__
#define __MM_SOUND_PCM_ASYNC__

/* Common */
typedef void (*mm_sound_pcm_stream_cb_t)(void *p, size_t nbytes, void *userdata);

int mm_sound_pcm_set_message_callback_async (MMSoundPcmHandle_t handle, MMMessageCallback callback, void *user_param);
int mm_sound_pcm_get_latency_async(MMSoundPcmHandle_t handle, int *latency);
int mm_sound_pcm_is_started_async(MMSoundPcmHandle_t handle, bool *is_started);

/* PCM capture async */
int mm_sound_pcm_capture_open_async(MMSoundPcmHandle_t *handle, const unsigned int rate, MMSoundPcmChannel_t channel,
									MMSoundPcmFormat_t format, mm_sound_source_type_e source_type,
									mm_sound_pcm_stream_cb_t callback, void* userdata);
int mm_sound_pcm_capture_start_async(MMSoundPcmHandle_t handle);
int mm_sound_pcm_capture_stop_async(MMSoundPcmHandle_t handle);
int mm_sound_pcm_capture_peek(MMSoundPcmHandle_t handle, const void **buffer, const unsigned int *length);
int mm_sound_pcm_capture_drop(MMSoundPcmHandle_t handle);
int mm_sound_pcm_capture_close_async(MMSoundPcmHandle_t handle);

/* PCM playback async */
int mm_sound_pcm_play_open_async (MMSoundPcmHandle_t *handle, const unsigned int rate, MMSoundPcmChannel_t channel,
							MMSoundPcmFormat_t format, int volume_config,
							mm_sound_pcm_stream_cb_t callback, void* userdata);
int mm_sound_pcm_play_start_async(MMSoundPcmHandle_t handle);
int mm_sound_pcm_play_stop_async(MMSoundPcmHandle_t handle);
int mm_sound_pcm_play_write_async(MMSoundPcmHandle_t handle, void* ptr, unsigned int length_byte);
int mm_sound_pcm_play_close_async(MMSoundPcmHandle_t handle);

#endif /* __MM_SOUND_PCM_ASYNC__ */
