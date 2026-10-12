# MdView - build with MSYS2 (MINGW64 / UCRT64 shell):  make
# Cross build from Linux:                               make CROSS=x86_64-w64-mingw32-

CROSS   ?=
CC      := $(CROSS)gcc
WINDRES := $(CROSS)windres

CFLAGS  ?= -O2
CFLAGS  += -std=gnu11 -Wall -Wextra -municode -DUNICODE -D_UNICODE
LDFLAGS += -mwindows -municode -static -s
LIBS    := -lcomctl32 -lcomdlg32 -lshlwapi -lshell32 -lgdi32 -luser32 -ladvapi32

OBJS    := build/main.o build/md2rtf.o build/app.res.o
TARGET  := mdview.exe

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

build/%.o: src/%.c src/md2rtf.h | build
	$(CC) $(CFLAGS) -c -o $@ $<

build/app.res.o: src/app.rc src/app.manifest src/mdview.ico | build
	$(WINDRES) -I src -O coff -o $@ $<

build:
	mkdir -p build

clean:
	rm -rf build $(TARGET)

.PHONY: all clean
