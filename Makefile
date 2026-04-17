objs := $(patsubst src/%.c,bin/%.o,$(wildcard src/*.c))
headers := $(wildcard src/*.h)

$(info objs = $(objs))
$(info headers = $(headers))
$(info )

CFLAGS=-g -fno-common -Wall -Wextra

bin/compiler: $(objs)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

bin/%.o: src/%.c $(headers) | bin/
	$(CC) $(CFLAGS) -c $< -o $@

bin/:
	mkdir -p bin

test: bin/compiler
	./test.sh

clean:
	rm -rf bin core.*
