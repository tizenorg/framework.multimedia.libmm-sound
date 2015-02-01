SUBDIRS = common \
		pkgconfig \
		. \
		server \
		testsuite

lib_LTLIBRARIES = libmmfsound.la

includelibmmfsounddir = $(includedir)/mmf
includelibmmfsound_HEADERS = include/mm_sound.h \
							include/mm_sound_private.h \
							include/mm_sound_pa_client.h \
							include/mm_sound_pcm_async.h

libmmfsound_la_SOURCES = mm_sound.c \
			mm_sound_client.c \
			mm_sound_device.c \
			mm_sound_pcm.c \
			mm_sound_pa_client.c \
			mm_sound_pcm_async.c

libmmfsound_la_DEPENDENCIES = common/libmmfsoundcommon.la
								

libmmfsound_la_CFLAGS = -I$(srcdir)/include \
			$(MMCOMMON_CFLAGS) \
			$(VCONF_CFLAGS) \
			-I$(srcdir)/servertype/include \
			$(GLIB2_CFLAGS) \
			$(MMSESSION_CFLAGS) \
			$(AUDIOSESSIONMGR_CFLAGS) \
			-D_USE_INTERNAL_SOUND_PATH_ \
			$(PASIMPLE_CFLAGS) \
			$(PA_CFLAGS) \
			$(VCONF_CFLAGS)

if USE_PULSE
libmmfsound_la_CFLAGS += -DPULSE_CLIENT
endif

libmmfsound_la_LIBADD = $(MMCOMMON_LIBS) \
			$(MMSESSION_LIBS) \
			$(AUDIOSESSIONMGR_LIBS) \
			$(GLIB2_LIBS) \
			$(srcdir)/common/libmmfsoundcommon.la \
			$(PASIMPLE_LIBS) \
			$(PA_LIBS) \
			$(VCONF_LIBS)


libmmfsound_la_LDFLAGS = -version-info 1:0:1

#For logmanager
libmmfsound_la_CFLAGS += $(MMLOGSVR_CFLAGS) -DMMF_LOG_OWNER=0x020 -DMMF_DEBUG_PREFIX=\"MMF-SOUND\"
libmmfsound_la_LIBADD += $(MMLOGSVR_LIBS)







lib_LTLIBRARIES += libmmfkeysound.la

includelibmmfkeysounddir = $(includedir)/mmf

libmmfkeysound_la_SOURCES = mm_sound_keysound.c

libmmfkeysound_la_CFLAGS = -I$(srcdir)/include \
			$(MMCOMMON_CFLAGS) \
			$(VCONF_CFLAGS) \
			-D_USE_INTERNAL_SOUND_PATH_

libmmfkeysound_la_LIBADD = $(MMCOMMON_LIBS) $(VCONF_LIBS)
libmmfkeysound_la_CFLAGS += $(MMLOGSVR_CFLAGS) -DMMF_LOG_OWNER=0x020 -DMMF_DEBUG_PREFIX=\"MMF-SOUND\"
libmmfkeysound_la_LIBADD += $(MMLOGSVR_LIBS)

lib_LTLIBRARIES += libmmfbootsound.la

includelibmmfbootsounddir = $(includedir)/mmf

libmmfbootsound_la_SOURCES = mm_sound_bootsound.c
libmmfbootsound_la_CFLAGS = -I$(srcdir)/include \
			$(MMCOMMON_CFLAGS) \
			$(VCONF_CFLAGS) \
			-D_USE_INTERNAL_SOUND_PATH_

libmmfbootsound_la_LIBADD = $(MMCOMMON_LIBS) $(VCONF_LIBS)
libmmfbootsound_la_CFLAGS += $(MMLOGSVR_CFLAGS) -DMMF_LOG_OWNER=0x020 -DMMF_DEBUG_PREFIX=\"MMF-SOUND\"
libmmfbootsound_la_LIBADD += $(MMLOGSVR_LIBS)
