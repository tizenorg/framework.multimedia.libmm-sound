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
#include <string.h>
#include <stdlib.h>

#define debug_log(msg, args...) fprintf(stderr, msg, ##args)
#define debug_error(msg, args...) fprintf(stderr, msg, ##args)
#define MAX_STRING_LEN 256
#define MAX_PATH_LEN		1024
#define MIN_TONE_PLAY_TIME 300
#include "../include/mm_sound.h"
#include "../include/mm_sound_common.h"
#include "../include/mm_sound_private.h"
#include "../include/mm_sound_pa_client.h"

#include <glib.h>

#include <pthread.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <vconf.h>
#include <mm_session_private.h>
#include <audio-session-manager-types.h>

#define POWERON_FILE	"/usr/share/feedback/sound/operation/power_on.wav"
#define KEYTONE_FILE	"/opt/usr/share/settings/Previews/touch.wav"


// For testsuite status
enum {
    CURRENT_STATUS_MAINMENU = 0,
    CURRENT_STATUS_FILENAME = 1,
    CURRENT_STATUS_POSITION = 2,
    CURRENT_STATUS_DIRNAME = 3,
};

int g_menu_state = CURRENT_STATUS_MAINMENU;

volume_type_t g_volume_type = VOLUME_TYPE_MEDIA;
unsigned int g_volume_value;

GIOChannel *stdin_channel;
char g_file_name[MAX_STRING_LEN];
char g_dir_name[MAX_PATH_LEN];


GMainLoop* g_loop;

// Function
static void interpret (char *buf);
gboolean timeout_menu_display(void *data);
gboolean timeout_quit_program(void *data);
gboolean input (GIOChannel *channel);

static char* __get_playback_device_str (mm_sound_device_out out);
static char* __get_capture_device_str (mm_sound_device_in in);
static char* __get_route_str (mm_sound_route route);
static int __mm_sound_foreach_available_route_cb (mm_sound_route route, void *user_data);
static void __mm_sound_available_route_changed_cb (mm_sound_route route, bool available, void *user_data);
static void __mm_sound_active_device_changed_cb (mm_sound_device_in device_in, mm_sound_device_out device_out, void *user_data);

void mycallback(void *data, int id)
{
	char *str = (char*)data;
	if(data != NULL)
		debug_log("mycallback called (user data:%s ,id:%d)\n", str, id);
	else
		debug_log("mycallback called (no user data)\n");
}
volatile char test_callback_done;
void test_callback(void *data, int id)
{
	debug_log("test_callback is called\n");
	test_callback_done = 1;
}
void device_connected_cb (MMSoundDevice_t device_h, bool is_connected, void *user_data)
{
	int ret = 0;
	int type = 0;
	int io_direction = 0;
	int state = 0;
	int id = 0;
	char *name = NULL;
	debug_log("*** device_connected_cb is called, device_h[0x%x], is_connected[%d], user_date[0x%x]\n", device_h, is_connected, user_data);
	ret = mm_sound_get_device_type(device_h, &type);
	if (ret) {
		debug_error("failed to mm_sound_get_device_type()\n");
	}
	ret = mm_sound_get_device_io_direction(device_h, &io_direction);
	if (ret) {
		debug_error("failed to mm_sound_get_device_io_direction()\n");
	}
	ret = mm_sound_get_device_state(device_h, &state);
	if (ret) {
		debug_error("failed to mm_sound_get_device_state()\n");
	}
	ret = mm_sound_get_device_id(device_h, &id);
	if (ret) {
		debug_error("failed to mm_sound_get_device_id()\n");
	}
	ret = mm_sound_get_device_name(device_h, &name);
	if (ret) {
		debug_error("failed to mm_sound_get_device_name()\n");
	}
	debug_log("*** --- type[%d], id[%d], io_direction[%d], state[%d], name[%s]\n", type, id, io_direction, state, name);
}
void device_info_changed_cb (MMSoundDevice_t device_h, int changed_info_type, void *user_data)
{
	int ret = 0;
	int type = 0;
	int io_direction = 0;
	int state = 0;
	int id = 0;
	char *name = NULL;
	debug_log("*** device_info_changed_cb is called, device_h[0x%x], changed_info_type[%d], user_date[0x%x]\n", device_h, changed_info_type, user_data);
	ret = mm_sound_get_device_type(device_h, &type);
	if (ret) {
		debug_error("failed to mm_sound_get_device_type()\n");
	}
	ret = mm_sound_get_device_io_direction(device_h, &io_direction);
	if (ret) {
		debug_error("failed to mm_sound_get_device_io_direction()\n");
	}
	ret = mm_sound_get_device_state(device_h, &state);
	if (ret) {
		debug_error("failed to mm_sound_get_device_state()\n");
	}
	ret = mm_sound_get_device_id(device_h, &id);
	if (ret) {
		debug_error("failed to mm_sound_get_device_id()\n");
	}
	ret = mm_sound_get_device_name(device_h, &name);
	if (ret) {
		debug_error("failed to mm_sound_get_device_name()\n");
	}
	debug_log("*** --- type[%d], id[%d], io_direction[%d], state[%d], name[%s]\n", type, id, io_direction, state, name);
}
void quit_program()
{
	g_main_loop_quit(g_loop);
}

static void displaymenu()
{
	if (g_menu_state == CURRENT_STATUS_MAINMENU) {
		g_print("==================================================================\n");
		g_print("	Sound Path APIs\n");
		g_print("==================================================================\n");
		g_print("1. Play speaker  \t");
		g_print("2. Play headset  \n");
		g_print("3. (blank)     \n");
		g_print("4. Rec. with mic \t");
		g_print("5. (blank)     \t");
		g_print("6. Call receiver \n");
		g_print("7. Call end      \t");
		g_print("8. VT call speaker  \t");
		g_print("9. VT call end      \n");
		g_print("==================================================================\n");
		g_print("	Sound Play APIs\n");
		g_print("==================================================================\n");
		g_print("k : Key Sound     \t");
		g_print("a : play sound    \t");
		g_print("A : play loud solo\n");
		g_print("c : play sound ex \t");
		g_print("F : Play DTMF     \t");
		g_print("b : Play directory\n");
		g_print("s : Stop play     \n");
		g_print("==================================================================\n");
		g_print("	Volume APIs\n");
		g_print("==================================================================\n");
		g_print("q : Get media    \t");
		g_print("w : Inc. media   \t");
		g_print("e : Dec. media   \n");
		g_print("r : Get system   \t");
		g_print("t : Inc. system  \t");
		g_print("y : Dec. system  \n");
		g_print("g : Get voice   \t");
		g_print("h : Inc. voice  \t");
		g_print("j : Dec. voice  \n");
		g_print("B : Set audio balance\n");
		g_print("M : Set mute all\n");
		g_print("==================================================================\n");
		g_print("	Audio route APIs\n");
		g_print("==================================================================\n");
		g_print("u : Foreach Available Routes \t");
		g_print("i : Get Active Devices     \n");
		g_print("o : Add Available Routes Callback   \t");
		g_print("O : Remove Available Routes Callback   \n");
		g_print("p : Add Active Route Callback\t");
		g_print("P : Remove Active Route Callback \n");
		g_print("{ : Get BT A2DP Status\n");
		g_print("} : Set Active Route\n");
		g_print("==================================================================\n");
		g_print("	Session Test\n");
		g_print("==================================================================\n");
		g_print("z : Call start \t");
		g_print("Z : VideoCall start \t");
		g_print("N : Notification start \n");
		g_print("n : VOIP start \t");
		g_print("v : Session end   \t");
		g_print("V : Current Status \n");
		g_print("==================================================================\n");
		g_print("	Audio device APIs\n");
		g_print("==================================================================\n");
		g_print("L : Get current list of connected devices \n");
		g_print("C : Add device connected callback \t");
		g_print("D : Remove device connected callback \n");
		g_print("Q : Add device info. changed callback \t");
		g_print("W : Remove device info. changed callback \n");
		g_print("==================================================================\n");
		g_print("d : Input Directory \t");
		g_print("f : Input File name \t");
		g_print("x : Exit Program \n");
		g_print("==================================================================\n");
		g_print(" Input command >>>>>>>> ");
	}
	else if (g_menu_state == CURRENT_STATUS_FILENAME) {
		g_print(">>>>Input file name to play : ");
	}
	else if (g_menu_state == CURRENT_STATUS_DIRNAME) {
			g_print(">>>>Input directory which contain audio files : ");
	}
	else {
		g_print("**** Unknown status.\n");
		quit_program();
	}
}

