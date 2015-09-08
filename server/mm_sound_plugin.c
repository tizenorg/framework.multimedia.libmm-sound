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
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <dlfcn.h>

#include "../include/mm_sound_common.h"
#include "include/mm_sound_plugin.h"
#include <mm_error.h>
#include <mm_debug.h>

static char* __strcatdup(const char *str1, const char *str2);
static int _MMSoundPluginGetList(const char *plugdir ,char ***list);
static int _MMSoundPluginDestroyList(char **list);

char* MMSoundPluginGetTypeName(int type)
{
    static char *typename[] = {
        "ERROR",
        "SOUND",
        "RUN",
    };

    if (type < MM_SOUND_PLUGIN_TYPE_LAST && type > -1)
        return typename[type];
    else
        return "Unknown"; /* error condition */
}

int MMSoundPluginScan(const char *plugindir, const int type, MMSoundPluginType **pluginlist)
{
    char **list = NULL;
    int err = MM_ERROR_NONE;
    char *item = NULL;
    int index = 0;
    MMSoundPluginType plugin[100];
    int plugin_index = 0;

    debug_fenter ();

    debug_msg(" Plugin dir :: %s \n", plugindir);
    err = _MMSoundPluginGetList(plugindir, &list);
    if (err != MM_ERROR_NONE)
        return err;

    while((item = list[index++]) != NULL) {
        if(MMSoundPluginOpen(item, &plugin[plugin_index]) != MM_ERROR_NONE) {
            debug_warning("%s is not sound plugin\n", item);
            continue;
        }
        if (plugin[plugin_index].type != type)
            MMSoundPluginClose(&plugin[plugin_index]);
        else
            plugin_index++;
    }

    _MMSoundPluginDestroyList(list);

    *pluginlist = (MMSoundPluginType*) malloc(sizeof(MMSoundPluginType) * (plugin_index+1));
    if ((*pluginlist) == NULL) {
        debug_critical("Memory allocation fail\n");
        /* Occur segmentation fault */
        *pluginlist = (void*)1;
    }

    memcpy(*pluginlist, plugin, sizeof(MMSoundPluginType) * (plugin_index+1));
    /* Marking end of array */
    (*pluginlist)[plugin_index].type = MM_SOUND_PLUGIN_TYPE_NONE;
    (*pluginlist)[plugin_index].module = NULL;

    debug_fleave ();

    return MM_ERROR_NONE;
}

int MMSoundPluginRelease(MMSoundPluginType *pluginlist)
{
    int loop = 0;

    debug_fenter ();

    while (pluginlist[loop].type != MM_SOUND_PLUGIN_TYPE_NONE) {
        MMSoundPluginClose(&pluginlist[loop++]);
    }

    free (pluginlist);

    debug_fleave ();

    return MM_ERROR_NONE;
}

int MMSoundPluginOpen(char *file, MMSoundPluginType *plugin)
{
    void *pdll = NULL;
    int (*func)(void) = NULL;
    int t = -1;

    debug_fenter ();

    pdll = dlopen(file, RTLD_NOW|RTLD_GLOBAL);

    if (pdll == NULL) {
        debug_error("%s\n", dlerror());
        return MM_ERROR_SOUND_INVALID_FILE;
    }

    func = (int (*)(void))dlsym(pdll, "MMSoundGetPluginType");
    if (func == NULL) {
        dlclose(pdll);
        debug_error("Cannot find symbol : MMSoundGetPluginType\n");
        return MM_ERROR_SOUND_INVALID_FILE;
    }
    t = func();

    debug_msg("%s is %s\n", file,
                t == MM_SOUND_PLUGIN_TYPE_CODEC ? "CODEC":
                t == MM_SOUND_PLUGIN_TYPE_RUN ? "RUN" : "Unknown");
    switch(t)
    {
        case MM_SOUND_PLUGIN_TYPE_CODEC:
        case MM_SOUND_PLUGIN_TYPE_RUN:
            plugin->type = t;
            plugin->module = pdll;
            break;
        default:
            debug_error("Type is %d\n",t);
            dlclose(pdll);
            return MM_ERROR_SOUND_INVALID_FILE;
    }

    debug_fleave ();

    return MM_ERROR_NONE;
}

