objs := $(patsubst %.c,bin/%.o,$(wildcard *.c))
headers := $(wildcard *.h)

tests := $(patsubst test/%.c,bin/test/%,$(wildcard test/*.c))

CFLAGS=-g -fno-common -Wall -Wextra -Wno-switch -Wno-unused-function -Werror=return-type

bin/compiler: $(objs)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

bin/%.o: %.c $(headers) | bin/
	$(CC) $(CFLAGS) -c $< -o $@

bin/test/%: test/%.c bin/compiler | bin/test
	./bin/compiler -o bin/test/$*.s test/$*.c
	$(CC) bin/test/$*.s -xc test/assert -o $@

.PHONY:
test: $(tests)

bin/:
	mkdir -p bin

bin/test: bin/
	mkdir -p bin/test

clean:
	rm -rf bin core.* a.out tmp*
