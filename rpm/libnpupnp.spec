Summary: Universal Plug and Play (UPnP) SDK
Name: libnpupnp
Version: 1.0.0
Release: 1%{?dist}
License: BSD
Group: Application/Multimedia
URL: http://www.lesbonscomptes.com/upmpdcli/
Source: http://www.lesbonscomptes.com/upmpdcli/downloads/%{name}-%{version}.tar.gz
BuildRequires:  expat-devel
BuildRequires:  libcurl-devel
BuildRequires:  libmicrohttpd-devel
# Only for ixml, but comes in the same package
BuildRequires:  libupnp-devel
Buildroot: %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

%description
UPnP library, based on Pupnp code, but extensively
rewritten. As its predecessor, it provides developers with an
API and open source code for building control points, devices, and
bridges.

%prep
%setup -q

%build
%configure CXXFLAGS="-DPIC -fPIC"
make %{?_smp_mflags}

%install
%{__rm} -rf %{buildroot}
%{__make} install DESTDIR=%{buildroot} STRIP=/bin/true INSTALL='install -p'
%{__rm} -f %{buildroot}%{_libdir}/libnpupnp.a
%{__rm} -f %{buildroot}%{_libdir}/libnpupnp.la

%files
%defattr(-,root,root,-)
%{_includedir}/npupnp
%{_libdir}/libnpupnp.so*
%{_libdir}/pkgconfig/*.pc

%clean
%{__rm} -rf %{buildroot}

%changelog
* Tue Jan 28 2020 Jean-Francois Dockes <jf@dockes.org> - 1.0.0-1
- Initial version

