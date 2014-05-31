SUBDIRS = plugin

bin_PROGRAMS = sound_server
sound_server_SOURCES = mm_sound_mgr_codec.c \
						mm_sound_mgr_ipc.c \
						mm_sound_mgr_pulse.c \
						mm_sound_mgr_asm.c \
						mm_sound_mgr_device.c \
						mm_sound_mgr_headset.c \
						mm_sound_mgr_dock.c \
						mm_sound_mgr_wfd.c \
						mm_sound_mgr_session.c \
						mm_sound_mgr_run.c \
						mm_sound_plugin.c \
						mm_sound_server.c \
						mm_sound_thread_pool.c

sound_server_CFLAGS = -I$(srcdir)/../include \
					  -I$(srcdir)/include \
					  $(MMCOMMON_CFLAGS) \
					  $(MMLOGSVR_CFLAGS) \
					  -DMMF_LOG_OWNER=0x002 \
					  -DMMF_DEBUG_PREFIX=\"MMF-SOUND\" \
					  -D__DEBUG_MODE__ -DUSE_VCONF -D__USE_LOGMANAGER__ \
					  $(MMSESSION_CFLAGS) \
					  $(AVSYSTEM_CFLAGS) \
			  		  $(AUDIOSESSIONMGR_CFLAGS) \
			  		  $(VCONF_CFLAGS) \
			  		  $(GLIB2_CFLAGS) \
			  		  $(GIO_CFLAGS) \
			  		  $(HEYNOTI_CFLAGS) \
			  		  $(BLUETOOTH_API_CFLAGS)

sound_server_LDADD = $(MMLOGSVR_LIBS) \
		     -ldl \
		     $(MMCOMMON_LIBS) \
		     $(MMSESSION_LIBS) \
		     $(AVSYSTEM_LIBS) \
		     $(AUDIOSESSIONMGR_LIBS) \
		     $(GLIB2_LIBS) \
		     $(GIO_LIBS) \
		     $(VCONF_LIBS) \
		     $(HEYNOTI_LIBS) \
		     $(BLUETOOTH_API_LIBS) \
		     $(srcdir)/../libmmfsound.la \
		     $(srcdir)/../common/libmmfsoundcommon.la 


if SUPPORT_BT_SCO
sound_server_CFLAGS += $(BLUETOOTH_CFLAGS)
sound_server_LDADD += $(BLUETOOTH_LIBS)
endif

if USE_PULSE
sound_server_CFLAGS += -DPULSE_CLIENT $(PULSE_CFLAGS)
sound_server_LDADD += $(PULSE_LIBS)
endif

if USE_SECURITY
sound_server_CFLAGS += $(SECURITY_CFLAGS) -DUSE_SECURITY
sound_server_LDADD += $(SECURITY_LIBS)
endif

sound_server_DEPENDENCIES = $(srcdir)/../common/libmmfsoundcommon.la
