Name:		recorder
Version:	1.0.7
Release:	1%{?dist}
Summary:	A lock-free, real-time flight recorder for C or C++ programs
License:	LGPLv3+
Url:		https://github.com/c3d/%{name}
Source:		https://github.com/c3d/%{name}/archive/v%{version}/%{name}-%{version}.tar.gz
BuildRequires:	make >= 3.82
BuildRequires:  make-it-quick >= 0.2.5
BuildRequires:  gcc
BuildRequires:  gcc-c++

%description
Flight recorder for C and C++ programs using printf-like 'record' statements

%package devel
Summary:        Development files for recorder library
%description devel
Libraries and include files required to build an application using librecorder

%package scope
Summary:        A real-time graphing tool for data collected by recorder library
BuildRequires:  qt5-devel
BuildRequires:  qt5-qtcharts-devel
Requires:       qt5-qtcharts
%description scope
The recorder_scope tool draws real-time charts from data collected by
the recorder library

%prep
%autosetup -n recorder-%{version}

%build
%make_build COLORIZE= TARGET=opt
(cd scope && qmake-qt5 && make)

%check
%make_build COLORIZE= TARGET=opt check

%install
%make_install COLORIZE= TARGET=opt DOC_INSTALL= PREFIX_DLL=%{_libdir}/
(cd scope && \
     %{__install} -d %{?buildroot}%{_bindir}/ && \
     %{__install} recorder_scope %{?buildroot}%{_bindir}/ )

%files
%license COPYING
%doc README.md
%doc AUTHORS
%doc NEWS
%{_libdir}/lib%{name}.so.*

%post -p /sbin/ldconfig
%postun -p /sbin/ldconfig

%files devel
%{_libdir}/lib%{name}.so
%dir %{_includedir}/%{name}
%{_includedir}/%{name}/*
%{_datadir}/pkgconfig/%{name}.pc
%{_mandir}/man3/*.3.gz

%files scope
%{_bindir}/recorder_scope
%{_mandir}/man1/*.1.gz

%changelog
* Fri Apr 26 2019 Christophe de Dinechin <dinechin@redhat.com> - 1.0.7-1
- Initial Fedora package from upstream release
