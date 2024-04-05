Summary: UPnP base library
Name: libnpupnp
Version: 6.1.2
Release: 1%{?dist}
License: BSD
Group: Application/Multimedia
URL: http://www.lesbonscomptes.com/upmpdcli/
Source: http://www.lesbonscomptes.com/upmpdcli/downloads/%{name}-%{version}.tar.gz
BuildRequires: meson
BuildRequires: libcurl-devel
BuildRequires: libmicrohttpd-devel
# Opensuse:
#BuildRequires: libexpat-devel
# Fedora
BuildRequires:  expat-devel

Buildroot: %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

%description
UPnP library, based on pupnp code, extensively
rewritten. As its predecessor, it provides developers with an
API and open source code for building control points, devices, and
bridges.

%prep
%autosetup

%build
%meson
%meson_build

%install
%{__rm} -rf %{buildroot}
%meson_install

%clean
%{__rm} -rf %{buildroot}

%files
%defattr(-,root,root,-)
%{_includedir}/npupnp
%{_libdir}/libnpupnp.so*
%{_libdir}/pkgconfig/*.pc

%changelog
* Mon Feb 24 2020 Jean-Francois Dockes <jf@dockes.org> - 2.1.1-1
- Fix crash on addr-less interfaces in getifaddrs output
* Mon Feb 24 2020 Jean-Francois Dockes <jf@dockes.org> - 2.1.0-1
- Fix no ipv6 interface found bug. Use std::thread et al.
* Fri Feb 07 2020 Jean-Francois Dockes <jf@dockes.org> - 2.0.1-1
- Small fix in message format
* Wed Feb 05 2020 Jean-Francois Dockes <jf@dockes.org> - 2.0.0-1
- V 2 changes API and removes dep on libixml
* Tue Jan 28 2020 Jean-Francois Dockes <jf@dockes.org> - 1.0.0-1
- Initial version

