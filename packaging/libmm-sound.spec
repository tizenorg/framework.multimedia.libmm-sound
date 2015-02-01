Name:       libmm-sound
Summary:    MMSound Package contains client lib and sound_server binary
Version:    0.9.182
Release:    0
Group:      System/Libraries
License:    Apache-2.0
Source0:    %{name}-%{version}.tar.gz
Source1:    sound-server.service
Source2:    sound-server.path
Requires(post): /sbin/ldconfig
Requires(post): /usr/bin/vconftool
Requires(postun): /sbin/ldconfig
BuildRequires: pkgconfig(mm-common)
BuildRequires: pkgconfig(mm-log)
BuildRequires: pkgconfig(mm-session)
BuildRequires: pkgconfig(audio-session-mgr)
BuildRequires: pkgconfig(glib-2.0)
BuildRequires: pkgconfig(gio-2.0)
BuildRequires: pkgconfig(vconf)
BuildRequires: pkgconfig(libpulse)
BuildRequires: pkgconfig(iniparser)
BuildRequires: pkgconfig(capi-network-bluetooth)
%ifarch %{arm}
%endif
%if "%{?tizen_profile_name}" == "wearable"
BuildRequires: pkgconfig(bluetooth-api)
%endif
BuildRequires: pkgconfig(libtremolo)
BuildRequires: model-build-features

%description
MMSound Package contains client lib and sound_server binary for sound system


%package devel
Summary: MMSound development package
Group:      Development/Libraries
Requires:   %{name} = %{version}-%{release}

%description devel
MMSound development package for sound system

%package sdk-devel
Summary: MMSound development package
Group:      Development/Libraries
Requires:   %{name}-devel = %{version}-%{release}

%description sdk-devel
MMSound development package for sound system

%package tool
Summary: MMSound utility package - contians mm_sound_testsuite, sound_check
Group:      TO_BE/FILLED_IN
Requires:   %{name} = %{version}-%{release}

%description tool
MMSound utility package - contians mm_sound_testsuite, sound_check for sound system

%prep
%setup -q

%build
%define tizen_audio_feature_ogg_enable 1
%define tizen_audio_feature_bluetooth_enable 1

%if "%{?tizen_profile_name}" == "wearable"
%define tizen_audio_feature_hfp 1
%else if
%define tizen_audio_feature_hfp 0
%endif

%ifarch %{arm}
	CFLAGS="%{optflags} -fvisibility=hidden -DMM_DEBUG_FLAG -DEXPORT_API=\"__attribute__((visibility(\\\"default\\\")))\"" ;export CFLAGS
%else
	CFLAGS="%{optflags} -fvisibility=hidden -DMM_DEBUG_FLAG -DEXPORT_API=\"__attribute__((visibility(\\\"default\\\")))\"" ;export CFLAGS
%endif

%if "%{?tizen_profile_name}" == "wearable"
	CFLAGS+=" -DTIZEN_MICRO";export CFLAGS
%else if "%{?tizen_profile_name}" == "mobile"
%endif

%if 0%{?tizen_audio_feature_bluetooth_enable}
	CFLAGS+=" -DSUPPORT_BT_SCO";export CFLAGS
%endif

./autogen.sh
%configure \
%if 0%{?tizen_audio_feature_hfp}
       --enable-hfp \
%endif
%if 0%{?tizen_audio_feature_ogg_enable}
       --enable-ogg \
%endif
%if 0%{?tizen_audio_feature_bluetooth_enable}
       --enable-bluetooth \
%endif
%ifarch %{arm}
	--prefix=/usr --enable-pulse
%else
	--prefix=/usr --enable-pulse
%endif

make %{?_smp_mflags}

%install
rm -rf %{buildroot}
mkdir -p %{buildroot}/usr/share/license
cp LICENSE.APLv2 %{buildroot}/usr/share/license/%{name}
cp LICENSE.APLv2 %{buildroot}/usr/share/license/libmm-sound-tool
mkdir -p %{buildroot}/opt/etc/dump.d/module.d/
cp dump_audio.sh %{buildroot}/opt/etc/dump.d/module.d/dump_audio.sh

%make_install
install -d %{buildroot}%{_libdir}/systemd/system/multi-user.target.wants
install -m0644 %{SOURCE1} %{buildroot}%{_libdir}/systemd/system/
install -m0644 %{SOURCE2} %{buildroot}%{_libdir}/systemd/system/
ln -sf ../sound-server.path %{buildroot}%{_libdir}/systemd/system/multi-user.target.wants/sound-server.path

%post
/sbin/ldconfig

