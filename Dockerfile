FROM alpine:3.12 as builder
LABEL maintainer="Kevein Liu<khas@flomesh.io>"

ENV  pkg_prefix              /usr/local
ENV  pkg_confdir	     /etc/pipy
ENV  pkg_bindir              ${pkg_prefix}/bin
ENV  CXX       		     clang++
ENV  CC			     clang

COPY . /pipy

RUN apk add --no-cache --virtual .build-deps openssh-client git cmake clang alpine-sdk linux-headers zlib zlib-dev

RUN rm -fr pipy/build \
        && mkdir pipy/build \
        && cd pipy/gui \
        && npm install \
        && npm run build \
        && cd ../build \
        && cmake -DPIPY_GUI=ON -DCMAKE_BUILD_TYPE=Release .. \
        && make -j$(getconf _NPROCESSORS_ONLN) \
        && mkdir ${pkg_confdir} \
        && cp /pipy/bin/pipy ${pkg_bindir} \
        && cp -r /pipy/test ${pkg_confdir} \
	&& apk del .build-deps


FROM alpine:3.12 as prod
COPY --from=builder /pipy/bin/pipy /usr/local/bin/pipy
COPY --from=builder /etc/pipy /etc/pipy
RUN apk add --no-cache ca-certificates libstdc++ libcap su-exec tar curl busybox-extras iptables tzdata socat logrotate
RUN adduser -Su 1340 pipy \
    && setcap cap\_net\_admin=eip /usr/local/bin/pipy \
    && chmod -R g=u /usr/local/bin/pipy /etc/pipy \
    && chown -R pipy:0 /usr/local/bin/pipy /etc/pipy 

COPY docker-entrypoint.sh /docker-entrypoint.sh

USER pipy
EXPOSE 6000
STOPSIGNAL SIGQUIT

ENTRYPOINT ["/docker-entrypoint.sh"]
CMD ["pipy", "docker-start"]
