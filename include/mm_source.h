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

#ifndef __MM_SOURCE_H__
#define __MM_SOURCE_H__

enum {
    MM_SOURCE_NONE = 0,
    MM_SOURCE_FILE,
    MM_SOURCE_MEMORY,
    MM_SOURCE_MEMORY_NOTALLOC,
    MM_SOURCE_NUM,
};

enum {
	MM_SOURCE_NOT_DRM_CONTENTS = 0,
	MM_SOURCE_CHECK_DRM_CONTENTS
};

typedef struct {
    int             type;           /**< source type (file or memory) */
    void            *ptr;           /**< pointer to buffer */
    unsigned int    cur_size;       /**< size of current memory */
    unsigned int    tot_size;       /**< size of current memory */
    int             fd;             /**< file descriptor for file */
	unsigned int    medOffset;		/**Media Offset */
} MMSourceType;

#define MMSourceIsUnUsed(psource) \
    ((psource)->type == MM_SOUND_SOURCE_NONE)

#define MMSourceIsFile(psource) \
    ((psource)->type == MM_SOUND_SOURCE_FILE)

#define MMSourceIsMemory(psource) \
    ((psource)->type == MM_SOUND_SOURCE_MEMORY)

#define MMSourceGetPtr(psource) \
    ((psource)->ptr)

#define MMSourceGetCurSize(psource) \
    ((psource)->cur_size)

#define MMSourceGetTotSize(psource) \
    ((psource)->tot_size)

int mm_source_open_file(const char *filename, MMSourceType* source, int drmsupport);
int mm_source_open_full_memory(const void *ptr, int totsize, int alloc, MMSourceType *source);
int mm_source_open_memory(const void *ptr, int totsize, int size, MMSourceType *source);
int mm_source_append_memory(const void *ptr, int size, MMSourceType *source);
int mm_source_close(MMSourceType *source);

#endif  /* __MM_SOURCE_H__ */

