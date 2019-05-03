Name:           recorder
Version:        1.0.8
Release:        1%{?dist}
Summary:        Lock-free, real-time flight recorder for C or C++ programs
License:        LGPLv2+
Url:            https://github.com/c3d/%{name}
Source:         https://github.com/c3d/%{name}/archive/v%{version}/%{name}-%{version}.tar.gz
BuildRequires:  make >= 3.82
BuildRequires:  make-it-quick >= 0.2.5
BuildRequires:  gcc
BuildRequires:  gcc-c++

%description
Flight recorder for C and C++ programs using printf-like 'record' statements.

%package devel
Summary:        Development files for recorder library
Requires:       %{name}%{?_isa} = %{version}-%{release}
%description devel
Development files for the flight recorder library.

%package scope
Summary:        A real-time graphing tool for data collected by recorder library
License:        GPLv3+
BuildRequires:  qt5-devel
BuildRequires:  qt5-qtcharts-devel
Requires:       %{name}%{?_isa} = %{version}-%{release}
%description scope
Instrumentation that draws real-time charts, processes or saves data
collected by the flight_recorder library.

%prep
%autosetup -n recorder-%{version}
%configure

%build
%make_build COLORIZE= TARGET=opt V=1
(cd scope &&                            \
 %{qmake_qt5}                           \
        INSTALL_BINDIR=%{_bindir}       \
        INSTALL_LIBDIR=%{_libdir}       \
        INSTALL_DATADIR=%{_datadir}     \
        INSTALL_MANDIR=%{_mandir} &&    \
 make)

%check
%make_build COLORIZE= TARGET=opt V=1 check

%install
%make_install COLORIZE= TARGET=opt DOC_INSTALL=
%make_install -C scope INSTALL_ROOT=%{buildroot}

%files
%license COPYING
%doc README.md
%doc AUTHORS
%doc NEWS
%{_libdir}/lib%{name}.so.1
%{_libdir}/lib%{name}.so.%{version}

%files devel
%{_libdir}/lib%{name}.so
%dir %{_includedir}/%{name}
%{_includedir}/%{name}/*
%{_datadir}/pkgconfig/%{name}.pc
%{_mandir}/man3/*.3.*

%files scope
%{_bindir}/recorder_scope
%{_mandir}/man1/*.1.*

%changelog
* Fri May 3 2019 Christophe de Dinechin <dinechin@redhat.com> - 1.0.8-1
- Adjust Fedora package to address review comments

* Fri Apr 26 2019 Christophe de Dinechin <dinechin@redhat.com> - 1.0.7-1
- Initial Fedora package from upstream release
