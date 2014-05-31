lib_LTLIBRARIES = libsoundpluginkeytone.la

#libsoundpluginkeytone_la_SOURCES = mm_sound_plugin_run_keytone.c
#libsoundpluginkeytone_la_SOURCES = mm_sound_plugin_run_keytone_alsa.c
libsoundpluginkeytone_la_SOURCES = mm_sound_plugin_run_key_tone.c 

libsoundpluginkeytone_la_CFLAGS  = \
				$(MMCOMMON_CFLAGS) \
				-I$(srcdir)/../../../include \
				-I$(srcdir)/../../include \
				$(MMLOGSVR_CFLAGS) -DMMF_LOG_OWNER=0x020 -DMMF_DEBUG_PREFIX=\"MMF-SOUND\"

libsoundpluginkeytone_la_LIBADD  = $(MMCOMMON_LIBS) \
				$(MMLOGSVR_LIBS) \
				$(srcdir)/../../../common/libmmfsoundcommon.la 

libsoundpluginkeytone_la_CFLAGS += $(AVSYSTEM_CFLAGS)
libsoundpluginkeytone_la_LIBADD += $(AVSYSTEM_LIBS)

install-exec-hook:
	mkdir -p $(DESTDIR)$(libdir)/soundplugins
	ln -sf /usr/lib/libsoundpluginkeytone.so $(DESTDIR)$(libdir)/soundplugins/libsoundpluginkeytone.so

