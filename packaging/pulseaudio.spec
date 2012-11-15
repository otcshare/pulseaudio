%define pulseversion 2.1

Name:       pulseaudio
Summary:    Improved Linux sound server
Version:    2.1
Release:    0
Group:      Multimedia/PulseAudio
License:    LGPLv2+
URL:        http://pulseaudio.org
Source0:    http://127.0.0.1/pulseaudio-2.1.tar.gz
Patch0: 0001-build-sys-install-files-for-a-module-development.patch
Patch1: 0002-jack-detection-fix-for-wired-headset.patch
Patch2: 0003-volume-ramp-additions-to-the-low-level-infra.patch
Patch3: 0004-volume-ramp-adding-volume-ramping-to-sink-input.patch
Patch4: 0005-volume-ramp-add-volume-ramping-to-sink.patch
Patch5: 0006-make-pa_thread_mq_done-safe-for-subsequent-calls.patch
Patch6: 0007-bluetooth-Add-ports-to-the-bluetooth-sink-source.patch
Patch7: 0008-modules-Use-pa_streq-instead-of-strcmp.patch
Patch8: 0009-bluetooth-Fix-bluetooth.nrec-property-not-updated.patch
Patch9: 0010-bluetooth-Don-t-use-the-old-socket-IPC-mechanism-wit.patch
Patch10: 0011-bluetooth-Refactor-property-parsing-code.patch
Patch11: 0012-bluetooth-Minor-style-fixes.patch
Patch12: 0013-bluetooth-Consider-different-input-and-output-MTU.patch
Patch13: 0014-bluetooth-Avoid-duplicating-profile-argument-twice.patch
Patch14: 0015-bluetooth-Replace-deprecated-ListAdapters.patch
Patch15: 0016-bluetooth-Replace-deprecated-ListDevices.patch
Patch16: 0017-bluetooth-Remove-minor-unnecessary-check.patch
Patch17: 0018-bluetooth-Minor-style-fix.patch
Patch18: 0019-bluetooth-Fix-missing-state-checks-for-a2dp_source.patch
Patch19: 0020-bluetooth-Fix-bluetooth.protocol-property.patch
Patch20: 0021-bluetooth-Don-t-force-any-profile-on-discovery-modul.patch
Patch21: 0022-bluetooth-Remove-commented-out-code.patch
Patch22: 0023-bluetooth-Remove-return-value-of-bt_transport_config.patch
Patch23: 0024-bluetooth-Remove-return-value-of-setup_stream.patch
Patch24: 0025-bluetooth-Refactor-parsing-of-signal-PropertyChanged.patch
Patch25: 0026-bluetooth-Refactor-code-to-helper-function.patch
Patch26: 0027-bluetooth-Fix-wrongly-set-phone-role-for-HFGW.patch
Patch27: 0028-bluetooth-Fix-check-if-transport-exists-before-acqui.patch
Patch28: 0029-sink-source-Support-creating-suspended-sinks-and-sou.patch
Patch29: 0030-bluetooth-Provide-dummy-set_port-callbacks.patch
Patch30: 0031-bluetooth-Config-MTU-transport-after-acquire.patch
Patch31: 0032-bluetooth-Support-port-availability-flag.patch
Patch32: 0033-bluetooth-Acquire-transport-when-becomes-available.patch
Patch33: 0034-bluetooth-Release-transport-when-not-available.patch
Patch34: 0035-bluetooth-Do-not-switch-to-HFGW-automatically.patch
Patch35: 0036-bluetooth-Set-initial-profile-priority-based-on-prof.patch
Patch36: 0037-loopback-Enable-routing-on-loopback-streams.patch
Patch37: 0038-loopback-Cork-sink-input-if-source-is-suspended.patch
Patch38: 0039-loopback-Cork-source-output-if-sink-is-suspended.patch
Patch39: 0040-loopback-Disable-adjust-timer-when-suspended.patch
Patch40: 0041-device-port-Create-the-profiles-hashmap-at-initializ.patch
Patch41: 0042-changes-to-simple-API-Samsung.patch
Patch42: 0043-add-support-for-dlog-Samsung.patch
Patch43: 0044-add-support-for-PMAPI-Samsung.patch
Patch44: 0045-add-Samsung-policy-module-Samsung.patch
Patch45: 0046-packaging-added-spec.in-and-pulseaudio.service.patch
Patch46: 0047-configuration-IVI-additions-to-default-config-files.patch
Patch47: 0048-scripts-added-OBS-exporter-script.patch
Requires:   udev
Requires(post): /sbin/ldconfig
Requires(postun): /sbin/ldconfig
Requires: pulseaudio-config
BuildRequires:  pkgconfig(speexdsp)
BuildRequires:  pkgconfig(sndfile)
BuildRequires:  pkgconfig(alsa)
BuildRequires:  pkgconfig(glib-2.0)
BuildRequires:  pkgconfig(gconf-2.0)
BuildRequires:  pkgconfig(bluez)
BuildRequires:  pkgconfig(dbus-1)
BuildRequires:  pkgconfig(xi)
BuildRequires:  pkgconfig(libudev)
BuildRequires:  pkgconfig(vconf)
BuildRequires:  libatomic_ops-devel
BuildRequires:  pkgconfig(openssl)
BuildRequires:  json-c-devel
BuildRequires:  m4
BuildRequires:  libtool-ltdl-devel
BuildRequires:  libtool
BuildRequires:  intltool
BuildRequires:  fdupes


