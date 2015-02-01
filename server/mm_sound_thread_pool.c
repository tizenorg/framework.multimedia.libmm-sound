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
#include <pthread.h>
#include <unistd.h>

#include <mm_error.h>
#include <mm_debug.h>
#include <mm_sound_thread_pool.h>

#define USE_G_THREAD_POOL
#ifdef USE_G_THREAD_POOL
#include <glib.h>
GThreadPool* g_pool;

#define MAX_UNUSED_THREADS_IN_THREADPOOL	10

typedef struct __THREAD_INFO
{
	void (*func)(gpointer data);
	void *param;
} THREAD_INFO;

static void __DummyWork (void* param)
{
	debug_msg ("thread index = %d\n", (int)param);
	sleep (1);
}

static void __ThreadWork(gpointer data, gpointer user_data)
{
	THREAD_INFO* info = (THREAD_INFO*)data;
	if (info) {
		 if (info->func) {
				debug_log ("Calling [%p] with param [%p]\n", info->func, info->param);
				info->func (info->param);
		 } else {
			 	debug_warning ("No func to call....\n");
		 }

		 /* Info was allocated by MMSoundThreadPoolRun(). 
			The actual content of info should be  freed whether inside func or outside (if handle) */
		 debug_log ("free [%p]\n", info);
		 free (info);
		 info = NULL;
	} else {
		debug_warning ("No valid thread info...Nothing to do...\n");
	}
}

int MMSoundThreadPoolDump(int fulldump)
{
	if (g_pool == NULL) {
		debug_error ("No thread pool initialized....\n");
		return MM_ERROR_SOUND_INTERNAL;
	}

	if (fulldump) {
		debug_log ("##### [ThreadPool] max threads=[%d], max unused=[%d], max idle time=[%d]\n",
				g_thread_pool_get_max_threads (g_pool),
				g_thread_pool_get_max_unused_threads(),
				g_thread_pool_get_max_idle_time()	);
	}
	debug_log ("***** [ThreadPool] running=[%d], unused=[%d]\n",
			g_thread_pool_get_num_threads (g_pool),
			g_thread_pool_get_num_unused_threads() );

	return MM_ERROR_NONE;
}

int MMSoundThreadPoolInit()
{
	int i=0;
	GError* error = NULL;

	/* Create thread pool (non-exclude mode with infinite max threads) */
	g_pool = g_thread_pool_new (__ThreadWork, NULL, -1, FALSE, &error);
	if (g_pool == NULL && error != NULL) {
		debug_error ("thread pool created failed : %s\n", error->message);
		g_error_free (error);
		return MM_ERROR_SOUND_INTERNAL;
	}
	debug_msg ("thread pool created successfully\n");

	MMSoundThreadPoolDump(TRUE);

	/* Thread pool setting : this will maintain at least 10 unused threads and this will be reused. */
	/* If no unused thread left, new thread will be created, but always maintain 10 unused thread */
	debug_msg ("thread pool set max unused threads to %d\n", MAX_UNUSED_THREADS_IN_THREADPOOL);
	g_thread_pool_set_max_unused_threads (MAX_UNUSED_THREADS_IN_THREADPOOL);

	/* To reserve unused threads, let's start some threads for beggining 
	   This dummy thread will be remained unused as soon as it started */	 
	debug_msg ("run threads to reserve minimum thread\n");
	for (i=0; i<MAX_UNUSED_THREADS_IN_THREADPOOL; i++) {
		MMSoundThreadPoolRun ((void *)i, __DummyWork);
	}

	MMSoundThreadPoolDump(TRUE);

     return MM_ERROR_NONE;
}

int MMSoundThreadPoolRun(void *param, void (*func)(void*))
{
	GError* error = NULL;

	/* Dump current thread pool */
	MMSoundThreadPoolDump(FALSE);

	/* Create thread info structure. 
	   This thread info data will be free in __ThreadWork(), after use. */
	THREAD_INFO* thread_info = (THREAD_INFO*)malloc (sizeof(THREAD_INFO));
	if (thread_info) {
		thread_info->func = func;
		thread_info->param = param;
		debug_log ("alloc thread_info = %p\n", thread_info);

		/* Add thread to queue of thread pool */
		g_thread_pool_push (g_pool, thread_info, &error);
		if (error) {
			debug_error ("g_thread_pool_push failed : %s\n", error->message);
			g_error_free (error);
			free(thread_info);
			return MM_ERROR_SOUND_INTERNAL;
		}
	} else {
		debug_error("failed to alloc thread info\n");
		return MM_ERROR_SOUND_INTERNAL;
	}

	return MM_ERROR_NONE;
}

int MMSoundThreadPoolFini(void)
{
	/* If immediate is TRUE, no new task is processed for pool. 
	Otherwise pool is not freed before the last task is processed. 
	Note however, that no thread of this pool is interrupted, while processing a task. 
	Instead at least all still running threads can finish their tasks before the pool is freed.

	If wait_ is TRUE, the functions does not return before all tasks to be processed 
	(dependent on immediate, whether all or only the currently running) are ready. 
	Otherwise the function returns immediately.	*/
	debug_msg ("thread pool will be free\n");
	g_thread_pool_free (g_pool, TRUE, FALSE);
    
	return MM_ERROR_NONE;
}

