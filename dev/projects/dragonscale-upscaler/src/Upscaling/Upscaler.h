#pragma once

#include "Config.h"
#include "Upscaling/AuxiliaryUpscaler.h"
#include "Upscaling/FrameResources.h"
#include "Upscaling/Jitter.h"

#include <string>
#include <string_view>

namespace DragonScale::Upscaling
{
	struct RenderPlan
	{
		bool active = false;
		std::uint32_t displayWidth = 0;
		std::uint32_t displayHeight = 0;
		std::uint32_t renderWidth = 0;
		std::uint32_t renderHeight = 0;
		JitterOffset jitter;
		float renderScale = 1.0f;
	};

	struct RenderStateSnapshot
	{
		bool valid = false;
		float projectionPosScaleX = 0.0f;
		float projectionPosScaleY = 0.0f;
		float dynamicResolutionWidthRatio = 1.0f;
		float dynamicResolutionHeightRatio = 1.0f;
		float dynamicResolutionPreviousWidthRatio = 1.0f;
		float dynamicResolutionPreviousHeightRatio = 1.0f;
		std::int32_t dynamicResolutionLock = 1;
	};

	class Upscaler
	{
	public:
		[[nodiscard]] static Upscaler& GetSingleton();

		void Load();
		void InstallHooks();
		void OnDataLoaded();

		void ConfigureTemporalAA();
		void ConfigureFrame(RE::BSGraphics::State* a_state);
		void PerformUpscaling();
		void DisableTemporalAAForPostProcessing();
		void BeginTemporalAAPass() noexcept;
		void EndTemporalAAPass() noexcept;
		void PostDisplay();
		void BeginNativeRenderScope();
		void EndNativeRenderScope();

		[[nodiscard]] bool IsActive() const noexcept;
		[[nodiscard]] bool ShouldReplaceTemporalAAPass() const noexcept;
		[[nodiscard]] bool UpscaledThisFrame() const noexcept;
		[[nodiscard]] const RenderPlan& GetRenderPlan() const noexcept;

	private:
		Upscaler() = default;

		[[nodiscard]] bool ShouldUseFsr() const noexcept;
		[[nodiscard]] bool ShouldRunRenderPipeline() const noexcept;
		[[nodiscard]] RenderPlan BuildRenderPlan(const RE::BSGraphics::State& a_state) const noexcept;
		[[nodiscard]] RenderStateSnapshot CaptureRenderState(RE::BSGraphics::State& a_state) const noexcept;
		[[nodiscard]] SourceExtentValidationResult ValidateSourceExtent(const ResourceSnapshot& a_snapshot, const RenderPlan& a_plan) const;
		[[nodiscard]] SourceExtentValidationResult ValidateCopyThroughExtent(const ResourceSnapshot& a_snapshot, const RenderPlan& a_plan) const;
		void ApplyRenderPlan(RE::BSGraphics::State& a_state, const RenderPlan& a_plan);
		void ResetRenderPlan(RE::BSGraphics::State& a_state);
		void ApplyDebugSourceMode(ResourceSnapshot& a_snapshot) const;
		void ApplySuccessfulFrameState();
		void PrepareImageSpaceStateAfterUpscale();
		void RestoreDownstreamRenderState();
		void MarkRenderTargetDirty() noexcept;
		void EnsureDynamicResolutionEnabled(std::string_view a_reason);
		void LogDynamicResolutionState(std::string_view a_reason, const RE::BSGraphics::State& a_state) noexcept;
		void RefreshEnbDetection(std::string_view a_reason);
		[[nodiscard]] bool ShouldResetHistory(const ResourceSnapshot& a_snapshot) noexcept;
		void LogMipBias(const RenderPlan& a_plan) noexcept;

		RenderPlan currentPlan_;
		RenderStateSnapshot originalState_;
		FrameResources frameResources_;
		RE::BSGraphics::State* currentState_ = nullptr;
		std::uint32_t lastDisplayWidth_ = 0;
		std::uint32_t lastDisplayHeight_ = 0;
		std::uint32_t lastRenderWidth_ = 0;
		std::uint32_t lastRenderHeight_ = 0;
		float lastLoggedMipBias_ = 99.0f;
		bool loaded_ = false;
		bool hooksInstalled_ = false;
		bool enbDetected_ = false;
		bool strictExtentValidation_ = false;
		bool dynamicResolutionSettingMissingLogged_ = false;
		bool dynamicResolutionSettingForcedLogged_ = false;
		bool taaSettingMissingLogged_ = false;
		bool temporalAAMissingLogged_ = false;
		bool runtimeTemporalAAForcedLogged_ = false;
		bool directMainOutputReadyLogged_ = false;
		bool directMainOutputMissingLogged_ = false;
		bool temporalAAPassValid_ = false;
		bool upscaledThisFrame_ = false;
		float appliedDynamicResolutionWidthRatio_ = 1.0f;
		float appliedDynamicResolutionHeightRatio_ = 1.0f;
		std::uint64_t lastUpscaledFrame_ = 0;
		std::uint64_t lastDynamicResolutionLogFrame_ = 0;
		std::uint64_t lastRestoreStateLogFrame_ = 0;
		std::uint64_t lastImageSpaceStateLogFrame_ = 0;
		std::string lastExtentMismatchReason_;
		std::string lastAuxiliaryRepairReason_;
	};
}
