%bcond_with pulseaudio_pmapi
%bcond_with pulseaudio_dlog
%bcond_with pulseaudio_bt_profile_set
%bcond_with pulseaudio_udev_with_usb_only
%bcond_with pulseaudio_with_bluez5
%bcond_with pulseaudio_samsung_policy
%bcond_with x

Name:             pulseaudio
Summary:          Improved Linux sound server
Version:          5.0
Release:          0
Group:            Multimedia/Audio
License:          GPL-2.0+ and LGPL-2.1+
URL:              http://pulseaudio.org
Source0:          http://www.freedesktop.org/software/pulseaudio/releases/%{name}-%{version}.tar.gz
Source99:         baselibs.conf
Source1001:       pulseaudio.manifest
BuildRequires:    libtool-ltdl-devel
BuildRequires:    libtool
BuildRequires:    intltool
BuildRequires:    fdupes
BuildRequires:    pkgconfig(speexdsp)
BuildRequires:    pkgconfig(sndfile)
BuildRequires:    pkgconfig(alsa)
BuildRequires:    pkgconfig(glib-2.0)
BuildRequires:    pkgconfig(gconf-2.0)
BuildRequires:    pkgconfig(bluez)
BuildRequires:    pkgconfig(sbc)
BuildRequires:    pkgconfig(dbus-1)
%if %{with x}
BuildRequires:    pkgconfig(xi)
%endif
BuildRequires:    pkgconfig(libudev)
BuildRequires:    pkgconfig(openssl)
BuildRequires:    pkgconfig(json)
BuildRequires:    pkgconfig(tdb)
BuildRequires:    pkgconfig(vconf)
BuildRequires:    systemd-devel
BuildRequires:    libcap-devel
%if %{with pulseaudio_dlog}
BuildRequires:    pkgconfig(dlog)
%endif
Requires:         udev
Requires(post):   /sbin/ldconfig
Requires(postun): /sbin/ldconfig

%description
PulseAudio is a sound server for Linux and other Unix like operating
systems. It is intended to be an improved drop-in replacement for the
Enlightened Sound Daemon (ESOUND).

%package -n libpulse
Summary:    PulseAudio client libraries
Group:      Multimedia/Audio

%description -n libpulse
Client libraries used by applications that access a PulseAudio sound server
via PulseAudio's native interface.

%package -n libpulse-mainloop-glib
Summary:        GLIB  2
Group:          Multimedia/Audio

%description -n libpulse-mainloop-glib
pulseaudio is a networked sound server for Linux and other Unix like
operating systems and Microsoft Windows. It is intended to be an
improved drop-in replacement for the Enlightened Sound Daemon (ESOUND).

This package contains the GLIB Main Loop bindings for the PulseAudio
sound server.

%package -n libpulse-devel
Summary:    PulseAudio client development headers and libraries
Group:      Multimedia/Development
Requires:   libpulse = %{version}
Requires:   libpulse-mainloop-glib = %{version}

%description -n libpulse-devel
Headers and libraries for developing applications that access a PulseAudio
sound server via PulseAudio's native interface

%package utils
Summary:    Command line tools for the PulseAudio sound server
Group:      Multimedia/Audio
Requires:   %{name} = %{version}-%{release}

%description utils
These tools provide command line access to various features of the
PulseAudio sound server. Included tools are:
pabrowse - Browse available PulseAudio servers on the local network.
paplay - Playback a WAV file via a PulseAudio sink.
pacat - Cat raw audio data to a PulseAudio sink.
parec - Cat raw audio data from a PulseAudio source.
pacmd - Connect to PulseAudio's built-in command line control interface.
pactl - Send a control command to a PulseAudio server.
padsp - /dev/dsp wrapper to transparently support OSS applications.
pax11publish - Store/retrieve PulseAudio default server/sink/source
settings in the X11 root window.

%package module-bluetooth
Summary:    Bluetooth module for PulseAudio sound server
Group:      Multimedia/Audio
Requires:   %{name} = %{version}-%{release}

