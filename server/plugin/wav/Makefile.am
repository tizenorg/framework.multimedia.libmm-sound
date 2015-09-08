lib_LTLIBRARIES = libsoundpluginwave.la

libsoundpluginwave_la_SOURCES = mm_sound_plugin_codec_wave.c 

libsoundpluginwave_la_CFLAGS  = \
				$(MMCOMMON_CFLAGS) \
      			       -I$(srcdir)/../../../include \
      			       -I$(srcdir)/../../include \
				$(MMLOGSVR_CFLAGS) -DMMF_LOG_OWNER=0x002 -DMMF_DEBUG_PREFIX=\"MMF-SOUND\"

libsoundpluginwave_la_LIBADD  = $(MMCOMMON_LIBS) \
				$(MMLOGSVR_LIBS) \
				$(srcdir)/../../../common/libmmfsoundcommon.la 

install-exec-hook:
	mkdir -p $(DESTDIR)$(libdir)/soundplugins
	ln -sf /usr/lib/libsoundpluginwave.so $(DESTDIR)$(libdir)/soundplugins/libsoundpluginwave.so

