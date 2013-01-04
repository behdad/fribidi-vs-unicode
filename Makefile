MODULES = fribidi glib-2.0
CFLAGS= `pkg-config --cflags $(MODULES)`
LDFLAGS= `pkg-config --libs $(MODULES)`

all: test

%: %.c
	$(CC) $(CFLAGS) $< $(LDFLAGS) -o $@

clean:
	rm -f test
