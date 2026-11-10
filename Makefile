
CC = gcc
CFLAGS = -O2 -std=c99 -Wall -Wextra -Wpedantic
LDLIBS = -lm -lxcb

TARGET = certinho

SRCS = pestinha.c events.c world.c behavior.c homeostasis.c conditioning.c memory.c neuro.c display_xcb.c body.c fx.c gfx.c xutil.c ui.c sensors.c
HDRS = events.h world.h behavior.h homeostasis.h conditioning.h memory.h neuro.h display_xcb.h body.h fx.h gfx.h xutil.h ui.h sensors.h

all: $(TARGET)

$(TARGET): $(SRCS) $(HDRS)
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(LDLIBS)

test_body: tests/test_body.c body.c body.h
	$(CC) $(CFLAGS) -o $@ tests/test_body.c body.c -lm

test_fx: tests/test_fx.c fx.c gfx.c xutil.c body.c fx.h gfx.h xutil.h body.h
	$(CC) $(CFLAGS) -o $@ tests/test_fx.c fx.c gfx.c xutil.c body.c $(LDLIBS)

test_display: tests/test_display.c display_xcb.c xutil.c gfx.c display_xcb.h xutil.h gfx.h
	$(CC) $(CFLAGS) -o $@ tests/test_display.c display_xcb.c xutil.c gfx.c $(LDLIBS)

test_sensors: tests/test_sensors.c sensors.c sensors.h
	$(CC) $(CFLAGS) -o $@ tests/test_sensors.c sensors.c $(LDLIBS)

test: test_body test_fx test_display test_sensors
	./test_body
	./test_fx
	./test_display 1 0 2
	./test_sensors
	@echo "Todos os testes passaram!"

demo: $(TARGET)
	./$(TARGET) --demo

evaluate: $(TARGET)
	./$(TARGET) --evaluate

metrics: $(TARGET)
	./$(TARGET) --metrics

clean:
	rm -f $(TARGET) test_body test_fx test_display test_sensors *.o

.PHONY: all test demo evaluate metrics clean

