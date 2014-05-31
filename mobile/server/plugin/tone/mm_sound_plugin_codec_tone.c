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
#include <stdlib.h>
#include <string.h>

#include <semaphore.h>
#include <unistd.h>
#include <pthread.h>

#include <avsys-audio.h>

#include "../../include/mm_sound_thread_pool.h"
#include "../../include/mm_sound_plugin_codec.h"
#include <mm_error.h>
#include <mm_debug.h>
#include <mm_sound.h>

#include <sys/types.h>
#include <sys/stat.h>

/* For Beep */
#include <fcntl.h>
#include <math.h>
#include <glib.h>
#ifndef M_PI
#define M_PI  3.14159265358979323846
#endif

#ifndef M_PI_2
#define M_PI_2  1.57079632679489661923
#endif

#define SAMPLERATE 44100

#define SAMPLE_SIZE 16
#define CHANNELS 1
#define MAX_DURATION 100
#define TONE_COLUMN 6

typedef enum {
   STATE_NONE = 0,
   STATE_READY,
   STATE_BEGIN,
   STATE_PLAY,
   STATE_STOP,
} state_e;

typedef enum {
	CONTROL_STOPPED,
	CONTROL_READY,
	CONTROL_STARTING,
	CONTROL_PLAYING,
} control_e;

typedef struct {
	pthread_mutex_t syncker;
	pthread_cond_t cond;
	int state;
} tone_control_t;

typedef struct {
     /* AMR Buffer */
	int				size; /* sizeof hole amr data */

     /* Audio Infomations */
	avsys_handle_t	audio_handle;

     /* control Informations */
	int				repeat_count;
	int				(*stop_cb)(int);
	int				cb_param;
	int				state;
	int				number;
	double			volume;
	int				time;
	int				pid;

} tone_info_t;

typedef enum
{
	LOW_FREQUENCY = 0,
	MIDDLE_FREQUENCY,
	HIGH_FREQUENCY,
	PLAYING_TIME,
	LOOP_COUNT,
	LOOP_INDEX,
} volume_type_e;

