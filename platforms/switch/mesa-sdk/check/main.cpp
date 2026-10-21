// Link-only Switch check. This executable is not run or deployed.
#include <vulkan/vulkan.h>
#include <cstdlib>
extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vk_icdGetInstanceProcAddr(VkInstance instance, const char* name);

using Proc = PFN_vkVoidFunction;
Proc volatile required_procs[] = {
    reinterpret_cast<Proc>(&vkGetInstanceProcAddr),
    reinterpret_cast<Proc>(&vkGetDeviceProcAddr),
    reinterpret_cast<Proc>(&vkCreateInstance),
    reinterpret_cast<Proc>(&vkEnumerateInstanceExtensionProperties),
    reinterpret_cast<Proc>(&vkEnumerateInstanceLayerProperties),
    reinterpret_cast<Proc>(&vkEnumerateInstanceVersion),
    reinterpret_cast<Proc>(&vkCreateViSurfaceNN),
    reinterpret_cast<Proc>(&vk_icdGetInstanceProcAddr),
    reinterpret_cast<Proc>(&posix_memalign),
};
int main() {
    // Keep every relocation live without invoking Vulkan or touching hardware.
    for (auto proc : required_procs) {
        if (proc == nullptr) {
            return 1;
        }
    }
    return 0;
}
