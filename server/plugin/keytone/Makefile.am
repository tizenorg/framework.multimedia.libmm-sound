lib_LTLIBRARIES = libsoundpluginkeytone.la

libsoundpluginkeytone_la_SOURCES = mm_sound_plugin_run_key_tone.c

libsoundpluginkeytone_la_CFLAGS  = \
				$(MMCOMMON_CFLAGS) \
				$(VCONF_CFLAGS) \
				$(GIO_CFLAGS) \
				-I$(srcdir)/../../../include \
				-I$(srcdir)/../../include \
				$(MMLOGSVR_CFLAGS) -DMMF_LOG_OWNER=0x002 -DMMF_DEBUG_PREFIX=\"MMF-SOUND\"

libsoundpluginkeytone_la_LIBADD  = $(MMCOMMON_LIBS) \
				$(MMLOGSVR_LIBS) \
				$(VCONF_LIBS) \
				$(GIO_LIBS) \
				$(srcdir)/../../../common/libmmfsoundcommon.la

if OGG_SUPPORT
libsoundpluginkeytone_la_CFLAGS += $(TREMOLO_CFLAGS)
libsoundpluginkeytone_la_LIBADD += $(TREMOLO_LIBS)
endif

install-exec-hook:
	mkdir -p $(DESTDIR)$(libdir)/soundplugins
	ln -sf /usr/lib/libsoundpluginkeytone.so $(DESTDIR)$(libdir)/soundplugins/libsoundpluginkeytone.so