%description
PulseAudio is a sound server for Linux and other Unix like operating
systems. It is intended to be an improved drop-in replacement for the
Enlightened Sound Daemon (ESOUND).

%package libs
Summary:    PulseAudio client libraries
Group:      Multimedia/PulseAudio
Requires:   %{name} = %{version}-%{release}

%description libs
Client libraries used by applications that access a PulseAudio sound server
via PulseAudio's native interface.


%package libs-devel
Summary:    PulseAudio client development headers and libraries
Group:      Development/Libraries
Requires:   %{name} = %{version}-%{release}

%description libs-devel
Headers and libraries for developing applications that access a PulseAudio
sound server via PulseAudio's native interface


%package utils
Summary:    Command line tools for the PulseAudio sound server
Group:      Multimedia/PulseAudio
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
Group:      Multimedia/PulseAudio
Requires:   %{name} = %{version}-%{release}

%description module-bluetooth
This module enables PulseAudio to work with bluetooth devices, like headset
or audio gatewa

%package module-devel
Summary:        Headers and libraries for PulseAudio module development
License:        LGPLv2+
Group:          Development/Libraries
Requires:       %{name}-libs-devel = %{version}-%{release}

%package config
Summary: Default configuration for PulseAudio.
Group: System Environment/Configuration

%description config
Default configuration for PulseAudio.

%description module-devel
Headers and libraries for developing pulseaudio modules

#%package module-zeroconf
#Summary: PA module-zeroconf
#Group:   Multimedia/PulseAudio
#
#%description module-zeroconf
#PA module-zeroconf.

%package module-raop
Summary: PA module-raop
Group:   Multimedia/PulseAudio

%description module-raop
PA module-raop.

%package module-filter
Summary: PA module-filter
Group:   Multimedia/PulseAudio

%description module-filter
PA module-filter.

%package module-combine-sink
Summary: PA module-combine-sink
Group:   Multimedia/PulseAudio

%description module-combine-sink
PA module-combine-sink.

%package module-augment-properties
Summary: PA module-augment-properties
Group:   Multimedia/PulseAudio

%description module-augment-properties
PA module-augment-properties.

%package module-dbus-protocol
Summary: PA module-dbus-protocol
Group:   Multimedia/PulseAudio

%description module-dbus-protocol
PA module-dbus-protocol.

%package module-null-source
Summary: PA module-null-source
Group:   Multimedia/PulseAudio

%description module-null-source
PA module-null-source.

%package module-switch-on-connect
Summary: PA module-swich-on-connect
Group:   Multimedia/PulseAudio

%description module-switch-on-connect
PA module-swich-on-connect.

%package localization
Summary:    PA localization files.
Group:      Multimedia/PulseAudio
Requires:   %{name} = %{version}-%{release}

%description localization
PA localization files.

