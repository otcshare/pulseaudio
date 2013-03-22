#

%bcond_with tizen

Name:           pulseaudio
Version:        2.1
Release:        0
License:        GPL-2.0+ ; LGPL-2.1+
%define drvver  2.1
%define soname  0
Summary:        A Networked Sound Server
Url:            http://pulseaudio.org
Group:          Multimedia/Audio
Source:         http://www.freedesktop.org/software/pulseaudio/releases/%{name}-%{version}.tar.xz
Source1:        default.pa-for-gdm
Source2:        setup-pulseaudio
Source3:        sysconfig.sound-pulseaudio
Source99:       baselibs.conf
BuildRequires:  fdupes
BuildRequires:  gdbm-devel
BuildRequires:  intltool
BuildRequires:  libcap-devel
BuildRequires:  libtool
BuildRequires:  libudev-devel >= 143
BuildRequires:  update-desktop-files
BuildRequires:  pkgconfig(alsa)
BuildRequires:  pkgconfig(bluez)
BuildRequires:  pkgconfig(x11)
BuildRequires:  pkgconfig(xi)
BuildRequires:  pkgconfig(x11-xcb)
BuildRequires:  pkgconfig(xcb) >= 1.6
BuildRequires:  pkgconfig(ice)
BuildRequires:  pkgconfig(sm)
BuildRequires:  pkgconfig(xtst)
BuildRequires:  pkgconfig(dbus-1)
BuildRequires:  pkgconfig(glib-2.0)
BuildRequires:  pkgconfig(json) >= 0.9
BuildRequires:  pkgconfig(openssl)
BuildRequires:  pkgconfig(orc-0.4)
BuildRequires:  pkgconfig(sndfile)
BuildRequires:  pkgconfig(speex)
%if %{with tizen}
BuildRequires:  pkgconfig(capi-system-power)
BuildRequires:  pkgconfig(dlog)
BuildRequires:  pkgconfig(vconf)
%endif
Requires:       udev >= 146
Requires(pre):         pwdutils

%description
pulseaudio is a networked sound server for Linux, other Unix like
operating systems and Microsoft Windows. It is intended to be an
improved drop-in replacement for the Enlightened Sound Daemon (ESOUND).

%package esound-compat
Summary:        ESOUND compatibility for PulseAudio
Group:          Multimedia/Audio
Requires:       %{name} = %{version}

%description esound-compat
pulseaudio is a networked sound server for Linux and other Unix like
operating systems and Microsoft Windows. It is intended to be an
improved drop-in replacement for the Enlightened Sound Daemon (ESOUND).

This package provides the compatibility layer for drop-in replacement
of ESOUND.

%package module-devel
License:        LGPL-2.0+
Summary:        Headers and libraries for PulseAudio module development
Group:          Development/Libraries
Requires:       libpulse-devel = %{version}

%description module-devel
Headers and libraries for developing pulseaudio modules

%package module-x11
Summary:        X11 module for PulseAudio
Group:          Multimedia/Audio
Requires:       %{name} = %{version}
Requires:       %{name}-utils = %{version}

%description module-x11
pulseaudio is a networked sound server for Linux and other Unix like
operating systems and Microsoft Windows. It is intended to be an
improved drop-in replacement for the Enlightened Sound Daemon (ESOUND).

This package provides the components needed to automatically start
the PulseAudio sound server on X11 startup.

%package module-zeroconf
Summary:        Zeroconf module for PulseAudio
Group:          Multimedia/Audio
Requires:       %{name} = %{version}

%description module-zeroconf
pulseaudio is a networked sound server for Linux and other Unix like
operating systems and Microsoft Windows. It is intended to be an
improved drop-in replacement for the Enlightened Sound Daemon (ESOUND).

This package provides zeroconf network support for the PulseAudio sound server

%package module-jack
Summary:        JACK support for the PulseAudio sound server
Group:          Multimedia/Audio
Requires:       %{name} = %{version}

%description module-jack
pulseaudio is a networked sound server for Linux and other Unix like
operating systems and Microsoft Windows. It is intended to be an
improved drop-in replacement for the Enlightened Sound Daemon (ESOUND).

This package includes support for Jack-based applications.

