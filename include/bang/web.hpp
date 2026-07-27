#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include "bang/json.hpp"

namespace bang {

struct WebConfig {
  std::string host = "127.0.0.1";
  int port = 8765;
  std::filesystem::path static_dir = "web";
  std::filesystem::path run_dir;
  std::filesystem::path checkpoint;
  std::filesystem::path static_policy;
  // Sessions are journaled here (seed + action history) and restored by
  // replay after a server restart. Empty disables persistence.
  std::filesystem::path session_dir = "web-sessions";
  int max_sessions = 64;
  int torch_threads = 1;
  bool require_trained_policy = false;
  bool stochastic_policy = true;
};

JsonValue RunWebSmokeTest(const WebConfig& config = {});
JsonValue RunWebForcedResponseSmokeTest(const WebConfig& config = {});
JsonValue RunWebAbilityPromptSmokeTest(const WebConfig& config = {});
JsonValue RunWebPersistenceSmokeTest(const WebConfig& config);
int RunWebServer(const WebConfig& config = {});

// Browser-facing entry points used by the Emscripten static build. They also
// stay available in native builds so the bridge can be covered by native tests.
extern "C" const char* bang_static_web_init(const char* static_policy_path);
extern "C" const char* bang_static_web_new_game(
    int seed, int human_player, int characters_per_side);
extern "C" const char* bang_static_web_current_state(const char* session_id);
extern "C" const char* bang_static_web_apply_action(
    const char* session_id, int action_id);

}  // namespace bang
