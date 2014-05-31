Name:       libmm-sound
Summary:    MMSound Package contains client lib and sound_server binary
%if 0%{?tizen_profile_mobile}
Version:    0.7.4
Release:    0
%else
Version:    0.10.35
Release:    0
VCS:        magnolia/framework/multimedia/libmm-sound#libmm-sound-0.8.49_0-114-gdda07391aaae65156acdc15e082daaedde5f2f42
%endif
Group:      System/Libraries
License:    Apache-2.0
Source0:    %{name}-%{version}.tar.gz
%if "%{_repository}" == "mobile"
Source1:    sound-server.service.mobile
Source2:    sound-server.path
Requires(pre): /bin/pidof
%else
Source1:    sound-server.service.wearable
%endif
Requires(post): /sbin/ldconfig
Requires(post): /usr/bin/vconftool
Requires(postun): /sbin/ldconfig
%if "%{_repository}" == "wearable"
BuildRequires: pkgconfig(gio-2.0)
BuildRequires: pkgconfig(security-server)
BuildRequires: pkgconfig(iniparser)
BuildRequires: pkgconfig(capi-network-bluetooth)
BuildRequires: pkgconfig(bluetooth-api)
BuildRequires: pkgconfig(libtremolo)
%endif
%if "%{_repository}" == "mobile"
BuildRequires: pkgconfig(sysman)
BuildRequires: pkgconfig(heynoti)
BuildRequires:  pkgconfig(security-server)
BuildRequires:  pkgconfig(libsystemd-daemon)
%{?systemd_requires}
%endif
BuildRequires: pkgconfig(mm-common)
BuildRequires: pkgconfig(avsystem)
BuildRequires: pkgconfig(mm-log)
BuildRequires: pkgconfig(mm-session)
BuildRequires: pkgconfig(audio-session-mgr)
BuildRequires: pkgconfig(glib-2.0)
BuildRequires: pkgconfig(vconf)


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
%if 0%{?tizen_profile_wearable}
cd wearable
%else
cd mobile
%endif

%if 0%{?tizen_profile_wearable}

%ifarch %{arm}
	CFLAGS="%{optflags} -fvisibility=hidden -DMM_DEBUG_FLAG -DASM_FOR_PRODUCT -DEXPORT_API=\"__attribute__((visibility(\\\"default\\\")))\"" ;export CFLAGS
%else
	CFLAGS="%{optflags} -fvisibility=hidden -DMM_DEBUG_FLAG -DASM_FOR_PRODUCT -DEXPORT_API=\"__attribute__((visibility(\\\"default\\\")))\"" ;export CFLAGS
%endif
	CFLAGS+=" -DOGG_SUPPORT";export CFLAGS
	CFLAGS+=" -DSUPPORT_BT_SCO";export CFLAGS
    CFLAGS+=" -DTIZEN_MICRO";export CFLAGS

%else

%ifarch %{arm}
CFLAGS="%{optflags} -fvisibility=hidden -DMM_DEBUG_FLAG -DEXPORT_API=\"__attribute__((visibility(\\\"default\\\")))\""; export CFLAGS
%else
%if 0%{?simulator}
CFLAGS="%{optflags} -fvisibility=hidden -DMM_DEBUG_FLAG -DEXPORT_API=\"__attribute__((visibility(\\\"default\\\")))\""; export CFLAGS
%else
CFLAGS="%{optflags} -fvisibility=hidden -DMM_DEBUG_FLAG -DEXPORT_API=\"__attribute__((visibility(\\\"default\\\")))\""; export CFLAGS
%endif
%endif

%endif

./autogen.sh

%if 0%{?tizen_profile_wearable}
%configure \
       --enable-ogg \
       --enable-bluetooth \
%ifarch %{arm}
	--prefix=/usr --enable-pulse --disable-security
%else
	--prefix=/usr --enable-pulse --disable-security
%endif
%else 
%configure --prefix=/usr --enable-pulse --enable-security
%endif
make %{?_smp_mflags}

%install
rm -rf %{buildroot}

%if 0%{?tizen_profile_wearable}
cd wearable
mkdir -p %{buildroot}/usr/share/license
mkdir -p %{buildroot}/opt/usr/devel/usr/bin
cp LICENSE.APLv2 %{buildroot}/usr/share/license/%{name}
cp LICENSE.APLv2 %{buildroot}/usr/share/license/libmm-sound-tool
%else
cd mobile
%endif