%package module-bluetooth
Summary:        Bluetooth support for the PulseAudio sound server
Group:          Multimedia/Audio
Requires:       %{name} = %{version}
Requires:       bluez >= 4.34

%description module-bluetooth
pulseaudio is a networked sound server for Linux and other Unix like
operating systems and Microsoft Windows. It is intended to be an
improved drop-in replacement for the Enlightened Sound Daemon (ESOUND).

Contains Bluetooth audio (A2DP/HSP/HFP) support for the PulseAudio sound server.

%package module-gconf
Summary:        GCONF module for PulseAudio
Group:          Multimedia/Audio
Requires:       %{name} = %{version}

%description module-gconf
pulseaudio is a networked sound server for Linux and other Unix like
operating systems and Microsoft Windows. It is intended to be an
improved drop-in replacement for the Enlightened Sound Daemon (ESOUND).

This package provides gconf storage of PulseAudio sound server settings.

%package -n libpulse
Summary:        Client interface to PulseAudio
Group:          Multimedia/Audio

%description -n libpulse
pulseaudio is a networked sound server for Linux and other Unix like
operating systems and Microsoft Windows. It is intended to be an
improved drop-in replacement for the Enlightened Sound Daemon (ESOUND).

This package contains the system libraries for clients of pulseaudio
sound server.

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
Summary:        Development package for the pulseaudio library
Group:          Development/Libraries
Requires:       libpulse = %{version}
Requires:       libpulse-mainloop-glib = %{version}
Requires:       pkgconfig
Requires:       pkgconfig(glib-2.0)

%description -n libpulse-devel
pulseaudio is a networked sound server for Linux and other Unix like
operating systems and Microsoft Windows. It is intended to be an
improved drop-in replacement for the Enlightened Sound Daemon (ESOUND).

This package contains the files needed to compile programs that use the
pulseaudio library.

%package utils
Summary:        PulseAudio utilities
Group:          Multimedia/Audio
Requires:       %{name} = %{version}
Requires:       libpulse = %{version}
Requires:       libpulse-mainloop-glib = %{version}

%description utils
pulseaudio is a networked sound server for Linux and other Unix like
operating systems and Microsoft Windows. It is intended to be an
improved drop-in replacement for the Enlightened Sound Daemon (ESOUND).

This package provides utilies for making use of the PulseAudio sound
server.

%package gdm-hooks
Summary:        PulseAudio GDM integration
Group:          Multimedia/Audio
Requires:       %{name} = %{version}
Requires:       gdm >= 2.22
Requires(pre):  gdm
#avoid cycle
#!BuildIgnore: gdm

%description gdm-hooks
pulseaudio is a networked sound server for Linux and other Unix like
operating systems and Microsoft Windows. It is intended to be an
improved drop-in replacement for the Enlightened Sound Daemon (ESOUND).

This package contains GDM integration hooks for the PulseAudio sound server.

%prep
%setup -q -T -b0

%build
export CFLAGS="%{optflags} -fno-strict-aliasing"
# libpulse and libpulsecommon need each other - no way with as-needed
export LD_AS_NEEDED=0
echo "%{version}" > .tarball-version
./bootstrap.sh
%configure \
        --disable-static \
        --disable-rpath \
%if %{with tizen}
        --enable-dlog \
        --enable-pmapi \
        --enable-spolicy \
%endif
        --enable-systemd \
        --with-system-user=pulse \
        --with-system-group=pulse \
        --with-access-group=pulse-access \
        --with-udev-rules-dir=/usr/lib/udev/rules.d \
        --disable-hal
make %{?_smp_mflags} V=1

%install
%make_install
%find_lang %{name}
install %{SOURCE2} %{buildroot}%{_bindir}
chmod 755 %{buildroot}%{_bindir}/setup-pulseaudio
mkdir -p %{buildroot}%{_sysconfdir}/profile.d
touch %{buildroot}%{_sysconfdir}/profile.d/pulseaudio.sh
touch %{buildroot}%{_sysconfdir}/profile.d/pulseaudio.csh
mkdir -p %{buildroot}%{_localstatedir}/lib/gdm/.pulse
cp $RPM_SOURCE_DIR/default.pa-for-gdm %{buildroot}%{_localstatedir}/lib/gdm/.pulse/default.pa
ln -s esdcompat %{buildroot}%{_bindir}/esd
rm -rf %{buildroot}%{_sysconfdir}/xdg/autostart/pulseaudio-kde.desktop

