FROM centos:7
LABEL maintainer="Kevein Liu<kevein@flomesh.cn>"

ARG VERSION
ENV VERSION=${VERSION:-0.6.0}

ARG REVISION
ENV REVISION=${REVISION:-1}

ARG COMMIT_ID
ENV CI_COMMIT_SHA=${COMMIT_ID}

ARG COMMIT_DATE
ENV CI_COMMIT_DATE=${COMMIT_DATE}

ENV pkg_prefix     /usr/local
ENV pkg_confdir	   /etc/pipy

COPY rpmmacros /rpmmacros
COPY pipy.tar.gz /pipy.tar.gz
COPY pipy.spec /pipy.spec

RUN yum install -y -q make unzip rpm-build rpmdevtools git chrpath expat-devel zlib-devel \
        && rpmdev-setuptree \
        && curl -sL https://rpm.nodesource.com/setup_12.x | bash - \
        && yum install -y -q nodejs \
        && mv /rpmmacros ~/.rpmmacros \
        && yum install -y centos-release-scl epel-release \
        && yum install -y llvm-toolset-7 cmake3 

RUN mv /pipy.tar.gz ~/rpmbuild/SOURCES \
	&& mv /pipy.spec ~/rpmbuild/SPECS \
	&& export CI_COMMIT_SHA \
	&& export CI_COMMIT_TAG=${VERSION}-${REVISION} \
        && export CI_COMMIT_DATE \
	&& spectool -g -R ~/rpmbuild/SPECS/pipy.spec \
	&& PATH="/opt/rh/llvm-toolset-7/root/usr/bin:/opt/rh/llvm-toolset-7/root/usr/sbin${PATH:+:${PATH}}" \
	&& rpmbuild -ba ~/rpmbuild/SPECS/pipy.spec \
        && mkdir /rpm \
	&& find ~/rpmbuild/RPMS -name pipy*.rpm -exec mv {} /rpm/ \;

COPY docker-entrypoint.sh /docker-entrypoint.sh

ENTRYPOINT ["/docker-entrypoint.sh"]