typedef struct st_tone
{
	float low_frequency;
	float middle_frequency;
	float high_frequency;
	int playingTime;
	int loopCnt;
	int loopIndx;
} TONE;

 static const int TONE_SEGMENT[][MM_SOUND_TONE_NUM] =
 {
	{941,	1336,	0,	-1,	0,	0,
	-1,	-1,	-1,	-1,	0,	0},// 0 key: 1336Hz, 941Hz

	{697,	1209,	0,	-1,	0,	0,
	-1,	-1,	-1,	-1,	0,	0},// 1 key: 1209Hz, 697Hz

	{697,	1336,	0,	-1,	0,	0,
	-1,	-1,	-1,	-1,	0,	0},// 2 key: 1336Hz, 697Hz

	{697,	1477,	0,	-1,	0,	0,
	-1,	-1,	-1,	-1,	0,	0}, // 3 key: 1477Hz, 697Hz

	{770,	1209,	0,	-1,	0,	0,
	-1,	-1,	-1,	-1,	0,	0},// 4 key: 1209Hz, 770Hz

	{770,	1336,	0,	-1,	0,	0,
	-1,	-1,	-1,	-1,	0,	0},// 5 key: 1336Hz, 770Hz

	{770,	1477,	0,	-1,	0,	0,
	-1,	-1,	-1,	-1,	0,	0},// 6 key: 1477Hz, 770Hz

	{852,	1209,	0,	-1,	0,	0,
	-1,	-1,	-1,	-1,	0,	0},// 7 key: 1209Hz, 852Hz

	{852,	1336,	0,	-1,	0,	0,
	-1,	-1,	-1,	-1,	0,	0},// 8 key: 1336Hz, 852Hz

	{852,	1477,	0,	-1,	0,	0,
	-1,	-1,	-1,	-1,	0,	0},// 9 key: 1477Hz, 852Hz

	{941,	1209,	0,	-1,	0,	0,
	-1,	-1,	-1,	-1,	0,	0},// * key: 1209Hz, 941Hz

	{941,	1477,	0,	-1,	0,	0,
	-1,	-1,	-1,	-1,	0,	0},// # key: 1477Hz, 941Hz

	{697,	1633,	0,	-1,	0,	0,
	-1,	-1,	-1,	-1,	0,	0},// A key: 1633Hz, 697Hz

	{770,	1633,	0,	-1,	0,	0,
	-1,	-1,	-1,	-1,	0,	0},// B key: 1633Hz, 770Hz

	{852,	1633,	0,	-1,	0,	0,
	-1,	-1,	-1,	-1,	0,	0},// C key: 1633Hz, 852Hz

	{941,	1633,	0,	-1,	0,	0,
	-1,	-1,	-1,	-1,	0,	0}, // D key: 1633Hz, 941Hz

	{425,	0,	0,	-1,	0,	0,
	-1,	-1,	-1,	-1,	0,	0},  //Call supervisory tone, Dial tone: CEPT: 425Hz, continuous

	{350,	440, 0,	-1,	0,	0,
	-1,	-1,	-1,	-1,	0,	0},  //Call supervisory tone, Dial tone: ANSI (IS-95): 350Hz+440Hz, continuous

	{400,	0,	0,	-1,	0,	0,
	-1,	-1,	-1,	-1,	0,	0},  //Call supervisory tone, Dial tone: JAPAN: 400Hz, continuous

	{425,	0,	0,	500,	0,	0,
		0,	0,	0,	500,	0,	0,
	-1,	-1,	-1,	-1,	0,	0},//Call supervisory tone, Busy: CEPT: 425Hz, 500ms ON, 500ms OFF...

	{480,	620, 0,	500,	0,	0,
		0,	0,	 0,	500,	0,	0,
	-1,	-1,	-1,	-1,	0,	0}, //Call supervisory tone, Busy: ANSI (IS-95): 480Hz+620Hz, 500ms ON, 500ms OFF...

	{400,	0,	0,	500,	0,	0,
		0,	0,	 0,	500,	0,	0,
	-1,	-1,	-1,	-1,	0,	0}, //Call supervisory tone, Busy: JAPAN: 400Hz, 500ms ON, 500ms OFF...

	{425,	0,	0,	200,	0,	0,
		0,	0,	0,	200,	0,	0,
	-1,	-1,	-1,	-1,	0,	0}, //Call supervisory tone, Congestion: CEPT, JAPAN: 425Hz, 200ms ON, 200ms OFF

	{480,	620, 0,	250,	0,	0,
		0,	0,	0,	250,	0,	0,
	-1,	-1,	-1,	-1,	0,	0}, //Call supervisory tone, Congestion: ANSI (IS-95): 480Hz+620Hz, 250ms ON, 250ms OFF...

	{425,	0,	0,	200,	0,	0,
	-1,	-1,	-1,	-1,	0,	0}, //Call supervisory tone, Radio path acknowlegment : CEPT, ANSI: 425Hz, 200ms ON

	{400,	0,	0,	1000,	0,	0,
		0,	0,	0,	2000,	0,	0,
	-1,	-1,	-1,	-1,	0,	0}, //Call supervisory tone, Radio path acknowlegment : JAPAN: 400Hz, 1s ON, 2s OFF...

	{425,	0,	0,	200,	0,	0,
		0,	0,	0,	200,	0,	0,
		-1,	-1,	-1,	-1,	3,	0}, //Call supervisory tone, Radio path not available: 425Hz, 200ms ON, 200 OFF 3 bursts

	{950, 1400, 1800,	330,	0,	0,
	0,	0,	0,	1000,	0,	0,
	-1,	-1,	-1,	-1,	0,	0}, //Call supervisory tone, Error/Special info: 950Hz+1400Hz+1800Hz, 330ms ON, 1s OFF...

	{425,	0,	0,	200,	0,	0,
		0,	0,	0,	600,	0,	0,
	 425,	0,	0,	200,	0,	0,
	 	0,	0,	0,	3000,0,	0,
	-1,	-1,	-1,	-1,	0,	0}, //Call supervisory tone, Call Waiting: CEPT, JAPAN: 425Hz, 200ms ON, 600ms OFF, 200ms ON, 3s OFF...

	{440,	0,	0,	300,	0,	0,
	0,	0,	0,	9700,	0,	0,
	440,	0,	0,	100,	0,	0,
	0,	0,	0,	100,	0,	0,
	440,	0,	0,	100,	0,	0,
	0,	0,	0,	9700,	0,	0,
	-1,	-1,	-1,	-1,	0,	0}, //Call supervisory tone, Call Waiting: ANSI (IS-95): 440 Hz, 300 ms ON, 9.7 s OFF, (100 ms ON, 100 ms OFF, 100 ms ON, 9.7s OFF ...)

	{425,	0,	0,	1000,	0,	0,
		0,	0,	0,	4000,	0,	0,
	-1,	-1,	-1,	-1,	0,	0}, //Call supervisory tone, Ring Tone: CEPT, JAPAN: 425Hz, 1s ON, 4s OFF...

	{440,  480,	0, 	2000,	0,	0,
		0,	  0,	0,	4000,	0,	0,
	-1,	-1,	-1,	-1,	0,	0}, //Call supervisory tone, Ring Tone: ANSI (IS-95): 440Hz + 480Hz, 2s ON, 4s OFF...

	{400,   1200,	  0, 	35,	0,	 0,
	-1,	-1,	-1,	-1,	0,	0}, // General beep: 400Hz+1200Hz, 35ms ON

	{1200,	0,	0, 100,	0,	0,
		0,	0,	0, 100,	0,	0,
		-1,	-1,	-1,	-1,	2,	0}, //Proprietary tone, positive acknowlegement: 1200Hz, 100ms ON, 100ms OFF 2 bursts

	{300, 400, 500,  400,	0,	0,
		-1,	-1,	-1,	-1,	0,	0}, //Proprietary tone, negative acknowlegement: 300Hz+400Hz+500Hz, 400ms ON

	{400, 1200,   0,  200,	0,	0,
		-1,	-1,	-1,	-1,	0,	0}, //Proprietary tone, prompt tone: 400Hz+1200Hz, 200ms ON

	{400,	1200,	0,	35,	0,	0,
		0,	0,	0,	200,		0,	0,
	400,		1200,	0,	35,	0,	0,
		-1,	-1,	-1,	-1,	0,	0}, //Proprietary tone, general double beep: twice 400Hz+1200Hz, 35ms ON, 200ms OFF, 35ms ON

	{440,	0,	0,	250,	0,	0,
	 620,	0,	0,	250,	0,	0,
		-1,	-1,	-1,	-1,	0,	0}, //Call supervisory tone (IS-95), intercept tone: alternating 440 Hz and 620 Hz tones, each on for 250 ms

	{440,	0,	0,	250, 0,	0,
	620,		0,	0,	250, 0,	0,
		-1,	-1,	-1,	-1,	8,	0}, //Call supervisory tone (IS-95), abbreviated intercept: intercept tone limited to 4 seconds

	{480,	620,	0,	250,	0,	0,
	     0,	0,   	0,	250,	0,	0,
		-1,	-1,	-1,	-1,	8,	0 }, //Call supervisory tone (IS-95), abbreviated congestion: congestion tone limited to 4 seconds

	{350,	440,	0,	100,	0,	0,
		0,	0,	0,	100,	0,	0,
		-1,	-1,	-1,	-1,	3,	0}, //Call supervisory tone (IS-95), confirm tone: a 350 Hz tone added to a 440 Hz tone repeated 3 times in a 100 ms on, 100 ms off cycle

	{480,	0,	0,	100,	0,	0,
		0,	0,	0,	100,	0,	0,
		-1,	-1,	-1,	-1,	4,	0}, //Call supervisory tone (IS-95), pip tone: four bursts of 480 Hz tone (0.1 s on, 0.1 s off).

	{ 425,	0,	0,	-1,	0,	0,
		-1,	-1,	-1,	-1,	0,	0}, //425Hz continuous

	{440,	480,	0,	2000,	0,	0,
		0,	0,	0,	4000,	0,	0,
		-1,	-1,	-1,	-1,	0,	0}, //CDMA USA Ringback: 440Hz+480Hz 2s ON, 4000 OFF ...

	{440,	0,	0,	250,	0,	0,
	620,		0,	0,	250,	0,	0,
		-1,	-1,	-1,	-1,	0,	0}, //CDMA Intercept tone: 440Hz 250ms ON, 620Hz 250ms ON ...

	{440,	0,	0,	250,	0,	0,
	620,		0,	0,	250,	0,	0,
		-1,	-1,	-1,	-1,	0,	0 }, //CDMA Abbr Intercept tone: 440Hz 250ms ON, 620Hz 250ms ON

	{480,	620,	0,	 250,	0,	0,
		0,	0,	0,	 250,	0,	0,
		-1,	-1,	-1,	-1,	0,	0 }, //CDMA Reorder tone: 480Hz+620Hz 250ms ON, 250ms OFF...

	{480,	620,	0,	250,	0,	0,
		0,	0,	0,	250,	0,	0,
		-1,	-1,	-1,	-1,	8,	0}, //CDMA Abbr Reorder tone: 480Hz+620Hz 250ms ON, 250ms OFF repeated for 8 times

	{480,	620,	0,	500,	0,	0,
		0,	0,	0,	500,	0,	0,
		-1,	-1,	-1,	-1,	0,	0}, //CDMA Network Busy tone: 480Hz+620Hz 500ms ON, 500ms OFF continuous

	{350,	440,	0,	100,	0,	0,
		0,	0,	0,	100,	0,	0,
		-1,	-1,	-1,	-1,	3,	0}, //CDMA Confirm tone: 350Hz+440Hz 100ms ON, 100ms OFF repeated for 3 times

	{660, 1000,	0,	500,	0,	0,
		0,	0,	0,	100,	0,	0,
		-1,	-1,	-1,	-1,	0,	0}, //CDMA answer tone: silent tone - defintion Frequency 0, 0ms ON, 0ms OFF

	{440,	0,	0,	300,	0,	0,
		-1,	-1,	-1,	-1,	0,	0}, //CDMA Network Callwaiting tone: 440Hz 300ms ON

	{480,	0,	0,	100,	0,	0,
		0,	0,	0,	100,	0,	0,
		-1,	-1,	-1,	-1,	4,	0}, //CDMA PIP tone: 480Hz 100ms ON, 100ms OFF repeated for 4 times

	{2090,	0,	0,	32,	0,	0,
	2556,	0,	0,	64,	19,	0,
	2090,	0,	0,	32,	0,	0,
	2556,	0,	0,	48,	0,	0,
	0,	0,	 0,	4000,	0,	0,
		-1,	-1,	-1,	-1,	0,	0}, //ISDN Call Signal Normal tone: {2091Hz 32ms ON, 2556 64ms ON} 20 times, 2091 32ms ON, 2556 48ms ON, 4s OFF

	{2091,	0,	0,	32,	0,	0,
	2556,	0,	0,	64,	7,	0,
	2091,	0,	0,	32,	0,	0,
	0,	0,	0,	400,		0,	0,
	2091,	0,	0,	32,	0,	0,
	2556,	0,	0,	64,	7,	4,
	2091,	0,	0,	32,	0,	0,
	0,	0,	0,	4000,	0,	0,
		-1,	-1,	-1,	-1,	0,	0}, //ISDN Call Signal Intergroup tone: {2091Hz 32ms ON, 2556 64ms ON} 8 times, 2091Hz 32ms ON, 400ms OFF, {2091Hz 32ms ON, 2556Hz 64ms ON} 8times, 2091Hz 32ms ON, 4s OFF.

	{2091,	0,	0,	32,	0,	0,
	2556,	0,	0,	64,	3,	0,
	2091,	0,	0,	32,	0,	0,
		0, 	0,	0,	200,	0,	0,
	2091,	0,	0,	32,	0,	0,
	2556,	0,	0,	64,	3,	4,
	2091,	0,	0,	32,	0,	0,
		0,	0,	0,	200,	0,	0,
		-1,	-1,	-1,	-1,	0,	0},//ISDN Call Signal SP PRI tone:{2091Hz 32ms ON, 2556 64ms ON} 4 times 2091Hz 16ms ON, 200ms OFF, {2091Hz 32ms ON, 2556Hz 64ms ON} 4 times, 2091Hz 16ms ON, 200ms OFF

	{0,	0,	0,	-1,	0,	0,
	-1,	-1,	-1,	-1,	0,	0}, //ISDN Call sign PAT3 tone: silent tone

	{2091,	0,	0,	32,	0,	0,
	2556,	0,	0,	64,	4,	0,
	2091,	0,	0,	20,	0,	0,
		-1,	-1,	-1,	-1,	0,	0}, //ISDN Ping Ring tone: {2091Hz 32ms ON, 2556Hz 64ms ON} 5 times 2091Hz 20ms ON

	{0,	0,	0,	-1,	0,	0,
	-1,	-1,	-1,	-1,	0,	0}, //ISDN Pat5 tone: silent tone

	{0,	0,	0,	-1,	0,	0,
	-1,	-1,	-1,	-1,	0,	0}, //ISDN Pat6 tone: silent tone

	{0,	0,	0,	-1,	0,	0,
	-1,	-1,	-1,	-1,	0,	0}, //ISDN Pat7 tone: silent tone

	{3700,	0,	0,	25,	0,	0,
	4000,	0,	0,	25,	39,	0,
		0,	0,	0, 4000,	0,	0,
		-1,	-1,	-1,	-1,	0,	0}, //TONE_CDMA_HIGH_L tone: {3700Hz 25ms, 4000Hz 25ms} 40 times 4000ms OFF, Repeat ....

	{2600,	0,	0,	25,	0,	0,
	2900,	0,	0,	25,	39,	0,
		0,	0,	0, 4000,	0,	0,
		-1,	-1,	-1,	-1,	0,	0},//TONE_CDMA_MED_L tone: {2600Hz 25ms, 2900Hz 25ms} 40 times 4000ms OFF, Repeat ....

	{1300,	0,	0,	25,	0,	0,
	1450,	0,	0,	25,	39,	0,
		0,	0,	0,4000,	0,	0,
	-1,	-1,	-1,	-1,	0,	0}, //TONE_CDMA_LOW_L tone: {1300Hz 25ms, 1450Hz 25ms} 40 times, 4000ms OFF, Repeat ....

	{3700,	0,	0,	25,	0,	0,
	4000,	0,	0,	25,	15,	0,
	0,	0,	0, 400,	0,	0,
	-1,	-1,	-1,	-1,	0,	0},//CDMA HIGH SS tone: {3700Hz 25ms, 4000Hz 25ms} repeat 16 times, 400ms OFF, repeat ....

	{2600,	0,	0,	25,	0,	0,
	2900,	0,	0,	25,	15,	0,
		0,	0,	0,400,	0,	0,
	-1,	-1,	-1,	-1,	0,	0}, //CDMA MED SS tone: {2600Hz 25ms, 2900Hz 25ms} repeat 16 times, 400ms OFF, repeat ....

	{1300,	0,	0,	25,	0,	0,
	1450,	0,	0,	25,	15,	0,
		0,	0,	0,	400,	0,	0,
	-1,	-1,	-1,	-1,	0,	0}, //CDMA LOW SS tone: {1300z 25ms, 1450Hz 25ms} repeat 16 times, 400ms OFF, repeat ....

	{3700,	0,	0,	25,	0,	0,
	4000,	0,	0,	25,	7,	0,
		0,	0,	0,	200,	0,	0,
	3700,	0,	0,	25,	0,	0,
	4000,	0,	0,	25,	7,	3,
		0,	0,	0,	200,	0,	0,
	3700,	0,	0,	25,	0,	0,
	4000,	0,	0,	25,	15,	6,
		0,	0,	0,	4000,0,	0,
		-1,	-1,	-1,	-1,	0,	0}, //CDMA HIGH SSL tone: {3700Hz 25ms, 4000Hz 25ms} 8 times, 200ms OFF, {3700Hz 25ms, 4000Hz 25ms} repeat 8 times, 200ms OFF, {3700Hz 25ms, 4000Hz 25ms} repeat 16 times, 4000ms OFF, repeat ...

	{2600,	0,	0,	25,	0,	0,
	2900,	0,	0,	25,	7,	0,
		0,	0,	0,	200,	0,	0,
	2600,	0,	0,	25,	0,	0,
	2900,	0,	0,	25,	7,	3,
		0,	0,	0,	200,	0,	0,
	2600,	0,	0,	25,	0,	0,
	2900,	0,	0,	25,	15,	6,
		0,	0,	0,	4000,0,	0,
		-1,	-1,	-1,	-1,	0,	0}, //CDMA MED SSL tone: {2600Hz 25ms, 2900Hz 25ms} 8 times, 200ms OFF, {2600Hz 25ms, 2900Hz 25ms} repeat 8 times, 200ms OFF, {2600Hz 25ms, 2900Hz 25ms} repeat 16 times, 4000ms OFF, repeat ...

	{1300,	0,	0,	25,	0,	0,
	1450,	0,	0,	25,	7,	0,
		0,	0,	0,	200,	0,	0,
	1300,	0,	0,	25,	0,	0,
	1450,	0,	0,	25,	7,	3,
		0,	0,	0,	200,	0,	0,
	1300,	0,	0,	25,	0,	0,
	1450,	0,	0,	25,	15,	6,
		0,	0,	0,	4000,0,	0,
		-1,	-1,	-1,	-1,	0,	0}, //CDMA LOW SSL tone: {1300Hz 25ms, 1450Hz 25ms} 8 times, 200ms OFF, {1300Hz 25ms, 1450Hz 25ms} repeat 8 times, 200ms OFF, {1300Hz 25ms, 1450Hz 25ms} repeat 16 times, 4000ms OFF, repeat ...

	{3700,	0,	0,	25,	0,	0,
	4000,	0,	0,	25,	19,	0,
	0,	0,	0,	1000,	0,	0,
	3700,	0,	0,	25,	0,	0,
	4000,	0,	0,	25,	19,	3,
	0,	0,	0,	3000,	0,	0,
		-1,	-1,	-1,	-1,	0,	0 },//CDMA HIGH SS2 tone: {3700Hz 25ms, 4000Hz 25ms} 20 times, 1000ms OFF, {3700Hz 25ms, 4000Hz 25ms} 20 times, 3000ms OFF, repeat ....

	{2600,	0,	0,	25,	0,	0,
	2900,	0,	0,	25,	19,	0,
	0,	0,	0,	1000,	0,	0,
	2600,	0,	0,	25,	0,	0,
	2900,	0,	0,	25,	19,	3,
	0,	0,	0,	3000,	0,	0,
		-1,	-1,	-1,	-1,	0,	0}, //CDMA MED SS2 tone: {2600Hz 25ms, 2900Hz 25ms} 20 times, 1000ms OFF, {2600Hz 25ms, 2900Hz 25ms} 20 times, 3000ms OFF, repeat ....

	{1300,	0,	0,	25,	0,	0,
	1450,	0,	0,	25,	19,	0,
	0,	0,	0,	1000,	0,	0,
	1300,	0,	0,	25,	0,	0,
	1450,	0,	0,	25,	19,	3,
	0,	0,	0,	3000,	0,	0,
		-1,	-1,	-1,	-1,	0,	0 }, //CDMA LOW SS2 tone: {1300Hz 25ms, 1450Hz 25ms} 20 times, 1000ms OFF, {1300Hz 25ms, 1450Hz 25ms} 20 times, 3000ms OFF, repeat ....

	{3700,	0,	0,	25,	0,	0,
	4000,	0,	0,	25,	9,	0,
		0,	0,	0,	500,	0,	0,
	3700,	0,	0,	25,	0,	0,
	4000,	0,	0,	25,	19,	3,
		0,	0,	0,	500,	0,	0,
	3700,	0,	0,	25,	0,	0,
	4000,	0,	0,	25,	9,	6,
	0,	0,	0,	3000,	0,	0,
		-1,	-1,	-1,	-1,	0,	0}, //CDMA HIGH SLS tone: {3700Hz 25ms, 4000Hz 25ms} 10 times, 500ms OFF, {3700Hz 25ms, 4000Hz 25ms} 20 times, 500ms OFF, {3700Hz 25ms, 4000Hz 25ms} 10 times, 3000ms OFF, REPEAT

	{2600,	0,	0,	25,	0,	0,
	2900,	0,	0,	25,	9,	0,
		0,	0,	0,	500,	0,	0,
	2600,	0,	0,	25,	0,	0,
	2900,	0,	0,	25,	19,	3,
		0,	0,	0,	500,	0,	0,
	2600,	0,	0,	25,	0,	0,
	2900,	0,	0,	25,	9,	6,
	0,	0,	0,	3000,	0,	0,
		-1,	-1,	-1,	-1,	0,	0}, //CDMA MED SLS tone: {2600Hz 25ms, 2900Hz 25ms} 10 times, 500ms OFF, {2600Hz 25ms, 2900Hz 25ms} 20 times, 500ms OFF, {2600Hz 25ms, 2900Hz 25ms} 10 times, 3000ms OFF, REPEAT

	{1300,	0,	0,	25,	0,	0,
	1450,	0,	0,	25,	9,	0,
	0,	0,	0,	500,	0,	0,
	1300,	0,	0,	25,	0,	0,
	1450,	0,	0,	25,	19,	3,
	0,	0,	0,	500,	0,	0,
	1300,	0,	0,	25,	0,	0,
	1450,	0,	0,	25,	9,	6,
	0,	0,	0,	3000,	0,	0,
		-1,	-1,	-1,	-1,	0,	0}, //CDMA LOW SLS tone: {1300Hz 25ms, 1450Hz 25ms} 10 times, 500ms OFF, {1300Hz 25ms, 1450Hz 25ms} 20 times, 500ms OFF, {1300Hz 25ms, 1450Hz 25ms} 10 times, 3000ms OFF, REPEAT//

	{3700,	0,	0,	25,	0,	0,
	4000,	0,	0,	25,	9,	0,
		0,	0,	0,	500,	0,	0,
	3700,	0,	0,	25,	0,	0,
	4000,	0,	0,	25,	9,	3,
		0,	0,	0,	500,	0,	0,
	3700,	0,	0,	25,	0,	0,
	4000,	0,	0,	25,	9,	6,
	0,	0,	0,	2500,	0,	0,
		-1,	-1,	-1,	-1,	0,	0}, ////CDMA HIGH S X4 tone: {3700Hz 25ms, 4000Hz 25ms} 10 times, 500ms OFF, {3700Hz 25ms, 4000Hz 25ms} 10 times, 500ms OFF, {3700Hz 25ms, 4000Hz 25ms} 10 times, 500ms OFF, {3700Hz 25ms, 4000Hz 25ms} 10 times, 2500ms OFF, REPEAT....

	{2600,	0,	0,	25,	0,	0,
	2900,	0,	0,	25,	9,	0,
		0,	0,	0,	500,	0,	0,
	2600,	0,	0,	25,	0,	0,
	2900,	0,	0,	25,	9,	4,
		0,	0,	0,	500,	0,	0,
	2600,	0,	0,	25,	0,	0,
	2900,	0,	0,	25,	9,	6,
	0,	0,	0,	2500,	0,	0,
		-1,	-1,	-1,	-1,	0,	0 },//CDMA MED S X4 tone: {2600Hz 25ms, 2900Hz 25ms} 10 times, 500ms OFF, {2600Hz 25ms, 2900Hz 25ms} 10 times, 500ms OFF, {2600Hz 25ms, 2900Hz 25ms} 10 times, 500ms OFF, {2600Hz 25ms, 2900Hz 25ms} 10 times, 2500ms OFF, REPEAT....

	{1300,	0,	0,	25,	0,	0,
	1450,	0,	0,	25,	9,	0,
		0,	0,	0,	500,	0,	0,
	1300,	0,	0,	25,	0,	0,
	1450,	0,	0,	25,	9,	3,
		0,	0,	0,	500,	0,	0,
	1300,	0,	0,	25,	0,	0,
	1450,	0,	0,	25,	9,	6,
	0,	0,	0,	2500,	0,	0,
		-1,	-1,	-1,	-1,	0,	0}, //CDMA LOW S X4 tone: {2600Hz 25ms, 2900Hz 25ms} 10 times, 500ms OFF, {2600Hz 25ms, 2900Hz 25ms} 10 times, 500ms OFF, {2600Hz 25ms, 2900Hz 25ms} 10 times, 500ms OFF, {2600Hz 25ms, 2900Hz 25ms} 10 times, 2500ms OFF, REPEAT....

	{3700,	0,	0,	25,	0,	0,
	4000,	0,	0,	25,	19,	0,
	0,	0,	0,	2000,	0,	0,
		-1,	-1,	-1,	-1,	0,	0 },//CDMA HIGH PBX L: {3700Hz 25ms, 4000Hz 25ms}20 times, 2000ms OFF, REPEAT....

	{2600,	0,	0,	25,	0,	0,
	2900,	0,	0,	25,	19,	0,
	0,	0,	0,	2000,	0,	0,
		-1,	-1,	-1,	-1,	0,	0}, //CDMA MED PBX L: {2600Hz 25ms, 2900Hz 25ms}20 times, 2000ms OFF, REPEAT....

	{1300,	0,	0,	25,	0,	0,
	1450,	0,	0,	25,	19,	0,
	0,	0,	0,	2000,	0,	0,
		-1,	-1,	-1,	-1,	0,	0 },//CDMA LOW PBX L: {1300Hz 25ms,1450Hz 25ms}20 times, 2000ms OFF, REPEAT....

	{3700,	0,	0,	25,	0,	0,
	4000,	0,	0,	25,	7,	0,
	0,	0,	0,	200,	0,	0,
	3700,	0,	0,	25,	0,	0,
	4000,	0,	0,	25,	7,	3,
	0,	0,	0,	2000,	0,	0,
		-1,	-1,	-1,	-1,	0,	0}, //CDMA HIGH PBX SS tone: {3700Hz 25ms, 4000Hz 25ms} 8 times 200 ms OFF, {3700Hz 25ms 4000Hz 25ms}8 times, 2000ms OFF, REPEAT....

	{2600,	0,	0,	25,	0,	0,
	2900,	0,	0,	25,	7,	0,
	0,	0,	0,	200,	0,	0,
	2600,	0,	0,	25,	0,	0,
	2900,	0,	0,	25,	7,	3,
	0,	0,	0,	2000,	0,	0,
		-1,	-1,	-1,	-1,	0,	0 }, //CDMA MED PBX SS tone: {2600Hz 25ms, 2900Hz 25ms} 8 times 200 ms OFF, {2600Hz 25ms 2900Hz 25ms}8 times, 2000ms OFF, REPEAT....

	{1300,	0,	0,	25,	0,	0,
	1450,	0,	0,	25,	7,	0,
	0,	0,	0,	200,	0,	0,
	1300,	0,	0,	25,	0,	0,
	1450,	0,	0,	25,	7,	3,
	0,	0,	0,	2000,	0,	0,
		-1,	-1,	-1,	-1,	0,	0 },//CDMA LOW PBX SS tone: {1300Hz 25ms, 1450Hz 25ms} 8 times 200 ms OFF, {1300Hz 25ms 1450Hz 25ms}8 times, 2000ms OFF, REPEAT....

	{3700,	0,	0,	25,	0,	0,
	4000,	0,	0,	25,	7,	0,
	0,	0,	0,	200,	0,	0,
	3700,	0,	0,	25,	0,	0,
	4000,	0,	0,	25,	7,	3,
	0,	0,	0,	200,	0,	0,
	3700,	0,	0,	25,	0,	0,
	4000,	0,	0,	25,	15,	6,
	0,	0,	0,	1000,	0,	0,
		-1,	-1,	-1,	-1,	0,	0}, //CDMA HIGH PBX SSL tone:{3700Hz 25ms, 4000Hz 25ms} 8 times 200ms OFF, {3700Hz 25ms, 4000Hz 25ms} 8 times, 200ms OFF, {3700Hz 25ms, 4000Hz 25ms} 16 times, 1000ms OFF, REPEAT....//

	{2600,	0,	0,	25,	0,	0,
	2900,	0,	0,	25,	7,	0,
	0,	0,	0,	200,	0,	0,
	2600,	0,	0,	25,	0,	0,
	2900,	0,	0,	25,	7,	3,
	0,	0,	0,	200,	0,	0,
	2600,	0,	0,	25,	0,	0,
	2900,	0,	0,	25,	15,	6,
	0,	0,	0,	1000,	0,	0,
		-1,	-1,	-1,	-1,	0,	0}, //CDMA MED PBX SSL tone:{2600Hz 25ms, 2900Hz 25ms} 8 times 200ms OFF, {2600Hz 25ms, 2900Hz 25ms} 8 times, 200ms OFF, {2600Hz 25ms, 2900Hz 25ms} 16 times, 1000ms OFF, REPEAT....//

	{1300,	0,	0,	25,	0,	0,
	1450,	0,	0,	25,	7,	0,
	0,	0,	0,	200,	0,	0,
	1300,	0,	0,	25,	0,	0,
	1450,	0,	0,	25,	7,	3,
	0,	0,	0,	200,	0,	0,
	1300,	0,	0,	25,	0,	0,
	1450,	0,	0,	25,	15,	6,
	0,	0,	0,	1000,	0,	0,
		-1,	-1,	-1,	-1,	0,	0}, //CDMA LOW PBX SSL tone:{1300Hz 25ms, 1450Hz 25ms} 8 times 200ms OFF, {1300Hz 25ms, 1450Hz 25ms} 8 times, 200ms OFF, {1300Hz 25ms, 1450Hz 25ms} 16 times, 1000ms OFF, REPEAT....//

	{3700,	0,	0,	25,	0,	0,
	4000,	0,	0,	25,	15,	0,
	0,	0,	0,	200,	0,	0,
	3700,	0,	0,	25,	0,	0,
	4000,	0,	0,	25,	7,	3,
	0,	0,	0,	1000,	0,	0,
		-1,	-1,	-1,	-1,	0,	0 },//CDMA HIGH PBX SLS tone:{3700Hz 25ms, 4000Hz 25ms} 8 times 200ms OFF, {3700Hz 25ms, 4000Hz 25ms} 16 times, 200ms OFF, {3700Hz 25ms, 4000Hz 25ms} 8 times, 1000ms OFF, REPEAT.... //

	{2600,	0,	0,	25,	0,	0,
	2900,	0,	0,	25,	15,	0,
		0,	0,	0,	200,	0,	0,
	2600,	0,	0,	25,	0,	0,
	2900,	0,	0,	25,	7,	3,
		0,	0,	0,	1000,0,	0,
		-1,	-1,	-1,	-1,	0,	0 }, //CDMA HIGH PBX SLS tone:{2600Hz 25ms, 2900Hz 25ms} 8 times 200ms OFF, {2600Hz 25ms, 2900Hz 25ms} 16 times, 200ms OFF, {2600Hz 25ms, 2900Hz 25ms} 8 times, 1000ms OFF, REPEAT....//

	{1300,	0,	0,	25,	0,	0,
	1450,	0,	0,	25,	15,	0,
	0,	0,	0,	200,	0,	0,
	1300,	0,	0,	25,	0,	0,
	1450,	0,	0,	25,	7,	3,
	0,	0,	0,	1000,	0,	0,
		-1,	-1,	-1,	-1,	0,	0 }, //CDMA HIGH PBX SLS tone:{1300Hz 25ms, 1450Hz 25ms} 8 times 200ms OFF, {1300Hz 25ms, 1450Hz 25ms} 16 times, 200ms OFF, {1300Hz 25ms, 1450Hz 25ms} 8 times, 1000ms OFF, REPEAT....//

	{3700,	0,	0,	25,	0,	0,
	4000,	0,	0,	25,	7,	0,
	0,	0,	0,	200,	0,	0,
	3700,	0,	0,	25,	0,	0,
	4000,	0,	0,	25,	7,	3,
	0,	0,	0,	200,	0,	0,
	3700,	0,	0,	25,	0,	0,
	4000,	0,	0,	25,	7,	6,
	0,	0,	0,	200,	0,	0,
	3700,	0,	0,	25,	0,	0,
	4000,	0,	0,	25,	7,	9,
	0,	0,	0,	800,	0,	0,
		-1,	-1,	-1,	-1,	0,	0}, //CDMA HIGH PBX X S4 tone: {3700Hz 25ms 4000Hz 25ms} 8 times, 200ms OFF, {3700Hz 25ms 4000Hz 25ms} 8 times, 200ms OFF, {3700Hz 25ms 4000Hz 25ms} 8 times, 200ms OFF, {3700Hz 25ms 4000Hz 25ms} 8 times, 800ms OFF, REPEAT...

	{2600,	0,	0,	25,	0,	0,
	2900,	0,	0,	25,	7,	0,
	0,	0,	0,	200,	0,	0,
	2600,	0,	0,	25,	0,	0,
	2900,	0,	0,	25,	7,	3,
	0,	0,	0,	200,	0,	0,
	2600,	0,	0,	25,	0,	0,
	2900,	0,	0,	25,	7,	6,
	0,	0,	0,	200,	0,	0,
	2600,	0,	0,	25,	0,	0,
	2900,	0,	0,	25,	7,	9,
	0,	0,	0,	800,	0,	0,
		-1,	-1,	-1,	-1,	0,	0}, //CDMA MED PBX X S4 tone: {2600Hz 25ms 2900Hz 25ms} 8 times, 200ms OFF, {2600Hz 25ms 2900Hz 25ms} 8 times, 200ms OFF, {2600Hz 25ms 2900Hz 25ms} 8 times, 200ms OFF, {2600Hz 25ms 2900Hz 25ms} 8 times, 800ms OFF, REPEAT...

	{1300,	0,	0,	25,	0,	0,
	1450,	0,	0,	25,	7,	0,
		0,	0,	0,	200,	0,	0,
	1300,	0,	0,	25,	0,	0,
	1450,	0,	0,	25,	7,	3,
		0,	0,	0,	200,	0,	0,
	1300,	0,	0,	25,	0,	0,
	1450,	0,	0,	25,	7,	6,
		0,	0,	0,	200,	0,	0,
	1300,	0,	0,	25,	0,	0,
	1450,	0,	0,	25,	7,	9,
		0,	0,	0,	800,	0,	0,
		-1,	-1,	-1,	-1,	0,	0}, //CDMA LOW PBX X S4 tone: {1300Hz 25ms 1450Hz 25ms} 8 times, 200ms OFF, {1300Hz 25ms 1450Hz 25ms} 8 times, 200ms OFF, {1300Hz 25ms 1450Hz 25ms} 8 times, 200ms OFF, {1300Hz 25ms 1450Hz 25ms} 8 times, 800ms OFF, REPEAT...

	{1109,	0,	0,	62,	0,	0,
		784,	0,	0,	62,	0,	0,
		740,	0,	0,	62,	0,	0,
		622,	0,	0,	62,	0,	0,
	1109,	0,	0,	62,	0,	0,
		-1,	-1,	-1,	-1,	0,	0}, //CDMA Alert Network Lite tone: 1109Hz 62ms ON, 784Hz 62ms ON, 740Hz 62ms ON 622Hz 62ms ON, 1109Hz 62ms ON

	{1245,	0,	0,	62,	0,	0,
	659,	0,	0,	62,	0,	0,
	1245,	0,	0,	62,	0,	0,
	659,	0,	0,	62,	0,	0,
	1245,	0,	0,	62,	0,	0,
	659,	0,	0,	62,	0,	0,
	1245,	0,	0,	62,	0,	0,
		-1,	-1,	-1,	-1,	0,	0},//CDMA Alert Auto Redial tone: {1245Hz 62ms ON, 659Hz 62ms ON} 3 times, 1245 62ms ON//

	{1150,	770, 0,  400, 0,	0,
		-1,	-1,	-1,	-1,	0,	0}, //CDMA One Min Beep tone: 1150Hz+770Hz 400ms ON//

	{941, 1477,	0,  120, 0,	0,
		-1,	-1,	-1,	-1,	0,	0}, //CDMA KEYPAD Volume key lite tone: 941Hz+1477Hz 120ms ON

	{587,	0,	 0, 375, 0,	0,
	1175,	0,	 0, 125, 0,	0,
		-1,	-1,	-1,	-1,	0,	0}, //CDMA PRESSHOLDKEY LITE tone: 587Hz 375ms ON, 1175Hz 125ms ON

	{587,	0,	0,	62, 0,	0,
	784,		0,	0,	62, 0,	0,
	831,		0,	0,	62, 0,	0,
	784,		0,	0,	62, 0,	0,
	1109,	0,	0,	62, 0,	0,
	784, 	0,	0,	62, 0,	0,
	831,		0,	0,	62, 0,	0,
	784, 	0,	0,	62, 0,	0,
		-1,	-1,	-1,	-1,	0,	0}, //CDMA ALERT INCALL LITE tone: 587Hz 62ms, 784 62ms, 831Hz 62ms, 784Hz 62ms, 1109 62ms, 784Hz 62ms, 831Hz 62ms, 784Hz 62ms

	{941,	0,	0,	125,	0,	0,
		0,	0,	0,	10,	0,	0,
		941,	0,	0,	125,	0,	0,
		0,	0,	0,	10,	0,	0,
	1245,	0,	0,	62,	0,	0,
		0,	0,	0,	10,	0,	0,
		0,	0,	0,	4990, 0,	0,
		-1,	-1,	-1,	-1,	0,	0}, //CDMA EMERGENCY RINGBACK tone: {941Hz 125ms ON, 10ms OFF} 3times 4990ms OFF, REPEAT...

	{1319,	0,	0,	125,	0,	0,
		0,	0,	0,	125,	0,	0,
		-1,	-1,	-1,	-1,	3,	0 }, //CDMA ALERT CALL GUARD tone: {1319Hz 125ms ON, 125ms OFF} 3 times

	{1047,	0,	0,	125,	0,	0,
		370,	0,	0,	125,	0,	0,
		-1,	-1,	-1,	-1,	0,	0}, //CDMA SOFT ERROR LITE tone: 1047Hz 125ms ON, 370Hz 125ms

	{1480,	0,	0,	125,	0,	0,
	1397,	0,	0,	125,	0,	0,
	784,	0,	0,	125,	0,	0,
		-1,	-1,	-1,	-1,	0,	0}, //CDMA CALLDROP LITE tone: 1480Hz 125ms, 1397Hz 125ms, 784Hz 125ms//

	{425,	0,	0,	125,	0,	0,
		0,	0,	0,	125,	0,	0,
		-1,	-1,	-1,	-1,	0,	0},//CDMA_NETWORK_BUSY_ONE_SHOT tone: 425Hz 500ms ON, 500ms OFF.

	{1150,	770,	0,	400,	0,	0,
		-1,	-1,	-1,	-1,	0,	0}, //CDMA_ABBR_ALERT tone: 1150Hz+770Hz 400ms ON

	{0,	0,	0,	-1,	0,	0,
	-1,	-1,	-1,	-1,	0,	0}, //CDMA_SIGNAL_OFF - silent tone
 };