install -D -m 0644 %{SOURCE3} %{buildroot}%{_sysconfdir}/sysconfig/sound
%pre
groupadd -r pulse &>/dev/null || :
useradd -r -c 'PulseAudio daemon' \
    -s /sbin/nologin -d /var/lib/pulseaudio -g pulse -G audio pulse &>/dev/null || :
groupadd -r pulse-access &>/dev/null || :

%post   -n libpulse -p /sbin/ldconfig

%postun -n libpulse -p /sbin/ldconfig

%post   -n libpulse-mainloop-glib -p /sbin/ldconfig

%postun -n libpulse-mainloop-glib -p /sbin/ldconfig

%post
/sbin/ldconfig
# Update the /etc/profile.d/pulseaudio.* files
setup-pulseaudio --auto > /dev/null

%postun -p /sbin/ldconfig

%lang_package

%files
%defattr(-,root,root)
%license LICENSE GPL LGPL
%config(noreplace) %{_sysconfdir}/sysconfig/sound
%{_bindir}/pulseaudio
%{_bindir}/setup-pulseaudio
%dir %{_datadir}/pulseaudio
%{_datadir}/pulseaudio/alsa-mixer
%{_libdir}/libpulsecore-%{drvver}.so
%dir %{_libdir}/pulseaudio
%{_libdir}/pulseaudio/libpulsedsp.so
%dir %{_libdir}/pulse-%{drvver}/
%dir %{_libdir}/pulse-%{drvver}/modules/
%{_libdir}/pulse-%{drvver}/modules/libalsa-util.so
%{_libdir}/pulse-%{drvver}/modules/libcli.so
%{_libdir}/pulse-%{drvver}/modules/liboss-util.so
%{_libdir}/pulse-%{drvver}/modules/libprotocol-cli.so
%{_libdir}/pulse-%{drvver}/modules/libprotocol-esound.so
%{_libdir}/pulse-%{drvver}/modules/libprotocol-http.so
%{_libdir}/pulse-%{drvver}/modules/libprotocol-native.so
%{_libdir}/pulse-%{drvver}/modules/libprotocol-simple.so
%{_libdir}/pulse-%{drvver}/modules/librtp.so
%{_libdir}/pulse-%{drvver}/modules/module-alsa-card.so
%{_libdir}/pulse-%{drvver}/modules/module-alsa-sink.so
%{_libdir}/pulse-%{drvver}/modules/module-alsa-source.so
%{_libdir}/pulse-%{drvver}/modules/module-always-sink.so
%{_libdir}/pulse-%{drvver}/modules/module-augment-properties.so
%{_libdir}/pulse-%{drvver}/modules/module-card-restore.so
%{_libdir}/pulse-%{drvver}/modules/module-cli.so
%{_libdir}/pulse-%{drvver}/modules/module-cli-protocol-tcp.so
%{_libdir}/pulse-%{drvver}/modules/module-cli-protocol-unix.so
%{_libdir}/pulse-%{drvver}/modules/module-combine.so
%{_libdir}/pulse-%{drvver}/modules/module-combine-sink.so
%{_libdir}/pulse-%{drvver}/modules/module-console-kit.so
%{_libdir}/pulse-%{drvver}/modules/module-dbus-protocol.so
%{_libdir}/pulse-%{drvver}/modules/module-default-device-restore.so
%{_libdir}/pulse-%{drvver}/modules/module-detect.so
%{_libdir}/pulse-%{drvver}/modules/module-device-manager.so
%{_libdir}/pulse-%{drvver}/modules/module-device-restore.so
%{_libdir}/pulse-%{drvver}/modules/module-echo-cancel.so
%{_libdir}/pulse-%{drvver}/modules/module-esound-compat-spawnfd.so
%{_libdir}/pulse-%{drvver}/modules/module-esound-compat-spawnpid.so
%{_libdir}/pulse-%{drvver}/modules/module-esound-protocol-tcp.so
%{_libdir}/pulse-%{drvver}/modules/module-esound-protocol-unix.so
%{_libdir}/pulse-%{drvver}/modules/module-esound-sink.so
%{_libdir}/pulse-%{drvver}/modules/module-filter-apply.so
%{_libdir}/pulse-%{drvver}/modules/module-filter-heuristics.so
%{_libdir}/pulse-%{drvver}/modules/module-hal-detect.so
%{_libdir}/pulse-%{drvver}/modules/module-http-protocol-tcp.so
%{_libdir}/pulse-%{drvver}/modules/module-http-protocol-unix.so
%{_libdir}/pulse-%{drvver}/modules/module-intended-roles.so
%{_libdir}/pulse-%{drvver}/modules/module-ladspa-sink.so
%{_libdir}/pulse-%{drvver}/modules/libraop.so
%{_libdir}/pulse-%{drvver}/modules/module-raop-sink.so
%{_libdir}/pulse-%{drvver}/modules/module-loopback.so
%{_libdir}/pulse-%{drvver}/modules/module-match.so
%{_libdir}/pulse-%{drvver}/modules/module-mmkbd-evdev.so
%{_libdir}/pulse-%{drvver}/modules/module-native-protocol-fd.so
%{_libdir}/pulse-%{drvver}/modules/module-native-protocol-tcp.so
%{_libdir}/pulse-%{drvver}/modules/module-native-protocol-unix.so
%{_libdir}/pulse-%{drvver}/modules/module-null-sink.so
%{_libdir}/pulse-%{drvver}/modules/module-null-source.so
%{_libdir}/pulse-%{drvver}/modules/module-oss.so
%{_libdir}/pulse-%{drvver}/modules/module-pipe-sink.so
%{_libdir}/pulse-%{drvver}/modules/module-pipe-source.so
%{_libdir}/pulse-%{drvver}/modules/module-position-event-sounds.so
%{_libdir}/pulse-%{drvver}/modules/module-remap-sink.so
%{_libdir}/pulse-%{drvver}/modules/module-rescue-streams.so
%{_libdir}/pulse-%{drvver}/modules/module-role-cork.so
%{_libdir}/pulse-%{drvver}/modules/module-rtp-recv.so
%{_libdir}/pulse-%{drvver}/modules/module-rtp-send.so
%{_libdir}/pulse-%{drvver}/modules/module-rygel-media-server.so
%{_libdir}/pulse-%{drvver}/modules/module-simple-protocol-tcp.so
%{_libdir}/pulse-%{drvver}/modules/module-simple-protocol-unix.so
%{_libdir}/pulse-%{drvver}/modules/module-sine.so
%{_libdir}/pulse-%{drvver}/modules/module-sine-source.so
%{_libdir}/pulse-%{drvver}/modules/module-stream-restore.so
%{_libdir}/pulse-%{drvver}/modules/module-suspend-on-idle.so
%{_libdir}/pulse-%{drvver}/modules/module-switch-on-connect.so
%{_libdir}/pulse-%{drvver}/modules/module-switch-on-port-available.so
%{_libdir}/pulse-%{drvver}/modules/module-systemd-login.so
%{_libdir}/pulse-%{drvver}/modules/module-tunnel-sink.so
%{_libdir}/pulse-%{drvver}/modules/module-tunnel-source.so
%{_libdir}/pulse-%{drvver}/modules/module-udev-detect.so
%{_libdir}/pulse-%{drvver}/modules/module-virtual-sink.so
%{_libdir}/pulse-%{drvver}/modules/module-virtual-source.so
%{_libdir}/pulse-%{drvver}/modules/module-virtual-surround-sink.so
%{_libdir}/pulse-%{drvver}/modules/module-volume-restore.so
%if %{with tizen}
%{_libdir}/pulse-%{drvver}/modules/module-policy.so
%endif