#else // USE_G_THREAD_POOL

#define THREAD_POOL_MAX 10

struct __control_t
{
	pthread_t threadid;
	pthread_cond_t condition;
	int stopflag;
	void *param;
	void (*func)(void *);
};

static int threadmap[THREAD_POOL_MAX];
static struct __control_t control[THREAD_POOL_MAX];

static pthread_cond_t startcond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t startsync = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t controlsync = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t funcsync = PTHREAD_MUTEX_INITIALIZER;

static int __GetEmptyPool(void)
{
	int count = 0;
	while ((count < THREAD_POOL_MAX) && threadmap[count] == 1)
		++count;
	return count == THREAD_POOL_MAX ? -1 : count;
}

static void __SetPool(int n)
{
    threadmap[n] = 1;
}

static void __ResetPool(int n)
{
    threadmap[n] = 0;
}

static void __InitPool(void)
{
    int count = 0;
    for (count = 0; count < THREAD_POOL_MAX; count++)
    {
        threadmap[count] = 0;
        if (pthread_cond_init(&control[count].condition, NULL) != 0)
            perror("Make Thread Condition");
    }
}

static void __DestroyPool(void)
{
    int count = 0;
    for (count = 0; count < THREAD_POOL_MAX; count++)
    {
        if (pthread_cond_destroy(&control[count].condition) != 0)
        {
            perror("Remove Thread Condition");
            exit(0);
        }
    }
}

static void* __ThreadWork(void *param)
{
    int myid = -1;

    myid = (int)param;

    pthread_mutex_lock(&startsync);
    pthread_cond_signal(&startcond);
    pthread_mutex_unlock(&startsync);

    while(1)
    {
        pthread_mutex_lock(&controlsync);
        pthread_cond_wait(&control[myid].condition, &controlsync);
        pthread_mutex_unlock(&controlsync);

        if (control[myid].func != NULL)
            control[myid].func(control[myid].param);
/*        if (control[myid].param != NULL)
            free(control[myid].param);*/

        control[myid].func = NULL;
        control[myid].param = NULL;
        pthread_mutex_lock(&startsync);
        __ResetPool(myid);
        pthread_mutex_unlock(&startsync);

        if (control[myid].stopflag) {
            pthread_exit(0);
        }
    }
}

int MMSoundThreadPoolDump(void)
{
	int count = 0;
	int ret = MM_ERROR_NONE;

	fprintf(stdout, "================================================================================\n");
	fprintf(stdout, "                              Thread States                                     \n");
	fprintf(stdout, "--------------------------------------------------------------------------------\n");
	for (count = 0; count < THREAD_POOL_MAX; count ++)
	{
		fprintf(stdout, "Thread %d\n", control[count].threadid);
		fprintf(stdout, "Current State is \"%s\"\n", threadmap[count] ? "Running" : "Ready");
		if (threadmap[count])
		{
			fprintf(stdout, "Running function address %p\n", control[count].func);
		}
		else
		{
			fprintf(stdout, "Threadmap is NULL\n");
			ret = MM_ERROR_SOUND_INTERNAL;
		}
	}
	fprintf(stdout, "================================================================================\n");
	return ret;
}

int MMSoundThreadPoolInit(void)
{
    volatile int count = 0;
    pthread_mutex_lock(&funcsync);

    __InitPool();

    for (count = 0; count < THREAD_POOL_MAX; count++)
    {
        control[count].stopflag = 0;
        pthread_mutex_lock(&startsync);
        if (pthread_create(&control[count].threadid, NULL, __ThreadWork, (void*)count) < 0)
        {
            perror("Make Thread Fail");
            exit(0);
        }
        pthread_cond_wait(&startcond, &startsync);
        pthread_mutex_unlock(&startsync);
        usleep(100); /* Delay for thread init */
    }
    pthread_mutex_unlock(&funcsync);
    return MM_ERROR_NONE;
}

int MMSoundThreadPoolRun(void *param, void (*func)(void*))
{
    int poolnum = -1;

    pthread_mutex_lock(&funcsync);
    pthread_mutex_lock(&startsync);
    poolnum = __GetEmptyPool();
    if (poolnum < 0) {
        pthread_mutex_unlock(&startsync);
        pthread_mutex_unlock(&funcsync);
        return MM_ERROR_COMMON_NO_FREE_SPACE;
    }
    __SetPool(poolnum);
    pthread_mutex_unlock(&startsync);
    control[poolnum].param = param;
    control[poolnum].func = func;
    pthread_cond_signal(&control[poolnum].condition);
    pthread_mutex_unlock(&funcsync);

    return MM_ERROR_NONE;
}

int MMSoundThreadPoolFini(void)
{
    int count = 0;

    pthread_mutex_lock(&funcsync);
    for (count = 0; count < THREAD_POOL_MAX; count++)
    {
        control[count].stopflag = 1;
        pthread_cond_signal(&control[count].condition);
        if (pthread_join(control[count].threadid, NULL) < 0)
        {
            perror("Join Fail");
            exit(0);
        }
    }
    __DestroyPool();
    pthread_mutex_unlock(&funcsync);

    return MM_ERROR_NONE;
}
#endif // USE_G_THREAD_POOL