static tone_control_t g_control;
static int (*g_thread_pool_func)(void*, void (*)(void*)) = NULL;

static int _MMSoundToneInit(void);
static int _MMSoundToneFini(void);
static void _running_tone(void *param);



int* MMSoundPlugCodecToneGetSupportTypes(void)
{
    debug_enter("\n");
    static int suported[2] = {MM_SOUND_SUPPORTED_CODEC_DTMF, 0};
    debug_leave("\n");
    return suported;
}

int MMSoundPlugCodecToneParse(MMSourceType *source, mmsound_codec_info_t *info)
{
	debug_enter("\n");
	// do nothing
	debug_leave("\n");
	return MM_ERROR_NONE;
}

int MMSoundPlugCodecToneCreate(mmsound_codec_param_t *param, mmsound_codec_info_t *info, MMHandleType *handle)
{
	avsys_audio_param_t audio_param;
	tone_info_t *toneInfo;

	int result = AVSYS_STATE_SUCCESS;

	debug_enter("\n");

	toneInfo = (tone_info_t *)malloc(sizeof(tone_info_t));
	if (toneInfo == NULL) {
		debug_error("memory allocation error\n");
		return MM_ERROR_OUT_OF_MEMORY;
	}

	memset(toneInfo, 0, sizeof(tone_info_t));

	toneInfo->state = STATE_READY;

	/* set audio param */
	memset (&audio_param, 0, sizeof(avsys_audio_param_t));

	/* Set sound player parameter */
	audio_param.samplerate = SAMPLERATE;
	audio_param.channels = CHANNELS;
	audio_param.format = AVSYS_AUDIO_FORMAT_16BIT;
	audio_param.mode = AVSYS_AUDIO_MODE_OUTPUT;
	audio_param.vol_type = param->volume_config;
	audio_param.priority = AVSYS_AUDIO_PRIORITY_0;

	result = avsys_audio_open(&audio_param, &toneInfo->audio_handle, &toneInfo->size);
	if (AVSYS_FAIL(result)) {
		debug_error("Device Open Error 0x%x\n", result);
		goto Error;
	}
	debug_log("Create audio_handle is %d\n", toneInfo->audio_handle);

	debug_msg("tone : %d\n", param->tone);
	debug_msg("repeat : %d\n", param->repeat_count);
	debug_msg("volume config : %x\n", param->volume_config);
	debug_msg("callback : %p\n", param->stop_cb);
	debug_msg("pid : %d\n", param->pid);

	toneInfo->number = param->tone;
	toneInfo->time = param->repeat_count;
	toneInfo->stop_cb = param->stop_cb;
	toneInfo->cb_param = param->param;
	toneInfo->pid = param->pid;
	toneInfo->volume = param->volume;

	result = g_thread_pool_func(toneInfo, _running_tone);
	if (result != 0) {
		debug_error("pthread_create() fail in pcm thread\n");
		result = MM_ERROR_SOUND_INTERNAL;
		goto Error;
	}

	*handle = (MMHandleType)toneInfo;

	debug_leave("\n");
	return MM_ERROR_NONE;

Error:
	if(toneInfo) {
		if(toneInfo->audio_handle)
			avsys_audio_close(toneInfo->audio_handle);

		free(toneInfo);
	}

	return result;

}

