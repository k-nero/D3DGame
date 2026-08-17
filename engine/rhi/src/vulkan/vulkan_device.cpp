//
// Created by Thanh Nguyen on 16/8/26.
//

// eng/rhi/src/vulkan/vulkan_device.cpp — milestone 2 PARITY ONLY.
//
// Scope: rainbow clear, resize, clean shutdown, driving an UNMODIFIED
// ClearSample. Everything else is engine_check(false && "m3").
//
// Baseline is VULKAN 1.3 CORE, deliberately:
//   - synchronization2  -> vkCmdPipelineBarrier2. This is the one backend where
//     barrier() is NOT a no-op, and VkPipelineStageFlags2/VkAccessFlags2/
//     VkImageLayout is exactly the triple rhi.h models. Vulkan is what actually
//     validates that seam; Metal taught us nothing there.
//   - dynamic rendering -> vkCmdBeginRendering, no VkRenderPass/VkFramebuffer
//     objects at all. A standalone clear is an empty rendering scope with
//     loadOp=CLEAR, the SAME shape the Metal backend needs. That agreement
//     across two backends is evidence the m5 render graph should own clears.
//   - timeline semaphore -> the true ID3D12Fence / MTL::SharedEvent analog.
//
// Vulkan-vs-D3D12 shape differences encoded below:
//   - the swapchain image index is NOT the frame index (same as D3D12) AND the
//     acquire is asynchronous, so it also needs a binary semaphore handshake
//     that D3D12's Present hides from you
//   - present-wait semaphores are per SWAPCHAIN IMAGE, not per frame-in-flight
//     (see the comment on render_finished_ — this one is a validation trap)
//   - vkAcquireNextImageKHR/vkQueuePresentKHR can report OUT_OF_DATE at any
//     time; resize is not the only path that rebuilds the swapchain

#if defined(_WIN32)
#  define VK_USE_PLATFORM_WIN32_KHR
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#endif

#include <vulkan/vulkan.h>