/usr/bin/vconftool set -t int memory/private/Sound/ASMReady 0 -g 29 -f -i -s system::vconf_multimedia
/usr/bin/vconftool set -t double file/private/sound/volume/balance 0.0 -g 29 -f -s system::vconf_multimedia
/usr/bin/vconftool set -t int file/private/sound/volume/muteall 0 -g 29 -f -s system::vconf_multimedia
/usr/bin/vconftool set -t int memory/private/Sound/VoiceControlOn 0 -g 29 -f -i -s system::vconf_multimedia
/usr/bin/vconftool set -t string memory/private/sound/booting "/usr/share/keysound/poweron.wav" -g 29 -f -i -s system::vconf_multimedia
#SPK REC EAR BTS BTA DOCK HDMI MIR USB MDOCK
#/usr/bin/vconftool set -t string file/private/sound/volume/system "09090909090909090909" -g 29 -f -s system::vconf_multimedia
#/usr/bin/vconftool set -t string file/private/sound/volume/notification "11" -g 29 -f -s system::vconf_multimedia
#/usr/bin/vconftool set -t string file/private/sound/volume/alarm "7" -g 29 -f -s system::vconf_multimedia
#/usr/bin/vconftool set -t string file/private/sound/volume/ringtone "11" -g 29 -f -s system::vconf_multimedia
#/usr/bin/vconftool set -t string file/private/sound/volume/media "07070907070707070707" -g 29 -f -s system::vconf_multimedia
#/usr/bin/vconftool set -t string file/private/sound/volume/call "04040404040404040404" -g 29 -f -s system::vconf_multimedia
#/usr/bin/vconftool set -t string file/private/sound/volume/voip "04040404040404040404" -g 29 -f -s system::vconf_multimedia
#/usr/bin/vconftool set -t string file/private/sound/volume/voice "07070707070707070707" -g 29 -f -s system::vconf_multimedia
#/usr/bin/vconftool set -t string file/private/sound/volume/fixed "00000000000000000000" -g 29 -f -s system::vconf_multimedia
/usr/bin/vconftool set -t int memory/private/sound/PrimaryVolumetypeForce -1 -g 29 -f -i -s system::vconf_multimedia
/usr/bin/vconftool set -t int memory/private/sound/PrimaryVolumetype -1 -g 29 -f -i -s system::vconf_multimedia
/usr/bin/vconftool set -t int memory/private/sound/hdmisupport 0 -g 29 -f -i -s system::vconf_multimedia
/usr/bin/vconftool set -t int memory/factory/loopback 0 -g 29 -f -i -s system::vconf_multimedia

/usr/bin/vconftool set -t int file/private/sound/volume/system 9 -g 29 -f -s system::vconf_multimedia
/usr/bin/vconftool set -t int file/private/sound/volume/notification 11 -g 29 -f -s system::vconf_multimedia
/usr/bin/vconftool set -t int file/private/sound/volume/alarm 7 -g 29 -f -s system::vconf_multimedia
/usr/bin/vconftool set -t int file/private/sound/volume/ringtone 11 -g 29 -f -s system::vconf_multimedia
/usr/bin/vconftool set -t int file/private/sound/volume/media 7 -g 29 -f -s system::vconf_multimedia
/usr/bin/vconftool set -t int file/private/sound/volume/call 4 -g 29 -f -s system::vconf_multimedia
/usr/bin/vconftool set -t int file/private/sound/volume/voip 4 -g 29 -f -s system::vconf_multimedia
/usr/bin/vconftool set -t int file/private/sound/volume/voice 7 -g 29 -f -s system::vconf_multimedia
/usr/bin/vconftool set -t int file/private/sound/volume/fixed 0 -g 29 -f -s system::vconf_multimedia


%postun -p /sbin/ldconfig


%files
%manifest libmm-sound.manifest
%defattr(-,root,root,-)
%{_bindir}/sound_server
%{_libdir}/libmmfsound.so.*
%{_libdir}/libmmfsoundcommon.so.*
%{_libdir}/libmmfkeysound.so.*
%{_libdir}/libmmfbootsound.so.*
%{_libdir}/libsoundplugintone.so*
%{_libdir}/libsoundpluginwave.so*
%{_libdir}/libsoundpluginkeytone.so*
%if 0%{?tizen_audio_feature_ogg_enable}
%{_libdir}/libsoundplugintremoloogg.so*
%endif
%{_libdir}/soundplugins/libsoundplugintone.so
%{_libdir}/soundplugins/libsoundpluginwave.so
%{_libdir}/soundplugins/libsoundpluginkeytone.so
%if 0%{?tizen_audio_feature_ogg_enable}
%{_libdir}/soundplugins/libsoundplugintremoloogg.so
%endif
%{_libdir}/systemd/system/multi-user.target.wants/sound-server.path
%{_libdir}/systemd/system/sound-server.service
%{_libdir}/systemd/system/sound-server.path
/usr/share/sounds/sound-server/*
%{_datadir}/license/%{name}
%{_datadir}/license/libmm-sound-tool
/usr/share/sounds/sound-server/*
/opt/etc/dump.d/module.d/dump_audio.sh

%files devel
%defattr(-,root,root,-)
%{_libdir}/libmmfkeysound.so
%{_libdir}/libmmfbootsound.so
%{_libdir}/libmmfsound.so
%{_libdir}/libmmfsoundcommon.so
%{_includedir}/mmf/mm_sound_private.h
%exclude %{_includedir}/mmf/mm_sound_pa_client.h

%files sdk-devel
%defattr(-,root,root,-)
%{_includedir}/mmf/mm_sound.h
%{_includedir}/mmf/mm_sound_pcm_async.h
%exclude %{_includedir}/mmf/mm_sound_pa_client.h
%{_libdir}/pkgconfig/mm-keysound.pc
%{_libdir}/pkgconfig/mm-bootsound.pc
%{_libdir}/pkgconfig/mm-sound.pc

%files tool
%manifest libmm-sound-tool.manifest
%defattr(-,root,root,-)
%{_bindir}/mm_sound_testsuite