int MMSoundPlugCodecToneDestroy(MMHandleType handle)
{
	tone_info_t *toneInfo = (tone_info_t*) handle;
	int err = MM_ERROR_NONE;
	if (!toneInfo) {
		debug_critical("Confirm the hadle (is NULL)\n");
		return MM_ERROR_SOUND_INTERNAL;
	}

	debug_enter("(handle %x)\n", handle);

	if (toneInfo)
		free (toneInfo);

	debug_leave("\n");
	return err;
}

static
int MMSoundPlugCodecTonePlay(MMHandleType handle)
{
	tone_info_t *toneInfo = (tone_info_t *) handle;

	debug_enter("(handle %x)\n", handle);

	toneInfo->state = STATE_BEGIN;
	debug_msg("sent start signal\n");

	debug_leave("\n");

	return MM_ERROR_NONE;
}

static char*
_create_tone (double *sample, TONE _TONE, double volume, int *toneSize)
{
	short *pbuf;
	double i = 0;
	double amplitude, f1, f2, f3;
	int quota= 0;
	float low_frequency		= _TONE.low_frequency;
	float middle_frequency	= _TONE.middle_frequency;
	float high_frequency		= _TONE.high_frequency;
	int sample_size = 0;

	if(sample == NULL) {
		debug_error("Sample buffer is not allocated\n");
		return NULL;
	}

	/* Create a buffer for the tone */
	if((_TONE.playingTime >  MAX_DURATION) || (_TONE.playingTime == -1) ) {
		*toneSize = ((MAX_DURATION / 1000.) * SAMPLERATE * SAMPLE_SIZE * CHANNELS) / 8;
	} else	 {
		*toneSize = ((_TONE.playingTime / 1000.) * SAMPLERATE * SAMPLE_SIZE * CHANNELS) / 8;
	}
	*toneSize = ((*toneSize+1)>>1)<<1;
	sample_size = (*toneSize) / (SAMPLE_SIZE / 8);

	debug_log("_TONE.playing_time: %d toneSize: %d\n", _TONE.playingTime, *toneSize);
	char* buffer = g_malloc (*toneSize);

	if(buffer == NULL) {
		debug_error("Buffer is not allocated\n");
		return NULL;
	} else {
		pbuf = (short*)buffer;

		if(_TONE.low_frequency > 0) {
			quota++;
		}
		if(_TONE.middle_frequency > 0) {
			quota++;
		}
		if(_TONE.high_frequency > 0) {
			quota++;
		}

		for (i = 0; i < sample_size; i++) {
			/*
			 * We add the fundamental frequencies together.
			 */

			f1 = sin (2 * M_PI * low_frequency     	* ((*sample) / SAMPLERATE));
			f2 = sin (2 * M_PI * middle_frequency	* ((*sample) / SAMPLERATE));
			f3 = sin (2 * M_PI * high_frequency	* ((*sample) / SAMPLERATE));

			if(f1 + f2 + f3 != 0) {
				amplitude = (f1 + f2 + f3) / quota;
				/* Adjust the volume */
				amplitude *= volume;

				/* Make the [-1:1] interval into a [-32767:32767] interval */
				amplitude *= 32767;
			} else {
				amplitude = 0;
			}

			/* Store it in the data buffer */
			*(pbuf++) = (short) amplitude;

			(*sample)++;
		}
	}
	return buffer;
}