%make_install
%if 0%{?tizen_profile_wearable}
install -d %{buildroot}%{_libdir}/systemd/system/sound.target.wants
install -m0644 %{SOURCE1} %{buildroot}%{_libdir}/systemd/system/
mv %{buildroot}%{_libdir}/systemd/system/sound-server.service.wearable %{buildroot}%{_libdir}/systemd/system/sound-server.service
ln -sf ../sound-server.service %{buildroot}%{_libdir}/systemd/system/sound.target.wants/sound-server.service
mv %{buildroot}/usr/bin/mm_sound_testsuite %{buildroot}/opt/usr/devel/usr/bin
%else
install -d %{buildroot}%{_libdir}/systemd/system/multi-user.target.wants
install -m0644 %{SOURCE1} %{buildroot}%{_libdir}/systemd/system/
mv %{buildroot}%{_libdir}/systemd/system/sound-server.service.mobile %{buildroot}%{_libdir}/systemd/system/sound-server.service
install -m0644 %{SOURCE2} %{buildroot}%{_libdir}/systemd/system/
ln -sf ../sound-server.path %{buildroot}%{_libdir}/systemd/system/multi-user.target.wants/sound-server.path

# FIXME: remove after systemd is in
mkdir -p %{buildroot}%{_sysconfdir}/rc.d/rc3.d
mkdir -p %{buildroot}%{_sysconfdir}/rc.d/rc4.d
mkdir -p %{buildroot}%{_sysconfdir}/rc.d/rc5.d
ln -s %{_sysconfdir}/rc.d/init.d/soundserver %{buildroot}%{_sysconfdir}/rc.d/rc3.d/S23soundserver
ln -s %{_sysconfdir}/rc.d/init.d/soundserver %{buildroot}%{_sysconfdir}/rc.d/rc4.d/S23soundserver

mkdir -p %{buildroot}/usr/share/license
cp LICENSE.APLv2 %{buildroot}/usr/share/license/%{name}
cp LICENSE.APLv2 %{buildroot}/usr/share/license/%{name}-devel
cp LICENSE.APLv2 %{buildroot}/usr/share/license/%{name}-sdk-devel
cp LICENSE.APLv2 %{buildroot}/usr/share/license/%{name}-tool

%endif


%post
/sbin/ldconfig
%if 0%{?tizen_profile_wearable}
/usr/bin/vconftool set -t int memory/private/Sound/ASMReady 0 -g 29 -f -i -s system::vconf_multimedia
/usr/bin/vconftool set -t double file/private/sound/volume/balance 0.0 -g 29 -f -s system::vconf_multimedia
/usr/bin/vconftool set -t int file/private/sound/volume/muteall 0 -g 29 -f -s system::vconf_multimedia
/usr/bin/vconftool set -t int memory/Sound/SoundVolAccumTime 0 -g 29 -f -i -s system::vconf_multimedia
/usr/bin/vconftool set -t int memory/Sound/SoundEnableSafetyVol 0 -g 29 -f -i -s system::vconf_multimedia
/usr/bin/vconftool set -t string memory/private/sound/booting "/usr/share/keysound/poweron.wav" -g 29 -f -i -s system::vconf_multimedia
/usr/bin/vconftool set -t int memory/private/sound/hdmisupport 0 -g 29 -f -i -s system::vconf_multimedia
/usr/bin/vconftool set -t int file/private/sound/volume/system 9 -g 29 -f -s system::vconf_multimedia
/usr/bin/vconftool set -t int file/private/sound/volume/notification 11 -g 29 -f -s system::vconf_multimedia
/usr/bin/vconftool set -t int file/private/sound/volume/alarm 7 -g 29 -f -s system::vconf_multimedia
/usr/bin/vconftool set -t int file/private/sound/volume/ringtone 11 -g 29 -f -s system::vconf_multimedia
/usr/bin/vconftool set -t int file/private/sound/volume/media 7 -g 29 -f -s system::vconf_multimedia
/usr/bin/vconftool set -t int file/private/sound/volume/call 4 -g 29 -f -s system::vconf_multimedia
/usr/bin/vconftool set -t int file/private/sound/volume/voip 6 -g 29 -f -s system::vconf_multimedia
/usr/bin/vconftool set -t int file/private/sound/volume/svoice 4 -g 29 -f -s system::vconf_multimedia
/usr/bin/vconftool set -t int file/private/sound/volume/fixed 0 -g 29 -f -s system::vconf_multimedia
/usr/bin/vconftool set -t int file/private/sound/volume/java 11 -g 29 -f -s system::vconf_multimedia
%else
/usr/bin/vconftool set -t int memory/Sound/ASMReady 0 -g 29 -f -i

