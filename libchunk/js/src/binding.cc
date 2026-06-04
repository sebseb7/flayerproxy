#include <napi.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

extern "C" {
#include "decode_wire.h"
#include "libchunk.h"
#include "mc_wire.h"
#include "packets_write.h"
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
      "sound_effect",        "entity_sound_effect",
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
      "attach_entity",
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

Napi::Value ParsePosition(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  if (info.Length() < 1 || !info[0].IsBuffer()) {
    Napi::TypeError::New(env, "parsePosition(buffer)").ThrowAsJavaScriptException();
    return env.Null();
  }
  Napi::Buffer<uint8_t> buf = info[0].As<Napi::Buffer<uint8_t>>();
  lc_position parsed;
  lc_status st = lc_parse_position(buf.Data(), buf.Length(), &parsed);
  if (st != LC_OK) return env.Null();

  Napi::Object o = Napi::Object::New(env);
  o.Set("teleportId", Napi::Number::New(env, parsed.teleport_id));
  o.Set("x", Napi::Number::New(env, parsed.x));
  o.Set("y", Napi::Number::New(env, parsed.y));
  o.Set("z", Napi::Number::New(env, parsed.z));
  o.Set("yaw", Napi::Number::New(env, parsed.yaw));
  o.Set("pitch", Napi::Number::New(env, parsed.pitch));
  return o;
}

Napi::Value ParseUpdateTime(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  if (info.Length() < 1 || !info[0].IsBuffer()) {
    Napi::TypeError::New(env, "parseUpdateTime(buffer)").ThrowAsJavaScriptException();
    return env.Null();
  }
  Napi::Buffer<uint8_t> buf = info[0].As<Napi::Buffer<uint8_t>>();
  lc_update_time parsed;
  lc_status st = lc_parse_update_time(buf.Data(), buf.Length(), &parsed);
  if (st != LC_OK) return env.Null();

  Napi::Object o = Napi::Object::New(env);
  o.Set("gameTime", Napi::BigInt::New(env, parsed.game_time));
  o.Set("dayTime", Napi::BigInt::New(env, parsed.day_time));
  o.Set("tickDayTime", Napi::Boolean::New(env, parsed.tick_day_time != 0));
  return o;
}

Napi::Value ParseGameEvent(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  if (info.Length() < 1 || !info[0].IsBuffer()) {
    Napi::TypeError::New(env, "parseGameEvent(buffer)").ThrowAsJavaScriptException();
    return env.Null();
  }
  Napi::Buffer<uint8_t> buf = info[0].As<Napi::Buffer<uint8_t>>();
  lc_game_event parsed;
  lc_status st = lc_parse_game_event(buf.Data(), buf.Length(), &parsed);
  if (st != LC_OK) return env.Null();

  Napi::Object o = Napi::Object::New(env);
  o.Set("event", Napi::Number::New(env, parsed.event));
  o.Set("value", Napi::Number::New(env, parsed.value));
  return o;
}

Napi::Value ParseSetTickingState(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  if (info.Length() < 1 || !info[0].IsBuffer()) {
    Napi::TypeError::New(env, "parseSetTickingState(buffer)").ThrowAsJavaScriptException();
    return env.Null();
  }
  Napi::Buffer<uint8_t> buf = info[0].As<Napi::Buffer<uint8_t>>();
  lc_set_ticking_state parsed;
  lc_status st = lc_parse_set_ticking_state(buf.Data(), buf.Length(), &parsed);
  if (st != LC_OK) return env.Null();

  Napi::Object o = Napi::Object::New(env);
  o.Set("tickRate", Napi::Number::New(env, parsed.tick_rate));
  o.Set("isFrozen", Napi::Boolean::New(env, parsed.is_frozen != 0));
  return o;
}

Napi::Value ParseUpdateHealth(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  if (info.Length() < 1 || !info[0].IsBuffer()) {
    Napi::TypeError::New(env, "parseUpdateHealth(buffer)").ThrowAsJavaScriptException();
    return env.Null();
  }
  Napi::Buffer<uint8_t> buf = info[0].As<Napi::Buffer<uint8_t>>();
  lc_update_health parsed;
  lc_status st = lc_parse_update_health(buf.Data(), buf.Length(), &parsed);
  if (st != LC_OK) return env.Null();

  Napi::Object o = Napi::Object::New(env);
  o.Set("health", Napi::Number::New(env, parsed.health));
  o.Set("food", Napi::Number::New(env, parsed.food));
  o.Set("saturation", Napi::Number::New(env, parsed.saturation));
  return o;
}

Napi::Value ParseUpdateViewPosition(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  if (info.Length() < 1 || !info[0].IsBuffer()) {
    Napi::TypeError::New(env, "parseUpdateViewPosition(buffer)").ThrowAsJavaScriptException();
    return env.Null();
  }
  Napi::Buffer<uint8_t> buf = info[0].As<Napi::Buffer<uint8_t>>();
  lc_update_view_position parsed;
  lc_status st = lc_parse_update_view_position(buf.Data(), buf.Length(), &parsed);
  if (st != LC_OK) return env.Null();

  Napi::Object o = Napi::Object::New(env);
  o.Set("chunkX", Napi::Number::New(env, parsed.chunk_x));
  o.Set("chunkZ", Napi::Number::New(env, parsed.chunk_z));
  return o;
}

#define CHECK_BUFFER_ARG(info, name) \
  if (info.Length() < 1 || !info[0].IsBuffer()) { \
    Napi::TypeError::New(info.Env(), name "(buffer) expects a buffer").ThrowAsJavaScriptException(); \
    return info.Env().Null(); \
  }

Napi::Value ParseEntityVelocity(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  CHECK_BUFFER_ARG(info, "parseEntityVelocity");
  Napi::Buffer<uint8_t> buf = info[0].As<Napi::Buffer<uint8_t>>();
  lc_entity_velocity parsed;
  lc_status st = lc_parse_entity_velocity(buf.Data(), buf.Length(), &parsed);
  if (st != LC_OK) return env.Null();

  Napi::Object o = Napi::Object::New(env);
  o.Set("entityId", Napi::Number::New(env, parsed.entity_id));
  Napi::Object vel = Napi::Object::New(env);
  vel.Set("x", Napi::Number::New(env, parsed.velocity.x));
  vel.Set("y", Napi::Number::New(env, parsed.velocity.y));
  vel.Set("z", Napi::Number::New(env, parsed.velocity.z));
  o.Set("velocity", vel);
  return o;
}

Napi::Value ParseRelEntityMove(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  CHECK_BUFFER_ARG(info, "parseRelEntityMove");
  Napi::Buffer<uint8_t> buf = info[0].As<Napi::Buffer<uint8_t>>();
  lc_rel_entity_move parsed;
  lc_status st = lc_parse_rel_entity_move(buf.Data(), buf.Length(), &parsed);
  if (st != LC_OK) return env.Null();

  Napi::Object o = Napi::Object::New(env);
  o.Set("entityId", Napi::Number::New(env, parsed.entity_id));
  o.Set("dx", Napi::Number::New(env, parsed.dx));
  o.Set("dy", Napi::Number::New(env, parsed.dy));
  o.Set("dz", Napi::Number::New(env, parsed.dz));
  o.Set("onGround", Napi::Boolean::New(env, parsed.on_ground != 0));
  return o;
}

Napi::Value ParseSyncEntityPosition(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  CHECK_BUFFER_ARG(info, "parseSyncEntityPosition");
  Napi::Buffer<uint8_t> buf = info[0].As<Napi::Buffer<uint8_t>>();
  lc_sync_entity_position parsed;
  lc_status st = lc_parse_sync_entity_position(buf.Data(), buf.Length(), &parsed);
  if (st != LC_OK) return env.Null();

  Napi::Object o = Napi::Object::New(env);
  o.Set("entityId", Napi::Number::New(env, parsed.entity_id));
  o.Set("x", Napi::Number::New(env, parsed.x));
  o.Set("y", Napi::Number::New(env, parsed.y));
  o.Set("z", Napi::Number::New(env, parsed.z));
  o.Set("dx", Napi::Number::New(env, parsed.dx));
  o.Set("dy", Napi::Number::New(env, parsed.dy));
  o.Set("dz", Napi::Number::New(env, parsed.dz));
  o.Set("yaw", Napi::Number::New(env, parsed.yaw));
  o.Set("pitch", Napi::Number::New(env, parsed.pitch));
  o.Set("onGround", Napi::Boolean::New(env, parsed.on_ground != 0));
  return o;
}

Napi::Value ParseEntityHeadRotation(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  CHECK_BUFFER_ARG(info, "parseEntityHeadRotation");
  Napi::Buffer<uint8_t> buf = info[0].As<Napi::Buffer<uint8_t>>();
  lc_entity_head_rotation parsed;
  lc_status st = lc_parse_entity_head_rotation(buf.Data(), buf.Length(), &parsed);
  if (st != LC_OK) return env.Null();

  Napi::Object o = Napi::Object::New(env);
  o.Set("entityId", Napi::Number::New(env, parsed.entity_id));
  o.Set("headYaw", Napi::Number::New(env, parsed.head_yaw));
  return o;
}

Napi::Value ParseEntityMoveLook(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  CHECK_BUFFER_ARG(info, "parseEntityMoveLook");
  Napi::Buffer<uint8_t> buf = info[0].As<Napi::Buffer<uint8_t>>();
  lc_entity_move_look parsed;
  lc_status st = lc_parse_entity_move_look(buf.Data(), buf.Length(), &parsed);
  if (st != LC_OK) return env.Null();

  Napi::Object o = Napi::Object::New(env);
  o.Set("entityId", Napi::Number::New(env, parsed.entity_id));
  o.Set("dx", Napi::Number::New(env, parsed.dx));
  o.Set("dy", Napi::Number::New(env, parsed.dy));
  o.Set("dz", Napi::Number::New(env, parsed.dz));
  o.Set("yaw", Napi::Number::New(env, parsed.yaw));
  o.Set("pitch", Napi::Number::New(env, parsed.pitch));
  o.Set("onGround", Napi::Boolean::New(env, parsed.on_ground != 0));
  return o;
}

Napi::Value ParseEntityLook(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  CHECK_BUFFER_ARG(info, "parseEntityLook");
  Napi::Buffer<uint8_t> buf = info[0].As<Napi::Buffer<uint8_t>>();
  lc_entity_look parsed;
  lc_status st = lc_parse_entity_look(buf.Data(), buf.Length(), &parsed);
  if (st != LC_OK) return env.Null();

  Napi::Object o = Napi::Object::New(env);
  o.Set("entityId", Napi::Number::New(env, parsed.entity_id));
  o.Set("yaw", Napi::Number::New(env, parsed.yaw));
  o.Set("pitch", Napi::Number::New(env, parsed.pitch));
  o.Set("onGround", Napi::Boolean::New(env, parsed.on_ground != 0));
  return o;
}

Napi::Value ParseSpawnEntity(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  CHECK_BUFFER_ARG(info, "parseSpawnEntity");
  Napi::Buffer<uint8_t> buf = info[0].As<Napi::Buffer<uint8_t>>();
  lc_spawn_entity parsed;
  lc_status st = lc_parse_spawn_entity(buf.Data(), buf.Length(), &parsed);
  if (st != LC_OK) return env.Null();

  Napi::Object o = Napi::Object::New(env);
  o.Set("entityId", Napi::Number::New(env, parsed.entity_id));
  
  char uuid_str[40];
  lc_uuid_to_string(&parsed.object_uuid, uuid_str, sizeof uuid_str);
  o.Set("uuid", Napi::String::New(env, uuid_str));
  o.Set("type", Napi::Number::New(env, parsed.type));
  o.Set("x", Napi::Number::New(env, parsed.x));
  o.Set("y", Napi::Number::New(env, parsed.y));
  o.Set("z", Napi::Number::New(env, parsed.z));
  
  Napi::Object vel = Napi::Object::New(env);
  vel.Set("x", Napi::Number::New(env, parsed.velocity.x));
  vel.Set("y", Napi::Number::New(env, parsed.velocity.y));
  vel.Set("z", Napi::Number::New(env, parsed.velocity.z));
  o.Set("velocity", vel);
  
  o.Set("pitch", Napi::Number::New(env, parsed.pitch));
  o.Set("yaw", Napi::Number::New(env, parsed.yaw));
  o.Set("headPitch", Napi::Number::New(env, parsed.head_pitch));
  o.Set("objectData", Napi::Number::New(env, parsed.object_data));
  return o;
}

Napi::Value ParseEntityTeleport(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  CHECK_BUFFER_ARG(info, "parseEntityTeleport");
  Napi::Buffer<uint8_t> buf = info[0].As<Napi::Buffer<uint8_t>>();
  lc_entity_teleport parsed;
  lc_status st = lc_parse_entity_teleport(buf.Data(), buf.Length(), &parsed);
  if (st != LC_OK) return env.Null();

  Napi::Object o = Napi::Object::New(env);
  o.Set("entityId", Napi::Number::New(env, parsed.entity_id));
  o.Set("x", Napi::Number::New(env, parsed.x));
  o.Set("y", Napi::Number::New(env, parsed.y));
  o.Set("z", Napi::Number::New(env, parsed.z));
  o.Set("yaw", Napi::Number::New(env, parsed.yaw));
  o.Set("pitch", Napi::Number::New(env, parsed.pitch));
  o.Set("onGround", Napi::Boolean::New(env, parsed.on_ground != 0));
  return o;
}

Napi::Value ParseEntityMetadata(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  CHECK_BUFFER_ARG(info, "parseEntityMetadata");
  Napi::Buffer<uint8_t> buf = info[0].As<Napi::Buffer<uint8_t>>();
  lc_entity_metadata parsed;
  lc_status st = lc_parse_entity_metadata(buf.Data(), buf.Length(), &parsed);
  if (st != LC_OK) return env.Null();

  Napi::Object o = Napi::Object::New(env);
  o.Set("entityId", Napi::Number::New(env, parsed.entity_id));
  Napi::Array meta = Napi::Array::New(env, parsed.metadata.count);
  for (size_t i = 0; i < parsed.metadata.count; i++) {
    const lc_metadata_entry &e = parsed.metadata.items[i];
    Napi::Object entry = Napi::Object::New(env);
    entry.Set("key", Napi::Number::New(env, e.key));
    entry.Set("typeId", Napi::Number::New(env, e.type_id));
    if (e.type_name) {
      entry.Set("typeName", Napi::String::New(env, e.type_name));
    }
    Napi::Value val = env.Null();
    switch (e.kind) {
      case LC_META_BYTE:
        val = Napi::Number::New(env, e.v.i8);
        break;
      case LC_META_INT:
      case LC_META_VARINT:
        val = Napi::Number::New(env, e.v.i32);
        break;
      case LC_META_LONG:
        val = Napi::Number::New(env, (double)e.v.i64);
        break;
      case LC_META_FLOAT:
        val = Napi::Number::New(env, e.v.f32);
        break;
      case LC_META_DOUBLE:
        val = Napi::Number::New(env, e.v.f64);
        break;
      case LC_META_BOOL:
        val = Napi::Boolean::New(env, e.v.boolean != 0);
        break;
      case LC_META_STRING:
        if (e.v.string) {
          val = Napi::String::New(env, e.v.string);
        }
        break;
      case LC_META_RAW:
        if (e.v.raw.data && e.v.raw.len > 0) {
          val = Napi::Buffer<uint8_t>::Copy(env, e.v.raw.data, e.v.raw.len);
        }
        break;
      default:
        break;
    }
    entry.Set("value", val);
    meta.Set(i, entry);
  }
  o.Set("metadata", meta);
  lc_entity_metadata_free(&parsed);
  return o;
}

Napi::Value ParseEntityEquipment(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  CHECK_BUFFER_ARG(info, "parseEntityEquipment");
  Napi::Buffer<uint8_t> buf = info[0].As<Napi::Buffer<uint8_t>>();
  lc_entity_equipment parsed;
  lc_status st = lc_parse_entity_equipment(buf.Data(), buf.Length(), &parsed);
  if (st != LC_OK) return env.Null();

  Napi::Object o = Napi::Object::New(env);
  o.Set("entityId", Napi::Number::New(env, parsed.entity_id));
  Napi::Array eq_arr = Napi::Array::New(env, parsed.equipments.count);
  for (size_t i = 0; i < parsed.equipments.count; i++) {
    const lc_equipment &eq = parsed.equipments.items[i];
    Napi::Object entry = Napi::Object::New(env);
    entry.Set("slot", Napi::Number::New(env, eq.slot));
    entry.Set("itemCount", Napi::Number::New(env, eq.item_count));
    entry.Set("itemId", Napi::Number::New(env, eq.item_id));
    if (eq.item_extra.data && eq.item_extra.len > 0) {
      entry.Set("itemExtra", Napi::Buffer<uint8_t>::Copy(env, eq.item_extra.data, eq.item_extra.len));
    }
    eq_arr.Set(i, entry);
  }
  o.Set("equipments", eq_arr);
  lc_entity_equipment_free(&parsed);
  return o;
}

Napi::Value ParseEntityDestroy(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  CHECK_BUFFER_ARG(info, "parseEntityDestroy");
  Napi::Buffer<uint8_t> buf = info[0].As<Napi::Buffer<uint8_t>>();
  lc_entity_destroy parsed;
  lc_status st = lc_parse_entity_destroy(buf.Data(), buf.Length(), &parsed);
  if (st != LC_OK) return env.Null();

  Napi::Object o = Napi::Object::New(env);
  Napi::Array ids = Napi::Array::New(env, parsed.count);
  for (size_t i = 0; i < parsed.count; i++) {
    ids.Set(i, Napi::Number::New(env, parsed.entity_ids[i]));
  }
  o.Set("entityIds", ids);
  lc_entity_destroy_free(&parsed);
  return o;
}

Napi::Value ParseSetPassengers(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  CHECK_BUFFER_ARG(info, "parseSetPassengers");
  Napi::Buffer<uint8_t> buf = info[0].As<Napi::Buffer<uint8_t>>();
  lc_set_passengers parsed;
  lc_status st = lc_parse_set_passengers(buf.Data(), buf.Length(), &parsed);
  if (st != LC_OK) return env.Null();

  Napi::Object o = Napi::Object::New(env);
  o.Set("entityId", Napi::Number::New(env, parsed.entity_id));
  Napi::Array passengers = Napi::Array::New(env, parsed.passenger_count);
  for (size_t i = 0; i < parsed.passenger_count; i++) {
    passengers.Set(i, Napi::Number::New(env, parsed.passengers[i]));
  }
  o.Set("passengers", passengers);
  lc_set_passengers_free(&parsed);
  return o;
}

Napi::Value ParseEntityUpdateAttributes(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  CHECK_BUFFER_ARG(info, "parseEntityUpdateAttributes");
  Napi::Buffer<uint8_t> buf = info[0].As<Napi::Buffer<uint8_t>>();
  lc_entity_update_attributes parsed;
  lc_status st = lc_parse_entity_update_attributes(buf.Data(), buf.Length(), &parsed);
  if (st != LC_OK) return env.Null();

  Napi::Object o = Napi::Object::New(env);
  o.Set("entityId", Napi::Number::New(env, parsed.entity_id));
  Napi::Array properties = Napi::Array::New(env, parsed.property_count);
  for (size_t i = 0; i < parsed.property_count; i++) {
    const lc_entity_attribute_property &p = parsed.properties[i];
    Napi::Object prop = Napi::Object::New(env);
    prop.Set("key", Napi::Number::New(env, p.key));
    const char *key_name = lc_entity_attribute_key_name(p.key);
    if (key_name) {
      prop.Set("keyName", Napi::String::New(env, key_name));
    }
    prop.Set("value", Napi::Number::New(env, p.value));

    Napi::Array modifiers = Napi::Array::New(env, p.modifier_count);
    for (size_t j = 0; j < p.modifier_count; j++) {
      const lc_entity_attribute_modifier &m = p.modifiers[j];
      Napi::Object mod = Napi::Object::New(env);
      if (m.uuid) {
        mod.Set("uuid", Napi::String::New(env, m.uuid));
      }
      mod.Set("amount", Napi::Number::New(env, m.amount));
      mod.Set("operation", Napi::Number::New(env, m.operation));
      
      const char *op_name = "unknown";
      switch (m.operation) {
        case 0: op_name = "add"; break;
        case 1: op_name = "multiply_base"; break;
        case 2: op_name = "multiply_total"; break;
      }
      mod.Set("operationName", Napi::String::New(env, op_name));
      modifiers.Set(j, mod);
    }
    prop.Set("modifiers", modifiers);
    properties.Set(i, prop);
  }
  o.Set("properties", properties);
  lc_entity_update_attributes_free(&parsed);
  return o;
}

Napi::Value ParseEntityStatus(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  CHECK_BUFFER_ARG(info, "parseEntityStatus");
  Napi::Buffer<uint8_t> buf = info[0].As<Napi::Buffer<uint8_t>>();
  lc_entity_status parsed;
  lc_status st = lc_parse_entity_status(buf.Data(), buf.Length(), &parsed);
  if (st != LC_OK) return env.Null();

  Napi::Object o = Napi::Object::New(env);
  o.Set("entityId", Napi::Number::New(env, parsed.entity_id));
  o.Set("status", Napi::Number::New(env, parsed.status));
  return o;
}

Napi::Value ParseEntityEffect(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  CHECK_BUFFER_ARG(info, "parseEntityEffect");
  Napi::Buffer<uint8_t> buf = info[0].As<Napi::Buffer<uint8_t>>();
  lc_entity_effect parsed;
  lc_status st = lc_parse_entity_effect(buf.Data(), buf.Length(), &parsed);
  if (st != LC_OK) return env.Null();

  Napi::Object o = Napi::Object::New(env);
  o.Set("entityId", Napi::Number::New(env, parsed.entity_id));
  o.Set("effectId", Napi::Number::New(env, parsed.effect_id));
  o.Set("amplifier", Napi::Number::New(env, parsed.amplifier));
  o.Set("duration", Napi::Number::New(env, parsed.duration));
  o.Set("flags", Napi::Number::New(env, parsed.flags));
  return o;
}

Napi::Value ParseRemoveEntityEffect(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  CHECK_BUFFER_ARG(info, "parseRemoveEntityEffect");
  Napi::Buffer<uint8_t> buf = info[0].As<Napi::Buffer<uint8_t>>();
  lc_remove_entity_effect parsed;
  lc_status st = lc_parse_remove_entity_effect(buf.Data(), buf.Length(), &parsed);
  if (st != LC_OK) return env.Null();

  Napi::Object o = Napi::Object::New(env);
  o.Set("entityId", Napi::Number::New(env, parsed.entity_id));
  o.Set("effectId", Napi::Number::New(env, parsed.effect_id));
  return o;
}

Napi::Value ParseAttachEntity(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  CHECK_BUFFER_ARG(info, "parseAttachEntity");
  Napi::Buffer<uint8_t> buf = info[0].As<Napi::Buffer<uint8_t>>();
  lc_attach_entity parsed;
  lc_status st = lc_parse_attach_entity(buf.Data(), buf.Length(), &parsed);
  if (st != LC_OK) return env.Null();

  Napi::Object o = Napi::Object::New(env);
  o.Set("attachedId", Napi::Number::New(env, parsed.attached_id));
  o.Set("holdingId", Napi::Number::New(env, parsed.holding_id));
  return o;
}


Napi::Value ParsePlayLogin(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  if (info.Length() < 1 || !info[0].IsBuffer()) {
    Napi::TypeError::New(env, "parsePlayLogin(buffer)").ThrowAsJavaScriptException();
    return env.Null();
  }
  Napi::Buffer<uint8_t> buf = info[0].As<Napi::Buffer<uint8_t>>();
  lc_play_login parsed;
  char **world_names = nullptr;
  size_t world_name_count = 0;
  lc_status st = lc_parse_play_login(buf.Data(), buf.Length(), &parsed, &world_names, &world_name_count);
  if (st != LC_OK) return env.Null();

  Napi::Object o = Napi::Object::New(env);
  o.Set("entityId", Napi::Number::New(env, parsed.entity_id));
  o.Set("viewDistance", Napi::Number::New(env, parsed.view_distance));
  o.Set("simulationDistance", Napi::Number::New(env, parsed.simulation_distance));
  o.Set("hasDeath", Napi::Boolean::New(env, parsed.world_state.has_death != 0));
  // Extract dimension name before freeing (dimension_name points to world_state.name)
  if (parsed.world_state.name) {
    o.Set("dimensionName", Napi::String::New(env, parsed.world_state.name));
  }
  lc_play_login_free(&parsed);
  return o;
}

Napi::Value PeekMapChunkCoords(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  if (info.Length() < 1 || !info[0].IsBuffer()) {
    Napi::TypeError::New(env, "peekMapChunkCoords(buffer)").ThrowAsJavaScriptException();
    return env.Null();
  }
  Napi::Buffer<uint8_t> buf = info[0].As<Napi::Buffer<uint8_t>>();
  int32_t x = 0;
  int32_t z = 0;
  lc_status st = lc_peek_map_chunk_coords(buf.Data(), buf.Length(), &x, &z);
  Napi::Object o = Napi::Object::New(env);
  if (st != LC_OK) {
    o.Set("ok", Napi::Boolean::New(env, false));
    return o;
  }
  o.Set("ok", Napi::Boolean::New(env, true));
  o.Set("x", Napi::Number::New(env, x));
  o.Set("z", Napi::Number::New(env, z));
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
  exports.Set("peekMapChunkCoords", Napi::Function::New(env, PeekMapChunkCoords));
  exports.Set("parsePlayLogin", Napi::Function::New(env, ParsePlayLogin));
  exports.Set("parsePosition", Napi::Function::New(env, ParsePosition));
  exports.Set("parseUpdateTime", Napi::Function::New(env, ParseUpdateTime));
  exports.Set("parseGameEvent", Napi::Function::New(env, ParseGameEvent));
  exports.Set("parseSetTickingState", Napi::Function::New(env, ParseSetTickingState));
  exports.Set("parseUpdateHealth", Napi::Function::New(env, ParseUpdateHealth));
  exports.Set("parseUpdateViewPosition", Napi::Function::New(env, ParseUpdateViewPosition));
  exports.Set("parseEntityVelocity", Napi::Function::New(env, ParseEntityVelocity));
  exports.Set("parseRelEntityMove", Napi::Function::New(env, ParseRelEntityMove));
  exports.Set("parseSyncEntityPosition", Napi::Function::New(env, ParseSyncEntityPosition));
  exports.Set("parseEntityHeadRotation", Napi::Function::New(env, ParseEntityHeadRotation));
  exports.Set("parseEntityMoveLook", Napi::Function::New(env, ParseEntityMoveLook));
  exports.Set("parseEntityLook", Napi::Function::New(env, ParseEntityLook));
  exports.Set("parseEntityMetadata", Napi::Function::New(env, ParseEntityMetadata));
  exports.Set("parseEntityEquipment", Napi::Function::New(env, ParseEntityEquipment));
  exports.Set("parseSpawnEntity", Napi::Function::New(env, ParseSpawnEntity));
  exports.Set("parseEntityTeleport", Napi::Function::New(env, ParseEntityTeleport));
  exports.Set("parseEntityDestroy", Napi::Function::New(env, ParseEntityDestroy));
  exports.Set("parseSetPassengers", Napi::Function::New(env, ParseSetPassengers));
  exports.Set("parseEntityUpdateAttributes", Napi::Function::New(env, ParseEntityUpdateAttributes));
  exports.Set("parseEntityStatus", Napi::Function::New(env, ParseEntityStatus));
  exports.Set("parseEntityEffect", Napi::Function::New(env, ParseEntityEffect));
  exports.Set("parseRemoveEntityEffect", Napi::Function::New(env, ParseRemoveEntityEffect));
  exports.Set("parseAttachEntity", Napi::Function::New(env, ParseAttachEntity));
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