%package vala-bindings
Summary:    PA Vala bindings.
Group:      Multimedia/PulseAudio
Requires:   %{name} = %{version}-%{release}

%description vala-bindings
PA Vala bindings.


%prep
%setup -q
echo "%{version}" > .tarball-version
%patch0 -p1
%patch1 -p1
%patch2 -p1
%patch3 -p1
%patch4 -p1
%patch5 -p1
%patch6 -p1
%patch7 -p1
%patch8 -p1
%patch9 -p1
%patch10 -p1
%patch11 -p1
%patch12 -p1
%patch13 -p1
%patch14 -p1
%patch15 -p1
%patch16 -p1
%patch17 -p1
%patch18 -p1
%patch19 -p1
%patch20 -p1
%patch21 -p1
%patch22 -p1
%patch23 -p1
%patch24 -p1
%patch25 -p1
%patch26 -p1
%patch27 -p1
%patch28 -p1
%patch29 -p1
%patch30 -p1
%patch31 -p1
%patch32 -p1
%patch33 -p1
%patch34 -p1
%patch35 -p1
%patch36 -p1
%patch37 -p1
%patch38 -p1
%patch39 -p1
%patch40 -p1
%patch41 -p1
%patch42 -p1
%patch43 -p1
%patch44 -p1
%patch45 -p1
%patch46 -p1
%patch47 -p1
./bootstrap.sh

%build
CPUS="`cat /proc/cpuinfo  | grep ^processor | wc -l`"
JOBS="-j$(($CPUS + 1))"

unset LD_AS_NEEDED
export LDFLAGS+="-Wl,--no-as-needed"
%configure --disable-static --enable-alsa --disable-ipv6 --disable-oss-output --disable-oss-wrapper --enable-bluez --disable-x11 --disable-hal --disable-hal-compat --disable-lirc --disable-avahi --disable-jack --without-fftw --disable-xen --enable-spolicy
make $JOBS

%install
rm -rf %{buildroot}
%make_install

install -d  %{buildroot}/%{_libdir}/systemd/system
install -m 644 pulseaudio.service %{buildroot}/%{_libdir}/systemd/system/pulseaudio.service
mkdir -p  %{buildroot}/%{_libdir}/systemd/system/multi-user.target.wants
ln -s  ../pulseaudio.service  %{buildroot}/%{_libdir}/systemd/system/multi-user.target.wants/pulseaudio.service

rm -rf  %{buildroot}/etc/xdg/autostart/pulseaudio-kde.desktop
rm -rf  %{buildroot}/usr/bin/start-pulseaudio-kde
rm -rf %{buildroot}/%{_libdir}/pulse-%{version}/modules/module-device-manager.so

%find_lang pulseaudio
fdupes  %{buildroot}/%{_datadir}
fdupes  %{buildroot}/%{_includedir}