%description module-bluetooth
This module enables PulseAudio to work with bluetooth devices, like headset
or audio gateway

%package module-devel
Summary:    Headers and libraries for PulseAudio module development
License:    LGPL-2.0+
Group:      Multimedia/Development
Requires:   libpulse-devel = %{version}

%description module-devel
Headers and libraries for developing pulseaudio modules outside
the source tree.

%package config
Summary: Default configuration for PulseAudio.
Group: System Environment/Configuration

%description config
Default configuration for PulseAudio.

%package module-raop
Summary: PA module-raop
Group:   Multimedia/Audio

%description module-raop
PA module-raop.

%package module-filter
Summary: PA module-filter
Group:   Multimedia/Audio

%description module-filter
PA module-filter.

%package module-combine-sink
Summary: PA module-combine-sink
Group:   Multimedia/Audio

%description module-combine-sink
PA module-combine-sink.

%package module-augment-properties
Summary: PA module-augment-properties
Group:   Multimedia/Audio

%description module-augment-properties
PA module-augment-properties.

%package module-dbus-protocol
Summary: PA module-dbus-protocol
Group:   Multimedia/Audio

%description module-dbus-protocol
PA module-dbus-protocol.

%package module-null-source
Summary: PA module-null-source
Group:   Multimedia/Audio

%description module-null-source
PA module-null-source.

%package module-switch-on-connect
Summary: PA module-swich-on-connect
Group:   Multimedia/Audio

%description module-switch-on-connect
PA module-swich-on-connect.

%package vala-bindings
Summary:    PA Vala bindings
Group:      Multimedia/Audio
Requires:   %{name} = %{version}-%{release}

%description vala-bindings
PA Vala bindings.

%prep
%setup -q -T -b0
echo "%{version}" > .tarball-version
cp %{SOURCE1001} .

%build
export CFLAGS="%{optflags} -fno-strict-aliasing"
export LD_AS_NEEDED=0
./bootstrap.sh
%configure --disable-static \
        --enable-alsa \
        --disable-ipv6 \
        --disable-oss-output \
        --disable-oss-wrapper \
        --disable-x11 \
        --disable-hal-compat \
        --disable-lirc \
        --disable-avahi \
        --disable-jack \
        --disable-xen \
        --without-fftw \
        --enable-bluez5 \
        --disable-bluez4 \
        --with-bluetooth-headset-backend=ofono \
        --enable-systemd \
        --with-database=tdb \
%if %{with pulseaudio_dlog}
        --enable-dlog \
%endif
%if %{with pulseaudio_pmapi}
        --enable-pmlock \
%endif
%if %{with pulseaudio_bt_profile_set}
        --enable-bt-profile-set \

%endif
%if %{with pulseaudio_udev_with_usb_only}
        --enable-udev-with-usb-only \
%endif
%if %{with pulseaudio_samsung_policy}
        --enable-samsung-policy \
%endif
        --with-udev-rules-dir=%{_libdir}/udev/rules.d \
        --with-system-user=pulse \
        --with-system-group=pulse \
        --with-access-group=pulse-access

make %{?_smp_mflags} V=0

%install
%make_install
%find_lang %{name}

pushd %{buildroot}/etc/pulse/filter
ln -sf filter_8000_44100.dat filter_11025_44100.dat
ln -sf filter_8000_44100.dat filter_12000_44100.dat
ln -sf filter_8000_44100.dat filter_16000_44100.dat
ln -sf filter_8000_44100.dat filter_22050_44100.dat
ln -sf filter_8000_44100.dat filter_24000_44100.dat
ln -sf filter_8000_44100.dat filter_32000_44100.dat
popd

rm -rf  %{buildroot}/etc/xdg/autostart/pulseaudio-kde.desktop
rm -rf  %{buildroot}/usr/bin/start-pulseaudio-kde
rm -rf  %{buildroot}/usr/bin/start-pulseaudio-x11
rm -rf %{buildroot}/%{_libdir}/pulse-%{version}/modules/module-device-manager.so

