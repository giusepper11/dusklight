// The upstream loaderless SDK owns its dispatch tables and public vk* API.
#include "switch/nxvk.hpp"

namespace dusk::sw {
void nvk_dispatch_fixup() {
  // Preserve the existing startup call without mutating Mesa-private tables.
}
} // namespace dusk::sw

// Dawn's existing Switch patch starts at the ICD API. Route that one boundary
// through the SDK's public loaderless API; subsequent lookups use its wrappers.
extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
__wrap_vk_icdGetInstanceProcAddr(VkInstance instance, const char* name) {
  return vkGetInstanceProcAddr(instance, name);
}
