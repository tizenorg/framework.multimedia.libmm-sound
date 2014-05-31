pcfiles = mm-sound.pc mm-keysound.pc

all-local: $(pcfiles)

%.pc: %.pc
	cp $< $@

pkgconfigdir= $(libdir)/pkgconfig
pkgconfig_DATA= $(pcfiles)

CLEANFILES= $(pcfiles)

pcinfiles= mm-sound.pc.in mm-keysound.pc.in

EXTRA_DIST= $(pcinfiles)