int MMSoundPluginClose(MMSoundPluginType *plugin)
{
	debug_fenter ();

    if(plugin->module)
        dlclose(plugin->module);
    plugin->type = MM_SOUND_PLUGIN_TYPE_NONE;
    plugin->module = NULL;

    debug_fleave ();
    return MM_ERROR_NONE;
}

int MMSoundPluginGetSymbol(MMSoundPluginType *plugin, const char *symbol, void **func)
{
    void *fn = NULL;

    debug_fenter ();

    if (plugin->module == NULL)
        return MM_ERROR_SOUND_INVALID_FILE;
    fn = dlsym(plugin->module, symbol);
    if (fn == NULL)
        return MM_ERROR_SOUND_INVALID_FILE;
    *func = fn;

    debug_fleave ();
    return MM_ERROR_NONE;
}
#define MAX_PATH_SIZE 256
static int _MMSoundPluginGetList(const char *plugdir ,char ***list)
{
	struct dirent **entry = NULL;
	int items;
	struct stat finfo;
	char **temp;
	int tn = 0;
	static char curdir[MAX_PATH_SIZE];
	int item_idx;
	int ret = MM_ERROR_NONE;

	items = scandir(plugdir, &entry, NULL, alphasort);
	debug_msg("Items %d\n", items);

	if (items == -1)
		return MM_ERROR_INVALID_ARGUMENT;

	temp = (char **)malloc(sizeof(char *) * (items + 1));
	if(!temp) {
		ret = MM_ERROR_OUT_OF_MEMORY;
		goto free_entry;
	}
	memset(temp, 0, sizeof(char*) * (items + 1));
	memset(curdir, '\0', sizeof(curdir));
	if(NULL == getcwd(curdir, sizeof(curdir)-1)) {
		if (temp) {
			free (temp);
			temp = NULL;
		}
		ret = MM_ERROR_OUT_OF_STORAGE;
		goto free_entry;
	}
	/* FIXME : need to handle error case */
	if (chdir(plugdir) != 0) {
		debug_error("chdir error\n");
		if (temp) {
			free (temp);
			temp = NULL;
		}
		ret = MM_ERROR_INVALID_ARGUMENT;
		goto free_entry;
	}

	for(item_idx = items; item_idx--; ) {
		if(stat(entry[item_idx]->d_name, &finfo) < 0) {
			debug_error("Stat error\n");
			if (temp) {
				free(temp);
				temp = NULL;
			}
			ret = MM_ERROR_INVALID_ARGUMENT;
			goto free_entry;
		}

		debug_msg("item %d is %s\n", item_idx, entry[item_idx]->d_name);

		if (S_ISREG(finfo.st_mode)) {
			temp[tn++] = __strcatdup(plugdir, entry[item_idx]->d_name);
		}
	}
	*list =  temp;
free_entry:
	for(item_idx = 0; item_idx < items; item_idx++) {
		free(entry[item_idx]);
	}
	free(entry);
	return ret;
}

static int _MMSoundPluginDestroyList(char **list)
{
	int tn = 0;
	while(list[tn]) {
		free(list[tn++]);
	}
	free (list);
	return MM_ERROR_NONE;
}

static char* __strcatdup(const char *str1, const char *str2)
{
    char *dest = NULL;
    int len = 0;
    len = strlen(str1) + strlen(str2) + 1;
    dest = (char*) malloc(len*sizeof(char));
    if (!dest)
        return NULL;
    strncpy(dest, str1, len-1);
    strncat(dest, str2, len-1);
    return dest;
}