%{_prefix}/lib/udev/rules.d/90-pulseaudio.rules
%dir %{_sysconfdir}/pulse/
%config(noreplace) %{_sysconfdir}/pulse/daemon.conf
%config(noreplace) %{_sysconfdir}/pulse/default.pa
%config(noreplace) %{_sysconfdir}/pulse/system.pa
%config(noreplace) %{_sysconfdir}/dbus-1/system.d/pulseaudio-system.conf
# created by setup-pulseaudio script
%ghost %{_sysconfdir}/profile.d/pulseaudio.sh
%ghost %{_sysconfdir}/profile.d/pulseaudio.csh

%files esound-compat
%defattr(-,root,root)
%{_bindir}/esdcompat
%{_bindir}/esd

%files gdm-hooks
%defattr(-,root,root)
%attr(0750, gdm, gdm) %dir %{_localstatedir}/lib/gdm
%attr(0700, gdm, gdm) %dir %{_localstatedir}/lib/gdm/.pulse
%attr(0600, gdm, gdm) %{_localstatedir}/lib/gdm/.pulse/default.pa

%files -n libpulse
%defattr(-,root,root)
%doc README LICENSE GPL LGPL
%dir %{_sysconfdir}/pulse/
%config(noreplace) %{_sysconfdir}/pulse/client.conf
%{_libdir}/libpulse.so.%{soname}
%{_libdir}/libpulse.so.%{soname}.*
%{_libdir}/libpulse-simple.so.*
%dir %{_libdir}/pulseaudio
%{_libdir}/pulseaudio/libpulsecommon-%{drvver}.so

