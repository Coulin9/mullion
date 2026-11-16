#include "log/log.hpp"
#include "util_env.hpp"

#include <directx-native/dxn_embedder.h>

namespace mullion {
Logger Logger::s_instance("mullion.log");
}

extern "C" DXN_DEFINE_NATIVE_ABI_VERSION(mullion_)

extern "C" int32_t
mullion_set_executable_path(const char *path) {
  return mullion::env::setExecutablePath(path);
}

extern "C" void *
mullion_event_create(int manual_reset, int initial_state) {
  return dxn_event_create(manual_reset, initial_state);
}

extern "C" void
mullion_event_close(void *handle) {
  dxn_event_close(handle);
}

extern "C" int
mullion_event_dup_fd(void *handle) {
  return dxn_event_dup_fd(handle);
}
