ifeq ($(ARCH),target)
	PKG_CONFIG_PATH=/usr/lib/pkgconfig
	export PKG_CONFIG_PATH
	CC=arm-linux-gcc -Wall
	CXX=arm-linux-g++ -Wall
else
	PKG_CONFIG_PATH=/usr/lib/pkgconfig
	export PKG_CONFIG_PATH
	CC=gcc -Wall
    CXX=gcc -Wall
endif

TS1 = utc_mm_sound_volume_get_step_func
TS2 = utc_mm_sound_volume_get_value_func
TS3 = utc_mm_sound_volume_set_value_func
TS4 = utc_mm_sound_volume_primary_type_set_func
TS5 = utc_mm_sound_volume_primary_type_clear_func
TS6 = utc_mm_sound_volume_add_callback_func
TS7 = utc_mm_sound_volume_remove_callback_func
TS8 = utc_mm_sound_volume_get_current_playing_type_func
TS9 = utc_mm_sound_pcm_capture_open_func
TS10 = utc_mm_sound_pcm_capture_read_func
TS11 = utc_mm_sound_pcm_capture_close_func
TS12 = utc_mm_sound_pcm_play_open_func
TS13 = utc_mm_sound_pcm_play_write_func
TS14 = utc_mm_sound_pcm_play_close_func
TS15 = utc_mm_sound_play_sound_func
TS16 = utc_mm_sound_stop_sound_func
TS17 = utc_mm_sound_play_tone_func
TS18 = utc_mm_sound_play_keysound_func



LIBS = `pkg-config --libs mm-sound mm-keysound `
LIBS +=-L/usr/lib/:/usr/lib/pkgconfig
LIBS +=$(TET_ROOT)/lib/tet3/tcm_s.o
LIBS +=-L$(TET_ROOT)/lib/tet3 -ltcm_s
LIBS +=-L$(TET_ROOT)/lib/tet3/ -lapi_s

INCS = -I. `pkg-config --cflags mm-sound mm-keysound `
INCS += -I$(TET_ROOT)/inc/tet3
INCS += -I/usr/include/mmf

CFLAGS = $(INCS) 
CC += $(CFLAGS)
LDFLAGS = $(LIBS)

all : $(TS1) $(TS2) $(TS3) $(TS4) $(TS5) $(TS6) $(TS7) $(TS8) $(TS9) $(TS10) $(TS11) $(TS12) $(TS13) $(TS14) $(TS15) $(TS16) $(TS17) $(TS18)

$(TS1): $(TS1).c
	$(CC) -o $(TS1) $(TS1).c $(LDFLAGS)

$(TS2): $(TS2).c
	$(CC) -o $(TS2) $(TS2).c $(LDFLAGS)
	
$(TS3): $(TS3).c
	$(CC) -o $(TS3) $(TS3).c $(LDFLAGS)
	
$(TS4): $(TS4).c
	$(CC) -o $(TS4) $(TS4).c $(LDFLAGS)

$(TS5): $(TS5).c
	$(CC) -o $(TS5) $(TS5).c $(LDFLAGS)
	
$(TS6): $(TS6).c
	$(CC) -o $(TS6) $(TS6).c $(LDFLAGS)
	
$(TS7): $(TS7).c
	$(CC) -o $(TS7) $(TS7).c $(LDFLAGS)

$(TS8): $(TS8).c
	$(CC) -o $(TS8) $(TS8).c $(LDFLAGS)
	
$(TS9): $(TS9).c
	$(CC) -o $(TS9) $(TS9).c $(LDFLAGS)
	
$(TS10): $(TS10).c
	$(CC) -o $(TS10) $(TS10).c $(LDFLAGS)
		
$(TS11): $(TS11).c
	$(CC) -o $(TS11) $(TS11).c $(LDFLAGS)
		
$(TS12): $(TS12).c
	$(CC) -o $(TS12) $(TS12).c $(LDFLAGS)
	
$(TS13): $(TS13).c
	$(CC) -o $(TS13) $(TS13).c $(LDFLAGS)
	
$(TS14): $(TS14).c
	$(CC) -o $(TS14) $(TS14).c $(LDFLAGS)
			
$(TS15): $(TS15).c
	$(CC) -o $(TS15) $(TS15).c $(LDFLAGS)
	
$(TS16): $(TS16).c
	$(CC) -o $(TS16) $(TS16).c $(LDFLAGS)
			
$(TS17): $(TS17).c
	$(CC) -o $(TS17) $(TS17).c $(LDFLAGS)
	
$(TS18): $(TS18).c
	$(CC) -o $(TS18) $(TS18).c $(LDFLAGS)
		
clean:
	rm -rf *~ *.o $(TS1) $(TS2) $(TS3) $(TS4) $(TS5) $(TS6) $(TS7) $(TS8) $(TS9) $(TS10) $(TS11) $(TS12) $(TS13) $(TS14) $(TS15) $(TS16) $(TS17) $(TS18)
	


