lib_LTLIBRARIES = libsoundplugintone.la

libsoundplugintone_la_SOURCES = mm_sound_plugin_codec_tone.c

libsoundplugintone_la_CFLAGS  = \
				$(MMCOMMON_CFLAGS) \
				-I$(srcdir)/../../../../common/include \
				-I$(srcdir)/../../../include \
				$(MMLOGSVR_CFLAGS) -DMMF_LOG_OWNER=0x020 -DMMF_DEBUG_PREFIX=\"MMF-SOUND\"

libsoundplugintone_la_LIBADD  = \
				$(MMCOMMON_LIBS) \
				$(MMLOGSVR_LIBS)

libsoundplugintone_la_CFLAGS += $(AVSYSTEM_CFLAGS)
libsoundplugintone_la_LIBADD += $(AVSYSTEM_LIBS)

install-exec-hook:
	mkdir -p $(DESTDIR)$(libdir)/soundplugins
	ln -sf /usr/lib/libsoundplugintone.so $(DESTDIR)$(libdir)/soundplugins/libsoundplugintone.so

