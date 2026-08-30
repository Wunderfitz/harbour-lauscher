Name:       harbour-lauscher
Summary:    Control Sony headphones from Sailfish OS
Version:    0.1.0
Release:    1
License:    MIT
URL:        https://github.com/mos9527/SonyHeadphonesClient
Source0:    %{name}-%{version}.tar.bz2

Requires:   sailfishsilica-qt5 >= 0.10.9
# No Requires on bluez5: harbour does not allow it as a dependency, and BlueZ is
# part of the base system on every Sailfish device anyway.

BuildRequires:  pkgconfig(sailfishapp) >= 1.0.2
BuildRequires:  pkgconfig(Qt5Core)
BuildRequires:  pkgconfig(Qt5Qml)
BuildRequires:  pkgconfig(Qt5Quick)
BuildRequires:  pkgconfig(Qt5DBus)
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
