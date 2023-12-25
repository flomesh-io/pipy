%bcond_with gui

# optionally fetch the version number from environmental variable of "VERSION"
# and "REVISION" respectively
%{!?pipy_version: %global pipy_version latest}
%{!?pipy_revision: %global pipy_revision 0}

Name:		pipy
Version: 	%pipy_version
Release: 	%pipy_revision%{?dist}

Summary: 	Pipy is a programmable network proxy for the cloud, edge and IoT.

License: 	NEU License
Source0: 	pipy.tar.gz
BuildRoot:      %{_tmppath}/%{name}-%{version}-%{revision}-root-%(%{__id_u} -n)
BuildRequires: 	/usr/bin/chrpath
BuildRequires: 	autoconf
BuildRequires: 	automake
%if 0%{?rhel} >= 8 || 0%{?fedora}
BuildRequires: 	clang
%else
BuildRequires: 	llvm-toolset-7.0-clang
%endif
BuildRequires: 	cmake3
BuildRequires: 	gcc
BuildRequires: 	make
%if %{with gui}
%if 0%{?rhel} >= 8 || 0%{?fedora}
BuildRequires: 	npm
%else
BuildRequires: 	rh-nodejs14-npm
%endif
%endif
BuildRequires: 	perl-interpreter, perl-IPC-Cmd
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
%if %{with gui}
%if 0%{?rhel} == 7
source /opt/rh/rh-nodejs14/enable
%endif
  npm install
  npm run build
%endif
%if 0%{?rhel} == 7
source /opt/rh/llvm-toolset-7.0/enable
PIPY_BPF=OFF
%endif
cd build
CXX=clang++ CC=clang cmake3 \
  -DPIPY_GUI=%{?with_gui:ON}%{!?with_gui:OFF} \
  -DPIPY_SAMPLES=%{?with_gui:ON}%{!?with_gui:OFF} \
  -DPIPY_STATIC=${PIPY_STATIC} \
  -DPIPY_BPF=${PIPY_BPF} \
  -DCMAKE_BUILD_TYPE=${BUILD_TYPE} ..
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
