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

#include <sys/stat.h>
#include <errno.h>

#include <mm_types.h>
#include <mm_error.h>
#include <mm_message.h>
#include <mm_debug.h>
#include <mm_sound.h>
#include <mm_sound_private.h>

#define KEYTONE_PATH "/tmp/keytone"		/* Keytone pipe path */
#define FILE_FULL_PATH 1024				/* File path lenth */

typedef struct {
	char filename[FILE_FULL_PATH];
	int volume_config;
} ipc_t;

EXPORT_API
int mm_sound_play_keysound(const char *filename, int volume_config)
{
	int err = MM_ERROR_NONE;
	int fd = -1;
	int size = 0;
	ipc_t data = {{0,},};

	debug_fenter();

	if (!filename)
		return MM_ERROR_SOUND_INVALID_FILE;

	/* Check whether file exists */
	fd = open(filename, O_RDONLY);
	if (fd == -1) {
		debug_error("file open failed with [%s][%d]\n", strerror(errno), errno);
		switch (errno) {
		case ENOENT:
			return MM_ERROR_SOUND_FILE_NOT_FOUND;
		default:
			return MM_ERROR_SOUND_INTERNAL;
		}
	}
	close(fd);
	fd = -1;

	/* Open PIPE */
	fd = open(KEYTONE_PATH, O_WRONLY | O_NONBLOCK);
	if (fd == -1) {
		debug_error("Fail to open pipe\n");
		return MM_ERROR_SOUND_FILE_NOT_FOUND;
	}
	data.volume_config = volume_config;
	strncpy(data.filename, filename, FILE_FULL_PATH);
	debug_msg("The file name [%s]\n", data.filename);
	size = sizeof(ipc_t);

	/* Write to PIPE */
	err = write(fd, &data, size);
	if (err < 0) {
		debug_error("Fail to write data: %s\n", strerror(err));
		close(fd);
		return MM_ERROR_SOUND_INTERNAL;
	}
	/* Close PIPE */
	close(fd);

	debug_fleave();
	return MM_ERROR_NONE;
}


