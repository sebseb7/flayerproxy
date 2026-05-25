CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -O2 -fPIC
INCLUDES = -Iinclude
GEN = node ../scripts/generate-state-map-colors.js
GEN_TOP = node ../scripts/generate-state-top-texture-colors.js
GEN_BIOME = node ../scripts/generate-biome-tint-colors.js
AVIF_CFLAGS ?= $(shell pkg-config --cflags libavif 2>/dev/null)
AVIF_LIBS ?= $(shell pkg-config --libs libavif 2>/dev/null)
ifeq ($(AVIF_LIBS),)
AVIF_LIBS = -lavif
endif
CHUNK_LIBS = -lchunk $(AVIF_LIBS)

SRC = src/buf.c src/nbt.c src/metadata.c src/slot.c src/slot_fprint.c src/packets.c src/debug.c src/chunk.c src/dump_json.c src/block_entity.c src/map_colors.c src/state_map_color.c src/state_block_name.c src/state_top_rgb.c src/biome_tint.c src/map_png.c src/map_avif.c src/map_tile.c
CFLAGS += $(AVIF_CFLAGS)
OBJ = $(SRC:src/%.c=build/%.o)

.PHONY: all clean test install show_decode decode_raw_dir summarize_raw_dir list_map_surface stitch_megatiles chunk_stream_receiver

all: build/libchunk.a build/show_decode build/decode_raw_dir build/summarize_raw_dir build/list_map_surface build/stitch_megatiles build/chunk_stream_receiver

build:
	mkdir -p build

src/state_map_color.c src/state_block_name.c: ../scripts/generate-state-map-colors.js | build
	$(GEN)

src/state_top_rgb.c: ../scripts/generate-state-top-texture-colors.js | build
	$(GEN_TOP)

src/biome_tint.c: ../scripts/generate-biome-tint-colors.js | build
	$(GEN_BIOME)

build/%.o: src/%.c include/libchunk.h src/internal.h | build
	$(CC) $(CFLAGS) $(INCLUDES) -Iinclude -c $< -o $@

build/libchunk.a: $(OBJ)
	ar rcs $@ $(OBJ)

DECODE_EXAMPLES = examples/decode_wire.c

build/show_decode: examples/show_decode.c $(DECODE_EXAMPLES) build/libchunk.a | build
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ examples/show_decode.c $(DECODE_EXAMPLES) -Lbuild $(CHUNK_LIBS)

build/decode_raw_dir: examples/decode_raw_dir.c $(DECODE_EXAMPLES) build/libchunk.a | build
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ examples/decode_raw_dir.c $(DECODE_EXAMPLES) -Lbuild $(CHUNK_LIBS)

build/summarize_raw_dir: examples/summarize_raw_dir.c build/libchunk.a | build
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ examples/summarize_raw_dir.c -Lbuild $(CHUNK_LIBS)

build/list_map_surface: examples/list_map_surface.c build/libchunk.a | build
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ examples/list_map_surface.c -Lbuild $(CHUNK_LIBS)

build/stitch_megatiles: examples/stitch_megatiles.c build/libchunk.a | build
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ examples/stitch_megatiles.c -Lbuild $(CHUNK_LIBS) -pthread

build/chunk_stream_receiver: examples/chunk_stream_receiver.c build/libchunk.a | build
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ examples/chunk_stream_receiver.c -Lbuild $(CHUNK_LIBS)

build/test_packets: test/test_packets.c build/libchunk.a | build
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ test/test_packets.c -Lbuild $(CHUNK_LIBS)

show_decode: build/show_decode

decode_raw_dir: build/decode_raw_dir

summarize_raw_dir: build/summarize_raw_dir

list_map_surface: build/list_map_surface

stitch_megatiles: build/stitch_megatiles

chunk_stream_receiver: build/chunk_stream_receiver

test: build/test_packets
	./build/test_packets

install: build/libchunk.a include/libchunk.h
	install -d $(DESTDIR)/usr/local/lib $(DESTDIR)/usr/local/include
	install -m 644 build/libchunk.a $(DESTDIR)/usr/local/lib/
	install -m 644 include/libchunk.h $(DESTDIR)/usr/local/include/

clean:
	rm -rf build
