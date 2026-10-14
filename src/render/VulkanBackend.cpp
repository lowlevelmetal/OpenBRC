// Vulkan presentation backend.
//
// Vulkan 1.0 core plus VK_KHR_surface / VK_KHR_swapchain only, so it runs on old GPUs,
// Mesa, Windows drivers and MoltenVK alike. Nothing links against the Vulkan loader: SDL
// loads it (or MoltenVK) and volk fetches every entry point from SDL's
// vkGetInstanceProcAddr.
//
// Per new game frame: the indices (R8) and palette (256x1 BGRA) are copied through a
// per-frame staging buffer into device-local images, and a resolve pass turns them into
// an RGBA image at source resolution. Every present then draws that image into the
// swapchain image with the selected filter (plus the toast overlay), letterboxed.
//
// Objects are created on the main thread (init), used only by the render thread (draw)
// and destroyed on the main thread after the render thread has exited.
//
// Ported from the BarbieRecomp runtime (runtime/render/render_vk.c, the maintainer's own
// code).

#include "core/Log.h"
#include "render/Backend.h"
#include "render/Geometry.h"
#include "render/Overlay.h"
#include "render/shaders/SpirvShaders.h"

#include <SDL3/SDL_vulkan.h>
#include <volk.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace brc::render {
namespace {

constexpr int kFramesInFlight = 2;
constexpr VkFormat kResolvedFormat = VK_FORMAT_R8G8B8A8_UNORM;

const char* resultName(VkResult r) {
    switch (r) {
    case VK_SUCCESS:
        return "VK_SUCCESS";
    case VK_NOT_READY:
        return "VK_NOT_READY";
    case VK_TIMEOUT:
        return "VK_TIMEOUT";
    case VK_SUBOPTIMAL_KHR:
        return "VK_SUBOPTIMAL_KHR";
    case VK_ERROR_OUT_OF_HOST_MEMORY:
        return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY:
        return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED:
        return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST:
        return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_MEMORY_MAP_FAILED:
        return "VK_ERROR_MEMORY_MAP_FAILED";
    case VK_ERROR_LAYER_NOT_PRESENT:
        return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT:
        return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT:
        return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER:
        return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_SURFACE_LOST_KHR:
        return "VK_ERROR_SURFACE_LOST_KHR";
    case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:
        return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
    case VK_ERROR_OUT_OF_DATE_KHR:
        return "VK_ERROR_OUT_OF_DATE_KHR";
    default:
        return "VkResult";
    }
}

bool check(VkResult r, const char* what) {
    if (r == VK_SUCCESS)
        return true;
    log::error("render: Vulkan: {} failed: {} ({})", what, resultName(r), static_cast<int>(r));
    return false;
}

#define BRC_VK_CHECK(call)                                                                                   \
    do {                                                                                                     \
        if (!check((call), #call))                                                                           \
            return false;                                                                                    \
    } while (0)

bool hasExtension(const std::vector<VkExtensionProperties>& exts, const char* name) {
    return std::any_of(exts.begin(), exts.end(), [name](const VkExtensionProperties& e) {
        return std::strcmp(e.extensionName, name) == 0;
    });
}

bool envFlag(const char* name) {
    const char* v = std::getenv(name);
    return v && *v && *v != '0';
}

bool isSrgb(VkFormat f) {
    return f == VK_FORMAT_B8G8R8A8_SRGB || f == VK_FORMAT_R8G8B8A8_SRGB ||
           f == VK_FORMAT_A8B8G8R8_SRGB_PACK32;
}

bool isBgr(VkFormat f) {
    return f == VK_FORMAT_B8G8R8A8_UNORM || f == VK_FORMAT_B8G8R8A8_SRGB;
}

const char* presentModeName(VkPresentModeKHR m) {
    switch (m) {
    case VK_PRESENT_MODE_FIFO_KHR:
        return "FIFO";
    case VK_PRESENT_MODE_FIFO_RELAXED_KHR:
        return "FIFO_RELAXED";
    case VK_PRESENT_MODE_MAILBOX_KHR:
        return "MAILBOX";
    case VK_PRESENT_MODE_IMMEDIATE_KHR:
        return "IMMEDIATE";
    default:
        return "other";
    }
}

VkImageMemoryBarrier imageBarrier(VkImage image, VkImageLayout from, VkImageLayout to, VkAccessFlags src,
                                  VkAccessFlags dst) {
    VkImageMemoryBarrier b{};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.srcAccessMask = src;
    b.dstAccessMask = dst;
    b.oldLayout = from;
    b.newLayout = to;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    return b;
}

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                             VkDebugUtilsMessageTypeFlagsEXT /*type*/,
                                             const VkDebugUtilsMessengerCallbackDataEXT* data,
                                             void* /*user*/) {
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        log::error("render: [vulkan] {}", data->pMessage);
    else if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        log::warn("render: [vulkan] {}", data->pMessage);
    return VK_FALSE;
}

struct PushConstants {
    float srcSize[4];
    float outSize[4];
    float uvRect[4];
    float params[4];
};

} // namespace

class VulkanBackend final : public Backend {
public:
    VulkanBackend() = default;
    ~VulkanBackend() override { destroy(); }

    VulkanBackend(const VulkanBackend&) = delete;
    VulkanBackend& operator=(const VulkanBackend&) = delete;

    BackendKind kind() const override { return BackendKind::Vulkan; }
    std::string_view name() const override { return "Vulkan"; }
    std::string_view deviceName() const override { return m_deviceName; }
    SDL_WindowFlags windowFlags() const override { return SDL_WINDOW_VULKAN; }

    bool init(SDL_Window* window, int outW, int outH, bool vsync) override;
    DrawResult draw(const FrameData& f) override;
    void detach() override {
        if (m_device)
            vkDeviceWaitIdle(m_device);
    }

private:
    struct Image {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
    };

    struct Frame {
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        VkSemaphore acquired = VK_NULL_HANDLE;
        VkBuffer staging = VK_NULL_HANDLE;
        VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
        std::uint8_t* stagingMap = nullptr;
    };

    // Swapchain creation outcome.
    enum class SwapchainStatus { Ready, NoArea, Failed };

    bool createInstance();
    bool chooseDevice();
    bool createDevice();
    bool createObjects();
    bool createSource(int w, int h);
    void destroySource();
    SwapchainStatus createSwapchain(int wantW, int wantH, bool vsync);
    void destroySwapchainImages();
    void destroy();

    int findMemory(std::uint32_t bits, VkMemoryPropertyFlags need, VkMemoryPropertyFlags prefer) const;
    bool allocateMemory(const VkMemoryRequirements& req, VkMemoryPropertyFlags need,
                        VkMemoryPropertyFlags prefer, VkDeviceMemory* out, VkMemoryPropertyFlags* got);
    bool createImage(Image& img, std::uint32_t w, std::uint32_t h, VkFormat format, VkImageUsageFlags usage);
    void destroyImage(Image& img);
    bool createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags prefer,
                      VkBuffer* buffer, VkDeviceMemory* memory, void** map, bool* coherent);
    bool beginOnce(VkCommandBuffer* cb);
    bool endOnce(VkCommandBuffer cb);
    bool clearImages(std::initializer_list<Image*> images);
    void writeDescriptors();
    VkRenderPass createPass(VkFormat format, bool present);
    VkPipeline createPipeline(VkRenderPass pass, VkShaderModule fs, bool blend);
    VkShaderModule createShader(const std::uint32_t* code, std::size_t size);
    bool ensureReadback(VkDeviceSize size);
    void drawQuad(VkCommandBuffer cb, const Rect& r, const PushConstants& pc);
    DrawResult lost(const char* what, VkResult r);

