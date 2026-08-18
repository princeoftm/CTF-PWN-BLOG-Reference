CC ?= gcc
CFLAGS := -O2 -fPIC -fstack-protector-strong -fno-omit-frame-pointer -Wall -Wextra -Werror -D_FORTIFY_SOURCE=0
LDFLAGS := -shared -Wl,-z,relro,-z,now,-z,noexecstack

chronicle.so: src/chronicle.c src/redismodule.h
	$(CC) $(CFLAGS) -Isrc -o $@ src/chronicle.c $(LDFLAGS)

clean:
	rm -f chronicle.so
