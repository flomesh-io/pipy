FROM alpine:3.12
LABEL maintainer="Kevein Liu<khas@flomesh.io>"

ENV  pkg_prefix              /usr/local
ENV  pkg_confdir	     /etc/pipy
ENV  pkg_bindir              ${pkg_prefix}/bin
ENV  CXX       		     clang++
ENV  CC			     clang 

COPY . /pipy

RUN apk add --no-cache --virtual .build-deps openssh-client git cmake clang alpine-sdk linux-headers

RUN rm -fr pipy/build \
        && mkdir pipy/build \
        && cd pipy/build \
        && cmake -DCMAKE_BUILD_TYPE=Release .. \
        && make -j$(getconf _NPROCESSORS_ONLN) \
        && mkdir ${pkg_confdir} \
        && cp /pipy/bin/pipy ${pkg_bindir} \
        && cp -r /pipy/test ${pkg_confdir} \
	&& apk del .build-deps

RUN rm -fr /pipy \
	&& adduser -Su 1340 pipy \
        && apk add --no-cache libcap libstdc++ su-exec tar curl busybox-extras iptables tzdata socat logrotate jq \
        && chown -R pipy:0 /usr/local/bin/pipy /etc/pipy \
        && chmod -R g=u /usr/local/bin/pipy /etc/pipy \
	&& setcap cap\_net\_admin=eip /usr/local/bin/pipy


COPY docker-entrypoint.sh /docker-entrypoint.sh

ENTRYPOINT ["/docker-entrypoint.sh"]

STOPSIGNAL SIGQUIT

EXPOSE 6000

CMD ["pipy", "docker-start"]