static TONE
_mm_get_tone(int key, int CurIndex)
{
	TONE _TONE;

	_TONE.low_frequency		= TONE_SEGMENT[key][CurIndex * TONE_COLUMN + LOW_FREQUENCY];
	_TONE.middle_frequency	= TONE_SEGMENT[key][CurIndex * TONE_COLUMN + MIDDLE_FREQUENCY];
	_TONE.high_frequency		= TONE_SEGMENT[key][CurIndex * TONE_COLUMN + HIGH_FREQUENCY];
	_TONE.playingTime			= TONE_SEGMENT[key][CurIndex * TONE_COLUMN + PLAYING_TIME];
	_TONE.loopCnt			= TONE_SEGMENT[key][CurIndex * TONE_COLUMN + LOOP_COUNT];
	_TONE.loopIndx			= TONE_SEGMENT[key][CurIndex * TONE_COLUMN + LOOP_INDEX];

	return _TONE;
}

static int
_mm_get_waveCnt_PlayingTime(int toneTime, TONE _TONE, int *waveCnt, int *waveRestPlayTime)
{
	int ret = MM_ERROR_NONE;
	if(waveCnt == NULL || waveRestPlayTime == NULL) {
		debug_error("waveCnt || waveRestPlayTime buffer is NULL\n");
		return MM_ERROR_SOUND_INTERNAL;
	}
	/*Set  wave count and wave playing time*/
	if( _TONE.playingTime == -1) {
		*waveCnt = abs(toneTime) /MAX_DURATION;
		*waveRestPlayTime =  abs(toneTime) % MAX_DURATION;
	} else {
		*waveCnt = _TONE.playingTime /MAX_DURATION;
		*waveRestPlayTime =  _TONE.playingTime % MAX_DURATION;
	}
	return ret;
}

