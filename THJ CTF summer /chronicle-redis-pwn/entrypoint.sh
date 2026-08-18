#!/bin/sh
set -eu

: "${FLAG:?Set FLAG before starting the challenge container}"
umask 077
printf '%s\n' "$FLAG" > /tmp/.chronicle-anchor
unset FLAG

redis-server /opt/chronicle/redis.conf &
redis_pid=$!

stop() {
    kill -TERM "$redis_pid" 2>/dev/null || true
    wait "$redis_pid" 2>/dev/null || true
}

trap stop INT TERM

for _ in $(seq 1 100); do
    if redis-cli -p 6379 PING >/dev/null 2>&1; then
        break
    fi
    sleep 0.05
done

redis-cli -p 6379 PING >/dev/null
/opt/chronicle/seed/bootstrap.sh

wait "$redis_pid"
