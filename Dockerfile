FROM alpine:3.16.1 as builder
LABEL maintainer="Kevein Liu<khas@flomesh.io>"

ENV  pkg_prefix              /usr/local
ENV  pkg_confdir	     /etc/pipy
ENV  pkg_bindir              ${pkg_prefix}/bin
ENV  CXX       		     clang++
ENV  CC			     clang

ARG VERSION
ENV VERSION=${VERSION}

ARG REVISION
ENV REVISION=${REVISION}

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

COPY . /pipy

RUN apk add --no-cache --virtual .build-deps openssh-client cmake clang \
    alpine-sdk linux-headers autoconf automake libtool tiff jpeg zlib \
    zlib-dev pkgconf nasm file musl-dev

RUN if [ "$PIPY_GUI" == "ON" ] ; then apk add --no-cache nodejs npm && cd pipy && npm install && npm run build; fi

RUN rm -fr pipy/build \
    && mkdir pipy/build \
    && cd pipy/build \
    && export CI_COMMIT_SHA \
    && export CI_COMMIT_TAG=${VERSION}-${REVISION} \
    && export CI_COMMIT_DATE \
    && cmake -DPIPY_GUI=${PIPY_GUI} -DPIPY_STATIC=${PIPY_STATIC} -DPIPY_CODEBASES=${PIPY_GUI} -DCMAKE_BUILD_TYPE=${BUILD_TYPE} .. \
    && make -j$(getconf _NPROCESSORS_ONLN) \
    && mkdir ${pkg_confdir} \
    && cp /pipy/bin/pipy ${pkg_bindir} \
    && apk del .build-deps


FROM alpine:3.16.1 as prod
COPY --from=builder /pipy/bin/pipy /usr/local/bin/pipy
COPY --from=builder /pipy/tutorial /etc/pipy/tutorial
COPY tools/{wait,gkill} /usr/local/bin
RUN apk add --no-cache ca-certificates libstdc++ libcap su-exec tar curl busybox-extras iptables tzdata socat logrotate 
RUN adduser -Su 1340 pipy \
    && chmod -R g=u /usr/local/bin/pipy /etc/pipy \
    && chown -R pipy:0 /usr/local/bin/pipy /etc/pipy 

COPY docker-entrypoint.sh /docker-entrypoint.sh

USER pipy
EXPOSE 6000
STOPSIGNAL SIGQUIT

ENTRYPOINT ["/docker-entrypoint.sh"]
CMD ["pipy", "docker-start"]