static int
_mm_get_CurIndex(TONE _TONE, int *CurArrayPlayCnt, int *CurIndex)
{
	int ret = MM_ERROR_NONE;
	if(CurArrayPlayCnt == NULL || CurIndex == NULL) {
		debug_error("CurArrayPlayCnt || CurIndex buffer is NULL\n");
		return MM_ERROR_SOUND_INTERNAL;
	}

	if(_TONE.loopCnt != 0 && *CurArrayPlayCnt <= _TONE.loopCnt) {
		(*CurArrayPlayCnt)++;
		if(*CurArrayPlayCnt >_TONE.loopCnt) {
			(*CurIndex)++;
			*CurArrayPlayCnt = 0;
		} else {
			*CurIndex = _TONE.loopIndx;
		}
	} else {
		(*CurIndex)++;
	}
	debug_log("CurIndex: %d, CurArrayPlayCnt :%d", *CurIndex, *CurArrayPlayCnt);
	return ret;
}

static void _running_tone(void *param)
{
	int result = AVSYS_STATE_SUCCESS;
	char *ptoneBuf = NULL;
	char filename[100];

	if(param == NULL) {
		debug_error("param Buffer is not allocated\n");
		return;
	}
	tone_info_t *toneInfo = (tone_info_t*) param;
	int toneKey = 0;
	int toneTime =0;
	int prePlayingTime = 0;
	int duration = 0;
	int playingTime = 0;
	int toneSize = 0;
	int CurWaveIndex = 0;
	int numWave = 0;
	int waveRestPlayTime = 0;
	int CurIndex = 0;
	int CurArrayPlayCnt = 0;

	debug_enter("\n");
	double sample = 0;
	toneTime = toneInfo->time;
	if(toneTime != 0) {
		toneKey = toneInfo->number;
		debug_msg ("toneKey number = %d\n", toneKey);

		/* Set pid check file */
		snprintf(filename, sizeof(filename), "/proc/%d/cmdline", toneInfo->pid);

		debug_msg("Wait start signal\n");
		while(toneInfo->state == STATE_READY)
			usleep(10);
		debug_msg("Recv start signal\n");
		toneInfo->state = STATE_PLAY;
	} else {
		return;
	}

	while (1) {
		TONE _TONE = _mm_get_tone(toneKey, CurIndex); /*Pop one of Tone Set */
		if(_mm_get_waveCnt_PlayingTime(toneTime, _TONE, &numWave, &waveRestPlayTime) != MM_ERROR_NONE) {
			debug_error("_mm_get_waveCnt_PlayingTime return value error\n");
			goto exit;
		}
		debug_log ("Predefined Tone[%d] Total Play time (ms) : %d, _TONE.playing_time: %d _numWave = %d low_frequency: %0.f, middle_frequency: %0.f, high_frequency: %0.f\n",
			CurIndex, toneTime, _TONE.playingTime, numWave, _TONE.low_frequency, _TONE.middle_frequency, _TONE.high_frequency);

		if (_TONE.low_frequency == -1) { /* skip frequency which's value is -1*/
			CurIndex = _TONE.loopIndx;
			continue;
		}

		/* Write pcm data */

		for(CurWaveIndex = 0; CurWaveIndex < numWave+1; CurWaveIndex++) {
			if(CurWaveIndex == numWave ) { /* play the last tone set*/
				playingTime = waveRestPlayTime;
			} else {
				playingTime = MAX_DURATION;
			}
			duration = playingTime;

			if(playingTime == 0) {
				break;
			}

			if(prePlayingTime + playingTime > toneTime && toneTime != -1) {
				playingTime = toneTime - prePlayingTime;
			}

			ptoneBuf = _create_tone (&sample, _TONE, toneInfo->volume, &toneSize);
			if(ptoneBuf == NULL) {
				debug_error("Tone Buffer is not allocated\n");
				goto exit;
			}
			debug_log ("[TONE] Play.....%dth %dms\n", CurWaveIndex, playingTime);
			avsys_audio_write(toneInfo->audio_handle, ptoneBuf, ((toneSize * playingTime /duration + 1)>>1)<<1);
			prePlayingTime += playingTime;
			debug_log ("previous_sum: %d\n", prePlayingTime);
			g_free (ptoneBuf);
			ptoneBuf = NULL;

			if(prePlayingTime == toneTime || toneInfo->state != STATE_PLAY) {
				debug_log ("Finished.....on Total Playing Time : %d _TONE.playing_time: %d\n", prePlayingTime, _TONE.playingTime);
				avsys_audio_drain(toneInfo->audio_handle);
				debug_log ("Finished.....quit loop\n");
				goto exit;
			}
		}
		if(_mm_get_CurIndex(_TONE, &CurArrayPlayCnt, &CurIndex) != MM_ERROR_NONE) {
			debug_error("_mm_get_CurIndex return value error\n");
			goto exit;
		}

		debug_log ("CurIndex: %d previous_sum: %d CurArrayPlayCnt: %d\n", CurIndex, prePlayingTime, CurArrayPlayCnt);
	}

exit :
	result = avsys_audio_close(toneInfo->audio_handle);
	if(AVSYS_FAIL(result))	{
		debug_error("Device Close Error 0x%x\n", result);
	}

	pthread_mutex_destroy(&g_control.syncker);

	debug_msg("Play end\n");
	toneInfo->state = STATE_STOP;

	if (toneInfo->stop_cb)
	    toneInfo->stop_cb(toneInfo->cb_param);

	debug_leave("\n");
}

