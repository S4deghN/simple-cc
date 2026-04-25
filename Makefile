objs := $(patsubst %.c,bin/%.o,$(wildcard *.c))
headers := $(wildcard *.h)

$(info objs = $(objs))
$(info headers = $(headers))
$(info )

CFLAGS=-g -fno-common -Wall -Wextra -Wno-switch -Wno-unused-function -Werror=return-type

bin/compiler: $(objs)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

bin/%.o: %.c $(headers) | bin/
	$(CC) $(CFLAGS) -c $< -o $@

bin/:
	mkdir -p bin

test: bin/compiler
	./test.sh

clean:
	rm -rf bin core.* a.out tmp*
