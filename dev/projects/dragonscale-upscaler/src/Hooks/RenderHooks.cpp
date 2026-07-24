#include "Hooks/RenderHooks.h"

#include "Config.h"
#include "Diagnostics/Diagnostics.h"
#include "Diagnostics/OverlayRenderer.h"
#include "Upscaling/Upscaler.h"

#include <REX/W32/KERNEL32.h>

namespace
{
	[[nodiscard]] bool IsGogRuntime() noexcept
	{
		return REX::W32::GetModuleHandleW(L"steam_api64.dll") == nullptr;
	}

	template <class Hook>
	void WriteCall(REL::RelocationID id, std::ptrdiff_t offset)
	{
		REL::Relocation<std::uintptr_t> target{ id, offset };
		Hook::func = target.write_call<5>(Hook::thunk);
	}

	template <class Hook>
	void WriteCall(std::uintptr_t address)
	{
		Hook::func = SKSE::GetTrampoline().write_call<5>(address, Hook::thunk);
	}

	void PatchVanillaDynamicResolution()
	{
		const auto baseAddress = REL::RelocationID(35556, 36555).address();
		if (baseAddress == 0) {
			logger::warn("Could not locate vanilla dynamic-resolution update patch site");
			return;
		}

		const auto address = baseAddress + REL::Relocate<std::ptrdiff_t>(0x2D, 0x2D, 0x0);
		REL::safe_write(address, REL::NOP5, sizeof(REL::NOP5));
		logger::info("Disabled vanilla dynamic-resolution update path for DragonScale render scaling");
	}

	void PatchRenderTargetProperties(
		RE::BSGraphics::RenderTargetProperties& a_properties,
		bool a_supportUnorderedAccess,
		bool a_copyable) noexcept
	{
		if (a_supportUnorderedAccess) {
			a_properties.supportUnorderedAccess = true;
		}
		if (a_copyable) {
			a_properties.copyable = true;
		}
	}

