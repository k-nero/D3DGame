// eng/rhi/src/d3d12/d3d12_device.cpp — milestone 2, sections 1+2:
// debug arsenal -> adapter -> device -> feature asserts -> info-queue
// callback -> fence + wait_idle. Frame loop and resources are stubbed
// with engine_engine_check(false) and arrive in the next sections.
#include "d3d12_common.h"

#include <engine/rhi/rhi.h>
#include <engine/core/pool.h>

#include <cstring>
using namespace Microsoft::WRL;
using engine::pool::Handle;
using engine::pool::Pool;

namespace engine::rhi {
	namespace {
		using d3d12::ComPtr;

		// ============================================================ translation
		// Portable enhanced-barrier enums -> D3D12. The bit values were chosen to
		// map one flag at a time; loops keep this honest as flags are added.

		D3D12_BARRIER_SYNC to_d3d12(Sync s) {
			D3D12_BARRIER_SYNC r = D3D12_BARRIER_SYNC_NONE;
			if (any(s & Sync::All)) r |= D3D12_BARRIER_SYNC_ALL;
			if (any(s & Sync::Draw)) r |= D3D12_BARRIER_SYNC_DRAW;
			if (any(s & Sync::PixelShading)) r |= D3D12_BARRIER_SYNC_PIXEL_SHADING;
			if (any(s & Sync::RenderTarget)) r |= D3D12_BARRIER_SYNC_RENDER_TARGET;
			if (any(s & Sync::DepthStencil)) r |= D3D12_BARRIER_SYNC_DEPTH_STENCIL;
			if (any(s & Sync::Compute)) r |= D3D12_BARRIER_SYNC_COMPUTE_SHADING;
			if (any(s & Sync::Copy)) r |= D3D12_BARRIER_SYNC_COPY;
			return r;
		}

		// D3D12 rule: ACCESS_NO_ACCESS must pair with SYNC_NONE; ACCESS_COMMON (0)
		// is "any compatible access". Our empty flag set therefore translates
		// context-sensitively:
		D3D12_BARRIER_ACCESS to_d3d12(Access a, Sync paired_sync) {
			if (a == Access::NoAccess)
				return paired_sync == Sync::None
				? D3D12_BARRIER_ACCESS_NO_ACCESS
				: D3D12_BARRIER_ACCESS_COMMON;
			D3D12_BARRIER_ACCESS r = D3D12_BARRIER_ACCESS_COMMON;
			if (any(a & Access::RenderTarget)) r |= D3D12_BARRIER_ACCESS_RENDER_TARGET;
			if (any(a & Access::DepthWrite)) r |= D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE;
			if (any(a & Access::ShaderRead)) r |= D3D12_BARRIER_ACCESS_SHADER_RESOURCE;
			if (any(a & Access::UnorderedAccess)) r |= D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
			if (any(a & Access::CopySrc)) r |= D3D12_BARRIER_ACCESS_COPY_SOURCE;
			if (any(a & Access::CopyDst)) r |= D3D12_BARRIER_ACCESS_COPY_DEST;
			return r;
		}

		D3D12_BARRIER_LAYOUT to_d3d12(Layout l) {
			switch (l) {
			case Layout::Undefined: return D3D12_BARRIER_LAYOUT_UNDEFINED;
			case Layout::Present: return D3D12_BARRIER_LAYOUT_PRESENT;
			case Layout::RenderTarget: return D3D12_BARRIER_LAYOUT_RENDER_TARGET;
			case Layout::DepthWrite: return D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE;
			case Layout::ShaderRead: return D3D12_BARRIER_LAYOUT_SHADER_RESOURCE;
			case Layout::UnorderedAccess: return D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS;
			case Layout::CopySrc: return D3D12_BARRIER_LAYOUT_COPY_SOURCE;
			case Layout::CopyDst: return D3D12_BARRIER_LAYOUT_COPY_DEST;
			}
			engine_check(false);
			return D3D12_BARRIER_LAYOUT_UNDEFINED;
		}

