# Nome do executável final
TARGET = meu_pet

# Compilador e flags
CC = gcc
CFLAGS = -Wall -Wextra -O2 $(shell sdl2-config --cflags)
LDFLAGS = $(shell sdl2-config --libs)

# Encontra automaticamente todos os arquivos .c na pasta atual
SRCS = $(wildcard *.c)
OBJS = $(SRCS:.c=.o)

# Regra principal (padrão)
all: $(TARGET)

# Linkagem do executável
$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LDFLAGS)
	@echo "Build concluída com sucesso: $(TARGET)"

# Compilação dos arquivos objeto (.o) e rastreamento automático de headers (.h)
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Limpeza dos arquivos de compilação
clean:
	rm -f $(OBJS) $(TARGET)

# Executa o programa rapidamente
run: all
	./$(TARGET)

.PHONY: all clean run
