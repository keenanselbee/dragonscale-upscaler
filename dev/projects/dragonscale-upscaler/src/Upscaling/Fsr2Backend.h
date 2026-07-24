#pragma once

#include "Upscaling/UpscaleTypes.h"

#include <array>
#include <cstdint>

#ifdef DRAGONSCALE_WITH_FIDELITYFX_SDK
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Query;

#	include <FidelityFX/host/ffx_fsr2.h>
#	include <FidelityFX/host/ffx_interface.h>
#endif

namespace DragonScale::Upscaling
{
	class Fsr2Backend
	{
	public:
		[[nodiscard]] static Fsr2Backend& GetSingleton();

		void Load();
		void OnDataLoaded();

		[[nodiscard]] bool IsAvailable() const noexcept;
		[[nodiscard]] bool Dispatch(const UpscaleInputs& a_inputs);
		void ResetHistory() noexcept;
		void Destroy() noexcept;

	private:
		Fsr2Backend() = default;

		[[nodiscard]] bool EnsureContext(const UpscaleInputs& a_inputs);
		void DestroyContext() noexcept;

#ifdef DRAGONSCALE_WITH_FIDELITYFX_SDK
		struct GpuTimingQuery
		{
			ID3D11Query* disjoint = nullptr;
			ID3D11Query* start = nullptr;
			ID3D11Query* end = nullptr;
			bool issued = false;
		};

		void InitializeGpuTiming(ID3D11Device* a_device);
		void ShutdownGpuTiming() noexcept;
		void ResolveGpuTiming(ID3D11DeviceContext* a_context, GpuTimingQuery& a_query);
		void BeginGpuTiming(ID3D11DeviceContext* a_context);
		void EndGpuTiming(ID3D11DeviceContext* a_context);
		static FfxErrorCode GetDeviceCapabilitiesOverride(FfxInterface* a_backendInterface, FfxDeviceCapabilities* a_outDeviceCapabilities);
		FfxErrorCode GetDeviceCapabilities(FfxInterface* a_backendInterface, FfxDeviceCapabilities* a_outDeviceCapabilities);
		static FfxErrorCode CreatePipelineOverride(
			FfxInterface* a_backendInterface,
			FfxEffect a_effect,
			FfxPass a_pass,
			std::uint32_t a_permutationOptions,
			const FfxPipelineDescription* a_pipelineDescription,
			FfxUInt32 a_effectContextId,
			FfxPipelineState* a_outPipeline);
		FfxErrorCode CreatePipeline(
			FfxInterface* a_backendInterface,
			FfxEffect a_effect,
			FfxPass a_pass,
			std::uint32_t a_permutationOptions,
			const FfxPipelineDescription* a_pipelineDescription,
			FfxUInt32 a_effectContextId,
			FfxPipelineState* a_outPipeline);
#endif

		bool checkedAvailability_ = false;
		bool sdkAvailable_ = false;
		bool staticLibsAvailable_ = false;
		bool available_ = false;
		bool missingSdkLogged_ = false;
		bool missingResourcesLogged_ = false;
		bool dispatchFailureLogged_ = false;
		bool contextCreateFailureLogged_ = false;
		bool contextCreateDisabled_ = false;
		bool contextCreated_ = false;
		bool resetDispatch_ = true;
		std::uint32_t contextDisplayWidth_ = 0;
		std::uint32_t contextDisplayHeight_ = 0;
		std::uint32_t contextRenderWidth_ = 0;
		std::uint32_t contextRenderHeight_ = 0;
		DXGI_FORMAT contextOutputFormat_ = DXGI_FORMAT_UNKNOWN;
		std::uint32_t contextFlags_ = 0;
		bool contextHdr_ = false;
		float lastCameraFovRadians_ = 0.0f;

#ifdef DRAGONSCALE_WITH_FIDELITYFX_SDK
		FfxFsr2Context fsrContext_{};
		FfxInterface fsrInterface_{};
		FfxGetDeviceCapabilitiesFunc originalGetDeviceCapabilities_ = nullptr;
		FfxCreatePipelineFunc originalCreatePipeline_ = nullptr;
		void* fsrScratchBuffer_ = nullptr;
		std::array<GpuTimingQuery, 4> gpuTimingQueries_{};
		ID3D11Device* gpuTimingDevice_ = nullptr;
		std::uint32_t gpuTimingIndex_ = 0;
		bool gpuTimingInitialized_ = false;
		bool gpuTimingFailed_ = false;
		bool gpuTimingFailureLogged_ = false;
#endif
	};
}
