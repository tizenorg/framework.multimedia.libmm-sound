		
lib_LTLIBRARIES = libmmfsoundcommon.la

libmmfsoundcommon_la_SOURCES = mm_ipc.c \
							mm_sound_utils.c \
							mm_source.c

#libmmfsoundcommon_la_DEPENDENCIES = $(libdir)/libmmfcommon.la

libmmfsoundcommon_la_CFLAGS = -I$(srcdir)/../include \
			$(MMCOMMON_CFLAGS) \
			$(VCONF_CFLAGS)

libmmfsoundcommon_la_LIBADD = $(MMCOMMON_LIBS) \
								$(VCONF_LIBS)
			
#libmmfsound_la_LDFLAGS = -version-info 1:0:1


#For logmanager
libmmfsoundcommon_la_CFLAGS += $(MMLOGSVR_CFLAGS) -DMMF_LOG_OWNER=0x020 -DMMF_DEBUG_PREFIX=\"MMF-SOUND\"
libmmfsoundcommon_la_LIBADD += $(MMLOGSVR_LIBS)
