// SPDX-License-Identifier: Apache-2.0 OR MIT
#include "frame_share.h"

#include <dxgi1_2.h>

#include <cstring>

namespace companion {

namespace {
bool fail(std::string* error, const std::string& what) {
    if (error) *error = what;
    return false;
}
}

FrameShare::~FrameShare() { release(); }

bool FrameShare::init(const panelspun::VulkanContext& vk, std::string* error) {
    vk_ = &vk;
#define COMPANION_LOAD(name)                                                       \
    name = reinterpret_cast<PFN_##name>(vk.getDeviceProcAddr(vk.device, #name));   \
    if (!name) return fail(error, "missing Vulkan function " #name);
    COMPANION_VK_DEVICE_FUNCS(COMPANION_LOAD)
#undef COMPANION_LOAD
    PFN_vkGetPhysicalDeviceProperties2 props2 = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(
        vk.getInstanceProcAddr(vk.instance, "vkGetPhysicalDeviceProperties2"));
    if (!props2) return fail(error, "missing Vulkan function vkGetPhysicalDeviceProperties2");
    VkPhysicalDeviceIDProperties id{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
    VkPhysicalDeviceProperties2 p{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &id};
    props2(vk.physicalDevice, &p);
    if (!id.deviceLUIDValid) return fail(error, "the Vulkan device reports no LUID to match a D3D11 adapter to");

    winrt::com_ptr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(factory.put())))) return fail(error, "CreateDXGIFactory1 failed");
    winrt::com_ptr<IDXGIAdapter1> adapter;
    for (UINT i = 0;; ++i) {
        winrt::com_ptr<IDXGIAdapter1> a;
        if (factory->EnumAdapters1(i, a.put()) == DXGI_ERROR_NOT_FOUND) break;
        DXGI_ADAPTER_DESC1 d{};
        a->GetDesc1(&d);
        if (std::memcmp(&d.AdapterLuid, id.deviceLUID, VK_LUID_SIZE) == 0) {
            adapter = a;
            break;
        }
    }
    if (!adapter) return fail(error, "no D3D11 adapter has the Vulkan device's LUID");
    if (FAILED(D3D11CreateDevice(adapter.get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                 D3D11_SDK_VERSION, device_.put(), nullptr, context_.put())))
        return fail(error, "D3D11CreateDevice failed on the Vulkan device's adapter");
    D3D11_QUERY_DESC q{D3D11_QUERY_EVENT, 0};
    if (FAILED(device_->CreateQuery(&q, done_.put()))) return fail(error, "CreateQuery failed");
    return true;
}

bool FrameShare::recreate(UINT w, UINT h, std::string* error) {
    release();
    D3D11_TEXTURE2D_DESC d{};
    d.Width = w;
    d.Height = h;
    d.MipLevels = 1;
    d.ArraySize = 1;
    d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    d.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
    if (FAILED(device_->CreateTexture2D(&d, nullptr, texture_.put()))) return fail(error, "CreateTexture2D (shared) failed");
    winrt::com_ptr<IDXGIResource1> res = texture_.as<IDXGIResource1>();
    if (FAILED(res->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &handle_)))
        return fail(error, "CreateSharedHandle failed");

    const VkExternalMemoryHandleTypeFlagBits type = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT;
    VkExternalMemoryImageCreateInfo ext{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO, nullptr,
                                        static_cast<VkExternalMemoryHandleTypeFlags>(type)};
    VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, &ext};
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = VK_FORMAT_B8G8R8A8_UNORM;
    ici.extent = {w, h, 1};
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(vk_->device, &ici, nullptr, &image_) != VK_SUCCESS) return fail(error, "vkCreateImage (external) failed");
    VkMemoryWin32HandlePropertiesKHR hp{VK_STRUCTURE_TYPE_MEMORY_WIN32_HANDLE_PROPERTIES_KHR};
    if (vkGetMemoryWin32HandlePropertiesKHR(vk_->device, type, handle_, &hp) != VK_SUCCESS)
        return fail(error, "vkGetMemoryWin32HandlePropertiesKHR failed");
    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(vk_->device, image_, &req);
    std::uint32_t bits = req.memoryTypeBits & hp.memoryTypeBits;
    if (!bits) return fail(error, "no memory type can hold the imported D3D11 texture");
    VkMemoryDedicatedAllocateInfo dedicated{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO, nullptr, image_};
    VkImportMemoryWin32HandleInfoKHR import{VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR, &dedicated, type, handle_};
    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, &import, req.size};
    while (!(bits & (1u << mai.memoryTypeIndex))) ++mai.memoryTypeIndex;
    if (vkAllocateMemory(vk_->device, &mai, nullptr, &memory_) != VK_SUCCESS) return fail(error, "vkAllocateMemory (import) failed");
    if (vkBindImageMemory(vk_->device, image_, memory_, 0) != VK_SUCCESS) return fail(error, "vkBindImageMemory failed");
    width_ = w;
    height_ = h;
    acquiredOnce_ = false;
    return true;
}