#include <engine/core/asserts.h>
#include <engine/core/log.h>
#include <engine/core/pool.h>
#include <engine/rhi/rhi.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace engine::rhi {
    namespace {
        using pool::Handle;
        using pool::Pool;

        const char *vk_result_str(const VkResult r) {
            switch (r) {
                case VK_SUCCESS: return "VK_SUCCESS";
                case VK_NOT_READY: return "VK_NOT_READY";
                case VK_TIMEOUT: return "VK_TIMEOUT";
                case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
                case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
                case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
                case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
                case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
                case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
                case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
                case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
                case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
                case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
                case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
                default: return "VK_ERROR_<other>";
            }
        }

        // The ENGINE_HR of this backend. Logs the actual VkResult before dying —
        // "vkCreateDevice failed" without the code is a wasted debugging hour.
#define ENGINE_VK(expr)                                                        \
    do {                                                                       \
        const VkResult vkr_ = (expr);                                          \
        if (vkr_ != VK_SUCCESS) {                                              \
            log::error("vulkan: {} -> {}", #expr, vk_result_str(vkr_));        \
            engine_check(false);                                               \
        }                                                                      \
    } while (0)

        // ============================================================ translation
        // The portable enhanced-barrier enums -> Vulkan sync2. This is the mapping
        // rhi.h was designed against; D3D12 and Vulkan agree almost field-for-field
        // because enhanced barriers were modeled on VK_KHR_synchronization2.

        VkPipelineStageFlags2 to_vk(const Sync s) {
            VkPipelineStageFlags2 r = VK_PIPELINE_STAGE_2_NONE;
            if (any(s & Sync::All)) r |= VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            if (any(s & Sync::Draw)) r |= VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;
            if (any(s & Sync::PixelShading)) r |= VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            if (any(s & Sync::RenderTarget)) r |= VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            if (any(s & Sync::DepthStencil))
                r |= VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                     VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            if (any(s & Sync::Compute)) r |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            if (any(s & Sync::Copy)) r |= VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
            return r;
        }

        // NOTE the contrast with the D3D12 backend: there, Access::NoAccess has to
        // translate context-sensitively because ACCESS_NO_ACCESS may only pair with
        // SYNC_NONE. Vulkan has no such rule — ACCESS_2_NONE with STAGE_2_NONE is
        // the ordinary "nothing to wait on" spelling, which is exactly what a
        // first-use / from-UNDEFINED transition wants.
        VkAccessFlags2 to_vk(const Access a) {
            VkAccessFlags2 r = VK_ACCESS_2_NONE;
            if (any(a & Access::RenderTarget))
                r |= VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
                     VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
            if (any(a & Access::DepthWrite))
                r |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                     VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
            if (any(a & Access::ShaderRead)) r |= VK_ACCESS_2_SHADER_READ_BIT;
            if (any(a & Access::UnorderedAccess))
                r |= VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                     VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
            if (any(a & Access::CopySrc)) r |= VK_ACCESS_2_TRANSFER_READ_BIT;
            if (any(a & Access::CopyDst)) r |= VK_ACCESS_2_TRANSFER_WRITE_BIT;
            return r;
        }

        VkImageLayout to_vk(const Layout l) {
            switch (l) {
                case Layout::Undefined: return VK_IMAGE_LAYOUT_UNDEFINED;
                case Layout::Present: return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
                case Layout::RenderTarget: return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                case Layout::DepthWrite: return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                case Layout::ShaderRead: return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                case Layout::UnorderedAccess: return VK_IMAGE_LAYOUT_GENERAL;
                case Layout::CopySrc: return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                case Layout::CopyDst: return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            }
            engine_check(false);
            return VK_IMAGE_LAYOUT_UNDEFINED;
        }

        // ============================================================== resources
        struct VulkanTexture {
            VkImage image = VK_NULL_HANDLE; // swapchain-owned at m2: do NOT vkDestroyImage
            VkImageView view = VK_NULL_HANDLE; // ours: vkDestroyImageView on teardown
            uint32_t width = 0, height = 0;
        };

        template<class RhiH, class T>
        RhiH to_rhi(Handle<T> h) { return RhiH{.index = h.index, .gen = h.gen}; }

        template<class T, class RhiH>
        Handle<T> to_pool(RhiH h) { return Handle<T>{.index = h.index, .gen = h.gen}; }

        class VulkanDevice;

        // =========================================================== command list
        class VulkanCommandList final : public ICommandList {
        public:
            void init(VulkanDevice *dev, const VkCommandBuffer cmd) {
                dev_ = dev;
                cmd_ = cmd;
            }

            void barrier(std::span<const TextureBarrier> textures,
                         std::span<const BufferBarrier> buffers) override;

            void clear_render_target(TextureHandle, std::array<float, 4> rgba) override;

            void set_render_targets(std::span<const TextureHandle>, TextureHandle) override {
                // Same story as Metal: with dynamic rendering the attachments belong
                // to vkCmdBeginRendering, not to persistent state. Caching the
                // VkRenderingInfo and opening the scope lazily at first draw is m3.
                engine_check(false && "m3");
            }

            void set_viewport_scissor(const uint32_t w, const uint32_t h) override {
                // Legal to record outside a rendering scope (unlike Metal, where the
                // viewport is an encoder call), so just do it.
                const VkViewport vp{
                    .x = 0.f, .y = 0.f,
                    .width = float(w), .height = float(h),
                    .minDepth = 0.f, .maxDepth = 1.f, // depth [0,1], matching the
                };                                    // math conventions in tests/
                const VkRect2D sc{.offset = {0, 0}, .extent = {w, h}};
                vkCmdSetViewport(cmd_, 0, 1, &vp);
                vkCmdSetScissor(cmd_, 0, 1, &sc);
            }

            // ---- milestone 3 ----
            void set_pso(PSOHandle) override { engine_check(false && "m3"); }
            void set_index_buffer(BufferHandle, Format) override { engine_check(false && "m3"); }
            void push_constants(const void *, uint32_t) override { engine_check(false && "m3"); }
            void draw(uint32_t, uint32_t) override { engine_check(false && "m3"); }
            void draw_indexed(uint32_t, uint32_t) override { engine_check(false && "m3"); }
            void dispatch(uint32_t, uint32_t, uint32_t) override { engine_check(false && "m3"); }

        private:
            VulkanDevice *dev_ = nullptr;
            VkCommandBuffer cmd_ = VK_NULL_HANDLE;
        };

        // ================================================================= device
        class VulkanDevice final : public IDevice {
        public:
            explicit VulkanDevice(const DeviceDesc &desc) : desc_(desc) {
                engine_check(desc.frames_in_flight >= 1 &&
                    desc.frames_in_flight <= kMaxFramesInFlight);
                engine_check(desc.native_window && "Vulkan backend needs a native window handle");

                create_instance();
                create_surface();
                pick_physical_device();
                create_device_and_queue();
                create_swapchain();
                create_frame_resources();
            }

            ~VulkanDevice() override {
                // destroy_device already ran wait_idle, so nothing is in flight.
                destroy_frame_resources();
                destroy_swapchain();
                if (device_) vkDestroyDevice(device_, nullptr);
                if (surface_) vkDestroySurfaceKHR(instance_, surface_, nullptr);
                if (messenger_) {
                    const auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                        vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"));
                    if (destroy) destroy(instance_, messenger_, nullptr);
                }
                if (instance_) vkDestroyInstance(instance_, nullptr);
            }

            // ================================================== frame loop
            FrameContext begin_frame() override {
                engine_check(!in_frame_);
                in_frame_ = true;

                const auto idx =
                        static_cast<uint32_t>(frame_counter_ % desc_.frames_in_flight);
                FrameSlot &slot = frame_[idx];

                // THE wait, on the timeline semaphore: this slot's previous
                // submission must be fully consumed before its command pool is
                // reset. Same single line as D3D12, same role as the Metal
                // SharedEvent wait.
                wait_timeline(slot.timeline_value);

                // Resetting the POOL (not the buffer) is the cheap path — it
                // recycles the pool's block allocator in one call, which is what
                // ID3D12CommandAllocator::Reset does.
                ENGINE_VK(vkResetCommandPool(device_, slot.pool, 0));

                acquire_next_image(idx);

                constexpr VkCommandBufferBeginInfo bi{
                    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                    .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
                };
                ENGINE_VK(vkBeginCommandBuffer(slot.cmd, &bi));
                cmd_.init(this, slot.cmd);

                return FrameContext{
                    .cmd = &cmd_,
                    .backbuffer = images_[image_index_].handle,
                    .frame_index = idx,
                };
            }

            void end_frame() override {
                engine_check(in_frame_);
                in_frame_ = false;

                const auto idx =
                        static_cast<uint32_t>(frame_counter_ % desc_.frames_in_flight);
                FrameSlot &slot = frame_[idx];
                ENGINE_VK(vkEndCommandBuffer(slot.cmd));

                const uint64_t v = ++timeline_value_;

                // Wait on the ACQUIRE semaphore at COLOR_ATTACHMENT_OUTPUT: the GPU
                // may run everything before that stage while the presentation engine
                // still owns the image. D3D12 hides this handshake inside Present.
                const VkSemaphoreSubmitInfo wait{
                    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                    .semaphore = slot.acquire,
                    .value = 0, // binary semaphore: value ignored
                    .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                };
                const VkSemaphoreSubmitInfo signal[2]{
                    {
                        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                        .semaphore = images_[image_index_].render_finished,
                        .value = 0, // binary
                        .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                    },
                    {
                        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                        .semaphore = timeline_,
                        .value = v, // timeline: THIS is the fence value
                        .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                    },
                };
                const VkCommandBufferSubmitInfo cbi{
                    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
                    .commandBuffer = slot.cmd,
                };
                const VkSubmitInfo2 si{
                    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
                    .waitSemaphoreInfoCount = 1,
                    .pWaitSemaphoreInfos = &wait,
                    .commandBufferInfoCount = 1,
                    .pCommandBufferInfos = &cbi,
                    .signalSemaphoreInfoCount = 2,
                    .pSignalSemaphoreInfos = signal,
                };
                ENGINE_VK(vkQueueSubmit2(queue_, 1, &si, VK_NULL_HANDLE));

                const VkPresentInfoKHR pi{
                    .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
                    .waitSemaphoreCount = 1,
                    .pWaitSemaphores = &images_[image_index_].render_finished,
                    .swapchainCount = 1,
                    .pSwapchains = &swapchain_,
                    .pImageIndices = &image_index_,
                };
                // OUT_OF_DATE/SUBOPTIMAL here are NOT errors: the window changed
                // under us. Flag it and rebuild at the top of the next frame.
                if (const VkResult pr = vkQueuePresentKHR(queue_, &pi);
                    pr == VK_ERROR_OUT_OF_DATE_KHR || pr == VK_SUBOPTIMAL_KHR) {
                    needs_rebuild_ = true;
                } else if (pr == VK_ERROR_DEVICE_LOST) {
                    log::error("vulkan: DEVICE LOST on present");
                    engine_check(false);
                } else {
                    ENGINE_VK(pr);
                }

                slot.timeline_value = v;
                ++frame_counter_;
            }

            void resize(const uint32_t w, const uint32_t h) override {
                engine_check(!in_frame_);
                if (w == 0 || h == 0) return; // minimized
                if (w == desc_.width && h == desc_.height) return;
                desc_.width = w;
                desc_.height = h;
                needs_rebuild_ = true;
                rebuild_swapchain();
                log::info("vulkan: resized to {}x{}", w, h);
            }

            void wait_idle() override { ENGINE_VK(vkDeviceWaitIdle(device_)); }

            // ================================================== resources (m3)
            BufferHandle create_buffer(const BufferDesc &) override {
                engine_check(false && "m3");
                return {};
            }

            TextureHandle create_texture(const TextureDesc &) override {
                engine_check(false && "m3");
                return {};
            }

            PSOHandle create_graphics_pso(const GraphicsPSODesc &) override {
                engine_check(false && "m3");
                return {};
            }

            void destroy(BufferHandle) override { engine_check(false && "m3"); }

            void destroy(TextureHandle) override {
                // Only swapchain images exist today, and rebuild_swapchain owns them.
                engine_check(false && "m3");
            }

            void destroy(PSOHandle) override { engine_check(false && "m3"); }

            uint32_t bindless_index(BufferHandle) override {
                engine_check(false && "m3");
                return 0;
            }

            uint32_t bindless_index(TextureHandle) override {
                engine_check(false && "m3");
                return 0;
            }

            void *map(BufferHandle) override {
                engine_check(false && "m3");
                return nullptr;
            }

            void unmap(BufferHandle) override { engine_check(false && "m3"); }

            [[nodiscard]] const DeviceCaps &caps() const override { return caps_; }

            // ================================================== internals
            VulkanTexture *texture(TextureHandle h) {
                VulkanTexture *t = textures_.get(to_pool<VulkanTexture>(h));
                engine_check(t && "stale TextureHandle");
                return t;
            }

            [[nodiscard]] VkExtent2D extent() const { return extent_; }

        private:
            struct FrameSlot {
                VkCommandPool pool = VK_NULL_HANDLE;
                VkCommandBuffer cmd = VK_NULL_HANDLE;
                VkSemaphore acquire = VK_NULL_HANDLE; // binary, per frame-in-flight
                uint64_t timeline_value = 0; // 0 => never submitted: no wait
            };

            struct SwapImage {
                TextureHandle handle{};
                // Per SWAPCHAIN IMAGE, not per frame-in-flight. The validation trap:
                // vkQueuePresentKHR keeps waiting on this semaphore until the
                // presentation engine is done, and that is tied to the IMAGE, not to
                // our frame counter. A per-frame-in-flight present semaphore
                // eventually gets reused while a present is still pending on it.
                VkSemaphore render_finished = VK_NULL_HANDLE;
            };

            // ---------------------------------------------------------- §1: instance
            void create_instance() {
                std::vector<const char *> layers;
                std::vector<const char *> exts{VK_KHR_SURFACE_EXTENSION_NAME};
#if defined(_WIN32)
                exts.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#endif
                if (desc_.enable_debug) {
                    // The debug arsenal, Vulkan-flavoured: validation layer +
                    // debug-utils messenger, the analog of D3D12's debug layer +
                    // InfoQueue1 callback.
                    //
                    // NOTE: only the LunarG installer registers layers with the
                    // loader (registry on Windows, /usr/share on Linux). vcpkg does
                    // not, so VK_LAYER_PATH must be set or this silently finds
                    // nothing and you get an unvalidated instance:
                    //   Windows: <vcpkg_installed>/<triplet>/bin
                    //   Linux:   <vcpkg_installed>/<triplet>/share/vulkan/explicit_layer.d
                    // (paths per ports/vulkan-validationlayers/portfile.cmake)
                    if (has_layer("VK_LAYER_KHRONOS_validation")) {
                        layers.push_back("VK_LAYER_KHRONOS_validation");
                        exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
                    } else {
                        log::warn("vulkan: VK_LAYER_KHRONOS_validation not found — set "
                            "VK_LAYER_PATH to the vcpkg layer manifest directory");
                    }
                }

                constexpr VkApplicationInfo ai{
                    .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                    .pApplicationName = "engine",
                    .applicationVersion = VK_MAKE_VERSION(0, 1, 0),
                    .pEngineName = "engine",
                    .engineVersion = VK_MAKE_VERSION(0, 1, 0),
                    // 1.3 for sync2 + dynamic rendering in CORE, no extension dance.
                    .apiVersion = VK_API_VERSION_1_3,
                };
                const VkInstanceCreateInfo ci{
                    .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                    .pApplicationInfo = &ai,
                    .enabledLayerCount = static_cast<uint32_t>(layers.size()),
                    .ppEnabledLayerNames = layers.data(),
                    .enabledExtensionCount = static_cast<uint32_t>(exts.size()),
                    .ppEnabledExtensionNames = exts.data(),
                };
                ENGINE_VK(vkCreateInstance(&ci, nullptr, &instance_));

                if (!layers.empty()) create_debug_messenger();
            }

            static bool has_layer(const char *name) {
                uint32_t n = 0;
                vkEnumerateInstanceLayerProperties(&n, nullptr);
                std::vector<VkLayerProperties> props(n);
                vkEnumerateInstanceLayerProperties(&n, props.data());
                return std::ranges::any_of(props, [&](const VkLayerProperties &p) {
                    // C++20: std::ranges algorithms take the container, not begin/end
                    return std::strcmp(p.layerName, name) == 0;
                });
            }

            void create_debug_messenger() {
                const auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
                    vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT"));
                if (!create) return;

                const VkDebugUtilsMessengerCreateInfoEXT ci{
                    .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
                    .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                       VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
                    .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                   VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                   VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
                    .pfnUserCallback = &on_debug_message,
                };
                ENGINE_VK(create(instance_, &ci, nullptr, &messenger_));
            }

            // Zero-validation-warnings policy, same as the D3D12 InfoQueue callback:
            // errors are bugs and die immediately.
            static VKAPI_ATTR VkBool32 VKAPI_CALL on_debug_message(
                const VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                VkDebugUtilsMessageTypeFlagsEXT,
                const VkDebugUtilsMessengerCallbackDataEXT *data,
                void *) {
                if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
                    log::error("vulkan validation: {}", data->pMessage);
                    engine_check(false);
                } else {
                    log::warn("vulkan validation: {}", data->pMessage);
                }
                return VK_FALSE; // VK_TRUE would abort the offending call itself
            }

            // ----------------------------------------------------------- §2: surface
            void create_surface() {
#if defined(_WIN32)
                const VkWin32SurfaceCreateInfoKHR ci{
                    .sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
                    .hinstance = GetModuleHandleW(nullptr),
                    .hwnd = static_cast<HWND>(desc_.native_window),
                };
                ENGINE_VK(vkCreateWin32SurfaceKHR(instance_, &ci, nullptr, &surface_));
#else
                // Linux is NOT wired up, and deliberately not guessed at. There is no
                // Window implementation for it at all — engine/app/CMakeLists.txt has
                // only if(WIN32)/elseif(APPLE), so engine_app does not even link on
                // Linux. Beyond that, DeviceDesc::native_window is a single void*,
                // while both xlib (Display* + Window) and xcb (xcb_connection_t* +
                // xcb_window_t) need TWO values. Inventing a packing convention here
                // would be guessing at a seam the app layer has not defined yet
                // (rhi.h rule: EXTRACT, DON'T GUESS).
                //
                // To finish Linux: add engine/app/src/linux/app_linux.cpp with an
                // xcb or Wayland Window, decide what native_window carries, then
                // implement VK_KHR_xcb_surface / VK_KHR_wayland_surface here.
                engine_check(false && "Linux surface: no Window implementation exists yet "
                    "(see engine/app/src/ — only win32/ and macos/)");
#endif
            }

            // --------------------------------------------- §3: physical device
            void pick_physical_device() {
                uint32_t n = 0;
                ENGINE_VK(vkEnumeratePhysicalDevices(instance_, &n, nullptr));
                engine_check(n > 0 && "no Vulkan physical device — is a GPU driver installed?");
                std::vector<VkPhysicalDevice> devices(n);
                ENGINE_VK(vkEnumeratePhysicalDevices(instance_, &n, devices.data()));

                for (const VkPhysicalDevice pd: devices) {
                    VkPhysicalDeviceProperties props{};
                    vkGetPhysicalDeviceProperties(pd, &props);
                    if (props.apiVersion < VK_API_VERSION_1_3) continue;
                    if (!has_device_extension(pd, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) continue;

                    uint32_t family = 0;
                    if (!find_graphics_present_family(pd, family)) continue;
                    if (!has_required_features(pd)) continue;

                    // Prefer discrete; keep looking if this one is integrated.
                    physical_ = pd;
                    queue_family_ = family;
                    std::snprintf(caps_.adapter_name, sizeof(caps_.adapter_name), "%s",
                                  props.deviceName);
                    if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) break;
                }
                engine_check(physical_ &&
                    "no Vulkan 1.3 device with swapchain + graphics/present queue + "
                    "dynamicRendering/synchronization2/timelineSemaphore");

                VkPhysicalDeviceMemoryProperties mem{};
                vkGetPhysicalDeviceMemoryProperties(physical_, &mem);
                for (uint32_t i = 0; i < mem.memoryHeapCount; ++i)
                    if (mem.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
                        caps_.vram_bytes = std::max(caps_.vram_bytes, mem.memoryHeaps[i].size);

                log::info("vulkan: adapter '{}', {} MB device-local", caps_.adapter_name,
                          caps_.vram_bytes / (1024 * 1024));
            }

            static bool has_device_extension(VkPhysicalDevice pd, const char *name) {
                uint32_t n = 0;
                vkEnumerateDeviceExtensionProperties(pd, nullptr, &n, nullptr);
                std::vector<VkExtensionProperties> props(n);
                vkEnumerateDeviceExtensionProperties(pd, nullptr, &n, props.data());
                return std::ranges::any_of(props, [&](const VkExtensionProperties &p) {
                    return std::strcmp(p.extensionName, name) == 0;
                });
            }

            bool find_graphics_present_family(VkPhysicalDevice pd, uint32_t &out) const {
                uint32_t n = 0;
                vkGetPhysicalDeviceQueueFamilyProperties(pd, &n, nullptr);
                std::vector<VkQueueFamilyProperties> families(n);
                vkGetPhysicalDeviceQueueFamilyProperties(pd, &n, families.data());

                for (uint32_t i = 0; i < n; ++i) {
                    if (!(families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) continue;
                    VkBool32 present = VK_FALSE;
                    vkGetPhysicalDeviceSurfaceSupportKHR(pd, i, surface_, &present);
                    // One family doing both keeps the swapchain in EXCLUSIVE sharing
                    // mode — no concurrent-access plumbing, and it is what desktop
                    // drivers actually expose.
                    if (present) {
                        out = i;
                        return true;
                    }
                }
                return false;
            }

            static bool has_required_features(VkPhysicalDevice pd) {
                VkPhysicalDeviceVulkan13Features f13{
                    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES
                };
                VkPhysicalDeviceVulkan12Features f12{
                    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
                    .pNext = &f13,
                };
                VkPhysicalDeviceFeatures2 f2{
                    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
                    .pNext = &f12,
                };
                vkGetPhysicalDeviceFeatures2(pd, &f2);
                return f13.dynamicRendering && f13.synchronization2 && f12.timelineSemaphore;
            }

            // ------------------------------------------------- §4: device + queue
            void create_device_and_queue() {
                constexpr float priority = 1.f;
                const VkDeviceQueueCreateInfo qci{
                    .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                    .queueFamilyIndex = queue_family_,
                    .queueCount = 1,
                    .pQueuePriorities = &priority,
                };

                VkPhysicalDeviceVulkan13Features f13{
                    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES
                };
                f13.synchronization2 = VK_TRUE;
                f13.dynamicRendering = VK_TRUE;

                VkPhysicalDeviceVulkan12Features f12{
                    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
                    .pNext = &f13,
                };
                f12.timelineSemaphore = VK_TRUE;

                const char *dev_exts[]{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
                const VkDeviceCreateInfo ci{
                    .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                    .pNext = &f12, // feature structs chain through pNext
                    .queueCreateInfoCount = 1,
                    .pQueueCreateInfos = &qci,
                    .enabledExtensionCount = 1,
                    .ppEnabledExtensionNames = dev_exts,
                };
                ENGINE_VK(vkCreateDevice(physical_, &ci, nullptr, &device_));
                vkGetDeviceQueue(device_, queue_family_, 0, &queue_);

                // The timeline semaphore IS the fence. Starts at 0, matching the
                // D3D12 CreateFence(0, ...) and the Metal setSignaledValue(0).
                constexpr VkSemaphoreTypeCreateInfo tci{
                    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
                    .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
                    .initialValue = 0,
                };
                const VkSemaphoreCreateInfo sci{
                    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
                    .pNext = &tci,
                };
                ENGINE_VK(vkCreateSemaphore(device_, &sci, nullptr, &timeline_));
            }

            // ---------------------------------------------------- §5: swap chain
            void create_swapchain() {
                VkSurfaceCapabilitiesKHR surf{};
                ENGINE_VK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_, surface_, &surf));

                // currentExtent == UINT32_MAX means "you choose" (some Wayland/X11
                // setups); otherwise the surface dictates and we must obey it.
                extent_ = surf.currentExtent.width == UINT32_MAX
                              ? VkExtent2D{
                                  std::clamp(desc_.width, surf.minImageExtent.width,
                                             surf.maxImageExtent.width),
                                  std::clamp(desc_.height, surf.minImageExtent.height,
                                             surf.maxImageExtent.height)
                              }
                              : surf.currentExtent;

                format_ = pick_surface_format();

                uint32_t want = desc_.frames_in_flight + 1; // one to present, N to fill
                want = std::max(want, surf.minImageCount);
                if (surf.maxImageCount != 0) want = std::min(want, surf.maxImageCount);

                const VkSwapchainCreateInfoKHR ci{
                    .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
                    .surface = surface_,
                    .minImageCount = want,
                    .imageFormat = format_.format,
                    .imageColorSpace = format_.colorSpace,
                    .imageExtent = extent_,
                    .imageArrayLayers = 1,
                    .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                    .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE, // single queue family
                    .preTransform = surf.currentTransform,
                    .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
                    // FIFO is the only mode guaranteed present, and it is vsync —
                    // matching the D3D12 Present(1, 0) and Metal's default.
                    .presentMode = VK_PRESENT_MODE_FIFO_KHR,
                    .clipped = VK_TRUE,
                    .oldSwapchain = VK_NULL_HANDLE,
                };
                ENGINE_VK(vkCreateSwapchainKHR(device_, &ci, nullptr, &swapchain_));

                uint32_t n = 0;
                ENGINE_VK(vkGetSwapchainImagesKHR(device_, swapchain_, &n, nullptr));
                std::vector<VkImage> vk_images(n);
                ENGINE_VK(vkGetSwapchainImagesKHR(device_, swapchain_, &n, vk_images.data()));

                images_.resize(n);
                for (uint32_t i = 0; i < n; ++i) {
                    VulkanTexture t;
                    t.image = vk_images[i]; // swapchain-owned; never vkDestroyImage
                    t.width = extent_.width;
                    t.height = extent_.height;

                    const VkImageViewCreateInfo vci{
                        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                        .image = t.image,
                        .viewType = VK_IMAGE_VIEW_TYPE_2D,
                        .format = format_.format,
                        .subresourceRange = {
                            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                            .levelCount = 1,
                            .layerCount = 1,
                        },
                    };
                    ENGINE_VK(vkCreateImageView(device_, &vci, nullptr, &t.view));

                    images_[i].handle = to_rhi<TextureHandle>(textures_.create(std::move(t)));

                    constexpr VkSemaphoreCreateInfo sci{
                        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
                    };
                    ENGINE_VK(vkCreateSemaphore(device_, &sci, nullptr,
                        &images_[i].render_finished));
                }
                log::info("vulkan: swapchain {}x{}, {} images", extent_.width, extent_.height, n);
            }

            VkSurfaceFormatKHR pick_surface_format() const {
                uint32_t n = 0;
                vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &n, nullptr);
                std::vector<VkSurfaceFormatKHR> formats(n);
                vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &n, formats.data());
                engine_check(n > 0);

                for (const auto &f: formats)
                    // B8G8R8A8_UNORM to match Format::BGRA8_UNorm and the D3D12
                    // swapchain's DXGI_FORMAT_B8G8R8A8_UNORM. UNORM not SRGB: the
                    // sample writes its rainbow in whatever space it likes and we
                    // are not doing colour management at m2.
                    if (f.format == VK_FORMAT_B8G8R8A8_UNORM &&
                        f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
                        return f;
                return formats[0];
            }

            void destroy_swapchain() {
                for (SwapImage &img: images_) {
                    if (img.render_finished)
                        vkDestroySemaphore(device_, img.render_finished, nullptr);
                    if (!img.handle.is_null()) {
                        if (const VulkanTexture *t =
                            textures_.get(to_pool<VulkanTexture>(img.handle)))
                            vkDestroyImageView(device_, t->view, nullptr);
                        textures_.destroy(to_pool<VulkanTexture>(img.handle));
                    }
                }
                images_.clear();
                if (swapchain_) {
                    vkDestroySwapchainKHR(device_, swapchain_, nullptr);
                    swapchain_ = VK_NULL_HANDLE;
                }
            }

            void rebuild_swapchain() {
                // Like D3D12's ResizeBuffers path: the images must have zero
                // outstanding references NOW, so this is a full stall, not the
                // deferred-delete queue.
                ENGINE_VK(vkDeviceWaitIdle(device_));
                destroy_swapchain();
                create_swapchain();
                needs_rebuild_ = false;
            }

            void acquire_next_image(const uint32_t frame_idx) {
                if (needs_rebuild_) rebuild_swapchain();

                for (;;) {
                    const VkResult r = vkAcquireNextImageKHR(
                        device_, swapchain_, UINT64_MAX,
                        frame_[frame_idx].acquire, VK_NULL_HANDLE, &image_index_);
                    if (r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR) return;
                    if (r == VK_ERROR_OUT_OF_DATE_KHR) {
                        // Not an error. The semaphore is NOT signalled on this path,
                        // so it is safe to reuse on the retry.
                        rebuild_swapchain();
                        continue;
                    }
                    ENGINE_VK(r);
                }
            }

            // ------------------------------------------------ §6: frame resources
            void create_frame_resources() {
                for (uint32_t i = 0; i < desc_.frames_in_flight; ++i) {
                    const VkCommandPoolCreateInfo pci{
                        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                        // TRANSIENT: we reset the whole pool every frame and never
                        // reset individual buffers, so the driver can use a bump
                        // allocator. RESET_COMMAND_BUFFER would forbid that.
                        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
                        .queueFamilyIndex = queue_family_,
                    };
                    ENGINE_VK(vkCreateCommandPool(device_, &pci, nullptr, &frame_[i].pool));

                    const VkCommandBufferAllocateInfo ai{
                        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                        .commandPool = frame_[i].pool,
                        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                        .commandBufferCount = 1,
                    };
                    ENGINE_VK(vkAllocateCommandBuffers(device_, &ai, &frame_[i].cmd));

                    constexpr VkSemaphoreCreateInfo sci{
                        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
                    };
                    ENGINE_VK(vkCreateSemaphore(device_, &sci, nullptr, &frame_[i].acquire));
                }
            }

            void destroy_frame_resources() {
                for (uint32_t i = 0; i < desc_.frames_in_flight; ++i) {
                    if (frame_[i].acquire) vkDestroySemaphore(device_, frame_[i].acquire, nullptr);
                    // Destroying the pool frees its command buffers too.
                    if (frame_[i].pool) vkDestroyCommandPool(device_, frame_[i].pool, nullptr);
                    frame_[i] = {};
                }
                if (timeline_) vkDestroySemaphore(device_, timeline_, nullptr);
                timeline_ = VK_NULL_HANDLE;
            }

            void wait_timeline(const uint64_t value) const {
                if (value == 0) return; // never submitted
                const VkSemaphoreWaitInfo wi{
                    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
                    .semaphoreCount = 1,
                    .pSemaphores = &timeline_,
                    .pValues = &value,
                };
                ENGINE_VK(vkWaitSemaphores(device_, &wi, UINT64_MAX));
            }

            DeviceDesc desc_;
            DeviceCaps caps_{};

            VkInstance instance_ = VK_NULL_HANDLE;
            VkDebugUtilsMessengerEXT messenger_ = VK_NULL_HANDLE;
            VkSurfaceKHR surface_ = VK_NULL_HANDLE;
            VkPhysicalDevice physical_ = VK_NULL_HANDLE;
            VkDevice device_ = VK_NULL_HANDLE;
            VkQueue queue_ = VK_NULL_HANDLE;
            uint32_t queue_family_ = 0;

            VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
            VkSurfaceFormatKHR format_{};
            VkExtent2D extent_{};
            std::vector<SwapImage> images_;
            uint32_t image_index_ = 0; // NOT the frame index
            bool needs_rebuild_ = false;

            VkSemaphore timeline_ = VK_NULL_HANDLE; // the fence
            uint64_t timeline_value_ = 0;
            FrameSlot frame_[kMaxFramesInFlight];
            uint64_t frame_counter_ = 0;
            bool in_frame_ = false;

            VulkanCommandList cmd_;
            Pool<VulkanTexture> textures_;
        };

        // =========================================== command list (out-of-line)
        void VulkanCommandList::barrier(const std::span<const TextureBarrier> textures,
                                        const std::span<const BufferBarrier> buffers) {
            // The one backend where this is real work. D3D12 needs the same
            // information; Metal needs none of it.
            std::vector<VkImageMemoryBarrier2> images;
            images.reserve(textures.size());
            for (const TextureBarrier &b: textures) {
                const VulkanTexture *t = dev_->texture(b.texture);
                images.push_back(VkImageMemoryBarrier2{
                    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                    .srcStageMask = to_vk(b.sync_before),
                    .srcAccessMask = to_vk(b.access_before),
                    .dstStageMask = to_vk(b.sync_after),
                    .dstAccessMask = to_vk(b.access_after),
                    .oldLayout = to_vk(b.layout_before),
                    .newLayout = to_vk(b.layout_after),
                    // No queue-family ownership transfers at m2 (single queue).
                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .image = t->image,
                    .subresourceRange = {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .levelCount = VK_REMAINING_MIP_LEVELS,
                        .layerCount = VK_REMAINING_ARRAY_LAYERS,
                    },
                });
            }

            engine_check(buffers.empty() && "buffer barriers are m3 (no buffers exist yet)");

            const VkDependencyInfo di{
                .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .imageMemoryBarrierCount = static_cast<uint32_t>(images.size()),
                .pImageMemoryBarriers = images.data(),
            };
            vkCmdPipelineBarrier2(cmd_, &di);
        }

        void VulkanCommandList::clear_render_target(const TextureHandle h,
                                                    const std::array<float, 4> rgba) {
            const VulkanTexture *t = dev_->texture(h);

            // Same shape as the Metal backend, for the same reason: with dynamic
            // rendering a clear is loadOp=CLEAR on an attachment, so a standalone
            // clear is an EMPTY RENDERING SCOPE. Two independent backends landing on
            // the same workaround is the argument for the render graph owning clears
            // at m5 — vkCmdClearColorImage exists but needs TRANSFER_DST_OPTIMAL,
            // and ClearSample has already transitioned to RenderTarget by here.
            const VkRenderingAttachmentInfo color{
                .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                .imageView = t->view,
                .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .clearValue = {.color = {.float32 = {rgba[0], rgba[1], rgba[2], rgba[3]}}},
            };
            const VkRenderingInfo ri{
                .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                .renderArea = {.offset = {0, 0}, .extent = dev_->extent()},
                .layerCount = 1,
                .colorAttachmentCount = 1,
                .pColorAttachments = &color,
            };
            vkCmdBeginRendering(cmd_, &ri);
            vkCmdEndRendering(cmd_); // empty on purpose: the load op IS the work
        }
    } // namespace

    IDevice *create_vulkan_device(const DeviceDesc &desc) {
        return new VulkanDevice(desc);
    }
} // namespace engine::rhi