    SDL_Window* m_window = nullptr;
    bool m_libraryLoaded = false;
    std::string m_deviceName;
    VkInstance m_instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_messenger = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkPhysicalDevice m_physical = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties m_props{};
    VkPhysicalDeviceMemoryProperties m_memProps{};
    std::uint32_t m_queueFamily = 0;
    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_queue = VK_NULL_HANDLE;
    VkCommandPool m_pool = VK_NULL_HANDLE;
    std::array<Frame, kFramesInFlight> m_frames{};
    std::uint32_t m_frame = 0;

    VkSampler m_nearest = VK_NULL_HANDLE;
    VkSampler m_linear = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_setLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_pipeLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descPool = VK_NULL_HANDLE;
    VkDescriptorSet m_resolveSet = VK_NULL_HANDLE;
    VkDescriptorSet m_presentSet = VK_NULL_HANDLE;
    VkShaderModule m_vs = VK_NULL_HANDLE;
    VkShaderModule m_fsResolve = VK_NULL_HANDLE;
    VkShaderModule m_fsPresent = VK_NULL_HANDLE;
    VkRenderPass m_resolvePass = VK_NULL_HANDLE;
    VkRenderPass m_presentPass = VK_NULL_HANDLE;
    VkPipeline m_resolvePipe = VK_NULL_HANDLE;
    VkPipeline m_presentPipe = VK_NULL_HANDLE;

    // Source-sized resources.
    Image m_indexImg, m_paletteImg, m_resolvedImg, m_overlayImg;
    VkFramebuffer m_resolvedFb = VK_NULL_HANDLE;
    int m_srcW = 0;
    int m_srcH = 0;
    VkDeviceSize m_stagingSize = 0;
    VkDeviceSize m_offPalette = 0;
    VkDeviceSize m_offOverlay = 0;
    std::uint64_t m_uploadedSeq = 0;
    std::uint64_t m_overlaySeq = 0;

    // Swapchain.
    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    VkSurfaceFormatKHR m_format{};
    bool m_srgb = false;
    bool m_canReadback = false;
    VkPresentModeKHR m_mode = VK_PRESENT_MODE_FIFO_KHR;
    bool m_vsync = true;
    VkExtent2D m_extent{};
    std::vector<VkImage> m_images;
    std::vector<VkImageView> m_views;
    std::vector<VkFramebuffer> m_fbs;
    std::vector<VkSemaphore> m_rendered; // per swapchain image: render done -> present
    bool m_needRecreate = false;
    int m_wantW = 0;
    int m_wantH = 0;
    bool m_loggedSwapchain = false;

    // Screenshot readback.
    VkBuffer m_readback = VK_NULL_HANDLE;
    VkDeviceMemory m_readbackMemory = VK_NULL_HANDLE;
    VkDeviceSize m_readbackSize = 0;
    void* m_readbackMap = nullptr;
    bool m_readbackCoherent = false;
};

// ---------------------------------------------------------------------------- helpers

int VulkanBackend::findMemory(std::uint32_t bits, VkMemoryPropertyFlags need,
                              VkMemoryPropertyFlags prefer) const {
    for (int pass = 0; pass < 2; ++pass) {
        VkMemoryPropertyFlags want = pass == 0 ? need | prefer : need;
        for (std::uint32_t i = 0; i < m_memProps.memoryTypeCount; ++i)
            if ((bits & (1u << i)) && (m_memProps.memoryTypes[i].propertyFlags & want) == want)
                return static_cast<int>(i);
    }
    return -1;
}

bool VulkanBackend::allocateMemory(const VkMemoryRequirements& req, VkMemoryPropertyFlags need,
                                   VkMemoryPropertyFlags prefer, VkDeviceMemory* out,
                                   VkMemoryPropertyFlags* got) {
    int type = findMemory(req.memoryTypeBits, need, prefer);
    if (type < 0) {
        log::error("render: Vulkan: no suitable memory type");
        return false;
    }
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = static_cast<std::uint32_t>(type);
    BRC_VK_CHECK(vkAllocateMemory(m_device, &ai, nullptr, out));
    if (got)
        *got = m_memProps.memoryTypes[type].propertyFlags;
    return true;
}

void VulkanBackend::destroyImage(Image& img) {
    if (img.view)
        vkDestroyImageView(m_device, img.view, nullptr);
    if (img.image)
        vkDestroyImage(m_device, img.image, nullptr);
    if (img.memory)
        vkFreeMemory(m_device, img.memory, nullptr);
    img = Image{};
}

bool VulkanBackend::createImage(Image& img, std::uint32_t w, std::uint32_t h, VkFormat format,
                                VkImageUsageFlags usage) {
    VkImageCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = format;
    ci.extent = {w, h, 1};
    ci.mipLevels = 1;
    ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = usage;
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    BRC_VK_CHECK(vkCreateImage(m_device, &ci, nullptr, &img.image));
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(m_device, img.image, &req);
    if (!allocateMemory(req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, &img.memory, nullptr) &&
        !allocateMemory(req, 0, 0, &img.memory, nullptr))
        return false;
    BRC_VK_CHECK(vkBindImageMemory(m_device, img.image, img.memory, 0));
    VkImageViewCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = img.image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = format;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    BRC_VK_CHECK(vkCreateImageView(m_device, &vi, nullptr, &img.view));
    return true;
}

bool VulkanBackend::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags prefer,
                                 VkBuffer* buffer, VkDeviceMemory* memory, void** map, bool* coherent) {
    VkBufferCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    ci.size = size;
    ci.usage = usage;
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    BRC_VK_CHECK(vkCreateBuffer(m_device, &ci, nullptr, buffer));
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(m_device, *buffer, &req);
    VkMemoryPropertyFlags got = 0;
    if (!allocateMemory(req, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, prefer, memory, &got))
        return false;
    BRC_VK_CHECK(vkBindBufferMemory(m_device, *buffer, *memory, 0));
    BRC_VK_CHECK(vkMapMemory(m_device, *memory, 0, VK_WHOLE_SIZE, 0, map));
    if (coherent)
        *coherent = (got & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
    return true;
}

// Record and run a one-off command buffer synchronously (init / source resize only).
bool VulkanBackend::beginOnce(VkCommandBuffer* cb) {
    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = m_pool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    BRC_VK_CHECK(vkAllocateCommandBuffers(m_device, &ai, cb));
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    BRC_VK_CHECK(vkBeginCommandBuffer(*cb, &bi));
    return true;
}

bool VulkanBackend::endOnce(VkCommandBuffer cb) {
    bool ok = vkEndCommandBuffer(cb) == VK_SUCCESS;
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cb;
    ok = ok && vkQueueSubmit(m_queue, 1, &si, VK_NULL_HANDLE) == VK_SUCCESS;
    ok = ok && vkQueueWaitIdle(m_queue) == VK_SUCCESS;
    vkFreeCommandBuffers(m_device, m_pool, 1, &cb);
    if (!ok)
        log::error("render: Vulkan: one-time command submission failed");
    return ok;
}

// Clear images to black and leave them in SHADER_READ_ONLY_OPTIMAL so that descriptors
// never refer to an image in an undefined layout.
bool VulkanBackend::clearImages(std::initializer_list<Image*> images) {
    VkCommandBuffer cb = VK_NULL_HANDLE;
    if (!beginOnce(&cb))
        return false;
    std::vector<VkImageMemoryBarrier> b;
    for (Image* img : images)
        b.push_back(imageBarrier(img->image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                 0, VK_ACCESS_TRANSFER_WRITE_BIT));
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr,
                         0, nullptr, static_cast<std::uint32_t>(b.size()), b.data());
    VkClearColorValue black{};
    black.float32[3] = 1.0f;
    VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    for (Image* img : images)
        vkCmdClearColorImage(cb, img->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &range);
    b.clear();
    for (Image* img : images)
        b.push_back(imageBarrier(img->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                                 VK_ACCESS_SHADER_READ_BIT));
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
                         nullptr, 0, nullptr, static_cast<std::uint32_t>(b.size()), b.data());
    return endOnce(cb);
}

