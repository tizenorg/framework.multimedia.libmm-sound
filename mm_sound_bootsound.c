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
#include <memory.h>
#include <unistd.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/types.h>
#include <fcntl.h>
#include <vconf.h>

#include <sys/stat.h>
#include <errno.h>

#include <semaphore.h>
#include <errno.h>

#include <mm_types.h>
#include <mm_error.h>
#include <mm_message.h>
#include <mm_debug.h>
#include <mm_sound.h>
#include <mm_sound_private.h>

#define VCONF_BOOTING "memory/private/sound/booting"
#define MAX_RETRY 40
#define RETRY_INTERVAL_USEC 50000

EXPORT_API
int mm_sound_boot_ready(int timeout_sec)
{
    struct timespec ts;
    sem_t* sem = NULL;

    debug_msg("[BOOT] check for sync....");
    if ((sem = sem_open ("booting-sound", O_CREAT, 0660, 0))== SEM_FAILED) {
        debug_error ("error creating sem : %d", errno);
        return -1;
    }

    debug_msg("[BOOT] start to wait ready....timeout is set to %d sec", timeout_sec);
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout_sec;

    if (sem_timedwait(sem, &ts) == -1) {
        if (errno == ETIMEDOUT)
            debug_warning("[BOOT] timeout!\n");
    } else {
        debug_msg("[BOOT] ready wait success!!!!");
        sem_post(sem);
    }

    return 0;
}

EXPORT_API
int mm_sound_boot_play_sound(char* path)
{
    debug_msg("[BOOT] set vconf to play boot sound [%s]!!!!", path);
    if (path == NULL)
        return -1;

    return vconf_set_str(VCONF_BOOTING, path);
}


