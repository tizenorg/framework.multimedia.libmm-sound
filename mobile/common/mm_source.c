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
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "mm_types.h"
#include "mm_debug.h"
#include "mm_error.h"
#include "mm_source.h"


bool _is_drm_file(
        const char	 *filePath
)
{
	if(!filePath || filePath[0] == '\0')
	{
		debug_error("invalid argument\n");
		return false;
	}
	char *p = NULL;
	p = (char*) strrchr(filePath, '.');

	if( p &&  ((strncasecmp(p, ".odf", 4) == 0) || (strncasecmp(p, ".dcf", 4) == 0) || (strncasecmp(p, ".o4a", 4) == 0) || (strncasecmp(p, ".o4v", 4) == 0)))
	{
    	return true;
	}
	return false;
}

EXPORT_API
int mm_source_open_file(const char *filename, MMSourceType *source, int drmsupport)
{
	struct stat finfo = {0, };
	int fd = -1;
	void *mmap_buf = NULL;

	if(filename == NULL)
	{
		debug_error("filename is null\n");
		return MM_ERROR_SOUND_INVALID_FILE;
	}
	if(drmsupport)
	{
		if(_is_drm_file(filename))
		{
			debug_error("%s is DRM contents\n", filename);
			return MM_ERROR_SOUND_UNSUPPORTED_MEDIA_TYPE;
		}
	}

	fd = open(filename, O_RDONLY);
	debug_fenter();
	debug_msg("Open file name is %s\n", filename);
    unsigned int mediaSize,readSize,offSet=0;
	char genStr[20],i; 
		
	if (fd == -1)
	{
		debug_error("file open fail\n\n");
		return MM_ERROR_SOUND_INTERNAL;
	}
	if (fstat(fd, &finfo) == -1)
	{
		debug_error("file get info fail\n");
		close(fd);
		return MM_ERROR_SOUND_INTERNAL;
	}

	mediaSize = (unsigned int)finfo.st_size;
	
    {
		/* get the extension (3 characters from last including NULL)*/
		strncpy((void *)genStr,(void *)(filename+(strlen(filename)-2)),3);	
		
		debug_msg("ExtName of the Sound file %s\n",genStr);

		
		if(strcasecmp (genStr, "dm") == 0)
		{
			debug_msg("It is DM file going ahead with special decoding\n");

			/* Vlidation of the DM file */
			readSize = read(fd,(void*)genStr,0x8);
			if(readSize != 0x8)
			{
				debug_error("Error in Reading the file Header %x/0x8\n",readSize);
				return MM_ERROR_SOUND_INTERNAL;
			}

			genStr[readSize] ='\0';
			
			debug_msg("Header details of DM file %s\n",genStr);
			
			if(strcasecmp (genStr, "--random") != 0)
			{
				debug_error("It is not a valied DM file");
				return MM_ERROR_SOUND_INTERNAL;
			}

			/*checking the Media Type */
			readSize = lseek(fd, 0x32, SEEK_SET);
			if(readSize != 0x32)
			{
				debug_error("Error in Seeking the file to offset %x/0x32\n",readSize);
				return MM_ERROR_SOUND_INTERNAL;
			}
				
			readSize = read(fd,(void*)genStr,0xf);

			for(i=0;i<0xf;i++)
			{
				if(genStr[i] == (char)0xD)
				{
					genStr[i] ='\0';
					break;
				}
			}
			
			debug_msg("Header details of DM file %s\n",genStr);

			/*Finding the Media Offset */
			
			if(strcasecmp (genStr, "audio/mpeg") == 0)
			{
				offSet = 0x63;
			}
			else if(strcasecmp (genStr, "audio/wav") == 0)
			{
				offSet = 0x62;
			}
			else
			{
				debug_error("It is not MP3/Wav DM file \n");
				return MM_ERROR_SOUND_INTERNAL;
			}

			/*Finding the Media Size */
			
			mediaSize -= (offSet + 0x28);
			
			/*Seeking the file to start */
			readSize = lseek(fd, 0x0, SEEK_SET);
			
			if( readSize != 0x0)
			{
				debug_error("Error in Seeking the file to offset %x/0x32\n",readSize);
				return MM_ERROR_SOUND_INTERNAL;
			}

		}
		else
		{
			debug_msg("It is NON DM file going ahead with normal decoding\n");
		}
	}

	mmap_buf  = mmap(0, finfo.st_size, PROT_READ, MAP_SHARED, fd,0);
	
	if (mmap_buf == NULL)
	{
		debug_error("MMAP fail\n");
		close(fd);
		return MM_ERROR_SOUND_INTERNAL;
	}
	source->ptr = mmap_buf+offSet;
	source->medOffset = offSet;
	debug_msg("source ptr = %p\n", source->ptr);
	debug_msg("Med Offset Size : %d",source->medOffset);
	source->tot_size = finfo.st_size;
	source->cur_size = mediaSize;
	source->type = MM_SOURCE_FILE;
	source->fd = fd;

	return MM_ERROR_NONE;
}

