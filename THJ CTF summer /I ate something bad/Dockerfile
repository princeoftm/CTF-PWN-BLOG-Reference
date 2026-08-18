FROM debian:bookworm-slim

RUN apt-get update && \
    apt-get install -y --no-install-recommends socat && \
    rm -rf /var/lib/apt/lists/* && \
    useradd -m -u 1000 ctf

WORKDIR /home/ctf

COPY chal .
COPY flag.txt .
RUN chmod 755 chal && \
    chmod 444 flag.txt && \
    chown -R root:ctf /home/ctf

USER ctf
EXPOSE 1337

CMD ["socat", "TCP-LISTEN:1337,reuseaddr,fork,keepalive", "EXEC:./chal,stderr"]
