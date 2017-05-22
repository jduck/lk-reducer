CHK_SOURCES = lk-reducer.c
CHK_SOURCES_C = $(filter %.c,$(CHK_SOURCES))
OUTPUT = lk-reducer

.PHONY: debug clean

all:
	gcc -std=gnu99 -O3 $(CHK_SOURCES_C) -o $(OUTPUT)

debug:
	gcc -Wall -std=gnu99 -ggdb3 -O0 $(CHK_SOURCES_C) -o $(OUTPUT)

clean:
	-rm -v $(OUTPUT)