gboolean timeout_menu_display(void* data)
{
	displaymenu();
	return FALSE;
}

gboolean timeout_quit_program(void* data)
{
	quit_program();
	return FALSE;
}

gboolean input (GIOChannel *channel)
{
    char buf[MAX_STRING_LEN + 3];
    gsize read;

    g_io_channel_read(channel, buf, MAX_STRING_LEN, &read);
    buf[read] = '\0';
    g_strstrip(buf);
    interpret (buf);
    return TRUE;
}


static void input_filename(char *filename)
{
	MMSOUND_STRNCPY(g_file_name, filename, MAX_STRING_LEN);
	g_print("\nThe input filename is '%s' \n\n",g_file_name);

}

static void input_dirname(char *dirname)
{
	MMSOUND_STRNCPY(g_dir_name, dirname, MAX_PATH_LEN);
	g_print("\nThe input directory is '%s' \n\n",g_dir_name);
}

static char* __get_playback_device_str (mm_sound_device_out out)
{
	switch (out) {
	case MM_SOUND_DEVICE_OUT_SPEAKER: return "SPEAKER";
	case MM_SOUND_DEVICE_OUT_RECEIVER: return "RECEIVER";
	case MM_SOUND_DEVICE_OUT_WIRED_ACCESSORY: return "HEADSET";
	case MM_SOUND_DEVICE_OUT_BT_SCO: return "BTSCO";
	case MM_SOUND_DEVICE_OUT_BT_A2DP: return "BTA2DP";
	case MM_SOUND_DEVICE_OUT_DOCK: return "DOCK";
	case MM_SOUND_DEVICE_OUT_HDMI: return "HDMI";
	case MM_SOUND_DEVICE_OUT_MIRRORING: return "MIRRORING";
	case MM_SOUND_DEVICE_OUT_USB_AUDIO: return "USB";
	case MM_SOUND_DEVICE_OUT_MULTIMEDIA_DOCK: return "MULTIMEDIA_DOCK";
	default: return NULL;
	}
}

static char* __get_capture_device_str (mm_sound_device_in in)
{
	switch (in) {
	case MM_SOUND_DEVICE_IN_MIC: return "MAINMIC";
	case MM_SOUND_DEVICE_IN_WIRED_ACCESSORY: return "HEADSET";
	case MM_SOUND_DEVICE_IN_BT_SCO: return "BTMIC";
	default: return NULL;
	}
}

static char* __get_route_str (mm_sound_route route)
{
	switch (route) {
	case MM_SOUND_ROUTE_OUT_SPEAKER: return "OUT_SPEAKER";
	case MM_SOUND_ROUTE_OUT_RECEIVER: return "OUT_RECEIVER";
	case MM_SOUND_ROUTE_OUT_WIRED_ACCESSORY: return "OUT_WIRED_ACCESSORY";
	case MM_SOUND_ROUTE_OUT_BLUETOOTH_A2DP: return "OUT_BLUETOOTH_A2DP";
	case MM_SOUND_ROUTE_OUT_DOCK: return "OUT_DOCK";
	case MM_SOUND_ROUTE_OUT_HDMI: return "OUT_HDMI";
	case MM_SOUND_ROUTE_OUT_MIRRORING: return "OUT_MIRRORING";
	case MM_SOUND_ROUTE_OUT_USB_AUDIO: return "OUT_USB";
	case MM_SOUND_ROUTE_OUT_MULTIMEDIA_DOCK: return "OUT_MULTIMEDIA_DOCK";
	case MM_SOUND_ROUTE_IN_MIC: return "IN_MIC";
	case MM_SOUND_ROUTE_IN_WIRED_ACCESSORY: return "IN_WIRED_ACCESSORY";
	case MM_SOUND_ROUTE_IN_MIC_OUT_RECEIVER: return "IN_MIC_OUT_RECEIVER";
	case MM_SOUND_ROUTE_IN_MIC_OUT_SPEAKER: return "IN_MIC_OUT_SPEAKER";
	case MM_SOUND_ROUTE_IN_MIC_OUT_HEADPHONE: return "IN_MIC_OUT_HEADPHONE";
	case MM_SOUND_ROUTE_INOUT_HEADSET: return "INOUT_HEADSET";
	case MM_SOUND_ROUTE_INOUT_BLUETOOTH: return "INOUT_BLUETOOTH";
	default: return NULL;
	}
}

static int __mm_sound_foreach_available_route_cb (mm_sound_route route, void *user_data)
{
	g_print ("[%s] route = [0x%08x][%s]\n", __func__, route, __get_route_str(route));
	return true;
}

static void __mm_sound_available_route_changed_cb (mm_sound_route route, bool available, void *user_data)
{
	g_print ("[%s] route = [%d][0x%08x][%s]\n", __func__, available, route, __get_route_str(route));
}

static void __mm_sound_active_device_changed_cb (mm_sound_device_in device_in, mm_sound_device_out device_out, void *user_data)
{
	g_print ("[%s] in[0x%08x][%s], out[0x%08x][%s]\n", __func__,
			device_in, __get_capture_device_str(device_in), device_out, __get_playback_device_str(device_out));
}