void VulkanBackend::writeDescriptors() {
    const VkDescriptorImageInfo info[4] = {
        {m_nearest, m_indexImg.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {m_nearest, m_paletteImg.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {m_linear, m_resolvedImg.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {m_nearest, m_overlayImg.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
    };
    VkWriteDescriptorSet w[4]{};
    for (int i = 0; i < 4; ++i) {
        w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[i].dstSet = i < 2 ? m_resolveSet : m_presentSet;
        w[i].dstBinding = static_cast<std::uint32_t>(i & 1);
        w[i].descriptorCount = 1;
        w[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w[i].pImageInfo = &info[i];
    }
    vkUpdateDescriptorSets(m_device, 4, w, 0, nullptr);
}

// ---------------------------------------------------------------------------- pipelines

VkRenderPass VulkanBackend::createPass(VkFormat format, bool present) {
    VkAttachmentDescription att{};
    att.format = format;
    att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = present ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att.finalLayout = present ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &ref;
    VkSubpassDependency deps[2]{};
    if (present) {
        // Wait for the acquire semaphore (signalled at COLOR_ATTACHMENT_OUTPUT) ...
        deps[0] = {VK_SUBPASS_EXTERNAL,
                   0,
                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                   0,
                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                   0};
        // ... and make the result available to an optional screenshot copy.
        deps[1] = {0,
                   VK_SUBPASS_EXTERNAL,
                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                   VK_PIPELINE_STAGE_TRANSFER_BIT,
                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                   VK_ACCESS_TRANSFER_READ_BIT,
                   0};
    } else {
        // The previous present pass may still be sampling the resolved image.
        deps[0] = {VK_SUBPASS_EXTERNAL,
                   0,
                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                   0,
                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                   0};
        deps[1] = {0,
                   VK_SUBPASS_EXTERNAL,
                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                   VK_ACCESS_SHADER_READ_BIT,
                   0};
    }
    VkRenderPassCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ci.attachmentCount = 1;
    ci.pAttachments = &att;
    ci.subpassCount = 1;
    ci.pSubpasses = &sub;
    ci.dependencyCount = 2;
    ci.pDependencies = deps;
    VkRenderPass rp = VK_NULL_HANDLE;
    check(vkCreateRenderPass(m_device, &ci, nullptr, &rp), "vkCreateRenderPass");
    return rp;
}

VkPipeline VulkanBackend::createPipeline(VkRenderPass pass, VkShaderModule fs, bool blend) {
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = m_vs;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs;
    stages[1].pName = "main";
    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState att{};
    att.blendEnable = blend ? VK_TRUE : VK_FALSE;
    att.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    att.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    att.colorBlendOp = VK_BLEND_OP_ADD;
    att.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    att.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    att.alphaBlendOp = VK_BLEND_OP_ADD;
    att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                         VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &att;
    const VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    ds.dynamicStateCount = 2;
    ds.pDynamicStates = dyn;
    VkGraphicsPipelineCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    ci.stageCount = 2;
    ci.pStages = stages;
    ci.pVertexInputState = &vi;
    ci.pInputAssemblyState = &ia;
    ci.pViewportState = &vp;
    ci.pRasterizationState = &rs;
    ci.pMultisampleState = &ms;
    ci.pColorBlendState = &cb;
    ci.pDynamicState = &ds;
    ci.layout = m_pipeLayout;
    ci.renderPass = pass;
    VkPipeline p = VK_NULL_HANDLE;
    check(vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &ci, nullptr, &p),
          "vkCreateGraphicsPipelines");
    return p;
}

VkShaderModule VulkanBackend::createShader(const std::uint32_t* code, std::size_t size) {
    VkShaderModuleCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = size;
    ci.pCode = code;
    VkShaderModule m = VK_NULL_HANDLE;
    check(vkCreateShaderModule(m_device, &ci, nullptr, &m), "vkCreateShaderModule");
    return m;
}

// ---------------------------------------------------------------------------- source resources

void VulkanBackend::destroySource() {
    if (m_resolvedFb)
        vkDestroyFramebuffer(m_device, m_resolvedFb, nullptr);
    m_resolvedFb = VK_NULL_HANDLE;
    destroyImage(m_indexImg);
    destroyImage(m_resolvedImg);
    for (Frame& f : m_frames) {
        if (f.staging)
            vkDestroyBuffer(m_device, f.staging, nullptr);
        if (f.stagingMemory)
            vkFreeMemory(m_device, f.stagingMemory, nullptr); // implicitly unmapped
        f.staging = VK_NULL_HANDLE;
        f.stagingMemory = VK_NULL_HANDLE;
        f.stagingMap = nullptr;
    }
    m_srcW = m_srcH = 0;
}

// (Re)create everything whose size depends on the source frame.
bool VulkanBackend::createSource(int w, int h) {
    if (m_srcW) {
        vkDeviceWaitIdle(m_device);
        destroySource();
    }
    if (!createImage(m_indexImg, static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h),
                     VK_FORMAT_R8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT) ||
        !createImage(m_resolvedImg, static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h),
                     kResolvedFormat,
                     VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                         VK_IMAGE_USAGE_TRANSFER_DST_BIT))
        return false;
    VkFramebufferCreateInfo fi{};
    fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fi.renderPass = m_resolvePass;
    fi.attachmentCount = 1;
    fi.pAttachments = &m_resolvedImg.view;
    fi.width = static_cast<std::uint32_t>(w);
    fi.height = static_cast<std::uint32_t>(h);
    fi.layers = 1;
    BRC_VK_CHECK(vkCreateFramebuffer(m_device, &fi, nullptr, &m_resolvedFb));
    // Staging layout: indices | palette (1 KiB) | overlay, each 256-byte aligned.
    m_offPalette = (static_cast<VkDeviceSize>(w) * static_cast<VkDeviceSize>(h) + 255) & ~VkDeviceSize{255};
    m_offOverlay = m_offPalette + 1024;
    m_stagingSize = m_offOverlay + kOverlayTexW * kOverlayTexH;
    for (Frame& f : m_frames) {
        bool coherent = false;
        void* map = nullptr;
        if (!createBuffer(m_stagingSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &f.staging, &f.stagingMemory, &map,
                          &coherent))
            return false;
        f.stagingMap = static_cast<std::uint8_t*>(map);
        if (!coherent) {
            log::error("render: Vulkan: no host-coherent staging memory");
            return false;
        }
    }
    if (!clearImages({&m_indexImg, &m_resolvedImg}))
        return false;
    m_srcW = w;
    m_srcH = h;
    m_uploadedSeq = 0;
    writeDescriptors();
    return true;
}

// ---------------------------------------------------------------------------- swapchain

void VulkanBackend::destroySwapchainImages() {
    for (VkFramebuffer fb : m_fbs)
        if (fb)
            vkDestroyFramebuffer(m_device, fb, nullptr);
    for (VkImageView v : m_views)
        if (v)
            vkDestroyImageView(m_device, v, nullptr);
    for (VkSemaphore s : m_rendered)
        if (s)
            vkDestroySemaphore(m_device, s, nullptr);
    m_fbs.clear();
    m_views.clear();
    m_rendered.clear();
    m_images.clear();
}

VulkanBackend::SwapchainStatus VulkanBackend::createSwapchain(int wantW, int wantH, bool vsync) {
    VkSurfaceCapabilitiesKHR caps;
    VkResult r = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physical, m_surface, &caps);
    if (r != VK_SUCCESS) {
        log::error("render: Vulkan: cannot query the surface: {}", resultName(r));
        return SwapchainStatus::Failed;
    }
    VkExtent2D ext = caps.currentExtent;
    if (ext.width == UINT32_MAX) { // the window size decides (Wayland, headless)
        auto fit = [](int want, std::uint32_t lo, std::uint32_t hi) {
            std::uint32_t v = want > 0 ? static_cast<std::uint32_t>(want) : 0;
            return std::max(lo, std::min(v, hi));
        };
        ext.width = fit(wantW, caps.minImageExtent.width, caps.maxImageExtent.width);
        ext.height = fit(wantH, caps.minImageExtent.height, caps.maxImageExtent.height);
    }
    if (ext.width == 0 || ext.height == 0 || wantW <= 0 || wantH <= 0)
        return SwapchainStatus::NoArea;

    // Surface format: prefer plain 8-bit UNORM (the image holds sRGB-encoded values);
    // an sRGB format works too, the present shader then pre-linearises.
    VkSurfaceFormatKHR fmt{VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    std::uint32_t n = 0;
    std::vector<VkSurfaceFormatKHR> fmts;
    if (vkGetPhysicalDeviceSurfaceFormatsKHR(m_physical, m_surface, &n, nullptr) == VK_SUCCESS && n) {
        fmts.resize(n);
        if (vkGetPhysicalDeviceSurfaceFormatsKHR(m_physical, m_surface, &n, fmts.data()) < 0)
            n = 0;
        fmts.resize(n);
    }
    if (!fmts.empty() && !(fmts.size() == 1 && fmts[0].format == VK_FORMAT_UNDEFINED)) {
        bool found = false;
        for (int pass = 0; pass < 2 && !found; ++pass)
            for (const auto& f : fmts) {
                bool ok = pass == 0 ? (f.format == VK_FORMAT_B8G8R8A8_UNORM ||
                                       f.format == VK_FORMAT_R8G8B8A8_UNORM ||
                                       f.format == VK_FORMAT_A8B8G8R8_UNORM_PACK32)
                                    : isSrgb(f.format);
                if (ok && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                    fmt = f;
                    found = true;
                    break;
                }
            }
        if (!found)
            fmt = fmts[0];
    }

    // Present mode: FIFO (always supported) waits for the display's own refresh, whatever
    // its rate. Without vsync prefer MAILBOX (no tearing), then IMMEDIATE.
    VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
    if (!vsync) {
        std::vector<VkPresentModeKHR> modes;
        n = 0;
        if (vkGetPhysicalDeviceSurfacePresentModesKHR(m_physical, m_surface, &n, nullptr) == VK_SUCCESS &&
            n) {
            modes.resize(n);
            if (vkGetPhysicalDeviceSurfacePresentModesKHR(m_physical, m_surface, &n, modes.data()) < 0)
                n = 0;
            modes.resize(n);
        }
        auto has = [&](VkPresentModeKHR m) {
            return std::find(modes.begin(), modes.end(), m) != modes.end();
        };
        if (has(VK_PRESENT_MODE_MAILBOX_KHR))
            mode = VK_PRESENT_MODE_MAILBOX_KHR;
        else if (has(VK_PRESENT_MODE_IMMEDIATE_KHR))
            mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
    }

    std::uint32_t count = caps.minImageCount + 1;
    if (caps.maxImageCount && count > caps.maxImageCount)
        count = caps.maxImageCount;
    VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (!(caps.supportedCompositeAlpha & alpha))
        for (std::uint32_t b = 1; b; b <<= 1)
            if (caps.supportedCompositeAlpha & b) {
                alpha = static_cast<VkCompositeAlphaFlagBitsKHR>(b);
                break;
            }
    VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    bool readbackFormat = fmt.format == VK_FORMAT_B8G8R8A8_UNORM || fmt.format == VK_FORMAT_R8G8B8A8_UNORM ||
                          fmt.format == VK_FORMAT_A8B8G8R8_UNORM_PACK32 || isSrgb(fmt.format);
    bool readback = readbackFormat && (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    if (readback)
        usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

    vkDeviceWaitIdle(m_device);
    VkSwapchainKHR old = m_swapchain;
    VkSwapchainCreateInfoKHR ci{};
    ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface = m_surface;
    ci.minImageCount = count;
    ci.imageFormat = fmt.format;
    ci.imageColorSpace = fmt.colorSpace;
    ci.imageExtent = ext;
    ci.imageArrayLayers = 1;
    ci.imageUsage = usage;
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.preTransform = (caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
                          ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR
                          : caps.currentTransform;
    ci.compositeAlpha = alpha;
    ci.presentMode = mode;
    ci.clipped = VK_TRUE;
    ci.oldSwapchain = old;
    VkSwapchainKHR sc = VK_NULL_HANDLE;
    r = vkCreateSwapchainKHR(m_device, &ci, nullptr, &sc);
    destroySwapchainImages();
    if (old)
        vkDestroySwapchainKHR(m_device, old, nullptr); // retired even if creation failed
    m_swapchain = VK_NULL_HANDLE;
    if (r != VK_SUCCESS) {
        log::error("render: Vulkan: vkCreateSwapchainKHR failed: {}", resultName(r));
        return SwapchainStatus::Failed;
    }
    m_swapchain = sc;

    if (!m_presentPass || fmt.format != m_format.format) {
        if (m_presentPipe)
            vkDestroyPipeline(m_device, m_presentPipe, nullptr);
        if (m_presentPass)
            vkDestroyRenderPass(m_device, m_presentPass, nullptr);
        m_presentPipe = VK_NULL_HANDLE;
        m_presentPass = createPass(fmt.format, true);
        if (!m_presentPass)
            return SwapchainStatus::Failed;
        m_presentPipe = createPipeline(m_presentPass, m_fsPresent, true);
        if (!m_presentPipe)
            return SwapchainStatus::Failed;
    }
    n = 0;
    if (vkGetSwapchainImagesKHR(m_device, sc, &n, nullptr) != VK_SUCCESS || n == 0) {
        log::error("render: Vulkan: unusable swapchain image count {}", n);
        return SwapchainStatus::Failed;
    }
    m_images.resize(n);
    if (vkGetSwapchainImagesKHR(m_device, sc, &n, m_images.data()) != VK_SUCCESS)
        return SwapchainStatus::Failed;
    m_views.assign(n, VK_NULL_HANDLE);
    m_fbs.assign(n, VK_NULL_HANDLE);
    m_rendered.assign(n, VK_NULL_HANDLE);
    for (std::uint32_t i = 0; i < n; ++i) {
        VkImageViewCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = m_images[i];
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = fmt.format;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkSemaphoreCreateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        if (vkCreateImageView(m_device, &vi, nullptr, &m_views[i]) != VK_SUCCESS) {
            log::error("render: Vulkan: cannot create swapchain image views");
            return SwapchainStatus::Failed;
        }
        VkFramebufferCreateInfo fi{};
        fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fi.renderPass = m_presentPass;
        fi.attachmentCount = 1;
        fi.pAttachments = &m_views[i];
        fi.width = ext.width;
        fi.height = ext.height;
        fi.layers = 1;
        if (vkCreateFramebuffer(m_device, &fi, nullptr, &m_fbs[i]) != VK_SUCCESS ||
            vkCreateSemaphore(m_device, &si, nullptr, &m_rendered[i]) != VK_SUCCESS) {
            log::error("render: Vulkan: cannot create swapchain image resources");
            return SwapchainStatus::Failed;
        }
    }
    bool changed = !m_loggedSwapchain || mode != m_mode || fmt.format != m_format.format;
    m_format = fmt;
    m_srgb = isSrgb(fmt.format);
    m_canReadback = readback;
    m_mode = mode;
    m_vsync = vsync;
    m_extent = ext;
    if (changed)
        log::info("render: Vulkan swapchain {}x{}, format {}{}, {}, {} images", ext.width, ext.height,
                  static_cast<int>(fmt.format), m_srgb ? " (sRGB)" : "", presentModeName(mode), n);
    else
        log::debug("render: Vulkan swapchain {}x{}", ext.width, ext.height);
    m_loggedSwapchain = true;
    return SwapchainStatus::Ready;
}

// ---------------------------------------------------------------------------- init

bool VulkanBackend::createInstance() {
    if (!SDL_Vulkan_LoadLibrary(nullptr)) {
        log::warn("render: Vulkan: cannot load the Vulkan library: {}", SDL_GetError());
        return false;
    }
    m_libraryLoaded = true;
    auto getProc = reinterpret_cast<PFN_vkGetInstanceProcAddr>(SDL_Vulkan_GetVkGetInstanceProcAddr());
    if (!getProc) {
        log::warn("render: Vulkan: no vkGetInstanceProcAddr: {}", SDL_GetError());
        return false;
    }
    volkInitializeCustom(getProc);

    Uint32 sdlCount = 0;
    const char* const* sdlExts = SDL_Vulkan_GetInstanceExtensions(&sdlCount);
    if (!sdlExts) {
        log::warn("render: Vulkan: {}", SDL_GetError());
        return false;
    }
    std::vector<const char*> exts(sdlExts, sdlExts + sdlCount);
    auto enable = [&exts](const char* name) {
        if (std::none_of(exts.begin(), exts.end(),
                         [name](const char* e) { return std::strcmp(e, name) == 0; }))
            exts.push_back(name);
    };

    std::uint32_t n = 0;
    std::vector<VkExtensionProperties> avail;
    if (vkEnumerateInstanceExtensionProperties(nullptr, &n, nullptr) == VK_SUCCESS && n) {
        avail.resize(n);
        if (vkEnumerateInstanceExtensionProperties(nullptr, &n, avail.data()) < 0)
            n = 0;
        avail.resize(n);
    }

    std::vector<const char*> layers;
    bool debug = false;
    if (envFlag("OPENBRC_VK_VALIDATION")) {
        std::uint32_t nl = 0;
        std::vector<VkLayerProperties> lp;
        if (vkEnumerateInstanceLayerProperties(&nl, nullptr) == VK_SUCCESS && nl) {
            lp.resize(nl);
            if (vkEnumerateInstanceLayerProperties(&nl, lp.data()) < 0)
                nl = 0;
            lp.resize(nl);
        }
        for (const auto& l : lp)
            if (std::strcmp(l.layerName, "VK_LAYER_KHRONOS_validation") == 0)
                layers.push_back("VK_LAYER_KHRONOS_validation");
        if (layers.empty())
            log::warn(
                "render: Vulkan: OPENBRC_VK_VALIDATION is set but the validation layer is not installed");
        else
            log::info("render: Vulkan validation layer enabled");
        // The layer adds VK_EXT_debug_utils even when the driver lacks it.
        bool debugUtils = hasExtension(avail, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        if (!debugUtils && !layers.empty()) {
            std::uint32_t ln = 0;
            if (vkEnumerateInstanceExtensionProperties("VK_LAYER_KHRONOS_validation", &ln, nullptr) ==
                VK_SUCCESS) {
                std::vector<VkExtensionProperties> le(ln);
                if (vkEnumerateInstanceExtensionProperties("VK_LAYER_KHRONOS_validation", &ln, le.data()) >=
                    0)
                    debugUtils = hasExtension(le, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            }
        }
        if (debugUtils) {
            enable(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            debug = true;
        }
    }

    // MoltenVK (macOS) is a "portability" implementation: the loader only reports it when
    // the application opts in.
    VkInstanceCreateFlags flags = 0;
    if (hasExtension(avail, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
        enable(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }
    // MoltenVK without the loader still needs this for VK_KHR_portability_subset devices.
    if (hasExtension(avail, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME))
        enable(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);

    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "OpenBRC";
    app.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    app.pEngineName = "OpenBRC";
    app.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    app.apiVersion = VK_API_VERSION_1_0;
    VkInstanceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.flags = flags;
    ci.pApplicationInfo = &app;
    ci.enabledLayerCount = static_cast<std::uint32_t>(layers.size());
    ci.ppEnabledLayerNames = layers.data();
    ci.enabledExtensionCount = static_cast<std::uint32_t>(exts.size());
    ci.ppEnabledExtensionNames = exts.data();
    BRC_VK_CHECK(vkCreateInstance(&ci, nullptr, &m_instance));
    volkLoadInstanceOnly(m_instance);

    if (debug && vkCreateDebugUtilsMessengerEXT) {
        VkDebugUtilsMessengerCreateInfoEXT di{};
        di.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        di.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        di.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        di.pfnUserCallback = debugCallback;
        check(vkCreateDebugUtilsMessengerEXT(m_instance, &di, nullptr, &m_messenger),
              "vkCreateDebugUtilsMessengerEXT");
    }
    return true;
}

// Pick a GPU that can present to the window: prefer discrete, then integrated, ...
// OPENBRC_VK_DEVICE=<index> overrides the choice.
bool VulkanBackend::chooseDevice() {
    std::uint32_t n = 0;
    if (vkEnumeratePhysicalDevices(m_instance, &n, nullptr) != VK_SUCCESS || n == 0) {
        log::warn("render: Vulkan: no physical devices");
        return false;
    }
    std::vector<VkPhysicalDevice> devs(n);
    if (vkEnumeratePhysicalDevices(m_instance, &n, devs.data()) < 0)
        return false;
    devs.resize(n);
    const char* force = std::getenv("OPENBRC_VK_DEVICE");
    int bestScore = -1;
    for (std::uint32_t i = 0; i < n; ++i) {
        VkPhysicalDeviceProperties p;
        vkGetPhysicalDeviceProperties(devs[i], &p);
        std::uint32_t ne = 0;
        std::vector<VkExtensionProperties> ext;
        if (vkEnumerateDeviceExtensionProperties(devs[i], nullptr, &ne, nullptr) == VK_SUCCESS && ne) {
            ext.resize(ne);
            if (vkEnumerateDeviceExtensionProperties(devs[i], nullptr, &ne, ext.data()) < 0)
                ne = 0;
            ext.resize(ne);
        }
        if (!hasExtension(ext, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
            continue;
        std::uint32_t nq = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(devs[i], &nq, nullptr);
        std::vector<VkQueueFamilyProperties> q(nq);
        vkGetPhysicalDeviceQueueFamilyProperties(devs[i], &nq, q.data());
        int family = -1;
        for (std::uint32_t j = 0; j < nq && family < 0; ++j) {
            VkBool32 present = VK_FALSE;
            if ((q[j].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
                vkGetPhysicalDeviceSurfaceSupportKHR(devs[i], j, m_surface, &present) == VK_SUCCESS &&
                present)
                family = static_cast<int>(j);
        }
        if (family < 0)
            continue;
        int score = p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU     ? 4
                    : p.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 3
                    : p.deviceType == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU    ? 2
                    : p.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU            ? 0
                                                                             : 1;
        if (force && *force && std::atoi(force) == static_cast<int>(i))
            score = 100;
        if (score > bestScore) {
            bestScore = score;
            m_physical = devs[i];
            m_props = p;
            m_queueFamily = static_cast<std::uint32_t>(family);
        }
    }
    if (bestScore < 0) {
        log::warn("render: Vulkan: no GPU can present to this window");
        return false;
    }
    vkGetPhysicalDeviceMemoryProperties(m_physical, &m_memProps);
    m_deviceName = m_props.deviceName;
    return true;
}

bool VulkanBackend::createDevice() {
    float priority = 1.0f;
    VkDeviceQueueCreateInfo qi{};
    qi.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qi.queueFamilyIndex = m_queueFamily;
    qi.queueCount = 1;
    qi.pQueuePriorities = &priority;
    std::vector<const char*> exts{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    std::uint32_t n = 0;
    std::vector<VkExtensionProperties> avail;
    if (vkEnumerateDeviceExtensionProperties(m_physical, nullptr, &n, nullptr) == VK_SUCCESS && n) {
        avail.resize(n);
        if (vkEnumerateDeviceExtensionProperties(m_physical, nullptr, &n, avail.data()) < 0)
            n = 0;
        avail.resize(n);
    }
    // Required by the spec when the device exposes it (MoltenVK).
    if (hasExtension(avail, "VK_KHR_portability_subset"))
        exts.push_back("VK_KHR_portability_subset");
    VkDeviceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    ci.queueCreateInfoCount = 1;
    ci.pQueueCreateInfos = &qi;
    ci.enabledExtensionCount = static_cast<std::uint32_t>(exts.size());
    ci.ppEnabledExtensionNames = exts.data();
    BRC_VK_CHECK(vkCreateDevice(m_physical, &ci, nullptr, &m_device));
    volkLoadDevice(m_device);
    vkGetDeviceQueue(m_device, m_queueFamily, 0, &m_queue);
    return true;
}

bool VulkanBackend::createObjects() {
    VkCommandPoolCreateInfo pi{};
    pi.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pi.queueFamilyIndex = m_queueFamily;
    BRC_VK_CHECK(vkCreateCommandPool(m_device, &pi, nullptr, &m_pool));
    for (Frame& f : m_frames) {
        VkCommandBufferAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool = m_pool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        BRC_VK_CHECK(vkAllocateCommandBuffers(m_device, &ai, &f.cmd));
        VkFenceCreateInfo fi{};
        fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        BRC_VK_CHECK(vkCreateFence(m_device, &fi, nullptr, &f.fence));
        VkSemaphoreCreateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        BRC_VK_CHECK(vkCreateSemaphore(m_device, &si, nullptr, &f.acquired));
    }
    for (int i = 0; i < 2; ++i) {
        VkFilter filter = i ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
        VkSamplerCreateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter = filter;
        si.minFilter = filter;
        si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.maxLod = 0.0f;
        BRC_VK_CHECK(vkCreateSampler(m_device, &si, nullptr, i ? &m_linear : &m_nearest));
    }
    const VkDescriptorSetLayoutBinding bindings[2] = {
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
    };
    VkDescriptorSetLayoutCreateInfo li{};
    li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    li.bindingCount = 2;
    li.pBindings = bindings;
    BRC_VK_CHECK(vkCreateDescriptorSetLayout(m_device, &li, nullptr, &m_setLayout));
    VkPushConstantRange pcr{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants)};
    VkPipelineLayoutCreateInfo pli{};
    pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &m_setLayout;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges = &pcr;
    BRC_VK_CHECK(vkCreatePipelineLayout(m_device, &pli, nullptr, &m_pipeLayout));
    VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4};
    VkDescriptorPoolCreateInfo dpi{};
    dpi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpi.maxSets = 2;
    dpi.poolSizeCount = 1;
    dpi.pPoolSizes = &ps;
    BRC_VK_CHECK(vkCreateDescriptorPool(m_device, &dpi, nullptr, &m_descPool));
    const VkDescriptorSetLayout layouts[2] = {m_setLayout, m_setLayout};
    VkDescriptorSet sets[2] = {};
    VkDescriptorSetAllocateInfo dai{};
    dai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dai.descriptorPool = m_descPool;
    dai.descriptorSetCount = 2;
    dai.pSetLayouts = layouts;
    BRC_VK_CHECK(vkAllocateDescriptorSets(m_device, &dai, sets));
    m_resolveSet = sets[0];
    m_presentSet = sets[1];

    m_vs = createShader(shaders::kSpvFullscreenVert, sizeof shaders::kSpvFullscreenVert);
    m_fsResolve = createShader(shaders::kSpvResolveFrag, sizeof shaders::kSpvResolveFrag);
    m_fsPresent = createShader(shaders::kSpvPresentFrag, sizeof shaders::kSpvPresentFrag);
    if (!m_vs || !m_fsResolve || !m_fsPresent)
        return false;
    m_resolvePass = createPass(kResolvedFormat, false);
    if (!m_resolvePass)
        return false;
    m_resolvePipe = createPipeline(m_resolvePass, m_fsResolve, false);
    if (!m_resolvePipe)
        return false;

    if (!createImage(m_paletteImg, 256, 1, VK_FORMAT_B8G8R8A8_UNORM,
                     VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT) ||
        !createImage(m_overlayImg, kOverlayTexW, kOverlayTexH, VK_FORMAT_R8_UNORM,
                     VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT))
        return false;
    if (!clearImages({&m_paletteImg, &m_overlayImg}))
        return false;
    return createSource(640, 480);
}

bool VulkanBackend::init(SDL_Window* window, int outW, int outH, bool vsync) {
    m_window = window;
    bool ok = createInstance();
    if (ok && !SDL_Vulkan_CreateSurface(window, m_instance, nullptr, &m_surface)) {
        log::warn("render: Vulkan: cannot create a surface: {}", SDL_GetError());
        ok = false;
    }
    ok = ok && chooseDevice() && createDevice() && createObjects();
    m_wantW = outW;
    m_wantH = outH;
    ok = ok && createSwapchain(outW, outH, vsync) != SwapchainStatus::Failed;
    if (!ok) {
        destroy();
        return false;
    }
    m_vsync = vsync;
    std::uint32_t api = m_props.apiVersion;
    static constexpr const char* kTypes[] = {"other", "integrated GPU", "discrete GPU", "virtual GPU", "CPU"};
    log::info("render: Vulkan {}.{}.{} on {} ({}, driver {:#x})", VK_API_VERSION_MAJOR(api),
              VK_API_VERSION_MINOR(api), VK_API_VERSION_PATCH(api), m_deviceName,
              kTypes[m_props.deviceType <= 4 ? m_props.deviceType : 0], m_props.driverVersion);
    return true;
}

void VulkanBackend::destroy() {
    if (m_device) {
        vkDeviceWaitIdle(m_device);
        if (m_readback)
            vkDestroyBuffer(m_device, m_readback, nullptr);
        if (m_readbackMemory)
            vkFreeMemory(m_device, m_readbackMemory, nullptr);
        destroySwapchainImages();
        if (m_swapchain)
            vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
        destroySource();
        destroyImage(m_paletteImg);
        destroyImage(m_overlayImg);
        if (m_presentPipe)
            vkDestroyPipeline(m_device, m_presentPipe, nullptr);
        if (m_resolvePipe)
            vkDestroyPipeline(m_device, m_resolvePipe, nullptr);
        if (m_presentPass)
            vkDestroyRenderPass(m_device, m_presentPass, nullptr);
        if (m_resolvePass)
            vkDestroyRenderPass(m_device, m_resolvePass, nullptr);
        if (m_vs)
            vkDestroyShaderModule(m_device, m_vs, nullptr);
        if (m_fsResolve)
            vkDestroyShaderModule(m_device, m_fsResolve, nullptr);
        if (m_fsPresent)
            vkDestroyShaderModule(m_device, m_fsPresent, nullptr);
        if (m_descPool)
            vkDestroyDescriptorPool(m_device, m_descPool, nullptr);
        if (m_pipeLayout)
            vkDestroyPipelineLayout(m_device, m_pipeLayout, nullptr);
        if (m_setLayout)
            vkDestroyDescriptorSetLayout(m_device, m_setLayout, nullptr);
        if (m_nearest)
            vkDestroySampler(m_device, m_nearest, nullptr);
        if (m_linear)
            vkDestroySampler(m_device, m_linear, nullptr);
        for (Frame& f : m_frames) {
            if (f.fence)
                vkDestroyFence(m_device, f.fence, nullptr);
            if (f.acquired)
                vkDestroySemaphore(m_device, f.acquired, nullptr);
        }
        if (m_pool)
            vkDestroyCommandPool(m_device, m_pool, nullptr);
        vkDestroyDevice(m_device, nullptr);
    }
    if (m_surface)
        SDL_Vulkan_DestroySurface(m_instance, m_surface, nullptr);
    if (m_messenger && vkDestroyDebugUtilsMessengerEXT)
        vkDestroyDebugUtilsMessengerEXT(m_instance, m_messenger, nullptr);
    if (m_instance)
        vkDestroyInstance(m_instance, nullptr);
    if (m_libraryLoaded) {
        volkFinalize();
        SDL_Vulkan_UnloadLibrary();
    }
    // Back to the freshly constructed state (init() may run again after a failure).
    m_window = nullptr;
    m_libraryLoaded = false;
    m_instance = VK_NULL_HANDLE;
    m_messenger = VK_NULL_HANDLE;
    m_surface = VK_NULL_HANDLE;
    m_physical = VK_NULL_HANDLE;
    m_device = VK_NULL_HANDLE;
    m_queue = VK_NULL_HANDLE;
    m_pool = VK_NULL_HANDLE;
    m_frames = {};
    m_nearest = m_linear = VK_NULL_HANDLE;
    m_setLayout = VK_NULL_HANDLE;
    m_pipeLayout = VK_NULL_HANDLE;
    m_descPool = VK_NULL_HANDLE;
    m_resolveSet = m_presentSet = VK_NULL_HANDLE;
    m_vs = m_fsResolve = m_fsPresent = VK_NULL_HANDLE;
    m_resolvePass = m_presentPass = VK_NULL_HANDLE;
    m_resolvePipe = m_presentPipe = VK_NULL_HANDLE;
    m_swapchain = VK_NULL_HANDLE;
    m_format = {};
    m_readback = VK_NULL_HANDLE;
    m_readbackMemory = VK_NULL_HANDLE;
    m_readbackSize = 0;
    m_readbackMap = nullptr;
    m_loggedSwapchain = false;
}

// ---------------------------------------------------------------------------- drawing

DrawResult VulkanBackend::lost(const char* what, VkResult r) {
    log::error("render: Vulkan: {} failed: {} ({})", what, resultName(r), static_cast<int>(r));
    return DrawResult::Lost;
}

bool VulkanBackend::ensureReadback(VkDeviceSize size) {
    if (m_readback && m_readbackSize >= size)
        return true;
    if (m_readback)
        vkDestroyBuffer(m_device, m_readback, nullptr);
    if (m_readbackMemory)
        vkFreeMemory(m_device, m_readbackMemory, nullptr);
    m_readback = VK_NULL_HANDLE;
    m_readbackMemory = VK_NULL_HANDLE;
    m_readbackSize = 0;
    if (!createBuffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_CACHED_BIT, &m_readback,
                      &m_readbackMemory, &m_readbackMap, &m_readbackCoherent))
        return false;
    m_readbackSize = size;
    return true;
}

void VulkanBackend::drawQuad(VkCommandBuffer cb, const Rect& r, const PushConstants& pc) {
    VkViewport vp{static_cast<float>(r.x),
                  static_cast<float>(r.y),
                  static_cast<float>(r.w),
                  static_cast<float>(r.h),
                  0.0f,
                  1.0f};
    VkRect2D sc{{r.x, r.y}, {static_cast<std::uint32_t>(r.w), static_cast<std::uint32_t>(r.h)}};
    vkCmdSetViewport(cb, 0, 1, &vp);
    vkCmdSetScissor(cb, 0, 1, &sc);
    vkCmdPushConstants(cb, m_pipeLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof pc, &pc);
    vkCmdDraw(cb, 3, 1, 0, 0);
}

DrawResult VulkanBackend::draw(const FrameData& f) {
    VkResult r;
    if (!m_swapchain || m_needRecreate || f.vsync != m_vsync || f.outW != m_wantW || f.outH != m_wantH) {
        m_wantW = f.outW;
        m_wantH = f.outH;
        SwapchainStatus s = createSwapchain(f.outW, f.outH, f.vsync);
        if (s == SwapchainStatus::Failed)
            return DrawResult::Lost;
        if (s == SwapchainStatus::NoArea)
            return DrawResult::Skipped;
        m_needRecreate = false;
    }
    if ((f.srcW != m_srcW || f.srcH != m_srcH) && !createSource(f.srcW, f.srcH))
        return DrawResult::Lost;

    Frame& fr = m_frames[m_frame];
    r = vkWaitForFences(m_device, 1, &fr.fence, VK_TRUE, UINT64_MAX);
    if (r != VK_SUCCESS)
        return lost("vkWaitForFences", r);
    std::uint32_t img = 0;
    r = vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX, fr.acquired, VK_NULL_HANDLE, &img);
    if (r == VK_ERROR_OUT_OF_DATE_KHR) {
        m_needRecreate = true;
        return DrawResult::Skipped;
    }
    if (r == VK_SUBOPTIMAL_KHR)
        m_needRecreate = true; // still usable; recreate after this frame
    else if (r != VK_SUCCESS)
        return lost("vkAcquireNextImageKHR", r);
    vkResetFences(m_device, 1, &fr.fence);

    VkCommandBuffer cb = fr.cmd;
    vkResetCommandBuffer(cb, 0);
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &bi);

    // ---- uploads
    bool upload = f.frameSeq && f.index && f.frameSeq != m_uploadedSeq;
    bool uploadOverlay = f.overlay && f.overlaySeq && f.overlaySeq != m_overlaySeq;
    if (upload || uploadOverlay) {
        VkImageMemoryBarrier pre[3], post[3];
        std::uint32_t n = 0;
        auto toTransfer = [](VkImage image) {
            return imageBarrier(image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
                                VK_ACCESS_TRANSFER_WRITE_BIT);
        };
        auto toShader = [](VkImage image) {
            return imageBarrier(image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                                VK_ACCESS_SHADER_READ_BIT);
        };
        if (upload) {
            std::memcpy(fr.stagingMap, f.index,
                        static_cast<std::size_t>(f.srcW) * static_cast<std::size_t>(f.srcH));
            std::memcpy(fr.stagingMap + m_offPalette, f.palette, 1024);
            pre[n] = toTransfer(m_indexImg.image);
            post[n++] = toShader(m_indexImg.image);
            pre[n] = toTransfer(m_paletteImg.image);
            post[n++] = toShader(m_paletteImg.image);
        }
        if (uploadOverlay) {
            std::memcpy(fr.stagingMap + m_offOverlay, f.overlay, kOverlayTexW * kOverlayTexH);
            pre[n] = toTransfer(m_overlayImg.image);
            post[n++] = toShader(m_overlayImg.image);
        }
        // WAR: earlier frames may still sample these images.
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                             nullptr, 0, nullptr, n, pre);
        VkBufferImageCopy c{};
        c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        if (upload) {
            c.bufferOffset = 0;
            c.imageExtent = {static_cast<std::uint32_t>(f.srcW), static_cast<std::uint32_t>(f.srcH), 1};
            vkCmdCopyBufferToImage(cb, fr.staging, m_indexImg.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                                   &c);
            c.bufferOffset = m_offPalette;
            c.imageExtent = {256, 1, 1};
            vkCmdCopyBufferToImage(cb, fr.staging, m_paletteImg.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                   1, &c);
        }
        if (uploadOverlay) {
            c.bufferOffset = m_offOverlay;
            c.imageExtent = {kOverlayTexW, kOverlayTexH, 1};
            vkCmdCopyBufferToImage(cb, fr.staging, m_overlayImg.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                   1, &c);
        }
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
                             nullptr, 0, nullptr, n, post);
    }
    PushConstants pc{};
    pc.srcSize[0] = static_cast<float>(m_srcW);
    pc.srcSize[1] = static_cast<float>(m_srcH);
    pc.srcSize[2] = 1.0f / static_cast<float>(m_srcW);
    pc.srcSize[3] = 1.0f / static_cast<float>(m_srcH);
    pc.uvRect[2] = 1.0f;
    pc.uvRect[3] = 1.0f;
    pc.params[0] = static_cast<float>(static_cast<int>(f.filter));
    pc.params[1] = 1.0f;
    pc.params[2] = m_srgb ? 1.0f : 0.0f;

    // ---- palette resolve at source resolution
    if (upload) {
        VkRenderPassBeginInfo rb{};
        rb.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rb.renderPass = m_resolvePass;
        rb.framebuffer = m_resolvedFb;
        rb.renderArea = {{0, 0}, {static_cast<std::uint32_t>(m_srcW), static_cast<std::uint32_t>(m_srcH)}};
        vkCmdBeginRenderPass(cb, &rb, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_resolvePipe);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeLayout, 0, 1, &m_resolveSet, 0,
                                nullptr);
        drawQuad(cb, Rect{0, 0, m_srcW, m_srcH}, pc);
        vkCmdEndRenderPass(cb);
    }

    // ---- scale to the swapchain image
    VkClearValue clear{};
    clear.color.float32[3] = 1.0f;
    VkRenderPassBeginInfo pb{};
    pb.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    pb.renderPass = m_presentPass;
    pb.framebuffer = m_fbs[img];
    pb.renderArea = {{0, 0}, m_extent};
    pb.clearValueCount = 1;
    pb.pClearValues = &clear;
    vkCmdBeginRenderPass(cb, &pb, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_presentPipe);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeLayout, 0, 1, &m_presentSet, 0,
                            nullptr);
    const int extW = static_cast<int>(m_extent.width);
    const int extH = static_cast<int>(m_extent.height);
    Rect view = computeView(extW, extH, m_srcW, m_srcH, f.aspect);
    if (!view.empty() && (m_uploadedSeq || upload)) {
        pc.outSize[0] = static_cast<float>(view.w);
        pc.outSize[1] = static_cast<float>(view.h);
        pc.outSize[2] = 1.0f / static_cast<float>(view.w);
        pc.outSize[3] = 1.0f / static_cast<float>(view.h);
        drawQuad(cb, view, pc);
    }
    if (f.overlayAlpha > 0.0f && (m_overlaySeq || uploadOverlay)) {
        Rect o = overlayRect(extW, extH, f.overlayW, f.overlayH);
        if (!o.empty()) {
            pc.uvRect[2] = static_cast<float>(f.overlayW) / kOverlayTexW;
            pc.uvRect[3] = static_cast<float>(f.overlayH) / kOverlayTexH;
            pc.params[0] = kModeOverlay;
            pc.params[1] = f.overlayAlpha;
            drawQuad(cb, o, pc);
        }
    }
    vkCmdEndRenderPass(cb);

    // ---- optional screenshot of the output
    bool shot = f.capture && m_canReadback &&
                ensureReadback(static_cast<VkDeviceSize>(m_extent.width) * m_extent.height * 4);
    if (shot) {
        VkImageMemoryBarrier b =
            imageBarrier(m_images[img], VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         0, VK_ACCESS_TRANSFER_READ_BIT);
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                             nullptr, 0, nullptr, 1, &b);
        VkBufferImageCopy c{};
        c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        c.imageExtent = {m_extent.width, m_extent.height, 1};
        vkCmdCopyImageToBuffer(cb, m_images[img], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_readback, 1, &c);
        b = imageBarrier(m_images[img], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                         VK_ACCESS_TRANSFER_READ_BIT, 0);
        VkBufferMemoryBarrier hb{};
        hb.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        hb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        hb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        hb.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        hb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        hb.buffer = m_readback;
        hb.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0,
                             nullptr, 0, nullptr, 1, &b);
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1,
                             &hb, 0, nullptr);
    } else if (f.capture && !m_canReadback) {
        log::warn("render: Vulkan: this swapchain cannot be read back for a screenshot");
    }
    vkEndCommandBuffer(cb);

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &fr.acquired;
    si.pWaitDstStageMask = &waitStage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cb;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &m_rendered[img];
    r = vkQueueSubmit(m_queue, 1, &si, fr.fence);
    if (r != VK_SUCCESS)
        return lost("vkQueueSubmit", r);
    if (upload)
        m_uploadedSeq = f.frameSeq;
    if (uploadOverlay)
        m_overlaySeq = f.overlaySeq;

    VkPresentInfoKHR pi{};
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &m_rendered[img];
    pi.swapchainCount = 1;
    pi.pSwapchains = &m_swapchain;
    pi.pImageIndices = &img;
    r = vkQueuePresentKHR(m_queue, &pi);
    m_frame = (m_frame + 1) % kFramesInFlight;
    DrawResult result = DrawResult::Ok;
    if (r == VK_ERROR_OUT_OF_DATE_KHR) {
        m_needRecreate = true;
        result = DrawResult::Skipped; // not shown: draw it again on the new swapchain
    } else if (r == VK_SUBOPTIMAL_KHR) {
        m_needRecreate = true;
    } else if (r != VK_SUCCESS) {
        return lost("vkQueuePresentKHR", r);
    }

    if (shot) {
        r = vkWaitForFences(m_device, 1, &fr.fence, VK_TRUE, UINT64_MAX);
        if (r != VK_SUCCESS)
            return lost("vkWaitForFences", r);
        if (!m_readbackCoherent) {
            VkMappedMemoryRange mr{};
            mr.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
            mr.memory = m_readbackMemory;
            mr.size = VK_WHOLE_SIZE;
            vkInvalidateMappedMemoryRanges(m_device, 1, &mr);
        }
        Capture& cap = *f.capture;
        cap.width = static_cast<int>(m_extent.width);
        cap.height = static_cast<int>(m_extent.height);
        std::size_t bytes = static_cast<std::size_t>(cap.width) * static_cast<std::size_t>(cap.height) * 4;
        cap.rgba.resize(bytes);
        const auto* src = static_cast<const std::uint8_t*>(m_readbackMap);
        bool bgr = isBgr(m_format.format);
        for (std::size_t i = 0; i < bytes; i += 4) {
            cap.rgba[i + 0] = src[i + (bgr ? 2 : 0)];
            cap.rgba[i + 1] = src[i + 1];
            cap.rgba[i + 2] = src[i + (bgr ? 0 : 2)];
            cap.rgba[i + 3] = 255;
        }
        cap.ok = true;
    }
    return result;
}

std::unique_ptr<Backend> createVulkanBackend() {
    return std::make_unique<VulkanBackend>();
}

} // namespace brc::render
