#include <napi.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

extern "C" {
#include "decode_wire.h"
#include "libchunk.h"
}

namespace {

constexpr size_t kInitialBuf = 65536;
constexpr size_t kMaxBuf = 16 * 1024 * 1024;

Napi::Value SupportedPackets(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  static const char *names[] = {
      "map_chunk",           "unload_chunk",
      "update_light",        "block_change",
      "tile_entity_data",    "multi_block_change",
      "spawn_entity",        "entity_metadata",
      "entity_equipment",    "entity_destroy",
      "set_passengers",      "rel_entity_move",
      "entity_move_look",    "entity_look",
      "sync_entity_position",
      "entity_velocity",     "entity_head_rotation",
      "entity_update_attributes",
      "entity_teleport",     "entity_effect",
      "remove_entity_effect",
      "position",            "c2s_position",
      "c2s_position_look",   "c2s_look",
      "c2s_flying",          "c2s_teleport_confirm",
      "respawn",
      "initialize_world_border",
      "registry_data",
      "login",               "update_health",
      "experience",          "abilities",
      "entity_status",       "spawn_position",
      "difficulty",          "game_state_change",
      "window_items",        "set_slot",
      "held_item_slot",      "set_player_inventory",
      "set_cursor_item",     "update_time",
      "chunk_batch_start",   "chunk_batch_finished",
      "world_border_center", "world_border_size",
      "world_border_lerp_size",
      "world_border_warning_delay",
      "world_border_warning_reach",
      "simulation_distance", "update_view_distance",
      "update_view_position", "declare_commands",
      "player_info",         "player_remove",
      "playerlist_header",   "scoreboard_objective",
      "scoreboard_display_objective",
      "scoreboard_score",    "reset_score",
      "teams",               "boss_bar",
      "tracked_waypoint",    "tags",
      "server_data",         "update_recipes",
      "declare_recipes",     "advancements",
      "recipe_book_add",     "recipe_book_settings",
      nullptr,
  };
  Napi::Array arr = Napi::Array::New(env);
  uint32_t i = 0;
  for (; names[i]; i++) {
    arr.Set(i, Napi::String::New(env, names[i]));
  }
  return arr;
}

Napi::Object MakeResult(Napi::Env env, int rc, const std::string &text, const char *err = nullptr) {
  Napi::Object o = Napi::Object::New(env);
  if (rc == 1) {
    o.Set("ok", Napi::Boolean::New(env, true));
    o.Set("text", Napi::String::New(env, text));
  } else if (rc == 0) {
    o.Set("ok", Napi::Boolean::New(env, false));
    o.Set("unsupported", Napi::Boolean::New(env, true));
    o.Set("error", Napi::String::New(env, err ? err : "unsupported packet"));
  } else {
    o.Set("ok", Napi::Boolean::New(env, false));
    o.Set("error", Napi::String::New(env, err ? err : "parse error"));
  }
  return o;
}

int DecodeToString(const char *name, const uint8_t *wire, size_t wire_len, std::string *out) {
  out->assign(kInitialBuf, '\0');
  for (;;) {
    int rc = lc_decode_wire_to_string(name, wire, wire_len, out->data(), out->size());
    if (rc == 1) {
      out->resize(strlen(out->c_str()));
      return 1;
    }
    if (rc != -1) return rc;
    if (out->size() >= kMaxBuf) return -1;
    size_t next = out->size() * 2;
    if (next > kMaxBuf) next = kMaxBuf;
    out->assign(next, '\0');
  }
}

#if defined(__GLIBC__)
static int DecodeMapChunkJsonMem(const char *basename, const uint8_t *wire, size_t wire_len,
                                 std::string *out) {
  char *buf = nullptr;
  size_t len = 0;
  FILE *f = open_memstream(&buf, &len);
  if (!f) return -1;
  int rc = lc_decode_wire_map_chunk_json("map_chunk", basename, wire, wire_len, f);
  fclose(f);
  if (rc != 1) {
    free(buf);
    return rc;
  }
  out->assign(buf, len);
  free(buf);
  return 1;
}
#endif

static int DecodeMapChunkJsonGrow(const char *basename, const uint8_t *wire, size_t wire_len,
                                  std::string *out) {
#if defined(__GLIBC__)
  int rc = DecodeMapChunkJsonMem(basename, wire, wire_len, out);
  if (rc == 1) return 1;
  if (rc == 0) return 0;
#endif
  out->assign(kInitialBuf, '\0');
  for (;;) {
    FILE *f = fmemopen(out->data(), out->size(), "w+");
    if (!f) return -1;
    int rc = lc_decode_wire_map_chunk_json("map_chunk", basename, wire, wire_len, f);
    long pos = ftell(f);
    fclose(f);
    if (rc == 1 && pos > 0) {
      out->resize((size_t)pos);
      return 1;
    }
    if (rc != -1) return rc;
    if (out->size() >= kMaxBuf) return -1;
    out->assign(out->size() * 2, '\0');
  }
}

Napi::Value DecodeWire(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  if (info.Length() < 2 || !info[0].IsString() || !info[1].IsBuffer()) {
    Napi::TypeError::New(env, "decodeWire(packetName, buffer)").ThrowAsJavaScriptException();
    return env.Null();
  }
  std::string name = info[0].As<Napi::String>().Utf8Value();
  Napi::Buffer<uint8_t> buf = info[1].As<Napi::Buffer<uint8_t>>();
  std::string text;
  int rc = DecodeToString(name.c_str(), buf.Data(), buf.Length(), &text);
  return MakeResult(env, rc, text);
}

Napi::Value DecodeMapChunkJson(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  if (info.Length() < 2 || !info[0].IsString() || !info[1].IsBuffer()) {
    Napi::TypeError::New(env, "decodeMapChunkJson(basename, buffer)").ThrowAsJavaScriptException();
    return env.Null();
  }
  std::string basename = info[0].As<Napi::String>().Utf8Value();
  Napi::Buffer<uint8_t> buf = info[1].As<Napi::Buffer<uint8_t>>();
  std::string json;
  int rc = DecodeMapChunkJsonGrow(basename.c_str(), buf.Data(), buf.Length(), &json);
  if (rc == 1) {
    Napi::Object o = Napi::Object::New(env);
    o.Set("ok", Napi::Boolean::New(env, true));
    o.Set("json", Napi::String::New(env, json));
    return o;
  }
  return MakeResult(env, rc, "", rc == 0 ? "not map_chunk" : "map_chunk json error");
}

Napi::Value HexDump(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  if (info.Length() < 1 || !info[0].IsBuffer()) {
    Napi::TypeError::New(env, "hexDump(buffer)").ThrowAsJavaScriptException();
    return env.Null();
  }
  Napi::Buffer<uint8_t> data = info[0].As<Napi::Buffer<uint8_t>>();
  std::vector<char> scratch(data.Length() * 4 + 512, '\0');
  FILE *f = fmemopen(scratch.data(), scratch.size(), "w+");
  if (!f) {
    Napi::Error::New(env, "fmemopen failed").ThrowAsJavaScriptException();
    return env.Null();
  }
  lc_wire_hex_fprint(f, data.Data(), data.Length());
  fflush(f);
  long pos = ftell(f);
  fclose(f);
  if (pos < 0) pos = 0;
  return Napi::String::New(env, scratch.data(), (size_t)pos);
}

Napi::Object Init(Napi::Env env, Napi::Object exports) {
  exports.Set("supportedPackets", Napi::Function::New(env, SupportedPackets));
  exports.Set("decodeWire", Napi::Function::New(env, DecodeWire));
  exports.Set("decodeMapChunkJson", Napi::Function::New(env, DecodeMapChunkJson));
  exports.Set("hexDump", Napi::Function::New(env, HexDump));
  exports.Set("isPacketSupported", Napi::Function::New(env, [](const Napi::CallbackInfo &info) {
    Napi::Env env = info.Env();
    if (!info[0].IsString()) return Napi::Boolean::New(env, false);
    return Napi::Boolean::New(env,
                              lc_packet_name_supported(info[0].As<Napi::String>().Utf8Value().c_str()));
  }));
  return exports;
}

}  // namespace

NODE_API_MODULE(libchunk, Init)