		// ============================================================ resources
		struct D3D12Texture {
			ComPtr<ID3D12Resource> resource;
			D3D12_CPU_DESCRIPTOR_HANDLE rtv{}; // valid if usage has RenderTarget
			uint32_t rtv_slot = UINT32_MAX;
			uint32_t width = 0, height = 0;
		};

		template<class RhiH, class T>
		RhiH to_rhi(Handle<T> h) { return RhiH{ .index = h.index, .gen = h.gen }; }

		template<class T, class RhiH>
		Handle<T> to_pool(RhiH h) { return Handle<T>{.index = h.index, .gen = h.gen}; }

		class D3D12Device;

		// ============================================================ command list
		class D3D12CommandList final : public ICommandList {
		public:
			void init(D3D12Device* dev, ID3D12GraphicsCommandList7* cl) {
				dev_ = dev;
				cl_ = cl;
			}

			void barrier(std::span<const TextureBarrier> textures,
				std::span<const BufferBarrier> buffers) override;

			void clear_render_target(TextureHandle, std::array<float, 4> rgba) override;

			void set_render_targets(std::span<const TextureHandle> colors,
				TextureHandle depth) override;

			void set_viewport_scissor(uint32_t w, uint32_t h) override {
				const D3D12_VIEWPORT vp{ 0.f, 0.f, float(w), float(h), 0.f, 1.f };
				const D3D12_RECT sc{ 0, 0, LONG(w), LONG(h) };
				cl_->RSSetViewports(1, &vp);
				cl_->RSSetScissorRects(1, &sc);
			}

			// ---- milestone 3 ----
			void set_pso(PSOHandle) override { engine_check(false && "m3"); }
			void set_index_buffer(BufferHandle, Format) override { engine_check(false && "m3"); }
			void push_constants(const void*, uint32_t) override { engine_check(false && "m3"); }
			void draw(uint32_t, uint32_t) override { engine_check(false && "m3"); }
			void draw_indexed(uint32_t, uint32_t) override { engine_check(false && "m3"); }
			void dispatch(uint32_t, uint32_t, uint32_t) override { engine_check(false && "m3"); }

		private:
			D3D12Device* dev_ = nullptr;
			ID3D12GraphicsCommandList7* cl_ = nullptr;
		};

