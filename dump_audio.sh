#!/bin/sh
#--------------------------------------
#  audio
#--------------------------------------

AUDIO_DEBUG=$1/audio
mkdir -p ${AUDIO_DEBUG}
ps -ef | egrep "sound_server|pulseaudio" >> ${AUDIO_DEBUG}/pactl_dump
pactl stat >> ${AUDIO_DEBUG}/pactl_dump
pactl list >> ${AUDIO_DEBUG}/pactl_dump
vconftool get file/private/sound/volume/ >> ${AUDIO_DEBUG}/vconf_dump
vconftool get memory/private/sound/ >> ${AUDIO_DEBUG}/vconf_dump
vconftool get memory/private/Sound/ >> ${AUDIO_DEBUG}/vconf_dump
cat /proc/asound/cards >> ${AUDIO_DEBUG}/device_dump
ls /dev/snd/ -alZ >> ${AUDIO_DEBUG}/device_dump
ls /usr/share/alsa/ucm/* -alZ >> ${AUDIO_DEBUG}/device_dump
amixer contents >> ${AUDIO_DEBUG}/device_dump
hcitool con >> ${AUDIO_DEBUG}/device_dump
ls /tmp/ -alZ | grep ASM >> ${AUDIO_DEBUG}/tmp_dump
ls /tmp/ -alZ | grep session >> ${AUDIO_DEBUG}/tmp_dump
ipcs >> ${AUDIO_DEBUG}/ipcs_dump
rpm -qa | egrep "mm-sound|avsystem|audio|session|sound|ext0|wav-player|tone-player|alsa|sysconf|openal" >> ${AUDIO_DEBUG}/rpm_dump
openal-info >>  ${AUDIO_DEBUG}/openal_dump
