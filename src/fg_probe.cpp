#include "probe_support.hpp"
int main() {
    try {
        Vulkan v;
        v.open();
        uint32_t n = 0;
        auto f = v.inst<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(
            "vkGetPhysicalDeviceQueueFamilyProperties");
        f(v.physical, &n, nullptr);
        std::vector<VkQueueFamilyProperties> q(n);
        f(v.physical, &n, q.data());
        for (uint32_t i = 0; i < n; ++i)
            std::cout << "Queue family " << i << " count=" << q[i].queueCount
                      << " flags=" << q[i].queueFlags << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
