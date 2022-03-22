# syntax=docker/dockerfile:1.3
FROM alpine:3.12 as builder
LABEL maintainer="Kevein Liu<khas@flomesh.io>"

ARG RELEASE_VERSION
ENV RELEASE_VERSION=${RELEASE_VERSION:-0.4.0}

ARG REVISION
ENV REVISION=${REVISION:-1}

ARG COMMIT_SHA
ENV CI_COMMIT_SHA=${COMMIT_SHA}

ARG COMMIT_DATE
ENV CI_COMMIT_DATE=${COMMIT_DATE}


ENV  pkg_prefix              /usr/local
ENV  pkg_confdir	     /etc/pipy
ENV  pkg_bindir              ${pkg_prefix}/bin
ENV  CXX       		     clang++
ENV  CC			     clang

ENV NODE_VER=12.22.10-r0

RUN apk add --no-cache openssh-client git cmake clang alpine-sdk \
        linux-headers autoconf automake libtool tiff jpeg zlib zlib-dev \
        pkgconf nasm file musl-dev \
        nodejs=${NODE_VER} npm=${NODE_VER}

RUN --mount=type=bind,rw,target=/pipy rm -fr pipy/build \
        && mkdir pipy/build \
        && export CI_COMMIT_SHA \
        && export CI_COMMIT_TAG=${RELEASE_VERSION}-${REVISION} \
        && export CI_COMMIT_DATE \
        && cd pipy \
        && npm install \
        && npm run build \
        && cd build \
        && cmake -DPIPY_TUTORIAL=ON -DPIPY_GUI=OFF -DCMAKE_BUILD_TYPE=Release .. \
        && make -j${__NPROC} \
        && mkdir ${pkg_confdir} \
        && cp /pipy/bin/pipy ${pkg_bindir} \
        && cp -r /pipy/test ${pkg_confdir} \
        && mkdir -p /build/${pkg_bindir} /build/${pkg_confdir} \
        && cp -r ${pkg_confdir} /build/${pkg_confdir} \
        && cp -r ${pkg_bindir}/pipy /build/${pkg_bindir} \
        && cp -a /pipy/docker-entrypoint.sh /build/

ENTRYPOINT ["/docker-entrypoint.sh"]

FROM alpine:3.12 as prod
LABEL maintainer="Kevein Liu<khas@flomesh.io>"

ARG RELEASE_VERSION
ENV RELEASE_VERSION=${RELEASE_VERSION:-0.4.0}

ARG REVISION
ENV REVISION=${REVISION:-1}

COPY --from=builder /build/ /

RUN adduser -Su 1340 pipy \
	&& apk add --no-cache  libstdc++ su-exec tar curl busybox-extras iptables tzdata socat logrotate libcap \
    && setcap cap\_net\_admin=eip /usr/local/bin/pipy \
	&& chown -R pipy:0 /etc/pipy \
	&& chown -R pipy:0 /usr/local/bin/pipy \
	&& chmod -R g=u /etc/pipy

USER pipy
EXPOSE 6000
STOPSIGNAL SIGQUIT

ENTRYPOINT ["/docker-entrypoint.sh"]
CMD ["pipy", "docker-start"]
