// SPDX-License-Identifier: Apache-2.0 OR MIT
#pragma once
#include <string>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <d3d11.h>
#include <windows.h>
#include <winrt/base.h>

#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_win32.h>

#include "panelspun/vulkan_region.h"

namespace companion {

// A D3D11 texture on the window's own GPU, imported into Vulkan through an NT handle, so captured frames
// reach the swapchain without a CPU copy. Written for this repository from the Vulkan and D3D11
// specifications; XR Pilot's OXRSys interop (MPL-2.0) does the same for OpenXR swapchains and is not copied.
class FrameShare {
public:
    ~FrameShare();
    bool init(const panelspun::VulkanContext& vk, std::string* error);
    ID3D11Device* device() const { return device_.get(); }
    // Copies the top-left w x h of src into the shared texture and waits for the GPU to finish the copy.
    // Call only while Vulkan is not reading the texture (panelspun's update, after the frame is done).
    bool copy(ID3D11Texture2D* src, UINT w, UINT h, std::string* error);
    // Clears the region and blits the shared texture scaled into dst, a rect of the region image.
    void record(const panelspun::VulkanRegionFrame& frame, VkRect2D dst);
    UINT width() const { return width_; }
    UINT height() const { return height_; }
    bool ready() const { return image_ != VK_NULL_HANDLE; }

private:
    bool recreate(UINT w, UINT h, std::string* error);
    void release();

    const panelspun::VulkanContext* vk_ = nullptr;
    winrt::com_ptr<ID3D11Device> device_;
    winrt::com_ptr<ID3D11DeviceContext> context_;
    winrt::com_ptr<ID3D11Query> done_;
    winrt::com_ptr<ID3D11Texture2D> texture_;
    HANDLE handle_ = nullptr;
    VkImage image_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    bool acquiredOnce_ = false;
    UINT width_ = 0;
    UINT height_ = 0;
#define COMPANION_VK_DEVICE_FUNCS(X)                                                                     \
    X(vkCreateImage) X(vkDestroyImage) X(vkGetImageMemoryRequirements) X(vkAllocateMemory) X(vkFreeMemory) \
    X(vkBindImageMemory) X(vkGetMemoryWin32HandlePropertiesKHR) X(vkCmdPipelineBarrier) X(vkCmdBlitImage) \
    X(vkCmdClearColorImage) X(vkDeviceWaitIdle)
#define COMPANION_DECLARE(name) PFN_##name name = nullptr;
    COMPANION_VK_DEVICE_FUNCS(COMPANION_DECLARE)
#undef COMPANION_DECLARE
};

}