# get rid of *.la files
rm -f %{buildroot}/%{_libdir}/*.la
rm -f %{buildroot}/%{_libdir}/pulseaudio/*.la

# put the default configuration file in place
install -m 644 src/default.pa %{buildroot}/%{_sysconfdir}/pulse


%post
/sbin/ldconfig

%postun
/sbin/ldconfig

%post libs -p /sbin/ldconfig

%postun libs -p /sbin/ldconfig


%post module-bluetooth -p /sbin/ldconfig
%postun module-bluetooth -p /sbin/ldconfig


%docs_package

%lang_package


%files
%defattr(-,root,root,-)
%doc LICENSE GPL LGPL
%dir %{_sysconfdir}/pulse/
%{_bindir}/esdcompat
%{_bindir}/pulseaudio
%dir %{_libexecdir}/pulse
%{_libexecdir}/pulse/*
%{_libdir}/libpulsecore-%{version}.so
%{_libdir}/libpulse-mainloop-glib.so.*
/lib/udev/rules.d/90-pulseaudio.rules
%{_datadir}/pulseaudio/alsa-mixer/paths/*
%{_datadir}/pulseaudio/alsa-mixer/profile-sets/*
%{_bindir}/pamon
/etc/dbus-1/system.d/pulseaudio-system.conf
# cmake stuff
%{_libdir}/cmake/PulseAudio/PulseAudioConfig.cmake
%{_libdir}/cmake/PulseAudio/PulseAudioConfigVersion.cmake
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
%{_libdir}/pulse-%{version}/modules/module-rescue-streams.so
%{_libdir}/pulse-%{version}/modules/module-rtp-recv.so
%{_libdir}/pulse-%{version}/modules/module-rtp-send.so
%{_libdir}/pulse-%{version}/modules/module-simple-protocol-tcp.so
%{_libdir}/pulse-%{version}/modules/module-simple-protocol-unix.so
%{_libdir}/pulse-%{version}/modules/module-sine.so
%{_libdir}/pulse-%{version}/modules/module-tunnel-sink.so
%{_libdir}/pulse-%{version}/modules/module-tunnel-source.so
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
%{_libdir}/pulse-%{version}/modules/module-policy.so
%{_libdir}/systemd/system/pulseaudio.service
%{_libdir}/systemd/system/multi-user.target.wants/pulseaudio.service

%files libs
%defattr(-,root,root,-)
%{_libdir}/libpulse.so.*
%{_libdir}/libpulse-simple.so.*
%{_libdir}/pulseaudio/libpulsecommon-*.so

%files libs-devel
%defattr(-,root,root,-)
%{_includedir}/pulse/*
%{_libdir}/libpulse.so
%{_libdir}/libpulse-simple.so
%{_libdir}/pkgconfig/libpulse-simple.pc
%{_libdir}/pkgconfig/libpulse.pc
%{_datadir}/vala/vapi/libpulse.vapi
%{_libdir}/pkgconfig/libpulse-mainloop-glib.pc
%{_libdir}/libpulse-mainloop-glib.so

%files utils
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
%defattr(-,root,root,-)
%{_libdir}/pulse-%{version}/modules/module-bluetooth-proximity.so
%{_libdir}/pulse-%{version}/modules/module-bluetooth-device.so
%{_libdir}/pulse-%{version}/modules/module-bluetooth-discover.so
%{_libdir}/pulse-%{version}/modules/libbluetooth-ipc.so
%{_libdir}/pulse-%{version}/modules/libbluetooth-sbc.so
%{_libdir}/pulse-%{version}/modules/libbluetooth-util.so

#%files module-zeroconf
#%defattr(-,root,root,-)
#%{_libdir}/pulse-%{version}/modules/libavahi-wrap.so
#%{_libdir}/pulse-%{version}/modules/module-zeroconf*.so

%files module-raop
%defattr(-,root,root,-)
%{_libdir}/pulse-%{version}/modules/libraop.so
%{_libdir}/pulse-%{version}/modules/module-raop*.so

%files module-filter
%defattr(-,root,root,-)
%{_libdir}/pulse-%{version}/modules/module-filter-*.so

%files module-combine-sink
%defattr(-,root,root,-)
%{_libdir}/pulse-%{version}/modules/module-combine-sink.so

%files module-augment-properties
%defattr(-,root,root,-)
%{_libdir}/pulse-%{version}/modules/module-augment-properties.so

%files module-dbus-protocol
%defattr(-,root,root,-)
%{_libdir}/pulse-%{version}/modules/module-dbus-protocol.so

%files module-null-source
%defattr(-,root,root,-)
%{_libdir}/pulse-%{version}/modules/module-null-source.so

%files module-switch-on-connect
%defattr(-,root,root,-)
%{_libdir}/pulse-%{version}/modules/module-switch-on-connect.so

%files config
%defattr(-,root,root,-)
%{_sysconfdir}/pulse/default.pa
%{_sysconfdir}/pulse/client.conf
%{_sysconfdir}/pulse/default.pa
%{_sysconfdir}/pulse/system.pa
%{_sysconfdir}/pulse/daemon.conf

%files module-devel
%defattr(-,root,root)
%{_includedir}/pulsecore
%{_libdir}/pkgconfig/pulseaudio-module-devel.pc

%files localization
%defattr(-,root,root,-)
%{_datadir}/locale/*/LC_MESSAGES/pulseaudio.mo

%files vala-bindings
%defattr(-,root,root,-)
%{_datadir}/vala/vapi/*