static void interpret (char *cmd)
{
	int ret=0;
	static int handle = -1;
	MMSoundPlayParam soundparam = {0,};

	switch (g_menu_state)
	{
		case CURRENT_STATUS_MAINMENU:
			if(strncmp(cmd, "k", 1) == 0)
			{
				ret = mm_sound_play_keysound(KEYTONE_FILE, 8);
				if(ret < 0)
					debug_log("keysound play failed with 0x%x\n", ret);
			}
			else if(strncmp(cmd, "q", 1) == 0)
			{//get media volume
				unsigned int value = 100;
				ret = mm_sound_volume_get_value(g_volume_type, &value);
				if(ret < 0) {
					debug_log("mm_sound_volume_get_value 0x%x\n", ret);
				}
				else{
					g_print("*** MEDIA VOLUME : %u ***\n", value);
					g_volume_value = value;
				}
			}
			else if(strncmp(cmd, "w", 1) == 0)
			{//set media volume up
				unsigned int value = 100;
				ret = mm_sound_volume_get_value(g_volume_type, &value);
				if(ret < 0) {
					debug_log("mm_sound_volume_get_value 0x%x\n", ret);
				}
				else {
					debug_log("Loaded media volume is %d\n", value);
					int step;
					value++;
					mm_sound_volume_get_step(g_volume_type, &step);
					if(value >= step) {
						value = step-1;
					}
					ret = mm_sound_volume_set_value(g_volume_type, value);
					if(ret < 0) {
						debug_log("mm_sound_volume_set_value 0x%x\n", ret);
					}
				}
			}
			else if(strncmp(cmd, "e", 1) == 0)
			{//set media volume down
				unsigned int value = 100;
				ret = mm_sound_volume_get_value(g_volume_type, &value);
				if(ret < 0) {
					debug_log("mm_sound_volume_get_value 0x%x\n", ret);
				}
				else {
					if(value != 0) {
						value--;
					}

					ret = mm_sound_volume_set_value(g_volume_type, value);
					if(ret < 0) {
						debug_log("mm_sound_volume_set_value 0x%x\n", ret);
					}
				}
			}
			else if(strncmp(cmd, "r", 1) == 0)
			{//get media volume
				unsigned int value = 100;
				ret = mm_sound_volume_get_value(VOLUME_TYPE_SYSTEM, &value);
				if(ret < 0) {
					debug_log("mm_sound_volume_get_value 0x%x\n", ret);
				}
				else{
					g_print("*** SYSTEM VOLUME : %u ***\n", value);
					g_volume_value = value;
				}
			}
			else if(strncmp(cmd, "t", 1) == 0)
			{//set media volume up
				unsigned int value = 100;
				ret = mm_sound_volume_get_value(VOLUME_TYPE_SYSTEM, &value);
				if(ret < 0) {
					debug_log("mm_sound_volume_get_value 0x%x\n", ret);
				}
				else {
					debug_log("Loaded system volume is %d\n", value);
					int step;
					value++;
					mm_sound_volume_get_step(VOLUME_TYPE_SYSTEM, &step);
					if(value >= step) {
						value = step-1;
					}
					ret = mm_sound_volume_set_value(VOLUME_TYPE_SYSTEM, value);
					if(ret < 0) {
						debug_log("mm_sound_volume_set_value 0x%x\n", ret);
					}else {
						g_print("Current System volume is %d\n", value);
					}
				}
			}
			else if(strncmp(cmd, "y", 1) == 0)
			{//set media volume down
				unsigned int value = 100;
				ret = mm_sound_volume_get_value(VOLUME_TYPE_SYSTEM, &value);
				if(ret < 0) {
					debug_log("mm_sound_volume_get_value 0x%x\n", ret);
				}
				else {
					if(value != 0) {
						value--;
					}

					ret = mm_sound_volume_set_value(VOLUME_TYPE_SYSTEM, value);
					if(ret < 0) {
						debug_log("mm_sound_volume_set_value 0x%x\n", ret);
					}else {
						g_print("Current System volume is %d\n", value);
					}
				}
			}
			else if(strncmp(cmd, "g", 1) == 0)
			{//get voice volume
				unsigned int value = 100;
				ret = mm_sound_volume_get_value(VOLUME_TYPE_VOICE, &value);
				if(ret < 0) {
					debug_log("mm_sound_volume_get_value 0x%x\n", ret);
				}
				else{
					g_print("*** VOICE VOLUME : %u ***\n", value);
					g_volume_value = value;
				}
			}
			else if(strncmp(cmd, "h", 1) == 0)
			{//set voice volume up
				unsigned int value = 100;
				ret = mm_sound_volume_get_value(VOLUME_TYPE_VOICE, &value);
				if(ret < 0) {
					debug_log("mm_sound_volume_get_value 0x%x\n", ret);
				}
				else {
					debug_log("Loaded voice volume is %d\n", value);
					int step;
					value++;
					mm_sound_volume_get_step(VOLUME_TYPE_VOICE, &step);
					if(value >= step) {
						value = step-1;
					}
					ret = mm_sound_volume_set_value(VOLUME_TYPE_VOICE, value);
					if(ret < 0) {
						debug_log("mm_sound_volume_set_value 0x%x\n", ret);
					}else {
						g_print("Current Voice volume is %d\n", value);
					}
				}
			}
			else if(strncmp(cmd, "j", 1) == 0)
			{//set voice volume down
				unsigned int value = 100;
				ret = mm_sound_volume_get_value(VOLUME_TYPE_VOICE, &value);
				if(ret < 0) {
					debug_log("mm_sound_volume_get_value 0x%x\n", ret);
				}
				else {
					if(value != 0) {
						value--;
					}

					ret = mm_sound_volume_set_value(VOLUME_TYPE_VOICE, value);
					if(ret < 0) {
						debug_log("mm_sound_volume_set_value 0x%x\n", ret);
					}else {
						g_print("Current Voice volume is %d\n", value);
					}
				}
			}

			else if(strncmp(cmd, "B", 1) == 0)
			{
				int ret = 0;
				char input_string[128];
				float balance;

				fflush(stdin);
				ret = mm_sound_volume_get_balance(&balance);
				if (ret == MM_ERROR_NONE) {
					g_print ("### mm_sound_volume_get_balance Success, balance=%f\n", balance);
				} else {
					g_print ("### mm_sound_volume_get_balance Error = %x\n", ret);
				}
				g_print("> Enter new audio balance (current is %f) : ", balance);
				if (fgets(input_string, sizeof(input_string)-1, stdin) == NULL) {
					g_print ("### fgets return  NULL\n");
				}

				balance = atof (input_string);
				ret = mm_sound_volume_set_balance(balance);
				if (ret == MM_ERROR_NONE) {
					g_print ("### mm_sound_volume_set_balance(%f) Success\n", balance);
				} else {
					g_print ("### mm_sound_volume_set_balance(%f) Error = %x\n", balance, ret);
				}
			}

			else if(strncmp(cmd, "M", 1) == 0)
			{
				int ret = 0;
				char input_string[128];
				int muteall;

				fflush(stdin);
				ret = mm_sound_get_muteall(&muteall);
				if (ret == MM_ERROR_NONE) {
					g_print ("### mm_sound_get_muteall Success, muteall=%d\n", muteall);
				} else {
					g_print ("### mm_sound_get_muteall Error = %x\n", ret);
				}
				g_print("> Enter new muteall state (current is %d) : ", muteall);
				if (fgets(input_string, sizeof(input_string)-1, stdin) == NULL) {
					g_print ("### fgets return  NULL\n");
				}

				muteall = atoi (input_string);
				ret = mm_sound_set_muteall(muteall);
				if (ret == MM_ERROR_NONE) {
					g_print ("### mm_sound_set_muteall(%d) Success\n", muteall);
				} else {
					g_print ("### mm_sound_set_muteall(%d) Error = %x\n", muteall, ret);
				}
			}

			else if(strncmp(cmd, "a", 1) == 0)
			{
				debug_log("volume is %d type, %d\n", g_volume_type, g_volume_value);
				ret = mm_sound_play_sound(g_file_name, g_volume_type, mycallback ,"USERDATA", &handle);
				if(ret < 0)
					debug_log("mm_sound_play_sound() failed with 0x%x\n", ret);
			}
			else if(strncmp(cmd, "A", 1) == 0)
			{
				debug_log("volume is %d type, %d\n", g_volume_type, g_volume_value);
				ret = mm_sound_play_loud_solo_sound(g_file_name, g_volume_type, mycallback ,"USERDATA", &handle);
				if(ret < 0)
					debug_log("mm_sound_play_sound_loud_solo() failed with 0x%x\n", ret);
			}
			else if(strncmp(cmd, "F", 1) == 0)
			{
				char num = 0;
				char input_string[128] = "";
				char *tok = NULL;
				int tonetime=0;
				double volume=1.0;
				int volume_type = -1;
				bool enable_session = 0;
				MMSoundTone_t tone = MM_SOUND_TONE_DTMF_0;

				while(num != 'q') {
					fflush(stdin);
					g_print("enter number(0~H exit:q), volume type(0~7),  volume(0.0~1.0),  time(ms), enable_session(0:unable , 1:enable):\t ");
					if (fgets(input_string, sizeof(input_string)-1, stdin) == NULL) {
						g_print ("### fgets return  NULL\n");
					}
					tok = strtok(input_string, " ");
					if(!tok) continue;
					if(tok[0] == 'q') {
						break;
					}
					else if(tok[0] < '0' || tok[0] > '~') {
						if(tok[0] == '*' || tok[0] == '#')
							;
						else
							continue;
					}
					num = tok[0];
					if(num >= '0' && num <= '9') {
						tone = (MMSoundTone_t)(num - '0');
					}
					else if(num == '*') {
						tone = MM_SOUND_TONE_DTMF_S;
					}
					else if(num == '#') {
						tone =MM_SOUND_TONE_DTMF_P;
					}
					else if(num == 'A') {	tone = MM_SOUND_TONE_DTMF_A;	}
					else if(num == 'B') {	tone = MM_SOUND_TONE_DTMF_B;	}
					else if(num == 'C') {	tone = MM_SOUND_TONE_DTMF_C;	}
					else if(num == 'D') {	tone = MM_SOUND_TONE_DTMF_D;	}
					else if(num == 'E') {	tone = MM_SOUND_TONE_SUP_DIAL;	}
					else if(num == 'F') {	tone = MM_SOUND_TONE_ANSI_DIAL;	}
					else if(num == 'G') {	tone = MM_SOUND_TONE_JAPAN_DIAL;	}
					else if(num == 'H') {	tone = MM_SOUND_TONE_SUP_BUSY;		}
					else if(num == 'I') {		tone = MM_SOUND_TONE_ANSI_BUSY;		}
					else if(num == 'J') {		tone = MM_SOUND_TONE_JAPAN_BUSY;		}
					else if(num == 'K') {	tone = MM_SOUND_TONE_SUP_CONGESTION;		}
					else if(num == 'L') {		tone = MM_SOUND_TONE_ANSI_CONGESTION;		}
					else if(num == 'M') {	tone = MM_SOUND_TONE_SUP_RADIO_ACK;		}
					else if(num == 'N') {	tone = MM_SOUND_TONE_JAPAN_RADIO_ACK;		}
					else if(num == 'O') {	tone = MM_SOUND_TONE_SUP_RADIO_NOTAVAIL;	}
					else if(num == 'P') {	tone = MM_SOUND_TONE_SUP_ERROR;		}
					else if(num == 'Q') {	tone = MM_SOUND_TONE_SUP_CALL_WAITING;	}
					else if(num == 'R') {	tone = MM_SOUND_TONE_ANSI_CALL_WAITING;	}
					else if(num == 'S') {	tone = MM_SOUND_TONE_SUP_RINGTONE;		}
					else if(num == 'T') {	tone = MM_SOUND_TONE_ANSI_RINGTONE;	}
					else if(num == 'U') {	tone = MM_SOUND_TONE_PROP_BEEP;		}
					else if(num == 'V') {	tone = MM_SOUND_TONE_PROP_ACK;		}
					else if(num == 'W') {	tone = MM_SOUND_TONE_PROP_NACK;	}
					else if(num == 'X') {	tone = MM_SOUND_TONE_PROP_PROMPT;	}
					else if(num == 'Y') {	tone = MM_SOUND_TONE_PROP_BEEP2;	}
					else if(num == 'Z')  {	tone =MM_SOUND_TONE_CDMA_HIGH_SLS;	}
					else if(num == '[')  {	tone = MM_SOUND_TONE_CDMA_MED_SLS;	}
					else if(num == ']')  {	tone = MM_SOUND_TONE_CDMA_LOW_SLS;	}
					else if(num == '^')  {	tone =MM_SOUND_TONE_CDMA_HIGH_S_X4;	}
					else if(num == '_')  {	tone =MM_SOUND_TONE_CDMA_MED_S_X4;	}
					else if(num == 'a')  {	tone =MM_SOUND_TONE_CDMA_LOW_S_X4;	}
					else if(num == 'b')  {	tone =MM_SOUND_TONE_CDMA_HIGH_PBX_L;	}
					else if(num == 'c')  {	tone =MM_SOUND_TONE_CDMA_MED_PBX_L;	}
					else if(num == 'd')  {	tone =MM_SOUND_TONE_CDMA_LOW_PBX_L;	}
					else if(num == 'e')  {	tone =MM_SOUND_TONE_CDMA_HIGH_PBX_SS;	}
					else if(num == 'f')  {	tone =MM_SOUND_TONE_CDMA_MED_PBX_SS;	}
					else if(num == 'g')  {	tone =MM_SOUND_TONE_CDMA_LOW_PBX_SS;	}
					else if(num == 'h')  {	tone =MM_SOUND_TONE_CDMA_HIGH_PBX_SSL;	}
					else if(num == 'i')  {		tone =MM_SOUND_TONE_CDMA_MED_PBX_SSL;	}
					else if(num == 'j')  {	tone =MM_SOUND_TONE_CDMA_LOW_PBX_SSL;		}
					else if(num == 'k')  {	tone =MM_SOUND_TONE_CDMA_HIGH_PBX_SLS;	}
					else if(num == 'l')  {		tone =MM_SOUND_TONE_CDMA_MED_PBX_SLS;	}
					else if(num == 'm')  {	tone =MM_SOUND_TONE_CDMA_LOW_PBX_SLS;		}
					else if(num == 'n')  {	tone =MM_SOUND_TONE_CDMA_HIGH_PBX_S_X4;	}
					else if(num == 'o')  {	tone =MM_SOUND_TONE_CDMA_MED_PBX_S_X4;	}
					else if(num == 'p')  {	tone =MM_SOUND_TONE_CDMA_LOW_PBX_S_X4;	}
					else if(num == 'q')  {	tone =MM_SOUND_TONE_CDMA_ALERT_NETWORK_LITE;	}
					else if(num == 'r')  {	tone =MM_SOUND_TONE_CDMA_ALERT_AUTOREDIAL_LITE;	}
					else if(num == 's')  {	tone =MM_SOUND_TONE_CDMA_ONE_MIN_BEEP;	}
					else if(num == 't')  {	tone =MM_SOUND_TONE_CDMA_KEYPAD_VOLUME_KEY_LITE;		}
					else if(num == 'u')  {	tone =MM_SOUND_TONE_CDMA_PRESSHOLDKEY_LITE;	}
					else if(num == 'v')  {	tone =MM_SOUND_TONE_CDMA_ALERT_INCALL_LITE;	}
					else if(num == 'w')  {	tone =MM_SOUND_TONE_CDMA_EMERGENCY_RINGBACK;	}
					else if(num == 'x')  {	tone =MM_SOUND_TONE_CDMA_ALERT_CALL_GUARD;	}
					else if(num == 'y')  {	tone =MM_SOUND_TONE_CDMA_SOFT_ERROR_LITE;	}
					else if(num == 'z')  {	tone =MM_SOUND_TONE_CDMA_CALLDROP_LITE;	}
					else if(num == '{')  {	tone =MM_SOUND_TONE_LOW_FRE;	}
					else if(num == '}')  {	tone =MM_SOUND_TONE_MED_FRE;	}
					else if(num == '~')  {	tone =MM_SOUND_TONE_HIGH_FRE; }
					tok = strtok(NULL, " ");
					if(tok)  volume_type = (double)atoi(tok);

					tok = strtok(NULL, " ");
					if(tok)  volume = (double)atof(tok);

					tok = strtok(NULL, " ");
					if(tok)
					{
						tonetime = atoi(tok);
					}
					else
					{
						tonetime = MIN_TONE_PLAY_TIME;
					}

					tok = strtok(NULL, " ");
					if(tok)  enable_session = (bool)atof(tok);

					debug_log("volume type: %d\t volume is %f\t tonetime: %d\t enable_session %d \n", volume_type, volume, tonetime, enable_session);
					ret = mm_sound_play_tone_ex(tone, volume_type, volume, tonetime, &handle, enable_session);
					if(ret<0)
						debug_log ("[magpie] Play DTMF sound cannot be played ! %d\n", handle);
				}
			}
			else if (strncmp (cmd, "b",1) == 0)
			{
				DIR	*basedir;
				struct dirent *entry;
				struct stat file_stat;
				char fullpath[MAX_PATH_LEN]="";
				struct timespec start_time = {0,};
				struct timespec current_time = {0,};

				if(g_dir_name[strlen(g_dir_name)-1] == '/')
					g_dir_name[strlen(g_dir_name)-1] = '\0';

				basedir = opendir(g_dir_name);
				if(basedir != NULL)
				{
					while( (entry = readdir(basedir)) != NULL)
					{
						int playfail =0;
						int mywait = 0;
						if(entry->d_name[0] == '.')
							continue;
						memset(fullpath, '\0' ,sizeof(fullpath));
						snprintf(fullpath, sizeof(fullpath)-1,"%s/%s", g_dir_name, entry->d_name);
						debug_log("Try %s\n", fullpath);

						if (lstat(fullpath, &file_stat) == -1)
							continue;

						if(S_ISREG(file_stat.st_mode))
						{
							test_callback_done = 0 ;
							start_time.tv_sec = (long int)(time(NULL));
							start_time.tv_nsec = 0;
							ret = mm_sound_play_sound(fullpath, g_volume_type, test_callback, NULL, &handle);
							if(ret != MM_ERROR_NONE)
							{
								debug_log("Play file error : %s\n", fullpath);
								sleep(4);
								playfail = 1;
							}
						}
						else
						{
							debug_log("this is not regular file : %s\n", fullpath);
							playfail = 1;
						}
						while((test_callback_done == 0) && (playfail ==0))
						{
							current_time.tv_sec = (long int)(time(NULL));
							current_time.tv_nsec = 0;
							if(current_time.tv_sec - start_time.tv_sec > 200)
							{
								if((++mywait)%5 == 0)
								{
									debug_log("I'm waiting callback for %d seconds after play %s\n",
											(int)(current_time.tv_sec - start_time.tv_sec),
											fullpath);
								}
							}
							sleep(2);
						}
						debug_log("goto next file\n");
					}
					closedir(basedir);
				}
				else
				{
					debug_log("Cannot Open such a directory %s\n", g_dir_name);
				}

			}

		else if (strncmp (cmd, "c",1) == 0)
		{
			soundparam.volume = g_volume_value;
			soundparam.loop = -1;	/* loop case */
			soundparam.callback = mycallback;
			soundparam.data = NULL;
			soundparam.mem_ptr = NULL;
			soundparam.mem_size = 0;
			soundparam.filename = g_file_name;
			soundparam.volume_config = g_volume_type;

			if ((mm_sound_play_sound_ex (&soundparam, &handle))<0)
				debug_log ("Play EX sound cannot be played !\n");

			debug_log ("Ex sound is played Handle is [%d]\n", handle);
		}
		else if (strncmp (cmd, "f",1) == 0) {
			g_menu_state=CURRENT_STATUS_FILENAME;
		}

		else if (strncmp (cmd, "d",1) == 0) {
			g_menu_state=CURRENT_STATUS_DIRNAME;
		}
		else if (strncmp (cmd, "s",1) == 0) {
			if(mm_sound_stop_sound(handle))
				debug_log (" Cannot stop sound !!! %d \n", handle);
		}
		else if (strncmp (cmd, "1",1) == 0) {
			//ap to spk
			g_print("Not supported - Set path for speaker playback\n");
		}

		else if (strncmp (cmd, "2",1) == 0) {
			//ap to headset
			g_print("Not supported - Set path for headset playback\n");
		}
		else if (strncmp (cmd, "3",1) == 0) {

		}
		else if (strncmp (cmd, "4",1) == 0) {
			//recording
			g_print("Not supported - Set path for recording with main mic\n");
		}
		else if (strncmp (cmd, "5",1) == 0) {

		}
		else if (strncmp (cmd, "6",1) == 0) {
			//voice call
			g_print("Not supported - Set path for voicecall\n");
		}
		else if (strncmp (cmd, "7",1) == 0) {
			//voicecall release
			g_print("Not supported - release path for voicecall\n");
		}
		else if (strncmp (cmd, "8",1) == 0) {
			//voice call
			g_print("Not supported - Set path for VT call\n");
		}

		else if (strncmp (cmd, "9",1) == 0) {
			//voicecall release
			g_print("Not supported - release path for VT call\n");
		}

		/* -------------------------- Route Test : Starts -------------------------- */
#if 0
		g_print("==================================================================\n");
		g_print("	Audio route APIs\n");
		g_print("==================================================================\n");
		g_print("u : Foreach Available Routes \t");
		g_print("i : Get Active Devices     \n");
		g_print("o : Add Available Routes Callback   \t");
		g_print("O : Remove Available Routes Callback   \n");
		g_print("p : Add Active Route Callback\t");
		g_print("P : Remove Active Route Callback \n");
		g_print("{ : Get BT A2DP Status\n");
#endif
		else if (strncmp(cmd, "u", 1) == 0) {
			int ret = 0;
			ret = mm_sound_foreach_available_route_cb(__mm_sound_foreach_available_route_cb, NULL);
			if (ret == MM_ERROR_NONE) {
				g_print ("### mm_sound_foreach_available_route_cb() Success\n\n");
			} else {
				g_print ("### mm_sound_foreach_available_route_cb() Error : errno [%x]\n\n", ret);
			}
		}
		else if (strncmp(cmd, "i", 1) == 0) {
			int ret = 0;
			mm_sound_device_in in = MM_SOUND_DEVICE_IN_NONE;
			mm_sound_device_out out = MM_SOUND_DEVICE_OUT_NONE;
			ret = mm_sound_get_active_device(&in, &out);
			if (ret == MM_ERROR_NONE) {
				g_print ("### mm_sound_get_active_device() Success : in[0x%08x][%s], out[0x%08x][%s]\n\n",
						in, __get_capture_device_str(in), out, __get_playback_device_str(out));
			} else {
				g_print ("### mm_sound_get_active_device() Error : errno [%x]\n\n", ret);
			}
		}
		else if (strncmp(cmd, "o", 1) == 0) {
			int ret = 0;
			ret = mm_sound_add_available_route_changed_callback(__mm_sound_available_route_changed_cb, NULL);
			if (ret == MM_ERROR_NONE) {
				g_print ("### mm_sound_add_available_route_changed_callback() Success\n\n");
			} else {
				g_print ("### mm_sound_add_available_route_changed_callback() Error : errno [%x]\n\n", ret);
			}
		}
		else if (strncmp(cmd, "O", 1) == 0) {
			int ret = 0;
			ret = mm_sound_remove_available_route_changed_callback();
			if (ret == MM_ERROR_NONE) {
				g_print ("### mm_sound_remove_available_route_changed_callback() Success\n\n");
			} else {
				g_print ("### mm_sound_remove_available_route_changed_callback() Error : errno [%x]\n\n", ret);
			}
		}
		else if (strncmp(cmd, "p", 1) == 0) {
			int ret = 0;
			ret = mm_sound_add_active_device_changed_callback("null", __mm_sound_active_device_changed_cb, NULL);
			if (ret == MM_ERROR_NONE) {
				g_print ("### mm_sound_add_active_device_changed_callback() Success\n\n");
			} else {
				g_print ("### mm_sound_add_active_device_changed_callback() Error : errno [%x]\n\n", ret);
			}
		}
		else if (strncmp(cmd, "P", 1) == 0) {
			int ret = 0;
			ret = mm_sound_remove_active_device_changed_callback("null");
			if (ret == MM_ERROR_NONE) {
				g_print ("### mm_sound_remove_active_device_changed_callback() Success\n\n");
			} else {
				g_print ("### mm_sound_remove_active_device_changed_callback() Error : errno [%x]\n\n", ret);
			}
		}
		else if (strncmp(cmd, "{", 1) == 0) {
			int ret = 0;
			bool connected = 0;
			char* bt_name = NULL;

			ret = mm_sound_route_get_a2dp_status (&connected, &bt_name);
			if (ret == MM_ERROR_NONE) {
				g_print ("### mm_sound_route_get_a2dp_status() Success : connected=[%d] name=[%s]\n", connected, bt_name);
				if (bt_name)
					free (bt_name);
			} else {
				g_print ("### mm_sound_route_get_a2dp_status() Error : errno [%x]\n", ret);
			}
		}

		else if(strncmp(cmd, "}", 1) == 0)
		{
			int ret = 0;
			char input_string[128];
			mm_sound_route route = MM_SOUND_ROUTE_OUT_SPEAKER;
			char num;
			char need_broadcast;

			fflush(stdin);
			g_print ("1. MM_SOUND_ROUTE_OUT_SPEAKER\n");
			g_print ("2. MM_SOUND_ROUTE_OUT_RECEIVER\n");
			g_print ("3. MM_SOUND_ROUTE_OUT_WIRED_ACCESSORY\n");
			g_print ("4. MM_SOUND_ROUTE_OUT_BLUETOOTH_A2DP\n");
			g_print ("5. MM_SOUND_ROUTE_OUT_DOCK\n");
			g_print ("6. MM_SOUND_ROUTE_OUT_HDMI\n");
			g_print ("7. MM_SOUND_ROUTE_OUT_MIRRORING\n");
			g_print ("8. MM_SOUND_ROUTE_OUT_USB_AUDIO\n");
			g_print ("9. MM_SOUND_ROUTE_OUT_MULTIMEDIA_DOCK\n");
			g_print ("0. MM_SOUND_ROUTE_IN_MIC\n");
			g_print ("a. MM_SOUND_ROUTE_IN_WIRED_ACCESSORY\n");
			g_print ("b. MM_SOUND_ROUTE_IN_MIC_OUT_RECEIVER\n");
			g_print ("c. MM_SOUND_ROUTE_IN_MIC_OUT_SPEAKER\n");
			g_print ("d. MM_SOUND_ROUTE_IN_MIC_OUT_HEADPHONE\n");
			g_print ("e. MM_SOUND_ROUTE_INOUT_HEADSET\n");
			g_print ("f. MM_SOUND_ROUTE_INOUT_BLUETOOTH\n");
			g_print("> select route number and select is need broadcast(1 need , 0 no need):  (eg . 1 1)");

			if (fgets(input_string, sizeof(input_string)-1, stdin)) {
				g_print ("### fgets return  NULL\n");
			}
			num = input_string[0];
			need_broadcast = input_string[2];

			if(num == '1') { route = MM_SOUND_ROUTE_OUT_SPEAKER; }
			else if(num == '2') { route = MM_SOUND_ROUTE_OUT_RECEIVER; }
			else if(num == '3') { route = MM_SOUND_ROUTE_OUT_WIRED_ACCESSORY; }
			else if(num == '4') { route = MM_SOUND_ROUTE_OUT_BLUETOOTH_A2DP; }
			else if(num == '5') { route = MM_SOUND_ROUTE_OUT_DOCK; }
			else if(num == '6') { route = MM_SOUND_ROUTE_OUT_HDMI; }
			else if(num == '7') { route = MM_SOUND_ROUTE_OUT_MIRRORING; }
			else if(num == '8') { route = MM_SOUND_ROUTE_OUT_USB_AUDIO; }
			else if(num == '9') { route = MM_SOUND_ROUTE_OUT_MULTIMEDIA_DOCK; }
			else if(num == '0') { route = MM_SOUND_ROUTE_IN_MIC; }
			else if(num == 'a') { route = MM_SOUND_ROUTE_IN_WIRED_ACCESSORY; }
			else if(num == 'b') { route = MM_SOUND_ROUTE_IN_MIC_OUT_RECEIVER; }
			else if(num == 'c') { route = MM_SOUND_ROUTE_IN_MIC_OUT_SPEAKER; }
			else if(num == 'd') { route = MM_SOUND_ROUTE_IN_MIC_OUT_HEADPHONE; }
			else if(num == 'e') { route = MM_SOUND_ROUTE_INOUT_HEADSET; }
			else if(num == 'f') { route = MM_SOUND_ROUTE_INOUT_BLUETOOTH; }

			if (need_broadcast == '1') {
				ret = mm_sound_set_active_route(route);
				if (ret == MM_ERROR_NONE) {
					g_print ("### mm_sound_set_active_route(%s) Success\n\n", __get_route_str (route));
				} else {
					g_print ("### mm_sound_set_active_route(%s) Error : errno [%x]\n\n", __get_route_str (route), ret);
				}

			} else {
				ret = mm_sound_set_active_route_without_broadcast(route);
				if (ret == MM_ERROR_NONE) {
					g_print ("### mm_sound_set_active_route_without_broadcast(%s) Success\n\n", __get_route_str (route));
				} else {
					g_print ("### mm_sound_set_active_route_without_broadcast(%s) Error : errno [%x]\n\n", __get_route_str (route), ret);
				}
			}
	}

		else if(strncmp(cmd, "z", 1) ==0) {
			if(MM_ERROR_NONE != mm_session_init(MM_SESSION_TYPE_CALL))
			{
				g_print("Call session init failed\n");
			}
		}
		else if(strncmp(cmd, "Z", 1) ==0) {
			if(MM_ERROR_NONE != mm_session_init(MM_SESSION_TYPE_VIDEOCALL))
			{
				g_print("VideoCall session init failed\n");
			}
		}
		else if(strncmp(cmd, "N", 1) ==0) {
			if(MM_ERROR_NONE != mm_session_init(MM_SESSION_TYPE_NOTIFY))
			{
				g_print("Notify session init failed\n");
			}
		}
		else if(strncmp(cmd, "n", 1) ==0) {
			if(MM_ERROR_NONE != mm_session_init(MM_SESSION_TYPE_VOIP))
			{
				g_print("VOIP session init failed\n");
			}
		}
		else if(strncmp(cmd, "v", 1) ==0) {
			if(MM_ERROR_NONE != mm_session_finish())
			{
				g_print("Call session finish failed\n");
			}
		}

		else if(strncmp(cmd, "V", 1) ==0) {
			int value;
			if(vconf_get_int(SOUND_STATUS_KEY, &value)) {
				g_print("Can not get %s\n", SOUND_STATUS_KEY);
			}
			else
			{
				if(value == ASM_STATUS_NONE || value == ASM_STATUS_MONITOR)
				{
					g_print("No Session Instance\n");
				}
				if(value & ASM_STATUS_MEDIA_MMPLAYER) {
					g_print("MEDIA - PLAYER\n");
				}
				if(value & ASM_STATUS_MEDIA_MMSOUND) {
					g_print("MEDIA - SOUND\n");
				}
				if(value & ASM_STATUS_MEDIA_OPENAL) {
					g_print("MEDIA - OPENAL\n");
				}
				if(value & ASM_STATUS_NOTIFY) {
					g_print("NOTIFY\n");
				}
				if(value & ASM_STATUS_ALARM) {
					g_print("ALARM\n");
				}
				if(value & ASM_STATUS_CALL) {
					g_print("CALL\n");
				}
				if(value & ASM_STATUS_VIDEOCALL) {
					g_print("VIDEOCALL\n");
				}
				if(value & ASM_STATUS_VOIP) {
					g_print("VOIP\n");
				}
			}
		}

		else if(strncmp(cmd, "L", 1) ==0) {
			int ret = 0;
			mm_sound_device_flags_e flags = MM_SOUND_DEVICE_ALL_FLAG;
			MMSoundDeviceList_t device_list;
			int type = 0;
			int io_direction = 0;
			int state = 0;
			int id = 0;
			char *name = NULL;
			MMSoundDevice_t device_h = NULL;
			int dret = MM_ERROR_NONE;

			ret = mm_sound_get_current_device_list(flags, &device_list);
			if (ret) {
				g_print("failed to mm_sound_get_current_device_list(), ret[0x%x]\n", ret);
			} else {
				g_print("device_list[0x%x], device_h[0x%x]\n", device_list, device_h);
				do {
					dret = mm_sound_get_next_device (device_list, &device_h);
					if (dret) {
						debug_error("failed to mm_sound_get_next_device(), dret[0x%x]\n", dret);
					} else {
						ret = mm_sound_get_device_type(device_h, &type);
						if (ret) {
							debug_error("failed to mm_sound_get_device_type()\n");
						}
						ret = mm_sound_get_device_io_direction(device_h, &io_direction);
						if (ret) {
							debug_error("failed to mm_sound_get_device_io_direction()\n");
						}
						ret = mm_sound_get_device_state(device_h, &state);
						if (ret) {
							debug_error("failed to mm_sound_get_device_state()\n");
						}
						ret = mm_sound_get_device_id(device_h, &id);
						if (ret) {
							debug_error("failed to mm_sound_get_device_id()\n");
						}
						ret = mm_sound_get_device_name(device_h, &name);
						if (ret) {
							debug_error("failed to mm_sound_get_device_name()\n");
						}
						debug_log("*** --- [NEXT DEVICE] type[%d], id[%d], io_direction[%d], state[%d], name[%s]\n", type, id, io_direction, state, name);
					}
				} while (dret == MM_ERROR_NONE);
				do {
					dret = MM_ERROR_NONE;
					dret = mm_sound_get_prev_device (device_list, &device_h);
					if (dret) {
						debug_error("failed to mm_sound_get_prev_device(), dret[0x%x]\n", dret);
					} else {
						ret = mm_sound_get_device_type(device_h, &type);
						if (ret) {
							debug_error("failed to mm_sound_get_device_type()\n");
						}
						ret = mm_sound_get_device_io_direction(device_h, &io_direction);
						if (ret) {
							debug_error("failed to mm_sound_get_device_io_direction()\n");
						}
						ret = mm_sound_get_device_state(device_h, &state);
						if (ret) {
							debug_error("failed to mm_sound_get_device_state()\n");
						}
						ret = mm_sound_get_device_id(device_h, &id);
						if (ret) {
							debug_error("failed to mm_sound_get_device_id()\n");
						}
						ret = mm_sound_get_device_name(device_h, &name);
						if (ret) {
							debug_error("failed to mm_sound_get_device_name()\n");
						}
						debug_log("*** --- [PREV DEVICE] type[%d], id[%d], io_direction[%d], state[%d], name[%s]\n", type, id, io_direction, state, name);
					}
				} while (dret == MM_ERROR_NONE);
			}
		}

		else if(strncmp(cmd, "C", 1) ==0) {
			int ret = 0;
			char input_string[128];
			mm_sound_device_flags_e device_flag_1 = MM_SOUND_DEVICE_ALL_FLAG;
			mm_sound_device_flags_e device_flag_2 = MM_SOUND_DEVICE_ALL_FLAG;
			mm_sound_device_flags_e device_flag_3 = MM_SOUND_DEVICE_ALL_FLAG;

			char flag_1, flag_2, flag_3;

			fflush(stdin);
			g_print ("1. IO_DIRECTION_IN_FLAG\n");
			g_print ("2. IO_DIRECTION_OUT_FLAG\n");
			g_print ("3. IO_DIRECTION_BOTH_FLAG\n");
			g_print ("4. TYPE_INTERNAL_FLAG\n");
			g_print ("5. TYPE_EXTERNAL_FLAG\n");
			g_print ("6. STATE_DEACTIVATED_FLAG\n");
			g_print ("7. STATE_ACTIVATED_FLAG\n");
			g_print ("8. ALL_FLAG\n");
			g_print("> select flag numbers (total 3):  (eg. 2 5 7)");

			if (fgets(input_string, sizeof(input_string)-1, stdin)) {
				g_print ("### fgets return  NULL\n");
			}
			flag_1 = input_string[0];
			flag_2 = input_string[2];
			flag_3 = input_string[4];

			if(flag_1 == '1') { device_flag_1 = MM_SOUND_DEVICE_IO_DIRECTION_IN_FLAG; }
			else if(flag_1 == '2') { device_flag_1 = MM_SOUND_DEVICE_IO_DIRECTION_OUT_FLAG; }
			else if(flag_1 == '3') { device_flag_1 = MM_SOUND_DEVICE_IO_DIRECTION_BOTH_FLAG; }
			else if(flag_1 == '4') { device_flag_1 = MM_SOUND_DEVICE_TYPE_INTERNAL_FLAG; }
			else if(flag_1 == '5') { device_flag_1 = MM_SOUND_DEVICE_TYPE_EXTERNAL_FLAG; }
			else if(flag_1 == '6') { device_flag_1 = MM_SOUND_DEVICE_STATE_DEACTIVATED_FLAG; }
			else if(flag_1 == '7') { device_flag_1 = MM_SOUND_DEVICE_STATE_ACTIVATED_FLAG; }
			else if(flag_1 == '8') { device_flag_1 = MM_SOUND_DEVICE_ALL_FLAG; }
			if(flag_2 == '1') { device_flag_2 = MM_SOUND_DEVICE_IO_DIRECTION_IN_FLAG; }
			else if(flag_2 == '2') { device_flag_2 = MM_SOUND_DEVICE_IO_DIRECTION_OUT_FLAG; }
			else if(flag_2 == '3') { device_flag_2 = MM_SOUND_DEVICE_IO_DIRECTION_BOTH_FLAG; }
			else if(flag_2 == '4') { device_flag_2 = MM_SOUND_DEVICE_TYPE_INTERNAL_FLAG; }
			else if(flag_2 == '5') { device_flag_2 = MM_SOUND_DEVICE_TYPE_EXTERNAL_FLAG; }
			else if(flag_2 == '6') { device_flag_2 = MM_SOUND_DEVICE_STATE_DEACTIVATED_FLAG; }
			else if(flag_2 == '7') { device_flag_2 = MM_SOUND_DEVICE_STATE_ACTIVATED_FLAG; }
			else if(flag_2 == '8') { device_flag_2 = MM_SOUND_DEVICE_ALL_FLAG; }
			if(flag_3 == '1') { device_flag_3 = MM_SOUND_DEVICE_IO_DIRECTION_IN_FLAG; }
			else if(flag_3 == '2') { device_flag_3 = MM_SOUND_DEVICE_IO_DIRECTION_OUT_FLAG; }
			else if(flag_3 == '3') { device_flag_3 = MM_SOUND_DEVICE_IO_DIRECTION_BOTH_FLAG; }
			else if(flag_3 == '4') { device_flag_3 = MM_SOUND_DEVICE_TYPE_INTERNAL_FLAG; }
			else if(flag_3 == '5') { device_flag_3 = MM_SOUND_DEVICE_TYPE_EXTERNAL_FLAG; }
			else if(flag_3 == '6') { device_flag_3 = MM_SOUND_DEVICE_STATE_DEACTIVATED_FLAG; }
			else if(flag_3 == '7') { device_flag_3 = MM_SOUND_DEVICE_STATE_ACTIVATED_FLAG; }
			else if(flag_3 == '8') { device_flag_3 = MM_SOUND_DEVICE_ALL_FLAG; }

			ret = mm_sound_add_device_connected_callback(device_flag_1|device_flag_2|device_flag_3, device_connected_cb, NULL);
			if (ret) {
				g_print("failed to mm_sound_add_device_connected_callback(), ret[0x%x]\n", ret);
			} else {
				g_print("device_flags[0x%x], callback fun[0x%x]\n", device_flag_1|device_flag_2|device_flag_3, device_connected_cb);
			}
		}

		else if(strncmp(cmd, "D", 1) ==0) {
			int ret = 0;
			ret = mm_sound_remove_device_connected_callback();
			if (ret) {
				g_print("failed to mm_sound_remove_device_connected_callback(), ret[0x%x]\n", ret);
			}
		}

		else if(strncmp(cmd, "Q", 1) ==0) {
			int ret = 0;
			char input_string[128];
			mm_sound_device_flags_e device_flag_1 = MM_SOUND_DEVICE_ALL_FLAG;
			mm_sound_device_flags_e device_flag_2 = MM_SOUND_DEVICE_ALL_FLAG;
			mm_sound_device_flags_e device_flag_3 = MM_SOUND_DEVICE_ALL_FLAG;

			char flag_1, flag_2, flag_3;

			fflush(stdin);
			g_print ("1. IO_DIRECTION_IN_FLAG\n");
			g_print ("2. IO_DIRECTION_OUT_FLAG\n");
			g_print ("3. IO_DIRECTION_BOTH_FLAG\n");
			g_print ("4. TYPE_INTERNAL_FLAG\n");
			g_print ("5. TYPE_EXTERNAL_FLAG\n");
			g_print ("6. STATE_DEACTIVATED_FLAG\n");
			g_print ("7. STATE_ACTIVATED_FLAG\n");
			g_print ("8. ALL_FLAG\n");
			g_print("> select flag numbers (total 3):  (eg. 2 5 7)");

			if (fgets(input_string, sizeof(input_string)-1, stdin)) {
				g_print ("### fgets return  NULL\n");
			}
			flag_1 = input_string[0];
			flag_2 = input_string[2];
			flag_3 = input_string[4];

			if(flag_1 == '1') { device_flag_1 = MM_SOUND_DEVICE_IO_DIRECTION_IN_FLAG; }
			else if(flag_1 == '2') { device_flag_1 = MM_SOUND_DEVICE_IO_DIRECTION_OUT_FLAG; }
			else if(flag_1 == '3') { device_flag_1 = MM_SOUND_DEVICE_IO_DIRECTION_BOTH_FLAG; }
			else if(flag_1 == '4') { device_flag_1 = MM_SOUND_DEVICE_TYPE_INTERNAL_FLAG; }
			else if(flag_1 == '5') { device_flag_1 = MM_SOUND_DEVICE_TYPE_EXTERNAL_FLAG; }
			else if(flag_1 == '6') { device_flag_1 = MM_SOUND_DEVICE_STATE_DEACTIVATED_FLAG; }
			else if(flag_1 == '7') { device_flag_1 = MM_SOUND_DEVICE_STATE_ACTIVATED_FLAG; }
			else if(flag_1 == '8') { device_flag_1 = MM_SOUND_DEVICE_ALL_FLAG; }
			if(flag_2 == '1') { device_flag_2 = MM_SOUND_DEVICE_IO_DIRECTION_IN_FLAG; }
			else if(flag_2 == '2') { device_flag_2 = MM_SOUND_DEVICE_IO_DIRECTION_OUT_FLAG; }
			else if(flag_2 == '3') { device_flag_2 = MM_SOUND_DEVICE_IO_DIRECTION_BOTH_FLAG; }
			else if(flag_2 == '4') { device_flag_2 = MM_SOUND_DEVICE_TYPE_INTERNAL_FLAG; }
			else if(flag_2 == '5') { device_flag_2 = MM_SOUND_DEVICE_TYPE_EXTERNAL_FLAG; }
			else if(flag_2 == '6') { device_flag_2 = MM_SOUND_DEVICE_STATE_DEACTIVATED_FLAG; }
			else if(flag_2 == '7') { device_flag_2 = MM_SOUND_DEVICE_STATE_ACTIVATED_FLAG; }
			else if(flag_2 == '8') { device_flag_2 = MM_SOUND_DEVICE_ALL_FLAG; }
			if(flag_3 == '1') { device_flag_3 = MM_SOUND_DEVICE_IO_DIRECTION_IN_FLAG; }
			else if(flag_3 == '2') { device_flag_3 = MM_SOUND_DEVICE_IO_DIRECTION_OUT_FLAG; }
			else if(flag_3 == '3') { device_flag_3 = MM_SOUND_DEVICE_IO_DIRECTION_BOTH_FLAG; }
			else if(flag_3 == '4') { device_flag_3 = MM_SOUND_DEVICE_TYPE_INTERNAL_FLAG; }
			else if(flag_3 == '5') { device_flag_3 = MM_SOUND_DEVICE_TYPE_EXTERNAL_FLAG; }
			else if(flag_3 == '6') { device_flag_3 = MM_SOUND_DEVICE_STATE_DEACTIVATED_FLAG; }
			else if(flag_3 == '7') { device_flag_3 = MM_SOUND_DEVICE_STATE_ACTIVATED_FLAG; }
			else if(flag_3 == '8') { device_flag_3 = MM_SOUND_DEVICE_ALL_FLAG; }

			ret = mm_sound_add_device_information_changed_callback(device_flag_1|device_flag_2|device_flag_3, device_info_changed_cb, NULL);
			if (ret) {
				g_print("failed to mm_sound_add_device_information_changed_callback(), ret[0x%x]\n", ret);
			} else {
				g_print("device_flags[0x%x], callback fun[0x%x]\n", device_flag_1|device_flag_2|device_flag_3, device_info_changed_cb);
			}
		}

		else if(strncmp(cmd, "W", 1) ==0) {
			int ret = 0;
			ret = mm_sound_remove_device_information_changed_callback();
			if (ret) {
				g_print("failed to mm_sound_remove_device_information_changed_callback(), ret[0x%x]\n", ret);
			}
		}
		else if (strncmp(cmd, "x", 1) == 0) {
			quit_program();
		}
		break;

	case CURRENT_STATUS_FILENAME:
		input_filename(cmd);
		g_menu_state=CURRENT_STATUS_MAINMENU;
		break;

	case CURRENT_STATUS_DIRNAME:
		input_dirname(cmd);
		g_menu_state=CURRENT_STATUS_MAINMENU;
		break;
	case CURRENT_STATUS_POSITION:
		break;
	}
	//g_timeout_add(100, timeout_menu_display, 0);
}

