#include <napi.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

extern "C" {
#include "decode_wire.h"
#include "libchunk.h"
#include "mc_wire.h"
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
      "custom_payload",
      "feature_flags",
      "select_known_packs",
      "finish_configuration",
      "bundle_delimiter",
      "step_tick",
      "success",
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
      "system_chat",          "set_ticking_state",
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

using DecodeStringFn = int (*)(const char *, const uint8_t *, size_t, char *, size_t);

static int DecodeToStringGrow(DecodeStringFn decode, const char *name, const uint8_t *buf, size_t len,
                              std::string *out) {
  out->assign(kInitialBuf, '\0');
  for (;;) {
    int rc = decode(name, buf, len, out->data(), out->size());
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

static Napi::Value DecodeWith(Napi::Env env, DecodeStringFn decode, const Napi::CallbackInfo &info) {
  if (info.Length() < 2 || !info[0].IsString() || !info[1].IsBuffer()) {
    Napi::TypeError::New(env, "decode*(packetName, buffer)").ThrowAsJavaScriptException();
    return env.Null();
  }
  std::string name = info[0].As<Napi::String>().Utf8Value();
  Napi::Buffer<uint8_t> buf = info[1].As<Napi::Buffer<uint8_t>>();
  std::string text;
  int rc = DecodeToStringGrow(decode, name.c_str(), buf.Data(), buf.Length(), &text);
  return MakeResult(env, rc, text);
}

Napi::Value DecodePayload(const Napi::CallbackInfo &info) {
  return DecodeWith(info.Env(), lc_decode_payload_to_string, info);
}

Napi::Value DecodeWire(const Napi::CallbackInfo &info) {
  return DecodeWith(info.Env(), lc_decode_wire_to_string, info);
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

static Napi::Buffer<uint8_t> ByteBufToBuffer(Napi::Env env, lc_byte_buf *bb) {
  if (!bb || !bb->data || bb->len == 0) {
    if (bb) lc_byte_buf_free(bb);
    return Napi::Buffer<uint8_t>::New(env, 0);
  }
  Napi::Buffer<uint8_t> buf = Napi::Buffer<uint8_t>::Copy(env, bb->data, bb->len);
  lc_byte_buf_free(bb);
  return buf;
}

class FrameProcessor : public Napi::ObjectWrap<FrameProcessor> {
 public:
  static Napi::Object Init(Napi::Env env, Napi::Object exports);
  static Napi::Object NewInstance(const Napi::CallbackInfo &info);
  FrameProcessor(const Napi::CallbackInfo &info);
  ~FrameProcessor();

  static Napi::FunctionReference constructor;

 private:
  lc_frame_reader reader_;
  Napi::FunctionReference on_frame_;

  Napi::Value Feed(const Napi::CallbackInfo &info);
  Napi::Value Reset(const Napi::CallbackInfo &info);

  static void FrameCallback(void *ctx, int32_t packet_id, const uint8_t *payload, size_t payload_len);
};

Napi::FunctionReference FrameProcessor::constructor;

Napi::Object FrameProcessor::Init(Napi::Env env, Napi::Object exports) {
  Napi::Function func =
      DefineClass(env, "FrameProcessor",
                  {
                      InstanceMethod("feed", &FrameProcessor::Feed),
                      InstanceMethod("reset", &FrameProcessor::Reset),
                  });
  constructor = Napi::Persistent(func);
  constructor.SuppressDestruct();
  exports.Set("FrameProcessor", func);
  return exports;
}

FrameProcessor::FrameProcessor(const Napi::CallbackInfo &info) : Napi::ObjectWrap<FrameProcessor>(info) {
  lc_frame_reader_init(&reader_);
  if (info.Length() < 1 || !info[0].IsFunction()) {
    Napi::TypeError::New(Env(), "FrameProcessor(onFrame)").ThrowAsJavaScriptException();
    return;
  }
  on_frame_ = Napi::Persistent(info[0].As<Napi::Function>());
}

FrameProcessor::~FrameProcessor() { lc_frame_reader_free(&reader_); }

void FrameProcessor::FrameCallback(void *ctx, int32_t packet_id, const uint8_t *payload,
                                   size_t payload_len) {
  auto *self = static_cast<FrameProcessor *>(ctx);
  Napi::Env env = self->Env();
  Napi::HandleScope scope(env);
  self->on_frame_.Call(
      {Napi::Number::New(env, packet_id), Napi::Buffer<uint8_t>::Copy(env, payload, payload_len)});
}

Napi::Value FrameProcessor::Feed(const Napi::CallbackInfo &info) {
  Napi::Env env = Env();
  if (info.Length() < 1 || !info[0].IsBuffer()) {
    Napi::TypeError::New(env, "feed(buffer)").ThrowAsJavaScriptException();
    return env.Undefined();
  }
  Napi::Buffer<uint8_t> chunk = info[0].As<Napi::Buffer<uint8_t>>();
  lc_status st =
      lc_frame_reader_feed(&reader_, chunk.Data(), chunk.Length(), FrameCallback, this);
  if (st == LC_ERR_INVALID) {
    Napi::Error::New(env, "invalid or oversize frame").ThrowAsJavaScriptException();
  } else if (st == LC_ERR_OOM) {
    Napi::Error::New(env, "out of memory").ThrowAsJavaScriptException();
  }
  return env.Undefined();
}

Napi::Value FrameProcessor::Reset(const Napi::CallbackInfo &info) {
  (void)info;
  lc_frame_reader_reset(&reader_);
  return Env().Undefined();
}

Napi::Object FrameProcessor::NewInstance(const Napi::CallbackInfo &info) {
  return constructor.New({info[0]});
}

Napi::Value CreateFrameProcessor(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  if (info.Length() < 1 || !info[0].IsFunction()) {
    Napi::TypeError::New(env, "createFrameProcessor(onFrame)").ThrowAsJavaScriptException();
    return env.Null();
  }
  return FrameProcessor::NewInstance(info);
}

Napi::Value TryReadFrame(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  if (info.Length() < 1 || !info[0].IsBuffer()) {
    Napi::TypeError::New(env, "tryReadFrame(buffer)").ThrowAsJavaScriptException();
    return env.Null();
  }
  Napi::Buffer<uint8_t> buf = info[0].As<Napi::Buffer<uint8_t>>();
  lc_frame_view fv;
  lc_status st = lc_try_read_frame(buf.Data(), buf.Length(), &fv);
  Napi::Object o = Napi::Object::New(env);
  if (st == LC_ERR_TRUNCATED) {
    o.Set("complete", Napi::Boolean::New(env, false));
    return o;
  }
  if (st != LC_OK) {
    o.Set("complete", Napi::Boolean::New(env, false));
    o.Set("error", Napi::String::New(env, "invalid frame"));
    return o;
  }
  o.Set("complete", Napi::Boolean::New(env, true));
  o.Set("id", Napi::Number::New(env, fv.packet_id));
  o.Set("payload", Napi::Buffer<uint8_t>::Copy(env, fv.payload, fv.payload_len));
  o.Set("consumed", Napi::Number::New(env, (double)fv.frame_bytes));
  return o;
}

Napi::Value BuildFrame(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  if (info.Length() < 1 || !info[0].IsNumber()) {
    Napi::TypeError::New(env, "buildFrame(id, payload?)").ThrowAsJavaScriptException();
    return env.Null();
  }
  int32_t pkt_id = info[0].As<Napi::Number>().Int32Value();
  const uint8_t *payload = nullptr;
  size_t payload_len = 0;
  Napi::Buffer<uint8_t> payload_buf;
  if (info.Length() >= 2 && info[1].IsBuffer()) {
    payload_buf = info[1].As<Napi::Buffer<uint8_t>>();
    payload = payload_buf.Data();
    payload_len = payload_buf.Length();
  }
  lc_byte_buf out;
  memset(&out, 0, sizeof out);
  lc_status st = lc_build_frame(pkt_id, payload, payload_len, &out);
  if (st != LC_OK) {
    Napi::Error::New(env, "buildFrame failed").ThrowAsJavaScriptException();
    return env.Null();
  }
  return ByteBufToBuffer(env, &out);
}

Napi::Value WriteVarInt(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  if (info.Length() < 1 || !info[0].IsNumber()) {
    Napi::TypeError::New(env, "writeVarInt(value)").ThrowAsJavaScriptException();
    return env.Null();
  }
  int32_t value = info[0].As<Napi::Number>().Int32Value();
  lc_byte_buf out;
  memset(&out, 0, sizeof out);
  if (lc_write_varint(value, &out) != LC_OK) {
    Napi::Error::New(env, "writeVarInt failed").ThrowAsJavaScriptException();
    return env.Null();
  }
  return ByteBufToBuffer(env, &out);
}

Napi::Value WriteString(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  if (info.Length() < 1 || !info[0].IsString()) {
    Napi::TypeError::New(env, "writeString(value)").ThrowAsJavaScriptException();
    return env.Null();
  }
  std::string s = info[0].As<Napi::String>().Utf8Value();
  lc_byte_buf out;
  memset(&out, 0, sizeof out);
  if (lc_write_string(s.c_str(), &out) != LC_OK) {
    Napi::Error::New(env, "writeString failed").ThrowAsJavaScriptException();
    return env.Null();
  }
  return ByteBufToBuffer(env, &out);
}

Napi::Value ReadStringAt(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  if (info.Length() < 2 || !info[0].IsBuffer() || !info[1].IsNumber()) {
    Napi::TypeError::New(env, "readStringAt(buffer, offset)").ThrowAsJavaScriptException();
    return env.Null();
  }
  Napi::Buffer<uint8_t> buf = info[0].As<Napi::Buffer<uint8_t>>();
  size_t offset = (size_t)info[1].As<Napi::Number>().Uint32Value();
  char *s = nullptr;
  size_t next = 0;
  lc_status st = lc_read_string_at(buf.Data(), buf.Length(), offset, &s, &next);
  if (st != LC_OK) return env.Null();
  std::string str(s);
  free(s);
  Napi::Object o = Napi::Object::New(env);
  o.Set("value", Napi::String::New(env, str));
  o.Set("next", Napi::Number::New(env, (double)next));
  return o;
}

Napi::Value ReadVarIntAt(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  if (info.Length() < 2 || !info[0].IsBuffer() || !info[1].IsNumber()) {
    Napi::TypeError::New(env, "readVarIntAt(buffer, offset)").ThrowAsJavaScriptException();
    return env.Null();
  }
  Napi::Buffer<uint8_t> buf = info[0].As<Napi::Buffer<uint8_t>>();
  size_t offset = (size_t)info[1].As<Napi::Number>().Uint32Value();
  int32_t value = 0;
  lc_status st = lc_read_varint_at(buf.Data(), buf.Length(), &offset, &value);
  if (st != LC_OK) return env.Null();
  Napi::Object o = Napi::Object::New(env);
  o.Set("value", Napi::Number::New(env, value));
  o.Set("next", Napi::Number::New(env, (double)offset));
  return o;
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
  FrameProcessor::Init(env, exports);
  exports.Set("supportedPackets", Napi::Function::New(env, SupportedPackets));
  exports.Set("decodePayload", Napi::Function::New(env, DecodePayload));
  exports.Set("decodeWire", Napi::Function::New(env, DecodeWire));
  exports.Set("decodeMapChunkJson", Napi::Function::New(env, DecodeMapChunkJson));
  exports.Set("hexDump", Napi::Function::New(env, HexDump));
  exports.Set("createFrameProcessor", Napi::Function::New(env, CreateFrameProcessor));
  exports.Set("tryReadFrame", Napi::Function::New(env, TryReadFrame));
  exports.Set("buildFrame", Napi::Function::New(env, BuildFrame));
  exports.Set("writeVarInt", Napi::Function::New(env, WriteVarInt));
  exports.Set("writeString", Napi::Function::New(env, WriteString));
  exports.Set("readStringAt", Napi::Function::New(env, ReadStringAt));
  exports.Set("readVarIntAt", Napi::Function::New(env, ReadVarIntAt));
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