%files module-devel
%defattr(-,root,root)
%{_includedir}/pulsecore/*.h
%{_includedir}/pulsemodule/pulse/*.h
%{_libdir}/pkgconfig/pulseaudio-module-devel.pc

%files -n libpulse-devel
%defattr(-,root,root)
%{_includedir}/pulse/
%{_libdir}/libpulse.so
%{_libdir}/libpulse-mainloop-glib.so
%{_libdir}/libpulse-simple.so
%{_libdir}/pkgconfig/libpulse*.pc
%dir %{_libdir}/cmake
%dir %{_libdir}/cmake/PulseAudio
%{_libdir}/cmake/PulseAudio/PulseAudio*.cmake
%{_datadir}/vala

%files -n libpulse-mainloop-glib
%defattr(-,root,root)
%{_libdir}/libpulse-mainloop-glib.so.*

%files module-bluetooth
%defattr(-,root,root)
%{_libdir}/pulse-%{drvver}/modules/libbluetooth-ipc.so
%{_libdir}/pulse-%{drvver}/modules/libbluetooth-sbc.so
%{_libdir}/pulse-%{drvver}/modules/libbluetooth-util.so
%{_libdir}/pulse-%{drvver}/modules/module-bluetooth-device.so
%{_libdir}/pulse-%{drvver}/modules/module-bluetooth-discover.so
%{_libdir}/pulse-%{drvver}/modules/module-bluetooth-proximity.so
%attr(0755,root,root) %{_libexecdir}/pulse/proximity-helper

%files module-x11
%defattr(-,root,root)
%{_sysconfdir}/xdg/autostart/pulseaudio.desktop
%{_bindir}/start-pulseaudio-x11
%{_bindir}/start-pulseaudio-kde
%{_libdir}/pulse-%{drvver}/modules/module-x11-bell.so
%{_libdir}/pulse-%{drvver}/modules/module-x11-cork-request.so
%{_libdir}/pulse-%{drvver}/modules/module-x11-publish.so
%{_libdir}/pulse-%{drvver}/modules/module-x11-xsmp.so

%files module-zeroconf
%defattr(-,root,root)
#%{_libdir}/pulse-%{drvver}/modules/libavahi-wrap.so
%{_libdir}/pulse-%{drvver}/modules/libraop.so
#%{_libdir}/pulse-%{drvver}/modules/module-raop-discover.so
%{_libdir}/pulse-%{drvver}/modules/module-raop-sink.so
#%{_libdir}/pulse-%{drvver}/modules/module-zeroconf-discover.so
#%{_libdir}/pulse-%{drvver}/modules/module-zeroconf-publish.so

%files utils
%defattr(-,root,root)
%{_bindir}/pacat
%{_bindir}/pacmd
%{_bindir}/pactl
%{_bindir}/paplay
%{_bindir}/parec
%{_bindir}/pamon
%{_bindir}/parecord
%{_bindir}/pax11publish
%{_bindir}/padsp
%{_bindir}/pasuspender


%docs_package
%changelog