/usr/bin/vconftool set -t int file/private/sound/volume/system 5 -g 29 -f
/usr/bin/vconftool set -t int file/private/sound/volume/notification 7 -g 29 -f
/usr/bin/vconftool set -t int file/private/sound/volume/alarm 7 -g 29 -f
/usr/bin/vconftool set -t int file/private/sound/volume/ringtone 13 -g 29 -f
/usr/bin/vconftool set -t int file/private/sound/volume/media 7 -g 29 -f
/usr/bin/vconftool set -t int file/private/sound/volume/call 7 -g 29 -f
/usr/bin/vconftool set -t int file/private/sound/volume/voip 7 -g 29 -f
/usr/bin/vconftool set -t int file/private/sound/volume/fixed 0 -g 29 -f
/usr/bin/vconftool set -t int file/private/sound/volume/java 11 -g 29 -f
%endif

%postun -p /sbin/ldconfig


%files
%if 0%{?tizen_profile_wearable}
%manifest wearable/libmm-sound.manifest
%else
%manifest mobile/libmm-sound.manifest
%endif
%defattr(-,root,root,-)
%{_bindir}/sound_server
%{_libdir}/libmmfsound.so.*
%{_libdir}/libmmfsoundcommon.so.*
%{_libdir}/libmmfkeysound.so.*
%if 0%{?tizen_profile_wearable}
%{_libdir}/libmmfbootsound.so.*
%{_libdir}/libmmfpcmsound.so.*
%endif
%{_libdir}/libsoundplugintone.so*
%{_libdir}/libsoundpluginwave.so*
%{_libdir}/libsoundpluginkeytone.so*
%if 0%{?tizen_profile_wearable}
%{_libdir}/libsoundplugintremoloogg.so*
%{_libdir}/soundplugins/libsoundplugintremoloogg.so
%endif
%{_libdir}/soundplugins/libsoundplugintone.so
%{_libdir}/soundplugins/libsoundpluginwave.so
%{_libdir}/soundplugins/libsoundpluginkeytone.so
%{_libdir}/systemd/system/sound-server.service
%if 0%{?tizen_profile_wearable}
%{_libdir}/systemd/system/sound.target.wants/sound-server.service
%{_datadir}/license/%{name}
%{_datadir}/license/libmm-sound-tool
%else
%attr(0755,root,root) %{_sysconfdir}/rc.d/init.d/soundserver
%{_sysconfdir}/rc.d/rc3.d/S23soundserver
%{_sysconfdir}/rc.d/rc4.d/S23soundserver
/usr/share/sounds/sound-server/*
%{_libdir}/systemd/system/multi-user.target.wants/sound-server.path
%{_libdir}/systemd/system/sound-server.path
/usr/share/license/%{name}
%endif

%files devel
%defattr(-,root,root,-)
%{_libdir}/libmmfkeysound.so
%if 0%{?tizen_profile_wearable}
%{_libdir}/libmmfbootsound.so
%{_libdir}/libmmfpcmsound.so
%endif
%{_libdir}/libmmfsound.so
%{_libdir}/libmmfsoundcommon.so
%{_includedir}/mmf/mm_sound_private.h
%if 0%{?tizen_profile_mobile}
%{_includedir}/mmf/mm_sound_plugin.h
%{_includedir}/mmf/mm_sound_plugin_hal.h
/usr/share/license/%{name}-devel
%endif


%files sdk-devel
%defattr(-,root,root,-)
%{_includedir}/mmf/mm_sound.h
%{_libdir}/pkgconfig/mm-keysound.pc
%{_libdir}/pkgconfig/mm-sound.pc
%if 0%{?tizen_profile_wearable}
%{_libdir}/pkgconfig/mm-bootsound.pc
%{_libdir}/pkgconfig/mm-pcmsound.pc
%else
/usr/share/license/%{name}-sdk-devel
%endif


%files tool
%if 0%{?tizen_profile_wearable}
%manifest wearable/libmm-sound-tool.manifest
%endif
%defattr(-,root,root,-)
%if 0%{?tizen_profile_wearable}
/opt/usr/devel/usr/bin/mm_sound_testsuite
%else
%{_bindir}/mm_sound_testsuite
/usr/share/license/%{name}-tool
%endif