	template <int Tag, bool SupportUnorderedAccess, bool Copyable>
	struct CreateRenderTargetAccess
	{
		static void thunk(
			RE::BSGraphics::Renderer* a_renderer,
			RE::RENDER_TARGETS::RENDER_TARGET a_target,
			RE::BSGraphics::RenderTargetProperties* a_properties)
		{
			if (!a_properties) {
				func(a_renderer, a_target, a_properties);
				return;
			}

			const auto saved = *a_properties;
			PatchRenderTargetProperties(*a_properties, SupportUnorderedAccess, Copyable);
			func(a_renderer, a_target, a_properties);
			*a_properties = saved;
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	using CreateRenderTarget_Main = CreateRenderTargetAccess<0, true, false>;
	using CreateRenderTarget_Normals = CreateRenderTargetAccess<1, true, false>;
	using CreateRenderTarget_NormalsSwap = CreateRenderTargetAccess<2, true, false>;
	using CreateRenderTarget_MotionVectors = CreateRenderTargetAccess<3, true, false>;
	using CreateRenderTarget_RefractionNormals = CreateRenderTargetAccess<4, false, true>;
	using CreateRenderTarget_UnderwaterMask = CreateRenderTargetAccess<5, false, true>;

	struct Main_UpdateJitter
	{
		static void thunk(RE::BSGraphics::State* a_state)
		{
			DragonScale::Upscaling::Upscaler::GetSingleton().ConfigureTemporalAA();
			func(a_state);
			DragonScale::Upscaling::Upscaler::GetSingleton().ConfigureFrame(a_state);
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct Main_PostProcessing
	{
		static void thunk(RE::ImageSpaceManager* a_this, std::uint32_t a3, RE::RENDER_TARGET a_target, void* a4, bool a5)
		{
			auto& upscaler = DragonScale::Upscaling::Upscaler::GetSingleton();
			upscaler.PerformUpscaling();
			upscaler.DisableTemporalAAForPostProcessing();
			func(a_this, a3, a_target, a4, a5);
			upscaler.PostDisplay();
			DragonScale::Diagnostics::DiagnosticsManager::GetSingleton().UpdateHotkey();
			DragonScale::Diagnostics::OverlayRenderer::GetSingleton().Render();
			DragonScale::Diagnostics::DiagnosticsManager::GetSingleton().MaybeLog();
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct TAA_BeginTechnique
	{
		static void thunk(RE::BSImagespaceShaderISTemporalAA* a_shader, RE::BSTriShape* a_null)
		{
			func(a_shader, a_null);
			DragonScale::Upscaling::Upscaler::GetSingleton().BeginTemporalAAPass();
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct TAA_EndTechnique
	{
		static void thunk(RE::BSImagespaceShaderISTemporalAA* a_shader, RE::BSTriShape* a_null)
		{
			func(a_shader, a_null);
			DragonScale::Upscaling::Upscaler::GetSingleton().EndTemporalAAPass();
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct SetScissorRect
	{
		static void thunk(RE::BSGraphics::Renderer* a_renderer, int a_left, int a_top, int a_right, int a_bottom)
		{
			const auto& settings = DragonScale::Config::GetSingleton().GetSettings();
			if (settings.compatibility.patchScissorRects) {
				if (auto* state = RE::BSGraphics::State::GetSingleton()) {
					const auto& runtimeData = state->GetRuntimeData();
					if (!runtimeData.dynamicResolutionLock) {
						a_left = static_cast<int>(static_cast<float>(a_left) * runtimeData.dynamicResolutionWidthRatio);
						a_right = static_cast<int>(static_cast<float>(a_right) * runtimeData.dynamicResolutionWidthRatio);
						a_top = static_cast<int>(static_cast<float>(a_top) * runtimeData.dynamicResolutionHeightRatio);
						a_bottom = static_cast<int>(static_cast<float>(a_bottom) * runtimeData.dynamicResolutionHeightRatio);
					}
				}
			}

			func(a_renderer, a_left, a_top, a_right, a_bottom);
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSFaceGenManager_UpdatePendingCustomizationTextures
	{
		static void thunk()
		{
			DragonScale::Upscaling::Upscaler::GetSingleton().BeginNativeRenderScope();
			func();
			DragonScale::Upscaling::Upscaler::GetSingleton().EndNativeRenderScope();
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct Main_RenderPrecipitation
	{
		static void thunk()
		{
			DragonScale::Upscaling::Upscaler::GetSingleton().BeginNativeRenderScope();
			func();
			DragonScale::Upscaling::Upscaler::GetSingleton().EndNativeRenderScope();
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSImageSpace_Init_FXAA
	{
		static void thunk()
		{
			func();
			if (const auto address = REL::RelocationID(513281, 391028).address(); address != 0) {
				*reinterpret_cast<bool*>(address) = false;
			}
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};
}

namespace DragonScale::Hooks
{
	void Install()
	{
		if (REL::Module::IsVR()) {
			logger::warn("VR runtime detected; render hooks are not installed yet");
			return;
		}

		const auto updateJitterOffset = REL::Relocate<std::ptrdiff_t>(0xE5, IsGogRuntime() ? 0x133 : 0xE2, 0x0);
		WriteCall<Main_UpdateJitter>(REL::RelocationID(75460, 77245), updateJitterOffset);

		PatchVanillaDynamicResolution();

		const auto postProcessingOffset = REL::Relocate<std::ptrdiff_t>(0x1F0, 0x1E7, 0x0);
		WriteCall<Main_PostProcessing>(REL::RelocationID(100430, 107148), postProcessingOffset);

		const auto temporalAABase = REL::RelocationID(100540, 107270).address();
		if (temporalAABase == 0) {
			logger::warn("Could not locate temporal AA technique hook sites; DragonScale will use post-processing capture only");
		} else {
			WriteCall<TAA_BeginTechnique>(temporalAABase + REL::Relocate<std::ptrdiff_t>(0x3E9, 0x3EA, 0x448));
			WriteCall<TAA_EndTechnique>(temporalAABase + REL::Relocate<std::ptrdiff_t>(0x3F3, 0x3F4, 0x452));
			logger::info("Installed DragonScale temporal AA tracking hooks");
		}

		const auto& settings = Config::GetSingleton().GetSettings();
		if (settings.compatibility.patchScissorRects) {
			logger::info("DragonScale scissor rect compatibility hook is deferred until hook chaining is hardened");
		}

		if (settings.compatibility.patchFaceGenNative) {
			logger::info("DragonScale facegen native-render compatibility hook is deferred until hook chaining is hardened");
		}

		if (settings.compatibility.patchPrecipitationNative) {
			const auto precipitationAddress = REL::RelocationID(35560, 36559).address() + REL::Relocate<std::ptrdiff_t>(0x3A1, 0x3A1, 0x0);
			WriteCall<Main_RenderPrecipitation>(precipitationAddress);
			logger::info("Installed DragonScale precipitation native-render compatibility hook");
		}

		if (settings.compatibility.patchRenderTargetAccess) {
			const auto renderTargetCreate = REL::RelocationID(100458, 107175).address();
			if (renderTargetCreate == 0) {
				logger::warn("Could not locate render-target creation patch sites; DragonScale will use fallback output resources");
			} else {
				WriteCall<CreateRenderTarget_Main>(renderTargetCreate + REL::Relocate<std::ptrdiff_t>(0x3F0, 0x3F3, 0x0));
				WriteCall<CreateRenderTarget_Normals>(renderTargetCreate + REL::Relocate<std::ptrdiff_t>(0x458, 0x45B, 0x0));
				WriteCall<CreateRenderTarget_NormalsSwap>(renderTargetCreate + REL::Relocate<std::ptrdiff_t>(0x46B, 0x46E, 0x0));
				WriteCall<CreateRenderTarget_MotionVectors>(renderTargetCreate + REL::Relocate<std::ptrdiff_t>(0x4F0, 0x4EF, 0x0));
				WriteCall<CreateRenderTarget_RefractionNormals>(renderTargetCreate + REL::Relocate<std::ptrdiff_t>(0x503, 0x502, 0x0));
				WriteCall<CreateRenderTarget_UnderwaterMask>(renderTargetCreate + REL::Relocate<std::ptrdiff_t>(0xB19, 0xB19, 0x0));
				logger::info("Installed DragonScale render-target access compatibility hooks");
			}
		}

		if (settings.compatibility.forceFXAAOff) {
			logger::info("DragonScale FXAA init compatibility hook is deferred; FXAA is still forced off during data load");
		}

		logger::info("Installed render hooks for upscaling");
	}
}
