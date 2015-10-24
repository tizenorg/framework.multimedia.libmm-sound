pcfiles = mm-sound.pc mm-keysound.pc mm-bootsound.pc

all-local: $(pcfiles)

.pc: .pc
	cp $< $@

pkgconfigdir= $(libdir)/pkgconfig
pkgconfig_DATA= $(pcfiles)

CLEANFILES= $(pcfiles)

pcinfiles= mm-sound.pc.in mm-keysound.pc.in mm-bootsound.pc.in

EXTRA_DIST= $(pcinfiles)

