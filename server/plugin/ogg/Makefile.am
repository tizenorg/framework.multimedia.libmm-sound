lib_LTLIBRARIES = libsoundplugintremoloogg.la

libsoundplugintremoloogg_la_SOURCES = \
				 mm_sound_plugin_codec_ogg.c

libsoundplugintremoloogg_la_CFLAGS  = \
				$(MMCOMMON_CFLAGS) \
				$(TREMOLO_CFLAGS) \
				-I$(srcdir)/ \
				-I$(srcdir)/../../include \
				-I$(srcdir)/../../../include \
				$(MMLOGSVR_CFLAGS) -DMMF_LOG_OWNER=0x002 -DMMF_DEBUG_PREFIX=\"MMF-SOUND\"

libsoundplugintremoloogg_la_LIBADD = $(MMCOMMON_LIBS) \
				$(MMLOGSVR_LIBS) \
				$(TREMOLO_LIBS) \
				$(srcdir)/../../../common/libmmfsoundcommon.la

install-exec-hook:
	mkdir -p $(DESTDIR)$(libdir)/soundplugins
	ln -sf /usr/lib/libsoundplugintremoloogg.so $(DESTDIR)$(libdir)/soundplugins/libsoundplugintremoloogg.so
