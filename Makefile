CC ?= cc
CFLAGS ?= -O2 -std=c99 -Wall -Wextra -Wpedantic
LDLIBS ?= -lm -lxcb

SRCS = pestinha.c events.c world.c behavior.c homeostasis.c conditioning.c memory.c neuro.c display_xcb.c body.c fx.c gfx.c xutil.c ui.c sensors.c
HDRS = events.h world.h behavior.h homeostasis.h conditioning.h memory.h neuro.h display_xcb.h body.h fx.h gfx.h xutil.h ui.h sensors.h network_data.h runtime_types.h

all: pestinha

pestinha: $(SRCS) $(HDRS)
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(LDLIBS)

test_body: tests/test_body.c body.c body.h
	$(CC) $(CFLAGS) -o $@ tests/test_body.c body.c -lm

test_fx: tests/test_fx.c fx.c gfx.c xutil.c body.c fx.h gfx.h xutil.h body.h
	$(CC) $(CFLAGS) -o $@ tests/test_fx.c fx.c gfx.c xutil.c body.c $(LDLIBS)

test_display: tests/test_display.c display_xcb.c xutil.c gfx.c display_xcb.h xutil.h gfx.h body.h
	$(CC) $(CFLAGS) -o $@ tests/test_display.c display_xcb.c xutil.c gfx.c $(LDLIBS)

test_sensors: tests/test_sensors.c sensors.c sensors.h
	$(CC) $(CFLAGS) -o $@ tests/test_sensors.c sensors.c $(LDLIBS)

test: test_body test_fx test_display test_sensors
	@echo "=== [1/4] Teste da Física e do Piloto (body) ==="
	./test_body
	@echo "=== [2/4] Teste dos Efeitos Visuais (fx) ==="
	./test_fx
	@echo "=== [3/4] Teste do Display Desktop (display_xcb) ==="
	./test_display 1 0 2
	@echo "=== [4/4] Teste dos Sensores Ambientais (sensors) ==="
	./test_sensors
	@echo "=== TODOS OS TESTES PASSARAM COM SUCESSO! ==="

demo: pestinha
	./pestinha --demo

evaluate: pestinha
	./pestinha --evaluate

metrics: pestinha
	./pestinha --metrics

clean:
	rm -f pestinha test_body test_fx test_display test_sensors *.o /tmp/test_body /tmp/test_fx /tmp/test_display /tmp/test_sensors

.PHONY: all test demo evaluate metrics clean
