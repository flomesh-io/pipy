Name:		pipy
Version: 	%{getenv:VERSION}
Release: 	%{getenv:REVISION}%{?dist}

Summary: 	Pipy is a programmable network proxy for the cloud, edge and IoT.

License: 	NEU License
Source0: 	pipy.tar.gz
BuildRoot:      %{_tmppath}/%{name}-%{version}-%{revision}-root-%(%{__id_u} -n)
BuildRequires: 	/usr/bin/chrpath
BuildRequires: 	autoconf
BuildRequires: 	automake
%if 0%{?rhel} == 7
BuildRequires: llvm-toolset-7
%else
BuildRequires:  clang
%endif
BuildRequires: 	cmake3
BuildRequires: 	gcc
BuildRequires: 	make
BuildRequires: 	nodejs-packaging
BuildRequires: 	perl-interpreter
BuildRequires: 	perl(Module::Load::Conditional), perl(File::Temp)
BuildRequires: 	zlib-devel
#AutoReqProv: no
%define revision %{release}
%define prefix /usr/local

%global debug_package %{nil}

%description
Pipy is a tiny, high performance, highly stable, programmable proxy.

%prep
%setup -c -q -n %{name}-%{version} -T -a0

%build
rm -fr pipy/build
%{__mkdir} pipy/build
cd pipy
if [ $PIPY_GUI == "ON" ] ; then
  npm install
  npm run build
fi
cd build
CXX=clang++ CC=clang cmake3 -DPIPY_GUI=${PIPY_GUI} -DPIPY_STATIC=${PIPY_STATIC} -DPIPY_SAMPLES=${PIPY_GUI} -DCMAKE_BUILD_TYPE=${BUILD_TYPE} ..
make -j$(getconf _NPROCESSORS_ONLN)

%preun
if [ $1 -eq 0 ] ; then
        # Package removal, not upgrade 
        systemctl --no-reload disable pipy.service > /dev/null 2>&1 || true
        systemctl stop pipy.service > /dev/null 2>&1 || true
fi


%pre
getent group pipy >/dev/null || groupadd -r pipy
getent passwd pipy >/dev/null || useradd -r -g pipy -G pipy -d /etc/pipy -s /sbin/nologin -c "pipy" pipy


%install
mkdir -p %{buildroot}%{prefix}/bin
mkdir -p %{buildroot}/etc/pipy
cp pipy/bin/pipy %{buildroot}%{prefix}/bin
chrpath --delete %{buildroot}%{prefix}/bin/pipy

%post

%postun
if [ $1 -eq 0 ] ; then
        userdel pipy 2> /dev/null || true
fi

%clean
rm -rf %{buildroot}

%files
%attr(755, pipy, pipy) %{prefix}/bin/pipy

%changelog
