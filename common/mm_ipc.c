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
#include <errno.h>

#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <semaphore.h>
#include <fcntl.h>
#include <unistd.h>

#include <aio.h>

#include <mm_ipc.h>
#include <mm_error.h>
#include <mm_debug.h>

#define FIX_PROCESS 1

#include <assert.h>
#include <sys/ipc.h>
#include <sys/msg.h>

#include <vconf.h>

#define AUDIO_ROUTE_POLICY_LOCK "audio_route_policy_lock"
#define LOCK_TIMEOUT_SEC 6

EXPORT_API
int __mm_sound_lock()
{
    sem_t *sem = NULL;
    int ret;
    int err = MM_ERROR_NONE;
    struct timespec wait_time;

    sem = sem_open(AUDIO_ROUTE_POLICY_LOCK, O_CREAT, 0666, 1);
    if (sem == SEM_FAILED)
    {
        debug_error("Semaphore open Fail! (name:%s, %s)\n", AUDIO_ROUTE_POLICY_LOCK, strerror(errno));
        return MM_ERROR_SOUND_INTERNAL;
    }
retry_lock:
	wait_time.tv_sec = (long int)(time(NULL)) + LOCK_TIMEOUT_SEC;
	wait_time.tv_nsec = 0;
	ret = sem_timedwait(sem, &wait_time);
    if(ret == -1)
    {
        switch(errno)
        {
        case EINTR:
        	debug_error("Lock RETRY LOCK\n");
            goto retry_lock;
            break;
        case EINVAL:
        	debug_error("Invalid semaphore\n");
            err = MM_ERROR_SOUND_INTERNAL;
            break;
        case EAGAIN:
            debug_error("EAGAIN\n");
            err = MM_ERROR_SOUND_INTERNAL;
            break;
        case ETIMEDOUT:
            debug_error("sem_wait leached %d seconds timeout.\n", LOCK_TIMEOUT_SEC);
            {
            	//Recovery of sem_wait lock....in abnormal condition
            	int sem_value = -1;
            	if(0 == sem_getvalue(sem, &sem_value))
            	{
            		debug_error("%s sem value is %d\n",AUDIO_ROUTE_POLICY_LOCK, sem_value);
            		if(sem_value == 0)
            		{
            			ret = sem_post(sem);
            			if(ret == -1)
            			{
            				debug_error("sem_post error %s : %d\n", AUDIO_ROUTE_POLICY_LOCK, sem_value);
            			}
            			else
            			{
            				debug_error("lock recovery success...try lock again\n");
            				goto retry_lock;
            			}
            		}
            		else
            		{
            			debug_error("sem value is not 0. but failed sem_timedwait so retry.. : %s\n",AUDIO_ROUTE_POLICY_LOCK);
            			usleep(5);
            			goto retry_lock;
            		}
            	}
            	else
            	{
            		debug_error("sem_getvalue failed : %s\n",AUDIO_ROUTE_POLICY_LOCK);
            	}
            }
            err = MM_ERROR_SOUND_INTERNAL;
            break;
        }
    }
    sem_close(sem);
    return err;
}

EXPORT_API
int __mm_sound_unlock()
{
    sem_t *sem = NULL;
    int ret;
    int err = MM_ERROR_NONE;

    sem = sem_open(AUDIO_ROUTE_POLICY_LOCK, O_CREAT, 0666, 1);
    if (sem == SEM_FAILED)
    {
        debug_error("Semaphore open Fail! (name:%s, errno %d)\n", AUDIO_ROUTE_POLICY_LOCK, errno);
        return MM_ERROR_SOUND_INTERNAL;
    }

    ret = sem_post(sem);
    if (ret == -1)
    {
        debug_error("UNLOCK FAIL\n");
        err = MM_ERROR_SOUND_INTERNAL;
    }

    sem_close(sem);
    return err;
}

