FROM redis:7.2.15-bookworm AS build

RUN apt-get update \
    && apt-get install -y --no-install-recommends build-essential make \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build
COPY Makefile ./
COPY src ./src
RUN make

FROM redis:7.2.15-bookworm

COPY --from=build /build/chronicle.so /opt/chronicle/chronicle.so
COPY redis.conf /opt/chronicle/redis.conf
COPY entrypoint.sh /opt/chronicle/entrypoint.sh
COPY seed /opt/chronicle/seed

RUN chmod 0555 /opt/chronicle/chronicle.so \
    && chmod 0755 /opt/chronicle/entrypoint.sh /opt/chronicle/seed/bootstrap.sh

EXPOSE 6379
ENTRYPOINT ["/opt/chronicle/entrypoint.sh"]
