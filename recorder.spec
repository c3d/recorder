Name:		recorder
Version:	1.0.3
Release:	1%{?dist}
Summary:	A lock-free, real-time flight recorder for your C or C++ programs
License:	LGPLv3+
Url:		https://github.com/c3d/%{name}
Source:		https://github.com/c3d/%{name}/archive/v%{version}/%{name}-%{version}.tar.gz
BuildRequires:	make >= 3.82
BuildRequires:  make-it-quick >= 0.2.1
BuildRequires:  gcc
BuildRequires:  gcc-c++

%description
Flight recorder for C and C++ programs using printf-like 'record' statements

%package devel
Summary:        Development files for librecorder
%description devel
Libraries and include files required to build an application using librecorder

%package scope
Summary:        A real-time graphing tool for data collected by librecorder
BuildRequires:  qt5-devel
BuildRequires:  qt5-qtcharts-devel
%description scope
The recorder_scope application is a tool to draw real-time charts for
data collected by librecorder

%prep
%autosetup -n recorder-%{version}

%build
%make_build COLORIZE= TARGET=opt
%make_build COLORIZE= TARGET=opt AUTHORS NEWS
(cd scope && qmake-qt5 && make)

%check
%make_build COLORIZE= TARGET=opt check

%install
%make_install COLORIZE= TARGET=opt
(cd scope && \
     %{__install} -d %{?buildroot}%{_bindir}/ && \
     %{__install} recorder_scope %{?buildroot}%{_bindir}/ )

%files
%{_libdir}/lib%{name}.so.*
%license LICENSE
%doc README.md
%doc AUTHORS
%doc NEWS

%post -p /sbin/ldconfig
%postun -p /sbin/ldconfig

%files devel
%{_libdir}/lib%{name}.so
%{_includedir}/%{name}/*
%{_datadir}/pkgconfig/%{name}.pc
%{_mandir}/man3/record.3.gz

%files scope
%{_bindir}/recorder_scope

%changelog
