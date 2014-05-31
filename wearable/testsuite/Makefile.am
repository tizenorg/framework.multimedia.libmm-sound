bin_PROGRAMS = mm_sound_testsuite

mm_sound_testsuite_SOURCES = mm_sound_testsuite_simple.c

mm_sound_testsuite_CFLAGS = $(MMCOMMON_CFLAGS) \
				-I$(srcdir)/../include \
				$(AVSYSTEM_CFLAGS) \
				$(ALSA_CFLAGS) \
				$(VCONF_CFLAGS) \
				$(AUDIOSESSIONMGR_CFLAGS) \
				$(MMSESSION_CFLAGS)

mm_sound_testsuite_DEPENDENCIES = $(srcdir)/../.libs/libmmfsound.la \
								$(srcdir)/../.libs/libmmfkeysound.la 

mm_sound_testsuite_LDADD = $(MMCOMMON_LIBS) \
				$(MMSESSION_LIBS) \
				$(AVSYSTEM_LIBS) \
				$(VCONF_LIBS) \
				$(AUDIOSESSIONMGR_LIBS) \
				$(srcdir)/../.libs/libmmfsound.la \
				$(srcdir)/../.libs/libmmfkeysound.la \
				$(srcdir)/../common/.libs/libmmfsoundcommon.la \
				-lpthread
