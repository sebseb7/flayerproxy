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

PACKET_SRC = src/packets/custom_payload.c src/packets/feature_flags.c src/packets/select_known_packs.c \
	src/packets/finish_configuration.c src/packets/bundle_delimiter.c src/packets/step_tick.c \
	src/packets/success.c src/packets/advancements.c
SRC = src/buf.c src/nbt.c src/metadata.c src/slot.c src/slot_fprint.c src/packets.c $(PACKET_SRC) src/c2s_move.c src/debug.c src/chunk.c src/dump_json.c src/decode_wire.c src/play_stream.c src/play_wire_extra.c src/block_entity.c src/map_colors.c src/state_map_color.c src/state_block_name.c src/state_top_rgb.c src/biome_tint.c src/map_png.c src/map_avif.c src/map_tile.c src/packets_write.c src/update_tags.c src/mc_wire.c src/mc_static_grass.c src/mc_static_config.c src/mc_registry_join_template.c src/mc_static_registries.c src/mc_static_registries_data.c
CFLAGS += $(AVIF_CFLAGS)
OBJ = $(SRC:src/%.c=build/%.o)

.PHONY: all clean test install show_decode decode_raw_dir summarize_raw_dir list_map_surface stitch_megatiles chunk_stream_receiver mc_static_server mc_reference_client compare_join_capture

all: build/libchunk.a build/show_decode build/decode_raw_dir build/summarize_raw_dir build/list_map_surface build/stitch_megatiles build/chunk_stream_receiver build/mc_static_server build/mc_reference_client build/compare_join_capture

build:
	mkdir -p build build/packets

src/state_map_color.c src/state_block_name.c: ../scripts/generate-state-map-colors.js | build
	$(GEN)

src/state_top_rgb.c: ../scripts/generate-state-top-texture-colors.js | build
	$(GEN_TOP)

src/biome_tint.c: ../scripts/generate-biome-tint-colors.js | build
	$(GEN_BIOME)

build/%.o: src/%.c include/libchunk.h src/internal.h | build
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -Iinclude -c $< -o $@

build/libchunk.a: $(OBJ)
	ar rcs $@ $(OBJ)

build/show_decode: examples/show_decode.c build/libchunk.a | build
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ examples/show_decode.c -Lbuild $(CHUNK_LIBS)

build/decode_raw_dir: examples/decode_raw_dir.c build/libchunk.a | build
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ examples/decode_raw_dir.c -Lbuild $(CHUNK_LIBS)

build/summarize_raw_dir: examples/summarize_raw_dir.c build/libchunk.a | build
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ examples/summarize_raw_dir.c -Lbuild $(CHUNK_LIBS)

build/list_map_surface: examples/list_map_surface.c build/libchunk.a | build
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ examples/list_map_surface.c -Lbuild $(CHUNK_LIBS)

build/stitch_megatiles: examples/stitch_megatiles.c build/libchunk.a | build
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ examples/stitch_megatiles.c -Lbuild $(CHUNK_LIBS) -pthread

MC_SPECTATOR_SRC = src/mc_log.c src/mc_conn_state.c src/mc_upstream_bridge.c src/mc_wire.c src/mc_spectator.c src/mc_server_common.c src/mc_auth_offline.c src/mc_wire_templates.c src/mc_s2c_log.c src/mc_play_s2c_names.c src/mc_static_grass.c src/mc_static_config.c src/mc_registry_join_template.c src/mc_static_registries.c src/mc_static_registries_data.c src/mc_registry_capture.c src/mc_chunk_stream.c
MC_STATIC_SRC = src/mc_log.c src/mc_conn_state.c src/mc_upstream_bridge.c src/mc_wire.c src/mc_server_common.c src/mc_auth_offline.c src/mc_wire_templates.c src/mc_static_grass.c src/mc_static_config.c src/mc_registry_join_template.c src/mc_static_registries.c src/mc_static_registries_data.c src/mc_registry_capture.c src/mc_c2s_log.c src/mc_s2c_log.c src/mc_play_s2c_names.c src/mc_chunk_stream.c src/mc_static_server.c

build/mc_static_server: examples/mc_static_server.c build/libchunk.a $(MC_STATIC_SRC) $(MC_REGISTRY_FETCH_SRC) | build
	$(CC) $(CFLAGS) $(MC_CLIENT_CFLAGS) $(INCLUDES) -Isrc -o $@ examples/mc_static_server.c $(MC_STATIC_SRC) $(MC_REGISTRY_FETCH_SRC) -Lbuild $(CHUNK_LIBS) -lm -pthread $(MC_CLIENT_LIBS)

MC_CLIENT_SRC = src/mc_wire.c src/mc_server_common.c src/mc_log.c src/mc_dns.c src/mc_conn.c src/mc_online.c src/mc_play_s2c_names.c
MC_REGISTRY_FETCH_SRC = src/mc_dns.c src/mc_conn.c src/mc_online.c
MC_CLIENT_LIBS = -lz -lresolv -l:libssl.so.3 -l:libcrypto.so.3
# Node ships OpenSSL headers on some distros when libssl-dev is absent
MC_CLIENT_CFLAGS := $(shell [ -f /usr/include/openssl/evp.h ] && echo || echo -I/usr/include/node)

build/mc_reference_client: examples/mc_reference_client.c build/libchunk.a $(MC_CLIENT_SRC) | build
	$(CC) $(CFLAGS) $(MC_CLIENT_CFLAGS) $(INCLUDES) -Isrc -o $@ examples/mc_reference_client.c $(MC_CLIENT_SRC) -Lbuild $(CHUNK_LIBS) $(MC_CLIENT_LIBS)

build/compare_join_capture: examples/compare_join_capture.c build/libchunk.a | build
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ examples/compare_join_capture.c -Lbuild $(CHUNK_LIBS)

build/chunk_stream_receiver: examples/chunk_stream_receiver.c build/libchunk.a $(MC_SPECTATOR_SRC) $(MC_REGISTRY_FETCH_SRC) | build
	$(CC) $(CFLAGS) $(MC_CLIENT_CFLAGS) $(INCLUDES) -Isrc -o $@ examples/chunk_stream_receiver.c $(MC_SPECTATOR_SRC) $(MC_REGISTRY_FETCH_SRC) -Lbuild $(CHUNK_LIBS) -lm -pthread $(MC_CLIENT_LIBS)

build/test_packets: test/test_packets.c build/libchunk.a | build
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ test/test_packets.c -Lbuild $(CHUNK_LIBS)

show_decode: build/show_decode

decode_raw_dir: build/decode_raw_dir

summarize_raw_dir: build/summarize_raw_dir

list_map_surface: build/list_map_surface

stitch_megatiles: build/stitch_megatiles

chunk_stream_receiver: build/chunk_stream_receiver

mc_static_server: build/mc_static_server

mc_reference_client: build/mc_reference_client

compare_join_capture: build/compare_join_capture

test: build/test_packets
	./build/test_packets

install: build/libchunk.a include/libchunk.h
	install -d $(DESTDIR)/usr/local/lib $(DESTDIR)/usr/local/include
	install -m 644 build/libchunk.a $(DESTDIR)/usr/local/lib/
	install -m 644 include/libchunk.h $(DESTDIR)/usr/local/include/

clean:
	rm -rf build