		// ============================================================ device
		class D3D12Device final : public IDevice {
		public:
			explicit D3D12Device(const DeviceDesc& desc) : desc_(desc) {
				engine_check(desc.frames_in_flight >= 1 &&
					desc.frames_in_flight <= kMaxFramesInFlight);

				// ---------- §1: debug arsenal BEFORE device creation ----------
				if (desc.enable_debug) {
					ComPtr<ID3D12Debug> debug;
					if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
						debug->EnableDebugLayer();
						ComPtr<ID3D12Debug1> debug1;
						if (SUCCEEDED(debug.As(&debug1)))
							debug1->SetEnableGPUBasedValidation(TRUE);
					}
					else {
						log::warn("d3d12: debug layer unavailable (install the "
							"'Graphics Tools' optional feature)");
					}
					ComPtr<ID3D12DeviceRemovedExtendedDataSettings1> dred;
					if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dred)))) {
						dred->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
						dred->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
						dred->SetBreadcrumbContextEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
					}
				}

				const UINT ff = desc.enable_debug ? DXGI_CREATE_FACTORY_DEBUG : 0;
				ENGINE_HR(CreateDXGIFactory2(ff, IID_PPV_ARGS(&factory_)));

				ComPtr<IDXGIAdapter1> adapter;
				for (UINT i = 0; factory_->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_UNSPECIFIED, IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND; ++i) {
					DXGI_ADAPTER_DESC1 ad{};
					adapter->GetDesc1(&ad);
					if (ad.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
					if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device_)))) {
						d3d12::wide_to_utf8(ad.Description, caps_.adapter_name, sizeof(caps_.adapter_name));
						caps_.vram_bytes = ad.DedicatedVideoMemory;
						break;
					}
					adapter.Reset();
				}
				engine_check(device_ && "no D3D12 FL12.0 hardware adapter found");
				log::info("d3d12: adapter '{}', {} MB VRAM", caps_.adapter_name,
					caps_.vram_bytes / (1024 * 1024));

				assert_features();

				if (desc.enable_debug) {
					ComPtr<ID3D12InfoQueue1> iq;
					if (SUCCEEDED(device_.As(&iq)))
						ENGINE_HR(iq->RegisterMessageCallback(
							&on_debug_message, D3D12_MESSAGE_CALLBACK_FLAG_NONE,
							this, &iq_cookie_));
				}

				// ---------- §2: fence + queue ----------
				ENGINE_HR(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE,
					IID_PPV_ARGS(&fence_)));
				fence_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
				engine_check(fence_event_);

				const D3D12_COMMAND_QUEUE_DESC qd{ .Type = D3D12_COMMAND_LIST_TYPE_DIRECT };
				ENGINE_HR(device_->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue_)));

				// ---------- §3: per-frame allocators + one recycled list ----------
				for (uint32_t i = 0; i < desc_.frames_in_flight; ++i)
					ENGINE_HR(device_->CreateCommandAllocator(
						D3D12_COMMAND_LIST_TYPE_DIRECT,
						IID_PPV_ARGS(&frame_[i].allocator)));

				// CreateCommandList1 births the list CLOSED — begin_frame resets it.
				ENGINE_HR(device_->CreateCommandList1(
					0, D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_FLAG_NONE,
					IID_PPV_ARGS(&cmdlist_)));
				cmd_.init(this, cmdlist_.Get());

				// ---------- §4: RTV heap + swap chain ----------
				const D3D12_DESCRIPTOR_HEAP_DESC hd{
					.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
					.NumDescriptors = kRtvCapacity,
				};
				ENGINE_HR(device_->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&rtv_heap_)));
				rtv_size_ = device_->GetDescriptorHandleIncrementSize(
					D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

				engine_check(desc.native_window && "D3D12 backend needs an HWND");
				create_swapchain();
				acquire_backbuffers();
			}

			~D3D12Device() override {
				// destroy_device already ran wait_idle; drain the deferred queue
				// so ReportLiveDeviceObjects afterwards shows only true leaks.
				collect_deferred(UINT64_MAX);
				if (fence_event_) CloseHandle(fence_event_);
			}

			// ================================================== frame loop (§5)
			FrameContext begin_frame() override {
				engine_check(!in_frame_);
				in_frame_ = true;

				const uint32_t idx =
					static_cast<uint32_t>(frame_counter_ % desc_.frames_in_flight);
				FrameSlot& slot = frame_[idx];

				// THE wait: this slot's previous submission must be fully consumed
				// before its allocator memory is reused. This single line is the
				// frames-in-flight design.
				wait_fence(slot.fence_value);
				collect_deferred(fence_->GetCompletedValue());

				ENGINE_HR(slot.allocator->Reset());
				ENGINE_HR(cmdlist_->Reset(slot.allocator.Get(), nullptr));

				backbuffer_index_ = swapchain_->GetCurrentBackBufferIndex();
				return FrameContext{
					.cmd = &cmd_,
					.backbuffer = backbuffers_[backbuffer_index_],
					.frame_index = idx,
				};
			}

			void end_frame() override {
				engine_check(in_frame_);
				in_frame_ = false;

				ENGINE_HR(cmdlist_->Close());
				ID3D12CommandList* lists[] = { cmdlist_.Get() };
				queue_->ExecuteCommandLists(1, lists);

				const HRESULT pr = swapchain_->Present(1, 0); // vsync on; (0,ALLOW_TEARING) later
				if (pr == DXGI_ERROR_DEVICE_REMOVED || pr == DXGI_ERROR_DEVICE_RESET) {
					const HRESULT reason = device_->GetDeviceRemovedReason();
					log::error("d3d12: DEVICE REMOVED, reason 0x{:08x} — DRED "
						"breadcrumbs available in a debugger", uint32_t(reason));
					engine_check(false);
				}
				ENGINE_HR(pr);

				// Signal AFTER Present so this frame's fence value covers the
				// present operation too (it's queued work like everything else).
				const uint64_t v = ++fence_value_;
				ENGINE_HR(queue_->Signal(fence_.Get(), v));
				frame_[frame_counter_ % desc_.frames_in_flight].fence_value = v;

				++frame_counter_;
			}

			// ================================================== resize (§7)
			void resize(uint32_t w, uint32_t h) override {
				engine_check(!in_frame_);
				if (w == 0 || h == 0) return; // minimized
				if (w == desc_.width && h == desc_.height) return;
				desc_.width = w;
				desc_.height = h;

				// Backbuffers are the one thing that must NOT go through deferred
				// delete: ResizeBuffers demands zero outstanding references NOW.
				wait_idle();
				release_backbuffers();
				ENGINE_HR(swapchain_->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0));
				acquire_backbuffers();
				log::info("d3d12: resized to {}x{}", w, h);
			}

			void wait_idle() override {
				const uint64_t v = ++fence_value_;
				ENGINE_HR(queue_->Signal(fence_.Get(), v));
				wait_fence(v);
			}

			// ================================================== resources (m3)
			BufferHandle create_buffer(const BufferDesc&) override {
				engine_check(false && "m3");
				return {};
			}

			TextureHandle create_texture(const TextureDesc&) override {
				engine_check(false && "m3");
				return {};
			}

			PSOHandle create_graphics_pso(const GraphicsPSODesc&) override {
				engine_check(false && "m3");
				return {};
			}

			void destroy(BufferHandle) override { engine_check(false && "m3"); }

			void destroy(TextureHandle h) override {
				// valid already for user-created RTs later; for now only backbuffers
				// exist and those are managed by resize — forbid external destroy.
				(void)h;
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

			void* map(BufferHandle) override {
				engine_check(false && "m3");
				return nullptr;
			}

			void unmap(BufferHandle) override { engine_check(false && "m3"); }

			const DeviceCaps& caps() const override { return caps_; }

			// ================================================== internals
			D3D12Texture* texture(TextureHandle h) {
				D3D12Texture* t = textures_.get(to_pool<D3D12Texture>(h));
				engine_check(t && "stale TextureHandle");
				return t;
			}

			void destroy_later(ComPtr<ID3D12Resource> r) {
				deferred_.push_back({ std::move(r), fence_value_ + 1 });
			}

		private:
			static constexpr uint32_t kRtvCapacity = 64;
			static constexpr uint32_t kBackbufferCount = 3;

			struct FrameSlot {
				ComPtr<ID3D12CommandAllocator> allocator;
				uint64_t fence_value = 0; // 0 => never submitted: no wait
			};

			struct PendingDelete {
				ComPtr<ID3D12Resource> resource;
				uint64_t fence;
			};

			void assert_features() {
				D3D12_FEATURE_DATA_SHADER_MODEL sm{ D3D_SHADER_MODEL_6_6 };
				ENGINE_HR(device_->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &sm,
					sizeof(sm)));
				engine_check(sm.HighestShaderModel >= D3D_SHADER_MODEL_6_6 &&
					"SM 6.6 required — on the Agility runtime? check exe_dir/D3D12/");

				D3D12_FEATURE_DATA_D3D12_OPTIONS o{};
				ENGINE_HR(device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &o,
					sizeof(o)));
				engine_check(o.ResourceBindingTier == D3D12_RESOURCE_BINDING_TIER_3 &&
					"binding tier 3 required (bindless)");

				D3D12_FEATURE_DATA_D3D12_OPTIONS12 o12{};
				ENGINE_HR(device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS12,
					&o12, sizeof(o12)));
				engine_check(o12.EnhancedBarriersSupported && "enhanced barriers required");
			}

			void create_swapchain() {
				const DXGI_SWAP_CHAIN_DESC1 sd{
					.Width = desc_.width,
					.Height = desc_.height,
					.Format = DXGI_FORMAT_B8G8R8A8_UNORM,
					.SampleDesc = {1, 0},
					.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
					.BufferCount = kBackbufferCount,
					.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
				};
				const HWND hwnd = static_cast<HWND>(desc_.native_window);
				ComPtr<IDXGISwapChain1> sc1;
				ENGINE_HR(factory_->CreateSwapChainForHwnd(queue_.Get(), hwnd, &sd,
					nullptr, nullptr, &sc1));
				ENGINE_HR(sc1.As(&swapchain_));
				// We own resize; DXGI's alt-enter fullscreen toggle would fight us.
				ENGINE_HR(factory_->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER));
			}

			void acquire_backbuffers() {
				for (uint32_t i = 0; i < kBackbufferCount; ++i) {
					D3D12Texture t;
					ENGINE_HR(swapchain_->GetBuffer(i, IID_PPV_ARGS(&t.resource)));
					t.resource->SetName(L"backbuffer");
					t.width = desc_.width;
					t.height = desc_.height;
					t.rtv_slot = alloc_rtv_slot();
					t.rtv = rtv_cpu(t.rtv_slot);
					device_->CreateRenderTargetView(t.resource.Get(), nullptr, t.rtv);
					backbuffers_[i] = to_rhi<TextureHandle>(textures_.create(std::move(t)));
				}
			}

			void release_backbuffers() {
				for (auto& h : backbuffers_) {
					if (h.is_null()) continue;
					if (D3D12Texture* t = textures_.get(to_pool<D3D12Texture>(h)))
						free_rtv_slot(t->rtv_slot);
					textures_.destroy(to_pool<D3D12Texture>(h)); // ComPtr released NOW
					h = {}; // (post-wait_idle: safe)
				}
			}

			uint32_t alloc_rtv_slot() {
				if (!rtv_free_.empty()) {
					const uint32_t s = rtv_free_.back();
					rtv_free_.pop_back();
					return s;
				}
				engine_check(rtv_next_ < kRtvCapacity);
				return rtv_next_++;
			}

			void free_rtv_slot(uint32_t s) { rtv_free_.push_back(s); }

			D3D12_CPU_DESCRIPTOR_HANDLE rtv_cpu(uint32_t slot) const {
				D3D12_CPU_DESCRIPTOR_HANDLE h =
					rtv_heap_->GetCPUDescriptorHandleForHeapStart();
				h.ptr += SIZE_T(slot) * rtv_size_;
				return h;
			}

			void wait_fence(uint64_t value) {
				if (value == 0 || fence_->GetCompletedValue() >= value) return;
				ENGINE_HR(fence_->SetEventOnCompletion(value, fence_event_));
				WaitForSingleObject(fence_event_, INFINITE);
			}

			void collect_deferred(uint64_t completed) {
				std::erase_if(deferred_, [&](PendingDelete& p) {
					// C++20: std::erase_if
					return p.fence <= completed; // ComPtr releases on erase
					});
			}

			static void CALLBACK on_debug_message(D3D12_MESSAGE_CATEGORY,
				D3D12_MESSAGE_SEVERITY severity,
				D3D12_MESSAGE_ID,
				LPCSTR description, void*) {
				switch (severity) {
				case D3D12_MESSAGE_SEVERITY_CORRUPTION:
				case D3D12_MESSAGE_SEVERITY_ERROR:
					log::error("d3d12 validation: {}", description);
					engine_check(false && "D3D12 validation error — see log");
					break;
				case D3D12_MESSAGE_SEVERITY_WARNING:
					log::warn("d3d12 validation: {}", description);
					break;
				default: break;
				}
			}

			friend class D3D12CommandList;

			DeviceDesc desc_;
			DeviceCaps caps_{};

			ComPtr<IDXGIFactory6> factory_;
			ComPtr<ID3D12Device10> device_;
			ComPtr<ID3D12CommandQueue> queue_;
			ComPtr<ID3D12Fence> fence_;
			HANDLE fence_event_ = nullptr;
			uint64_t fence_value_ = 0;
			DWORD iq_cookie_ = 0;

			FrameSlot frame_[kMaxFramesInFlight];
			ComPtr<ID3D12GraphicsCommandList7> cmdlist_;
			D3D12CommandList cmd_;
			uint64_t frame_counter_ = 0;
			bool in_frame_ = false;

			ComPtr<IDXGISwapChain3> swapchain_;
			ComPtr<ID3D12DescriptorHeap> rtv_heap_;
			uint32_t rtv_size_ = 0;
			uint32_t rtv_next_ = 0;
			std::vector<uint32_t> rtv_free_;
			TextureHandle backbuffers_[kBackbufferCount]{};
			uint32_t backbuffer_index_ = 0;

			Pool<D3D12Texture> textures_;
			std::vector<PendingDelete> deferred_;
		};

		// ============================================ command list bodies
		void D3D12CommandList::barrier(std::span<const TextureBarrier> textures,
			std::span<const BufferBarrier> buffers) {
			engine_check(buffers.empty() && "buffer barriers arrive in m3");

			std::vector<D3D12_TEXTURE_BARRIER> tb; // frame-arena candidate, m3
			tb.reserve(textures.size());
			for (const TextureBarrier& b : textures) {
				tb.push_back(D3D12_TEXTURE_BARRIER{
					.SyncBefore = to_d3d12(b.sync_before),
					.SyncAfter = to_d3d12(b.sync_after),
					.AccessBefore = to_d3d12(b.access_before, b.sync_before),
					.AccessAfter = to_d3d12(b.access_after, b.sync_after),
					.LayoutBefore = to_d3d12(b.layout_before),
					.LayoutAfter = to_d3d12(b.layout_after),
					.pResource = dev_->texture(b.texture)->resource.Get(),
					.Subresources = {.IndexOrFirstMipLevel = 0xffffffff}, // all
					});
			}
			if (tb.empty()) return;
			const D3D12_BARRIER_GROUP group{
				.Type = D3D12_BARRIER_TYPE_TEXTURE,
				.NumBarriers = static_cast<UINT32>(tb.size()),
				.pTextureBarriers = tb.data(),
			};
			cl_->Barrier(1, &group);
		}

		void D3D12CommandList::clear_render_target(TextureHandle h,
			std::array<float, 4> rgba) {
			cl_->ClearRenderTargetView(dev_->texture(h)->rtv, rgba.data(), 0, nullptr);
		}

		void D3D12CommandList::set_render_targets(std::span<const TextureHandle> colors,
			TextureHandle depth) {
			engine_check(depth.is_null() && "depth arrives in m3");
			engine_check(colors.size() <= kMaxColorTargets);
			D3D12_CPU_DESCRIPTOR_HANDLE rtvs[kMaxColorTargets];
			for (size_t i = 0; i < colors.size(); ++i)
				rtvs[i] = dev_->texture(colors[i])->rtv;
			cl_->OMSetRenderTargets(static_cast<UINT>(colors.size()), rtvs, FALSE,
				nullptr);
		}
	} // namespace

	IDevice* create_d3d12_device(const DeviceDesc& desc) {
		return new D3D12Device(desc);
	}

	void d3d12_report_live_objects() {
#ifdef ENG_DEBUG
		d3d12::ComPtr<IDXGIDebug1> dxgi_debug;
		if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgi_debug))))
			dxgi_debug->ReportLiveObjects(
				DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_FLAGS(DXGI_DEBUG_RLO_DETAIL |
					DXGI_DEBUG_RLO_IGNORE_INTERNAL));
#endif
	}
} // namespace eng::rhi