static
int MMSoundPlugCodecToneStop(MMHandleType handle)
{
	tone_info_t *toneInfo = (tone_info_t*) handle;

	debug_enter("(handle %x)\n", handle);
	toneInfo->state = STATE_STOP;
	debug_msg("sent stop signal\n");
	debug_leave("\n");

	return MM_ERROR_NONE;
}

static
int MMSoundPlugCodecToneSetThreadPool(int (*func)(void*, void (*)(void*)))
{
    debug_enter("(func : 0x%x)\n", func);
    g_thread_pool_func = func;
    debug_leave("\n");
    return MM_ERROR_NONE;
}

static int _MMSoundToneInit(void)
{
	memset(&g_control, 0, sizeof(tone_control_t));
	pthread_mutex_init(&g_control.syncker, NULL);
	return MM_ERROR_NONE;
}

static int _MMSoundToneFini(void)
{
	pthread_mutex_destroy(&g_control.syncker);
	return MM_ERROR_NONE;
}

EXPORT_API
int MMSoundGetPluginType(void)
{
    debug_enter("\n");
    debug_leave("\n");
    return MM_SOUND_PLUGIN_TYPE_CODEC;
}

EXPORT_API
int MMSoundPlugCodecGetInterface(mmsound_codec_interface_t *intf)
{
    debug_enter("\n");

    intf->GetSupportTypes   = MMSoundPlugCodecToneGetSupportTypes;
    intf->Parse             = MMSoundPlugCodecToneParse;
    intf->Create            = MMSoundPlugCodecToneCreate;
    intf->Destroy           = MMSoundPlugCodecToneDestroy;
    intf->Play              = MMSoundPlugCodecTonePlay;
    intf->Stop              = MMSoundPlugCodecToneStop;
    intf->SetThreadPool     = MMSoundPlugCodecToneSetThreadPool;

    debug_leave("\n");

    return MM_ERROR_NONE;
}

