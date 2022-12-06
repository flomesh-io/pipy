FROM centos:7
LABEL maintainer="Khas-flomesh<khas@flomesh.io>"

ARG VERSION
ENV VERSION=${VERSION:-latest}

ARG REVISION
ENV REVISION=${REVISION:-0}

ARG COMMIT_ID
ENV CI_COMMIT_SHA=${COMMIT_ID}

ARG COMMIT_DATE
ENV CI_COMMIT_DATE=${COMMIT_DATE}


ARG PIPY_GUI
ENV PIPY_GUI=${PIPY_GUI:-OFF}

ARG PIPY_STATIC
ENV PIPY_STATIC=${PIPY_STATIC:-OFF}

ARG BUILD_TYPE
ENV BUILD_TYPE=${BUILD_TYPE:-Release}

ENV pkg_prefix     /usr/local
ENV pkg_confdir	   /etc/pipy

COPY rpmmacros /rpmmacros
COPY pipy.tar.gz /pipy.tar.gz
COPY pipy.spec /pipy.spec

RUN yum install -y -q gcc make unzip rpm-build rpmdevtools chrpath expat-devel zlib-devel \
    && rpmdev-setuptree \
    && mv /rpmmacros ~/.rpmmacros \
    && curl -sL https://rpm.nodesource.com/setup_14.x | bash - \
    && yum install -y -q nodejs \
    && yum install -y centos-release-scl epel-release \
    && yum install -y llvm-toolset-7 cmake3

RUN mv /pipy.tar.gz ~/rpmbuild/SOURCES \
    && mv /pipy.spec ~/rpmbuild/SPECS \
    && spectool -g -R ~/rpmbuild/SPECS/pipy.spec

RUN if test "$PIPY_GUI" = ON ; then   \
      rpmbuild_opts=' --with gui';    \
    else                              \
      rpmbuild_opts=' --without gui'; \
    fi;                               \
       export CI_COMMIT_SHA \
    && export CI_COMMIT_TAG=${VERSION}-${REVISION} \
    && export CI_COMMIT_DATE \
    && export PIPY_STATIC \
    && export BUILD_TYPE \
    && PATH="/opt/rh/llvm-toolset-7/root/usr/bin:/opt/rh/llvm-toolset-7/root/usr/sbin${PATH:+:${PATH}}" \
    && rpmbuild $rpmbuild_opts -ba ~/rpmbuild/SPECS/pipy.spec \
    && mkdir /rpm \
    && find ~/rpmbuild/RPMS -name pipy*.rpm -exec mv {} /rpm/ \;

COPY docker-entrypoint.sh /docker-entrypoint.sh

ENTRYPOINT ["/docker-entrypoint.sh"]