void volume_change_callback(volume_type_t type, unsigned int volume, void *user_data)
{
	if (type == VOLUME_TYPE_MEDIA)
		g_print("Volume Callback Runs :::: MEDIA VALUME %d\n", volume);
}

void muteall_change_callback(void* data)
{
	int  muteall;

	mm_sound_get_muteall(&muteall);
	g_print("Muteall Callback Runs :::: muteall value = %d\n", muteall);
}

void audio_route_policy_changed_callback(void* data, system_audio_route_t policy)
{
	int dummy = (int) data;
	system_audio_route_t lv_policy;
	char *str_route[SYSTEM_AUDIO_ROUTE_POLICY_MAX] = {
			"DEFAULT","IGN_A2DP","HANDSET"
		};
	g_print("Audio Route Policy has changed to [%s]\n", str_route[policy]);
	g_print("...read....current....policy...to cross check..%d\n", dummy);
	if(0 > mm_sound_route_get_system_policy(&lv_policy)) {
		g_print("Can not get policy...in callback function\n");
	}
	else {
		g_print("...readed policy [%s]\n", str_route[lv_policy]);
	}
}

int main(int argc, char *argv[])
{
	int ret = 0;
	stdin_channel = g_io_channel_unix_new(0);
	g_io_add_watch(stdin_channel, G_IO_IN, (GIOFunc)input, NULL);
	g_loop = g_main_loop_new (NULL, 1);

	g_print("callback function addr :: %p\n", volume_change_callback);
	g_volume_type = VOLUME_TYPE_MEDIA;
	ret = mm_sound_volume_get_value(g_volume_type, &g_volume_value);
	if(ret < 0) {
		g_print("mm_sound_volume_get_value 0x%x\n", ret);
	}

	MMSOUND_STRNCPY(g_file_name, POWERON_FILE, MAX_STRING_LEN);
	g_print("\nThe input filename is '%s' \n\n",g_file_name);

	mm_sound_add_volume_changed_callback(volume_change_callback, (void*) &g_volume_type);
	mm_sound_muteall_add_callback(muteall_change_callback);
	displaymenu();
	g_main_loop_run (g_loop);

	return 0;
}