EXPORT_API
int mm_source_open_full_memory(const void *ptr, int totsize, int alloc, MMSourceType *source)
{
	int err = MM_ERROR_NONE;
	if (ptr == NULL)
	{
		debug_error("PTR is NULL\n");
		return MM_ERROR_INVALID_ARGUMENT;
	}
	if (alloc)
	{
		err = mm_source_open_memory(ptr, totsize, totsize, source);
	}
	else
	{
	    source->ptr = ptr;
	    source->tot_size = totsize;
	    source->cur_size = totsize;

	    source->type = MM_SOURCE_MEMORY_NOTALLOC;
	    source->fd = -1;
	}
    return err;
}

EXPORT_API
int mm_source_open_memory(const void *ptr, int totsize, int size, MMSourceType *source)
{
    source->ptr = (unsigned char*) malloc(totsize);
    if (source->ptr == NULL)
    {
        debug_error("memory alloc fail\n");
        return MM_ERROR_SOUND_NO_FREE_SPACE;
    }
    source->tot_size = totsize;
    source->cur_size = 0;
    source->type = MM_SOURCE_MEMORY;
    source->fd = -1;

    return mm_source_append_memory(ptr, size, source);
}

EXPORT_API
int mm_source_append_memory(const void *ptr, int size, MMSourceType *source)
{
    if (source->cur_size + size > source->tot_size)
    {
        debug_error("memory too large\n");
        return MM_ERROR_SOUND_NO_FREE_SPACE;
    }

    memcpy(source->ptr + source->cur_size, ptr, size);
    source->cur_size += size;
    return MM_ERROR_NONE;
}

EXPORT_API
int mm_source_close(MMSourceType *source)
{
    debug_fenter();

    ////PREVENT
    if(source == NULL)
    {
    	debug_critical("source is null %s\n", __func__);
    	return MM_ERROR_CLASS;
    }
    /////
	debug_msg("Source type = %d\n", source->type);
    switch(source->type)
    {
        case MM_SOURCE_FILE:
        	if(source->ptr != NULL)
        	{
        		source->ptr -= source->medOffset;
				debug_msg("Med Offset Size : %d/%d",source->medOffset,source->tot_size);
        		if (munmap(source->ptr, source->tot_size) == -1) {
        			debug_error("MEM UNMAP fail\n\n");
        		}
        	}
            close(source->fd);
            break;
        case MM_SOURCE_MEMORY:
        	if(source->ptr != NULL)
        		free(source->ptr);
            break;
        case MM_SOURCE_MEMORY_NOTALLOC:
            break;
        default:
            debug_critical("Unknown Source\n");
            break;
    }
    memset(source, 0, sizeof(MMSourceType));
    return MM_ERROR_NONE;
}