bool FrameShare::copy(ID3D11Texture2D* src, UINT w, UINT h, std::string* error) {
    if (!w || !h) return true;
    if ((w != width_ || h != height_ || !image_) && !recreate(w, h, error)) {
        release();
        return false;
    }
    D3D11_BOX box{0, 0, 0, w, h, 1};
    context_->CopySubresourceRegion(texture_.get(), 0, 0, 0, 0, src, 0, &box);
    context_->End(done_.get());
    context_->Flush();
    while (context_->GetData(done_.get(), nullptr, 0, 0) == S_FALSE) SwitchToThread();
    return true;
}

void FrameShare::record(const panelspun::VulkanRegionFrame& frame, VkRect2D dst) {
    VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkClearColorValue black{};
    vkCmdClearColorImage(frame.commands, frame.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &range);
    if (!image_ || dst.extent.width == 0 || dst.extent.height == 0) return;
    VkImageMemoryBarrier acquire{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    acquire.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    acquire.oldLayout = acquiredOnce_ ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED;
    acquire.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    acquire.srcQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL;
    acquire.dstQueueFamilyIndex = frame.context->queueFamily;
    acquire.image = image_;
    acquire.subresourceRange = range;
    VkImageMemoryBarrier cleared{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    cleared.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    cleared.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    cleared.oldLayout = cleared.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    cleared.srcQueueFamilyIndex = cleared.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    cleared.image = frame.image;
    cleared.subresourceRange = range;
    VkImageMemoryBarrier both[2] = {acquire, cleared};
    vkCmdPipelineBarrier(frame.commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 2, both);
    VkImageBlit blit{};
    blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.srcOffsets[1] = {static_cast<int32_t>(width_), static_cast<int32_t>(height_), 1};
    blit.dstSubresource = blit.srcSubresource;
    blit.dstOffsets[0] = {dst.offset.x, dst.offset.y, 0};
    blit.dstOffsets[1] = {dst.offset.x + static_cast<int32_t>(dst.extent.width), dst.offset.y + static_cast<int32_t>(dst.extent.height), 1};
    vkCmdBlitImage(frame.commands, image_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, frame.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                   &blit, VK_FILTER_LINEAR);
    VkImageMemoryBarrier release{acquire};
    release.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    release.dstAccessMask = 0;
    release.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    release.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    release.srcQueueFamilyIndex = frame.context->queueFamily;
    release.dstQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL;
    vkCmdPipelineBarrier(frame.commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1,
                         &release);
    acquiredOnce_ = true;
}

void FrameShare::release() {
    if (vk_ && (image_ || memory_)) vkDeviceWaitIdle(vk_->device);
    if (image_) vkDestroyImage(vk_->device, image_, nullptr);
    if (memory_) vkFreeMemory(vk_->device, memory_, nullptr);
    image_ = VK_NULL_HANDLE;
    memory_ = VK_NULL_HANDLE;
    if (handle_) CloseHandle(handle_);
    handle_ = nullptr;
    texture_ = nullptr;
    width_ = height_ = 0;
}

}
