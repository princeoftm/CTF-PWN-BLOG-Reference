FROM ubuntu:24.04

RUN apt-get update && apt-get install -y --no-install-recommends socat && rm -rf /var/lib/apt/lists/*

RUN useradd -m -s /bin/bash necropet
WORKDIR /home/necropet

COPY necropet ./necropet
COPY libc.so.6 ./libc.so.6
COPY ld-linux-x86-64.so.2 ./ld-linux-x86-64.so.2
COPY thisisratratratrat_puipui.txt ./thisisratratratrat_puipui.txt

RUN chmod 755 ./necropet ./ld-linux-x86-64.so.2 && \
    chmod 644 ./libc.so.6 && \
    chmod 400 ./thisisratratratrat_puipui.txt && \
    chown necropet:necropet ./thisisratratratrat_puipui.txt

ENV NECROPET_FLAG_PATH=./thisisratratratrat_puipui.txt

USER necropet
EXPOSE 1024

CMD ["socat", "-T30", "TCP-LISTEN:1024,reuseaddr,fork", "EXEC:./ld-linux-x86-64.so.2 --library-path . ./necropet,pty,raw,echo=0,stderr"]