mkdir -p %{buildroot}/%{_includedir}/pulsemodule/pulse
mkdir -p %{buildroot}/%{_includedir}/pulsemodule/pulsecore

cp %{buildroot}/%{_includedir}/pulse/*.h %{buildroot}/%{_includedir}/pulsemodule/pulse

fdupes  %{buildroot}/%{_datadir}
fdupes  %{buildroot}/%{_includedir}

# get rid of *.la files
rm -f %{buildroot}/%{_libdir}/*.la
rm -f %{buildroot}/%{_libdir}/pulseaudio/*.la

%post -p /sbin/ldconfig
%postun -p /sbin/ldconfig

%post   -n libpulse -p /sbin/ldconfig
%postun -n libpulse -p /sbin/ldconfig

%post   -n libpulse-mainloop-glib -p /sbin/ldconfig
%postun -n libpulse-mainloop-glib -p /sbin/ldconfig

%lang_package

%files
%manifest %{name}.manifest
%defattr(-,root,root,-)
%doc LICENSE GPL LGPL
%{_sysconfdir}/pulse/filter/*.dat
%{_bindir}/esdcompat
%{_bindir}/pulseaudio
%dir %{_libexecdir}/pulse
%{_libexecdir}/pulse/*
%{_libdir}/libpulsecore-%{version}.so
%{_libdir}/udev/rules.d/90-pulseaudio.rules
%config(noreplace) /etc/dbus-1/system.d/pulseaudio-system.conf
# list all modules
%{_libdir}/pulse-%{version}/modules/libalsa-util.so
%{_libdir}/pulse-%{version}/modules/libcli.so
%{_libdir}/pulse-%{version}/modules/libprotocol-cli.so
%{_libdir}/pulse-%{version}/modules/libprotocol-http.so
%{_libdir}/pulse-%{version}/modules/libprotocol-native.so
%{_libdir}/pulse-%{version}/modules/libprotocol-simple.so
%{_libdir}/pulse-%{version}/modules/librtp.so
%{_libdir}/pulse-%{version}/modules/module-alsa-sink.so
%{_libdir}/pulse-%{version}/modules/module-alsa-source.so
%{_libdir}/pulse-%{version}/modules/module-always-sink.so
%{_libdir}/pulse-%{version}/modules/module-console-kit.so
%{_libdir}/pulse-%{version}/modules/module-device-restore.so
%{_libdir}/pulse-%{version}/modules/module-stream-restore.so
%{_libdir}/pulse-%{version}/modules/module-cli-protocol-tcp.so
%{_libdir}/pulse-%{version}/modules/module-cli-protocol-unix.so
%{_libdir}/pulse-%{version}/modules/module-cli.so
%{_libdir}/pulse-%{version}/modules/module-combine.so
%{_libdir}/pulse-%{version}/modules/module-default-device-restore.so
%{_libdir}/pulse-%{version}/modules/module-detect.so
%{_libdir}/pulse-%{version}/modules/module-esound-sink.so
%{_libdir}/pulse-%{version}/modules/module-http-protocol-tcp.so
%{_libdir}/pulse-%{version}/modules/module-http-protocol-unix.so
%{_libdir}/pulse-%{version}/modules/module-intended-roles.so
%{_libdir}/pulse-%{version}/modules/module-ladspa-sink.so
%{_libdir}/pulse-%{version}/modules/module-match.so
%{_libdir}/pulse-%{version}/modules/module-mmkbd-evdev.so
%{_libdir}/pulse-%{version}/modules/module-native-protocol-fd.so
%{_libdir}/pulse-%{version}/modules/module-native-protocol-tcp.so
%{_libdir}/pulse-%{version}/modules/module-native-protocol-unix.so
%{_libdir}/pulse-%{version}/modules/module-null-sink.so
%{_libdir}/pulse-%{version}/modules/module-pipe-sink.so
%{_libdir}/pulse-%{version}/modules/module-pipe-source.so
%{_libdir}/pulse-%{version}/modules/module-position-event-sounds.so
%{_libdir}/pulse-%{version}/modules/module-remap-sink.so
%{_libdir}/pulse-%{version}/modules/module-remap-source.so
%{_libdir}/pulse-%{version}/modules/module-rescue-streams.so
%{_libdir}/pulse-%{version}/modules/module-rtp-recv.so
%{_libdir}/pulse-%{version}/modules/module-rtp-send.so
%{_libdir}/pulse-%{version}/modules/module-simple-protocol-tcp.so
%{_libdir}/pulse-%{version}/modules/module-simple-protocol-unix.so
%{_libdir}/pulse-%{version}/modules/module-sine.so
%{_libdir}/pulse-%{version}/modules/module-tunnel-sink.so
%{_libdir}/pulse-%{version}/modules/module-tunnel-sink-new.so
%{_libdir}/pulse-%{version}/modules/module-tunnel-source.so
%{_libdir}/pulse-%{version}/modules/module-tunnel-source-new.so
%{_libdir}/pulse-%{version}/modules/module-suspend-on-idle.so
%{_libdir}/pulse-%{version}/modules/module-volume-restore.so
%{_libdir}/pulse-%{version}/modules/module-alsa-card.so
%{_libdir}/pulse-%{version}/modules/module-card-restore.so
%{_libdir}/pulse-%{version}/modules/module-sine-source.so
%{_libdir}/pulse-%{version}/modules/module-loopback.so
%{_libdir}/pulse-%{version}/modules/module-rygel-media-server.so
%{_libdir}/pulse-%{version}/modules/module-echo-cancel.so
%{_libdir}/pulse-%{version}/modules/module-virtual-sink.so
%{_libdir}/pulse-%{version}/modules/module-virtual-source.so
%{_libdir}/pulse-%{version}/modules/libprotocol-esound.so
%{_libdir}/pulse-%{version}/modules/module-esound-compat-spawnfd.so
%{_libdir}/pulse-%{version}/modules/module-esound-compat-spawnpid.so
%{_libdir}/pulse-%{version}/modules/module-esound-protocol-tcp.so
%{_libdir}/pulse-%{version}/modules/module-esound-protocol-unix.so
%{_libdir}/pulse-%{version}/modules/module-gconf.so
%{_libdir}/pulse-%{version}/modules/module-udev-detect.so
%{_libdir}/pulse-%{version}/modules/module-role-cork.so
%{_libdir}/pulse-%{version}/modules/module-switch-on-port-available.so
%{_libdir}/pulse-%{version}/modules/module-virtual-surround-sink.so
%{_libdir}/pulse-%{version}/modules/module-role-ducking.so
%{_libdir}/pulse-%{version}/modules/module-systemd-login.so
%if %{with pulseaudio_samsung_policy}
%{_libdir}/pulse-%{version}/modules/module-policy.so
%endif
%{_libdir}/pulse-%{version}/modules/libvolume-api.so
%{_libdir}/pulse-%{version}/modules/libmain-volume-policy.so
%{_libdir}/pulse-%{version}/modules/module-volume-api.so
%{_libdir}/pulse-%{version}/modules/module-main-volume-policy.so
%{_libdir}/pulse-%{version}/modules/module-audio-groups.so

%config(noreplace) /etc/bash_completion.d/pulseaudio-bash-completion.sh

%files -n libpulse
%manifest %{name}.manifest
%defattr(-,root,root,-)
%{_libdir}/libpulse.so.*
%{_libdir}/libpulse-simple.so.*
%{_libdir}/pulseaudio/libpulsecommon-*.so

%files -n libpulse-devel
%manifest %{name}.manifest
%defattr(-,root,root,-)
%{_includedir}/pulse/*
%{_libdir}/libpulse.so
%{_libdir}/libpulse-simple.so
%{_libdir}/libpulse-mainloop-glib.so
%{_libdir}/pkgconfig/libpulse*.pc
%{_datadir}/vala/vapi/libpulse.vapi
# cmake stuff
%{_libdir}/cmake/PulseAudio/PulseAudioConfig.cmake
%{_libdir}/cmake/PulseAudio/PulseAudioConfigVersion.cmake

%files -n libpulse-mainloop-glib
%manifest %{name}.manifest
%defattr(-,root,root)
%{_libdir}/libpulse-mainloop-glib.so.*

%files utils
%manifest %{name}.manifest
%defattr(-,root,root,-)
%doc %{_mandir}/man1/*
%doc %{_mandir}/man5/*
%{_bindir}/pacat
%{_bindir}/pacmd
%{_bindir}/pactl
%{_bindir}/paplay
%{_bindir}/parec
%{_bindir}/pamon
%{_bindir}/parecord
%{_bindir}/pasuspender

%files module-bluetooth
%manifest %{name}.manifest
%defattr(-,root,root,-)
%{_libdir}/pulse-%{version}/modules/module-bluetooth-discover.so
%{_libdir}/pulse-%{version}/modules/module-bluetooth-policy.so
%{_libdir}/pulse-%{version}/modules/module-bluez5-discover.so
%{_libdir}/pulse-%{version}/modules/module-bluez5-device.so
%{_libdir}/pulse-%{version}/modules/libbluez5-util.so

%files module-raop
%manifest %{name}.manifest
%defattr(-,root,root,-)
%{_libdir}/pulse-%{version}/modules/libraop.so
%{_libdir}/pulse-%{version}/modules/module-raop*.so

%files module-filter
%manifest %{name}.manifest
%defattr(-,root,root,-)
%{_libdir}/pulse-%{version}/modules/module-filter-*.so

%files module-combine-sink
%manifest %{name}.manifest
%defattr(-,root,root,-)
%{_libdir}/pulse-%{version}/modules/module-combine-sink.so

%files module-augment-properties
%manifest %{name}.manifest
%defattr(-,root,root,-)
%{_libdir}/pulse-%{version}/modules/module-augment-properties.so

%files module-dbus-protocol
%manifest %{name}.manifest
%defattr(-,root,root,-)
%{_libdir}/pulse-%{version}/modules/module-dbus-protocol.so

%files module-null-source
%manifest %{name}.manifest
%defattr(-,root,root,-)
%{_libdir}/pulse-%{version}/modules/module-null-source.so

%files module-switch-on-connect
%manifest %{name}.manifest
%defattr(-,root,root,-)
%{_libdir}/pulse-%{version}/modules/module-switch-on-connect.so

%files config
%manifest %{name}.manifest
%defattr(-,root,root,-)
%config(noreplace) %{_sysconfdir}/pulse/daemon.conf
%config(noreplace) %{_sysconfdir}/pulse/default.pa
%config(noreplace) %{_sysconfdir}/pulse/client.conf
%config(noreplace) %{_sysconfdir}/pulse/system.pa
%config(noreplace) %{_sysconfdir}/pulse/audio-groups.conf
%config(noreplace) %{_sysconfdir}/pulse/main-volume-policy.conf

%{_datadir}/pulseaudio/alsa-mixer/paths/*
%{_datadir}/pulseaudio/alsa-mixer/profile-sets/*

%files module-devel
%manifest %{name}.manifest
%defattr(-,root,root)
%{_includedir}/pulsemodule/pulsecore/*.h
%{_includedir}/pulsemodule/pulse/*.h
%{_includedir}/pulsemodule/modules/main-volume-policy/*.h
%{_includedir}/pulsemodule/modules/volume-api/*.h
%{_libdir}/pkgconfig/pulseaudio-module-devel.pc

%files vala-bindings
%manifest %{name}.manifest
%defattr(-,root,root,-)
%{_datadir}/vala/vapi/*

%docs_package
