Name:       harbour-lauscher
Summary:    Control Sony headphones from Sailfish OS
Version:    0.1
Release:    1
License:    GPLv3
URL:        https://github.com/Wunderfitz/harbour-lauscher
Source0:    %{name}-%{version}.tar.bz2

# Everything the app links against (Qt5Core/Gui/Qml/Quick/DBus, libsailfishapp,
# libstdc++) is picked up by rpmbuild's ELF dependency generator. What follows is
# what it cannot see: QML imports, the D-Bus service on the other end, and the
# sandbox profile named in the .desktop file.
Requires:   sailfishsilica-qt5 >= 0.10.9
Requires:   qt5-qtdeclarative-import-qtquick2plugin
# bluetoothd owns org.bluez: it does the SDP lookup, and BluezTransport speaks
# ProfileManager1/Profile1/Device1 to it. Without it the app has no transport.
Requires:   bluez5
# harbour-lauscher.desktop declares Permissions=Bluetooth, which sailjail
# resolves against /etc/sailjail/permissions/Bluetooth.permission.
Requires:   sailjail-permissions

BuildRequires:  pkgconfig(sailfishapp) >= 1.0.2
BuildRequires:  pkgconfig(Qt5Core)
BuildRequires:  pkgconfig(Qt5Gui)
BuildRequires:  pkgconfig(Qt5Qml)
BuildRequires:  pkgconfig(Qt5Quick)
BuildRequires:  pkgconfig(Qt5DBus)
# CONFIG += sailfishapp_i18n runs lupdate and lrelease during %%install
BuildRequires:  qt5-qttools-linguist
BuildRequires:  desktop-file-utils

%description
Lauscher reads battery levels and device information from Sony headphones and
switches their listening modes, speaking Sony's MDR protocol over Bluetooth
RFCOMM. The protocol implementation is libmdr from the SonyHeadphonesClient
project; the Bluetooth transport uses BlueZ's Profile1 D-Bus API directly.

%prep
%setup -q -n %{name}-%{version}

%build
%qmake5
%make_build

%install
%qmake5_install

desktop-file-install --delete-original       \
  --dir %{buildroot}%{_datadir}/applications \
  %{buildroot}%{_datadir}/applications/*.desktop

%files
%defattr(-,root,root,-)
%{_bindir}/%{name}
%{_datadir}/%{name}
%{_datadir}/applications/%{name}.desktop
%{_datadir}/icons/hicolor/*/apps/%{name}.png
