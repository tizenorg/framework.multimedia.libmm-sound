Name:       libmm-sound
Summary:    MMSound Package contains client lib and sound_server binary
Version:    0.7.4
Release:    0
Group:      System/Libraries
License:    Apache-2.0
Source0:    %{name}-%{version}.tar.gz
Source1:    sound-server.service
Source2:    sound-server.path
Requires(pre): /bin/pidof
Requires(post): /sbin/ldconfig
Requires(post): /usr/bin/vconftool
Requires(postun): /sbin/ldconfig
BuildRequires: pkgconfig(mm-common)
BuildRequires: pkgconfig(avsystem)
BuildRequires: pkgconfig(mm-log)
BuildRequires: pkgconfig(mm-session)
BuildRequires: pkgconfig(audio-session-mgr)
BuildRequires: pkgconfig(sysman)
BuildRequires: pkgconfig(glib-2.0)
BuildRequires: pkgconfig(vconf)
BuildRequires: pkgconfig(heynoti)
BuildRequires:  pkgconfig(security-server)

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
./autogen.sh
%ifarch %{arm}
CFLAGS="%{optflags} -fvisibility=hidden -DMM_DEBUG_FLAG -DEXPORT_API=\"__attribute__((visibility(\\\"default\\\")))\""; export CFLAGS
%else
%if 0%{?simulator}
CFLAGS="%{optflags} -fvisibility=hidden -DMM_DEBUG_FLAG -DEXPORT_API=\"__attribute__((visibility(\\\"default\\\")))\""; export CFLAGS
%else
CFLAGS="%{optflags} -fvisibility=hidden -DMM_DEBUG_FLAG -DSEPARATE_SPEAKER_AND_WIRED_ACCESSORY -DEXPORT_API=\"__attribute__((visibility(\\\"default\\\")))\""; export CFLAGS
%endif
%endif
%configure --prefix=/usr --enable-pulse --enable-security
make %{?_smp_mflags}

%install
%make_install
install -d %{buildroot}%{_libdir}/systemd/system/multi-user.target.wants
install -m0644 %{SOURCE1} %{buildroot}%{_libdir}/systemd/system/
install -m0644 %{SOURCE2} %{buildroot}%{_libdir}/systemd/system/
ln -sf ../sound-server.path %{buildroot}%{_libdir}/systemd/system/multi-user.target.wants/sound-server.path

# FIXME: remove after systemd is in
mkdir -p %{buildroot}%{_sysconfdir}/rc.d/rc3.d
mkdir -p %{buildroot}%{_sysconfdir}/rc.d/rc4.d
mkdir -p %{buildroot}%{_sysconfdir}/rc.d/rc5.d
ln -s %{_sysconfdir}/rc.d/init.d/soundserver %{buildroot}%{_sysconfdir}/rc.d/rc3.d/S23soundserver
ln -s %{_sysconfdir}/rc.d/init.d/soundserver %{buildroot}%{_sysconfdir}/rc.d/rc4.d/S23soundserver



%post
/sbin/ldconfig

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

%postun -p /sbin/ldconfig


%files
%manifest libmm-sound.manifest
%defattr(-,root,root,-)
%{_bindir}/sound_server
%{_libdir}/libmmfsound.so.*
%{_libdir}/libmmfsoundcommon.so.*
%{_libdir}/libmmfkeysound.so.*
%{_libdir}/libsoundplugintone.so*
%{_libdir}/libsoundpluginwave.so*
%{_libdir}/libsoundpluginkeytone.so*
%{_libdir}/soundplugins/libsoundplugintone.so
%{_libdir}/soundplugins/libsoundpluginwave.so
%{_libdir}/soundplugins/libsoundpluginkeytone.so
%attr(0755,root,root) %{_sysconfdir}/rc.d/init.d/soundserver
%{_sysconfdir}/rc.d/rc3.d/S23soundserver
%{_sysconfdir}/rc.d/rc4.d/S23soundserver
/usr/share/sounds/sound-server/*
%{_libdir}/systemd/system/multi-user.target.wants/sound-server.path
%{_libdir}/systemd/system/sound-server.service
%{_libdir}/systemd/system/sound-server.path

%files devel
%defattr(-,root,root,-)
%{_libdir}/libmmfkeysound.so
%{_libdir}/libmmfsound.so
%{_libdir}/libmmfsoundcommon.so
%{_includedir}/mmf/mm_sound_private.h
%{_includedir}/mmf/mm_sound_plugin.h
%{_includedir}/mmf/mm_sound_plugin_hal.h


%files sdk-devel
%defattr(-,root,root,-)
%{_includedir}/mmf/mm_sound.h
%{_libdir}/pkgconfig/mm-keysound.pc
%{_libdir}/pkgconfig/mm-sound.pc

%files tool
%defattr(-,root,root,-)
%{_bindir}/mm_sound_testsuite
