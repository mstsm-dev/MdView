# MdView — MSYS2 UCRT64 用 Makefile
#
#   make            リリースビルド（dist/MdView.exe）
#   make DEBUG=1    デバッグ情報付き・最適化なし
#   make dist       EXE と INI を dist/ にそろえる
#   make check      Markdown 変換の自己テスト（ホストの gcc でビルドして samples/ を変換）
#   make clean      生成物を削除
#
# クロスコンパイル例: make CROSS=x86_64-w64-mingw32-

CROSS   ?=
CC      := $(CROSS)gcc
WINDRES := $(CROSS)windres
HOSTCC  ?= gcc

APP     := MdView
INI     := $(APP).ini
TARGET  := dist/$(APP).exe

SOURCES := $(wildcard src/*.c)
OBJECTS := $(patsubst src/%.c,build/%.o,$(SOURCES))
RES_OBJ := build/resource.o
DEPS    := $(OBJECTS:.o=.d)

# Markdown 変換（Windows 非依存）。make check でホスト向けにもビルドする
MD_SOURCES := src/md2rtf.c src/md_block.c src/md_inline.c

DEFINES  := -DUNICODE -D_UNICODE -DCOBJMACROS -DWIN32_LEAN_AND_MEAN -D_WIN32_WINNT=0x0601
WARNINGS := -Wall -Wextra -Wno-unused-parameter -Wshadow -Wstrict-prototypes
CFLAGS   := -std=c11 $(WARNINGS) $(DEFINES) -Iinclude -MMD -MP
LDFLAGS  := -municode -mwindows -static
LDLIBS   := -luser32 -lgdi32 -lcomdlg32 -lcomctl32 -lshell32 -lshlwapi -lole32 -luuid -ldwmapi

ifeq ($(DEBUG),1)
  CFLAGS += -O0 -g
else
  CFLAGS  += -O2
  LDFLAGS += -s
endif

.PHONY: all dist check clean

all: $(TARGET)

# -municode が必要：エントリーポイントが wWinMain のため
$(TARGET): $(OBJECTS) $(RES_OBJ) | dist-dir
	$(CC) $(OBJECTS) $(RES_OBJ) $(LDFLAGS) $(LDLIBS) -o $@

build/%.o: src/%.c | build-dir
	$(CC) $(CFLAGS) -c $< -o $@

# アイコン・マニフェスト・初期表示の文書・版数を変えたときもリソースを作り直す
$(RES_OBJ): res/resource.rc res/$(APP).ico res/manifest.xml res/welcome.md include/resource.h include/version.h | build-dir
	$(WINDRES) --codepage=65001 $(DEFINES) -Iinclude -Ires -i $< -o $@

# Markdown 変換の自己テスト：同梱の Markdown（文書・サンプル・初期表示）を変換し、RTF の波かっこの対応を検査する
build/md2rtf_check: tests/md2rtf_check.c $(MD_SOURCES) include/md2rtf.h include/md_internal.h | build-dir
	$(HOSTCC) -std=c11 $(WARNINGS) -O1 -Iinclude tests/md2rtf_check.c $(MD_SOURCES) -o $@

check: build/md2rtf_check
	./build/md2rtf_check *.md samples/*.md res/welcome.md

dist: $(TARGET)
	cp $(INI) dist/

build-dir:
	@mkdir -p build

dist-dir:
	@mkdir -p dist

clean:
	rm -rf build dist

-include $(DEPS)
