#include "Upscaling/Upscaler.h"

#include "Diagnostics/Diagnostics.h"
#include "Hooks/RenderHooks.h"
#include "Upscaling/DisplayTweaksDetector.h"
#include "Upscaling/EnbDetector.h"
#include "Upscaling/Fsr2Backend.h"
#include "Upscaling/MenuStateTracker.h"
#include "Upscaling/RenderResourceTracker.h"

#include <REX/W32/KERNEL32.h>

#include <algorithm>
#include <cmath>
#include <format>

namespace DragonScale::Upscaling
{
	namespace
	{
		constexpr std::uint32_t kExtentTolerancePixels = 2;

		[[nodiscard]] QualityMode GetEffectiveFsr2Quality(const UpscalingSettings& a_settings) noexcept
		{
			if (a_settings.nativeAA || a_settings.quality == QualityMode::kNativeAA) {
				return QualityMode::kQuality;
			}

			return a_settings.quality;
		}

		[[nodiscard]] bool CloseEnough(std::uint32_t a_lhs, std::uint32_t a_rhs) noexcept
		{
			return a_lhs > a_rhs ? a_lhs - a_rhs <= kExtentTolerancePixels : a_rhs - a_lhs <= kExtentTolerancePixels;
		}

		[[nodiscard]] const char* SourceName(ResourceSource a_source) noexcept
		{
			switch (a_source) {
			case ResourceSource::kImageSpace:
				return "ImageSpace";
			case ResourceSource::kRendererSlot:
				return "RendererSlot";
			case ResourceSource::kTextureDiscoveryFallback:
				return "TextureDiscovery";
			case ResourceSource::kOwned:
				return "Owned";
			case ResourceSource::kMissing:
			default:
				return "Missing";
			}
		}

		void UpdateCameraData()
		{
			using func_t = decltype(&UpdateCameraData);
			static REL::Relocation<func_t> func{ REL::RelocationID(75472, 77258) };
			func();
		}

		[[nodiscard]] bool IsUpscalingEnabled()
		{
			const auto& settings = Config::GetSingleton().GetSettings();
			return settings.upscaling.enabled && settings.upscaling.method != UpscaleMethod::kNone;
		}

		[[nodiscard]] bool IsFsrPipelineMode() noexcept
		{
			const auto mode = Config::GetSingleton().GetSettings().debug.pipelineMode;
			return mode == DebugPipelineMode::kFsr2 || mode == DebugPipelineMode::kScaleCopy;
		}
	}

	Upscaler& Upscaler::GetSingleton()
	{
		static Upscaler singleton;
		return singleton;
	}

	void Upscaler::Load()
	{
		if (loaded_) {
			return;
		}

		loaded_ = true;

		const auto& settings = Config::GetSingleton().GetSettings();
		RenderResourceTracker::GetSingleton().Configure(settings.upscaling.useTextureDiscoveryFallback);
		Fsr2Backend::GetSingleton().Load();
		(void)DisplayTweaksDetector::GetSingleton().Refresh("load");
		RefreshEnbDetection("load");
		EnsureDynamicResolutionEnabled("load");

		logger::info("Upscaler loaded; ENB detected={}, strictExtentValidation={}", enbDetected_, strictExtentValidation_);
	}

	void Upscaler::InstallHooks()
	{
		if (hooksInstalled_) {
			return;
		}

		const auto& settings = Config::GetSingleton().GetSettings();
		if (!settings.upscaling.enabled) {
			logger::info("Upscaling hooks disabled by config");
			return;
		}

		if (settings.upscaling.method == UpscaleMethod::kNone) {
			logger::info("Upscaling method is None; render hooks are not installed");
			return;
		}

		if (settings.debug.pipelineMode == DebugPipelineMode::kFsr2 &&
			settings.upscaling.method == UpscaleMethod::kFsr2 &&
			!Fsr2Backend::GetSingleton().IsAvailable()) {
			logger::warn("FSR2 backend is not available yet; render hooks are not installed");
			return;
		}

		if (!settings.compatibility.preferEngineRenderHooks) {
			logger::warn("Engine render hooks are disabled; no alternate swap-chain path is implemented yet");
			return;
		}

		EnsureDynamicResolutionEnabled("hook-install");
		Hooks::Install();
		hooksInstalled_ = true;
	}

	void Upscaler::OnDataLoaded()
	{
		const auto& settings = Config::GetSingleton().GetSettings();
		(void)DisplayTweaksDetector::GetSingleton().Refresh("data-loaded");
		RefreshEnbDetection("data-loaded");
		EnsureDynamicResolutionEnabled("data-loaded");
		ConfigureTemporalAA();
		MenuStateTracker::GetSingleton().Register();
		if (settings.compatibility.forceFXAAOff) {
			const auto address = REL::RelocationID(513281, 391028).address();
			if (address != 0) {
				*reinterpret_cast<bool*>(address) = false;
				logger::info("Forced Skyrim FXAA off for DragonScale upscaling");
			}
		}
		Fsr2Backend::GetSingleton().OnDataLoaded();
	}

	void Upscaler::ConfigureTemporalAA()
	{
		if (!IsUpscalingEnabled()) {
			return;
		}

		if (auto* taaSetting = RE::GetINISetting("bUseTAA:Display")) {
			if (!taaSetting->data.b) {
				taaSetting->data.b = true;
				logger::info("Forced Skyrim TAA on for DragonScale upscaling");
			}
		} else if (!taaSettingMissingLogged_) {
			logger::warn("Could not find bUseTAA:Display; temporal AA could not be forced through the INI setting");
			taaSettingMissingLogged_ = true;
		}

		auto* imageSpaceManager = RE::ImageSpaceManager::GetSingleton();
		if (!imageSpaceManager) {
			if (!temporalAAMissingLogged_) {
				logger::warn("ImageSpaceManager is not available yet; runtime temporal AA will be forced later");
				temporalAAMissingLogged_ = true;
			}
			return;
		}

		auto& runtimeData = imageSpaceManager->GetRuntimeData();
		if (!runtimeData.BSImagespaceShaderISTemporalAA) {
			if (!temporalAAMissingLogged_) {
				logger::warn("Temporal AA image-space shader state is not available yet; runtime temporal AA will be forced later");
				temporalAAMissingLogged_ = true;
			}
			return;
		}

		if (!runtimeData.BSImagespaceShaderISTemporalAA->taaEnabled) {
			runtimeData.BSImagespaceShaderISTemporalAA->taaEnabled = true;
			if (!runtimeTemporalAAForcedLogged_) {
				logger::info("Forced runtime temporal AA on for DragonScale upscaling");
				runtimeTemporalAAForcedLogged_ = true;
			}
		}
	}

	void Upscaler::ConfigureFrame(RE::BSGraphics::State* a_state)
	{
		Diagnostics::ScopedCpuTimer timer(Diagnostics::CpuTimerKind::kConfigure);
		upscaledThisFrame_ = false;
		currentState_ = a_state;
		if (!a_state) {
			currentPlan_ = {};
			originalState_ = {};
			Diagnostics::DiagnosticsManager::GetSingleton().SetStatus(Diagnostics::Status::kDisabled);
			return;
		}

		Diagnostics::DiagnosticsManager::GetSingleton().BeginFrame(a_state->frameCount);
		originalState_ = CaptureRenderState(*a_state);

		const auto wasActive = currentPlan_.active;
		currentPlan_ = BuildRenderPlan(*a_state);
		const auto& settings = Config::GetSingleton().GetSettings();
		const auto quality = GetEffectiveFsr2Quality(settings.upscaling);
		Diagnostics::DiagnosticsManager::GetSingleton().SetResolution(
			currentPlan_.displayWidth,
			currentPlan_.displayHeight,
			currentPlan_.active ? currentPlan_.renderWidth : currentPlan_.displayWidth,
			currentPlan_.active ? currentPlan_.renderHeight : currentPlan_.displayHeight,
			quality,
			currentPlan_.active ? currentPlan_.renderScale : 1.0f);

		if (!currentPlan_.active) {
			if (wasActive) {
				ResetRenderPlan(*a_state);
				frameResources_.Release();
				AuxiliaryUpscaler::GetSingleton().Release();
				Fsr2Backend::GetSingleton().Destroy();
			}
			if (!settings.upscaling.enabled || settings.upscaling.method == UpscaleMethod::kNone) {
				Diagnostics::DiagnosticsManager::GetSingleton().SetStatus(Diagnostics::Status::kDisabled);
			}
			return;
		}

		Diagnostics::DiagnosticsManager::GetSingleton().SetStatus(Diagnostics::Status::kWaiting);
		if (settings.debug.pipelineMode == DebugPipelineMode::kCaptureOnly) {
			Diagnostics::DiagnosticsManager::GetSingleton().SetStatus(Diagnostics::Status::kWaiting, "CaptureOnly");
			return;
		}
		if (settings.debug.pipelineMode == DebugPipelineMode::kCopyThrough) {
			Diagnostics::DiagnosticsManager::GetSingleton().SetStatus(Diagnostics::Status::kWaiting, "CopyThrough");
			return;
		}

		EnsureDynamicResolutionEnabled("configure-frame");
		ConfigureTemporalAA();
		ApplyRenderPlan(*a_state, currentPlan_);
		LogDynamicResolutionState("configured", *a_state);
		LogMipBias(currentPlan_);
	}

	void Upscaler::PerformUpscaling()
	{
		Diagnostics::ScopedCpuTimer timer(Diagnostics::CpuTimerKind::kUpscale);
		const auto frameID = currentState_ ? currentState_->frameCount : 0;
		if (frameID != 0 && lastUpscaledFrame_ == frameID) {
			upscaledThisFrame_ = true;
			return;
		}

		upscaledThisFrame_ = false;
		if (!currentPlan_.active) {
			return;
		}

		const auto& settings = Config::GetSingleton().GetSettings();
		RefreshEnbDetection("render-capture");
		auto snapshot = RenderResourceTracker::GetSingleton().Capture(currentState_ ? currentState_->frameCount : 0);
		ApplyDebugSourceMode(snapshot);

		if (settings.debug.pipelineMode == DebugPipelineMode::kCaptureOnly) {
			Diagnostics::DiagnosticsManager::GetSingleton().SetStatus(Diagnostics::Status::kWaiting, "CaptureOnly captured resources");
			upscaledThisFrame_ = false;
			return;
		}

		if (settings.debug.pipelineMode == DebugPipelineMode::kCopyThrough) {
			auto copyValidation = ValidateCopyThroughExtent(snapshot, currentPlan_);
			if (!copyValidation.valid) {
				RestoreDownstreamRenderState();
				Diagnostics::DiagnosticsManager::GetSingleton().SetStatus(Diagnostics::Status::kWaiting, copyValidation.reason);
				if (copyValidation.reason != lastExtentMismatchReason_) {
					logger::warn(
						"DragonScale copy-through waiting for matching source/output: {}. source={}, planned={}x{}, actual={}x{}, color={}x{}, output={}x{}, viewportValid={}",
						copyValidation.reason,
						SourceName(snapshot.activeSource),
						currentPlan_.renderWidth,
						currentPlan_.renderHeight,
						copyValidation.width,
						copyValidation.height,
						snapshot.color.width,
						snapshot.color.height,
						snapshot.output.width,
						snapshot.output.height,
						snapshot.viewport.valid);
					lastExtentMismatchReason_ = copyValidation.reason;
				}
				return;
			}

			lastExtentMismatchReason_.clear();
			if (!frameResources_.PrepareCopyThrough(
					snapshot,
					currentPlan_.displayWidth,
					currentPlan_.displayHeight,
					copyValidation.width,
					copyValidation.height) ||
				!frameResources_.CopyInputToOutput(copyValidation.width, copyValidation.height)) {
				RestoreDownstreamRenderState();
				Diagnostics::DiagnosticsManager::GetSingleton().SetStatus(Diagnostics::Status::kWaiting, "CopyThrough resource copy failed");
				return;
			}

			frameResources_.CopyOutputToGameTarget(snapshot);
			upscaledThisFrame_ = true;
			lastUpscaledFrame_ = frameID != 0 ? frameID : snapshot.frameID;
			Diagnostics::DiagnosticsManager::GetSingleton().SetStatus(Diagnostics::Status::kRunning, "CopyThrough");
			ApplySuccessfulFrameState();
			return;
		}

		auto extentValidation = ValidateSourceExtent(snapshot, currentPlan_);
		if (!extentValidation.valid) {
			Fsr2Backend::GetSingleton().ResetHistory();
			RestoreDownstreamRenderState();
			Diagnostics::DiagnosticsManager::GetSingleton().SetStatus(Diagnostics::Status::kWaiting, extentValidation.reason);
			if (extentValidation.reason != lastExtentMismatchReason_) {
				logger::warn(
					"DragonScale waiting for matching source extent: {}. source={}, planned={}x{}, actual={}x{}, color={}x{}, output={}x{}, viewportValid={}",
					extentValidation.reason,
					SourceName(snapshot.activeSource),
					currentPlan_.renderWidth,
					currentPlan_.renderHeight,
					extentValidation.width,
					extentValidation.height,
					snapshot.color.width,
					snapshot.color.height,
					snapshot.output.width,
					snapshot.output.height,
					snapshot.viewport.valid);
				lastExtentMismatchReason_ = extentValidation.reason;
			}
			return;
		}

		lastExtentMismatchReason_.clear();
		auto& auxiliaryUpscaler = AuxiliaryUpscaler::GetSingleton();
		const auto auxiliaryValidation = auxiliaryUpscaler.Validate(snapshot, currentPlan_.displayWidth, currentPlan_.displayHeight);
		if (enbDetected_ && auxiliaryValidation.enabled && !auxiliaryValidation.requiredReady) {
			Fsr2Backend::GetSingleton().ResetHistory();
			RestoreDownstreamRenderState();
			Diagnostics::DiagnosticsManager::GetSingleton().SetStatus(Diagnostics::Status::kWaiting, auxiliaryValidation.reason);
			if (auxiliaryValidation.reason != lastAuxiliaryRepairReason_) {
				logger::warn("DragonScale waiting for ENB-safe auxiliary targets: {}", auxiliaryValidation.reason);
				lastAuxiliaryRepairReason_ = auxiliaryValidation.reason;
			}
			return;
		}
		if (auxiliaryValidation.enabled && auxiliaryValidation.requiredReady && lastAuxiliaryRepairReason_ != "ready") {
			logger::info("DragonScale auxiliary targets ready: underwaterMask={}", auxiliaryValidation.underwaterReady);
			lastAuxiliaryRepairReason_ = "ready";
		} else if (!auxiliaryValidation.enabled && auxiliaryValidation.reason != lastAuxiliaryRepairReason_) {
			logger::info("DragonScale auxiliary repair inactive: {}", auxiliaryValidation.reason);
			lastAuxiliaryRepairReason_ = auxiliaryValidation.reason;
		}

		if (!frameResources_.Prepare(
				snapshot,
				currentPlan_.displayWidth,
				currentPlan_.displayHeight,
				extentValidation.width,
				extentValidation.height,
				settings.upscaling.encodeReactiveMasks)) {
			Diagnostics::DiagnosticsManager::GetSingleton().SetStatus(Diagnostics::Status::kWaiting, "waiting for render resources");
			RestoreDownstreamRenderState();
			return;
		}

		if (settings.debug.pipelineMode == DebugPipelineMode::kScaleCopy) {
			if (!frameResources_.ScaleInputToOutput(
					currentPlan_.displayWidth,
					currentPlan_.displayHeight,
					extentValidation.width,
					extentValidation.height)) {
				Diagnostics::DiagnosticsManager::GetSingleton().SetStatus(Diagnostics::Status::kWaiting, "ScaleCopy failed");
				RestoreDownstreamRenderState();
				return;
			}

			frameResources_.CopyOutputToGameTarget(snapshot);
			upscaledThisFrame_ = true;
			lastUpscaledFrame_ = frameID != 0 ? frameID : snapshot.frameID;
			Diagnostics::DiagnosticsManager::GetSingleton().SetStatus(Diagnostics::Status::kRunning, "ScaleCopy");
			ApplySuccessfulFrameState();
			return;
		}

		const auto resetHistory = ShouldResetHistory(snapshot) || frameResources_.RecreatedThisFrame();
		if (resetHistory) {
			Fsr2Backend::GetSingleton().ResetHistory();
		}

		const auto& prepared = frameResources_.GetPrepared();
		const bool directMainOutputRequested =
			settings.debug.outputMode == DebugOutputMode::kDirectMain ||
			settings.compatibility.useDirectMainOutput;
		const bool directMainOutputAllowed = directMainOutputRequested && snapshot.activeSource != ResourceSource::kImageSpace;
		const bool directMainOutput = directMainOutputAllowed && snapshot.output.HasUAV();
		if (directMainOutputRequested && !directMainOutput && !directMainOutputMissingLogged_) {
			directMainOutputMissingLogged_ = true;
			logger::warn(
				"Direct main-target FSR2 output requested but unavailable for this source; source={}, outputUAV={}, bind=0x{:X}. Falling back to owned output copy-back.",
				SourceName(snapshot.activeSource),
				static_cast<const void*>(snapshot.output.uav),
				snapshot.output.bindFlags);
		}
		if (directMainOutput && !directMainOutputReadyLogged_) {
			directMainOutputReadyLogged_ = true;
			logger::info(
				"Direct main-target FSR2 output enabled: texture={}, uav={}, format={}, bind=0x{:X}",
				static_cast<const void*>(snapshot.output.texture),
				static_cast<const void*>(snapshot.output.uav),
				static_cast<std::uint32_t>(snapshot.output.format),
				snapshot.output.bindFlags);
		}

		UpscaleInputs inputs;
		inputs.device = snapshot.device;
		inputs.context = snapshot.context;
		inputs.color = prepared.inputColor;
		inputs.output = directMainOutput ? snapshot.output : prepared.outputColor;
		inputs.depth = prepared.typedDepth;
		inputs.motionVectors = prepared.motionVectors;
		inputs.reactiveMask = prepared.reactiveMask;
		inputs.transparencyCompositionMask = prepared.transparencyCompositionMask;
		inputs.displayWidth = currentPlan_.displayWidth;
		inputs.displayHeight = currentPlan_.displayHeight;
		inputs.renderWidth = extentValidation.width;
		inputs.renderHeight = extentValidation.height;
		inputs.jitter = currentPlan_.jitter;
		inputs.quality = GetEffectiveFsr2Quality(settings.upscaling);
		inputs.sharpness = settings.upscaling.sharpness;
		inputs.reset = resetHistory;

		if (Fsr2Backend::GetSingleton().Dispatch(inputs)) {
			upscaledThisFrame_ = true;
			lastUpscaledFrame_ = frameID != 0 ? frameID : snapshot.frameID;
			if (!directMainOutput) {
				if (settings.compatibility.preserveMainAlpha) {
					(void)frameResources_.PreserveOutputAlpha(
						currentPlan_.displayWidth,
						currentPlan_.displayHeight,
						extentValidation.width,
						extentValidation.height);
				}
				frameResources_.CopyOutputToGameTarget(snapshot);
			}
			if (auxiliaryValidation.requiredReady) {
				const auto repaired = auxiliaryUpscaler.Repair(
					snapshot,
					currentPlan_.displayWidth,
					currentPlan_.displayHeight,
					extentValidation.width,
					extentValidation.height,
					currentPlan_.jitter);
				if (!repaired && enbDetected_) {
					logger::warn("DragonScale auxiliary repair failed after FSR2 dispatch; downstream effects may see stale auxiliary buffers this frame");
				}
			}
			ApplySuccessfulFrameState();
			return;
		}

		upscaledThisFrame_ = false;
		RestoreDownstreamRenderState();

		if (!Fsr2Backend::GetSingleton().IsAvailable()) {
			if (currentState_) {
				ResetRenderPlan(*currentState_);
			}
			currentPlan_ = {};
			Diagnostics::DiagnosticsManager::GetSingleton().SetResolution(
				inputs.displayWidth,
				inputs.displayHeight,
				inputs.displayWidth,
				inputs.displayHeight,
				GetEffectiveFsr2Quality(Config::GetSingleton().GetSettings().upscaling),
				1.0f);
			Diagnostics::DiagnosticsManager::GetSingleton().SetStatus(Diagnostics::Status::kNativeRestored, "FSR2 backend unavailable; native restored");
			frameResources_.Release();
			AuxiliaryUpscaler::GetSingleton().Release();
			logger::warn("FSR2 backend became unavailable; restored native render plan");
		}
	}

	void Upscaler::DisableTemporalAAForPostProcessing()
	{
		if (!upscaledThisFrame_) {
			return;
		}

		auto* imageSpaceManager = RE::ImageSpaceManager::GetSingleton();
		if (!imageSpaceManager) {
			return;
		}

		auto& runtimeData = imageSpaceManager->GetRuntimeData();
		if (runtimeData.BSImagespaceShaderISTemporalAA) {
			runtimeData.BSImagespaceShaderISTemporalAA->taaEnabled = false;
		}
	}

	void Upscaler::BeginTemporalAAPass() noexcept
	{
		temporalAAPassValid_ = currentPlan_.active && ShouldRunRenderPipeline();
	}

	void Upscaler::EndTemporalAAPass() noexcept
	{
		temporalAAPassValid_ = false;
	}

	void Upscaler::PostDisplay()
	{
		if (!currentPlan_.active) {
			return;
		}

		RestoreDownstreamRenderState();
		upscaledThisFrame_ = false;
	}

	void Upscaler::BeginNativeRenderScope()
	{
		if (!currentPlan_.active || !currentState_) {
			return;
		}

		auto& runtimeData = currentState_->GetRuntimeData();
		runtimeData.dynamicResolutionLock = 1;
		UpdateCameraData();
	}

	void Upscaler::EndNativeRenderScope()
	{
		if (!currentPlan_.active || !currentState_) {
			return;
		}

		auto& runtimeData = currentState_->GetRuntimeData();
		runtimeData.dynamicResolutionLock = 0;
		UpdateCameraData();
		MarkRenderTargetDirty();
	}

	bool Upscaler::IsActive() const noexcept
	{
		return currentPlan_.active;
	}

	bool Upscaler::ShouldReplaceTemporalAAPass() const noexcept
	{
		return temporalAAPassValid_ && currentPlan_.active && ShouldRunRenderPipeline();
	}

	bool Upscaler::UpscaledThisFrame() const noexcept
	{
		const auto frameID = currentState_ ? currentState_->frameCount : 0;
		return upscaledThisFrame_ && frameID != 0 && lastUpscaledFrame_ == frameID;
	}

	const RenderPlan& Upscaler::GetRenderPlan() const noexcept
	{
		return currentPlan_;
	}

	bool Upscaler::ShouldUseFsr() const noexcept
	{
		const auto& settings = Config::GetSingleton().GetSettings();
		return settings.upscaling.enabled &&
		       settings.upscaling.method == UpscaleMethod::kFsr2 &&
		       settings.debug.pipelineMode == DebugPipelineMode::kFsr2 &&
		       Fsr2Backend::GetSingleton().IsAvailable();
	}

	bool Upscaler::ShouldRunRenderPipeline() const noexcept
	{
		const auto& settings = Config::GetSingleton().GetSettings();
		if (!settings.upscaling.enabled || settings.upscaling.method == UpscaleMethod::kNone) {
			return false;
		}

		if (settings.debug.pipelineMode == DebugPipelineMode::kFsr2) {
			return ShouldUseFsr();
		}

		return true;
	}

	RenderPlan Upscaler::BuildRenderPlan(const RE::BSGraphics::State& a_state) const noexcept
	{
		RenderPlan plan;
		plan.displayWidth = a_state.screenWidth;
		plan.displayHeight = a_state.screenHeight;

		if (!ShouldRunRenderPipeline() || plan.displayWidth == 0 || plan.displayHeight == 0) {
			return plan;
		}

		const auto& settings = Config::GetSingleton().GetSettings();
		if (settings.debug.pipelineMode != DebugPipelineMode::kFsr2 &&
			settings.debug.pipelineMode != DebugPipelineMode::kScaleCopy) {
			plan.renderWidth = plan.displayWidth;
			plan.renderHeight = plan.displayHeight;
			plan.renderScale = 1.0f;
			plan.jitter = {};
			plan.active = true;
			return plan;
		}

		const auto quality = GetEffectiveFsr2Quality(settings.upscaling);
		const auto ratio = (std::max)(GetUpscaleRatio(quality), 1.0f);

		plan.renderScale = 1.0f / ratio;
		plan.renderWidth = (std::max<std::uint32_t>)(1, static_cast<std::uint32_t>(std::round(static_cast<float>(plan.displayWidth) * plan.renderScale)));
		plan.renderHeight = (std::max<std::uint32_t>)(1, static_cast<std::uint32_t>(std::round(static_cast<float>(plan.displayHeight) * plan.renderScale)));

		const auto phaseCount = GetJitterPhaseCount(static_cast<std::int32_t>(plan.renderWidth), static_cast<std::int32_t>(plan.displayWidth));
		plan.jitter = GetJitterOffset(a_state.frameCount, phaseCount);
		plan.active = true;

		return plan;
	}

	RenderStateSnapshot Upscaler::CaptureRenderState(RE::BSGraphics::State& a_state) const noexcept
	{
		RenderStateSnapshot snapshot;
		snapshot.valid = true;
		snapshot.projectionPosScaleX = a_state.projectionPosScaleX;
		snapshot.projectionPosScaleY = a_state.projectionPosScaleY;

		auto& runtimeData = a_state.GetRuntimeData();
		snapshot.dynamicResolutionWidthRatio = runtimeData.dynamicResolutionWidthRatio;
		snapshot.dynamicResolutionHeightRatio = runtimeData.dynamicResolutionHeightRatio;
		snapshot.dynamicResolutionPreviousWidthRatio = runtimeData.dynamicResolutionPreviousWidthRatio;
		snapshot.dynamicResolutionPreviousHeightRatio = runtimeData.dynamicResolutionPreviousHeightRatio;
		snapshot.dynamicResolutionLock = runtimeData.dynamicResolutionLock;
		return snapshot;
	}

	void Upscaler::ApplyDebugSourceMode(ResourceSnapshot& a_snapshot) const
	{
		const auto mode = Config::GetSingleton().GetSettings().debug.sourceMode;
		switch (mode) {
		case DebugSourceMode::kRendererSlot:
			if (a_snapshot.rendererColor.HasTexture() && a_snapshot.rendererOutput.HasTexture()) {
				a_snapshot.color = a_snapshot.rendererColor;
				a_snapshot.output = a_snapshot.rendererOutput;
				a_snapshot.activeSource = ResourceSource::kRendererSlot;
			}
			break;
		case DebugSourceMode::kImageSpace:
			{
				const auto liveMinimumWidth = a_snapshot.actualRenderWidth != 0 ? a_snapshot.actualRenderWidth : currentPlan_.renderWidth;
				const auto liveMinimumHeight = a_snapshot.actualRenderHeight != 0 ? a_snapshot.actualRenderHeight : currentPlan_.renderHeight;
				if (a_snapshot.pipeline.HasLiveResources(liveMinimumWidth, liveMinimumHeight)) {
					a_snapshot.color = a_snapshot.pipeline.color;
					a_snapshot.output = a_snapshot.pipeline.output;
					a_snapshot.activeSource = ResourceSource::kImageSpace;
				}
			}
			break;
		case DebugSourceMode::kAuto:
		default:
			break;
		}

		static DebugSourceMode lastMode = DebugSourceMode::kAuto;
		static ResourceSource lastSource = ResourceSource::kMissing;
		if (mode != lastMode || a_snapshot.activeSource != lastSource) {
			logger::info(
				"DragonScale debug source selection: mode={}, selected={}, rendererColor={}x{}, rendererOutput={}x{}, imageSpaceColor={}x{}, imageSpaceOutput={}x{}",
				ToString(mode),
				SourceName(a_snapshot.activeSource),
				a_snapshot.rendererColor.width,
				a_snapshot.rendererColor.height,
				a_snapshot.rendererOutput.width,
				a_snapshot.rendererOutput.height,
				a_snapshot.pipeline.color.width,
				a_snapshot.pipeline.color.height,
				a_snapshot.pipeline.output.width,
				a_snapshot.pipeline.output.height);
			lastMode = mode;
			lastSource = a_snapshot.activeSource;
		}
	}

	SourceExtentValidationResult Upscaler::ValidateCopyThroughExtent(const ResourceSnapshot& a_snapshot, const RenderPlan& a_plan) const
	{
		SourceExtentValidationResult result;
		result.width = a_snapshot.actualRenderWidth != 0 ? a_snapshot.actualRenderWidth : a_plan.renderWidth;
		result.height = a_snapshot.actualRenderHeight != 0 ? a_snapshot.actualRenderHeight : a_plan.renderHeight;

		if (!a_snapshot.color.HasTexture() || !a_snapshot.output.HasTexture()) {
			result.reason = "copy-through color/output resources missing";
			return result;
		}

		if (a_snapshot.output.width < a_plan.displayWidth || a_snapshot.output.height < a_plan.displayHeight) {
			result.reason = std::format(
				"copy-through output target too small: {}x{} < display {}x{}",
				a_snapshot.output.width,
				a_snapshot.output.height,
				a_plan.displayWidth,
				a_plan.displayHeight);
			return result;
		}

		if (a_snapshot.color.width < a_plan.renderWidth || a_snapshot.color.height < a_plan.renderHeight) {
			result.reason = std::format(
				"copy-through color target too small: {}x{} < render {}x{}",
				a_snapshot.color.width,
				a_snapshot.color.height,
				a_plan.renderWidth,
				a_plan.renderHeight);
			return result;
		}

		if (a_snapshot.viewport.valid &&
			(!CloseEnough(a_snapshot.actualRenderWidth, a_plan.renderWidth) || !CloseEnough(a_snapshot.actualRenderHeight, a_plan.renderHeight))) {
			result.reason = std::format(
				"copy-through source extent mismatch: viewport {}x{} vs planned {}x{}",
				a_snapshot.actualRenderWidth,
				a_snapshot.actualRenderHeight,
				a_plan.renderWidth,
				a_plan.renderHeight);
			return result;
		}

		result.valid = true;
		result.width = a_plan.renderWidth;
		result.height = a_plan.renderHeight;
		return result;
	}

	SourceExtentValidationResult Upscaler::ValidateSourceExtent(const ResourceSnapshot& a_snapshot, const RenderPlan& a_plan) const
	{
		SourceExtentValidationResult result;
		result.width = a_snapshot.actualRenderWidth != 0 ? a_snapshot.actualRenderWidth : a_plan.renderWidth;
		result.height = a_snapshot.actualRenderHeight != 0 ? a_snapshot.actualRenderHeight : a_plan.renderHeight;

		if (!a_snapshot.HasRequiredInputs()) {
			result.reason = "required resources missing";
			return result;
		}

		if (a_snapshot.output.width < a_plan.displayWidth || a_snapshot.output.height < a_plan.displayHeight) {
			result.reason = std::format(
				"output target too small: {}x{} < display {}x{}",
				a_snapshot.output.width,
				a_snapshot.output.height,
				a_plan.displayWidth,
				a_plan.displayHeight);
			return result;
		}

		if (a_snapshot.color.width < a_plan.renderWidth || a_snapshot.color.height < a_plan.renderHeight) {
			result.reason = std::format(
				"color target too small: {}x{} < render {}x{}",
				a_snapshot.color.width,
				a_snapshot.color.height,
				a_plan.renderWidth,
				a_plan.renderHeight);
			return result;
		}

		if (a_snapshot.motionVectors.width < a_plan.renderWidth || a_snapshot.motionVectors.height < a_plan.renderHeight) {
			result.reason = std::format(
				"motion target too small: {}x{} < render {}x{}",
				a_snapshot.motionVectors.width,
				a_snapshot.motionVectors.height,
				a_plan.renderWidth,
				a_plan.renderHeight);
			return result;
		}

		if (!a_snapshot.viewport.valid) {
			if (a_snapshot.color.width == a_plan.renderWidth && a_snapshot.color.height == a_plan.renderHeight) {
				result.valid = true;
				result.width = a_plan.renderWidth;
				result.height = a_plan.renderHeight;
				return result;
			}

			result.reason = "viewport unavailable for full-size color source";
			return result;
		}

		if (!CloseEnough(a_snapshot.actualRenderWidth, a_plan.renderWidth) || !CloseEnough(a_snapshot.actualRenderHeight, a_plan.renderHeight)) {
			result.reason = std::format(
				"source extent mismatch: viewport {}x{} vs planned {}x{}",
				a_snapshot.actualRenderWidth,
				a_snapshot.actualRenderHeight,
				a_plan.renderWidth,
				a_plan.renderHeight);
			return result;
		}

		if (a_snapshot.temporalAAMask.HasTexture() &&
			(a_snapshot.temporalAAMask.width < a_plan.renderWidth || a_snapshot.temporalAAMask.height < a_plan.renderHeight)) {
			logger::info(
				"Reactive mask source is smaller than render extent; masks will be conservative. mask={}x{}, render={}x{}",
				a_snapshot.temporalAAMask.width,
				a_snapshot.temporalAAMask.height,
				a_plan.renderWidth,
				a_plan.renderHeight);
		}

		result.valid = true;
		result.width = a_plan.renderWidth;
		result.height = a_plan.renderHeight;
		return result;
	}

	void Upscaler::ApplyRenderPlan(RE::BSGraphics::State& a_state, const RenderPlan& a_plan)
	{
		a_state.projectionPosScaleX = -2.0f * a_plan.jitter.x / static_cast<float>(a_plan.renderWidth);
		a_state.projectionPosScaleY = 2.0f * a_plan.jitter.y / static_cast<float>(a_plan.renderHeight);

		auto& runtimeData = a_state.GetRuntimeData();
		runtimeData.dynamicResolutionPreviousWidthRatio = appliedDynamicResolutionWidthRatio_;
		runtimeData.dynamicResolutionPreviousHeightRatio = appliedDynamicResolutionHeightRatio_;
		runtimeData.dynamicResolutionWidthRatio = static_cast<float>(a_plan.renderWidth) / static_cast<float>(a_plan.displayWidth);
		runtimeData.dynamicResolutionHeightRatio = static_cast<float>(a_plan.renderHeight) / static_cast<float>(a_plan.displayHeight);
		runtimeData.dynamicResolutionLock = 1;
		appliedDynamicResolutionWidthRatio_ = runtimeData.dynamicResolutionWidthRatio;
		appliedDynamicResolutionHeightRatio_ = runtimeData.dynamicResolutionHeightRatio;
	}

	void Upscaler::ResetRenderPlan(RE::BSGraphics::State& a_state)
	{
		a_state.projectionPosScaleX = 0.0f;
		a_state.projectionPosScaleY = 0.0f;

		auto& runtimeData = a_state.GetRuntimeData();
		runtimeData.dynamicResolutionPreviousWidthRatio = 1.0f;
		runtimeData.dynamicResolutionPreviousHeightRatio = 1.0f;
		runtimeData.dynamicResolutionWidthRatio = 1.0f;
		runtimeData.dynamicResolutionHeightRatio = 1.0f;
		runtimeData.dynamicResolutionLock = 1;
		appliedDynamicResolutionWidthRatio_ = 1.0f;
		appliedDynamicResolutionHeightRatio_ = 1.0f;
		upscaledThisFrame_ = false;
		lastUpscaledFrame_ = 0;
		temporalAAPassValid_ = false;
	}

	void Upscaler::ApplySuccessfulFrameState()
	{
		const auto restoreMode = Config::GetSingleton().GetSettings().debug.restoreMode;
		switch (restoreMode) {
		case DebugRestoreMode::kNone:
			MarkRenderTargetDirty();
			return;
		case DebugRestoreMode::kNativeImmediately:
			RestoreDownstreamRenderState();
			return;
		case DebugRestoreMode::kConservative:
			if (currentState_ && originalState_.valid) {
				currentState_->projectionPosScaleX = originalState_.projectionPosScaleX;
				currentState_->projectionPosScaleY = originalState_.projectionPosScaleY;
				UpdateCameraData();
				MarkRenderTargetDirty();
			}
			return;
		case DebugRestoreMode::kNativeAfterImageSpace:
		default:
			PrepareImageSpaceStateAfterUpscale();
			return;
		}
	}

	void Upscaler::PrepareImageSpaceStateAfterUpscale()
	{
		if (!currentState_ || !currentPlan_.active || currentPlan_.displayWidth == 0 || currentPlan_.displayHeight == 0) {
			return;
		}

		auto& runtimeData = currentState_->GetRuntimeData();
		runtimeData.dynamicResolutionWidthRatio = static_cast<float>(currentPlan_.renderWidth) / static_cast<float>(currentPlan_.displayWidth);
		runtimeData.dynamicResolutionHeightRatio = static_cast<float>(currentPlan_.renderHeight) / static_cast<float>(currentPlan_.displayHeight);
		runtimeData.dynamicResolutionLock = 1;
		UpdateCameraData();
		MarkRenderTargetDirty();

		if (currentState_->frameCount >= lastImageSpaceStateLogFrame_ + 120) {
			lastImageSpaceStateLogFrame_ = currentState_->frameCount;
			logger::info(
				"DragonScale image-space post state: frame={}, ratio={:.4f}x{:.4f}, previous={:.4f}x{:.4f}, lock={}, jitterScale={:.7f},{:.7f}",
				currentState_->frameCount,
				runtimeData.dynamicResolutionWidthRatio,
				runtimeData.dynamicResolutionHeightRatio,
				runtimeData.dynamicResolutionPreviousWidthRatio,
				runtimeData.dynamicResolutionPreviousHeightRatio,
				static_cast<std::int32_t>(runtimeData.dynamicResolutionLock),
				currentState_->projectionPosScaleX,
				currentState_->projectionPosScaleY);
		}
	}

	void Upscaler::RestoreDownstreamRenderState()
	{
		if (!currentState_ || !originalState_.valid) {
			return;
		}

		const auto restoreMode = Config::GetSingleton().GetSettings().debug.restoreMode;
		if (restoreMode == DebugRestoreMode::kNone) {
			return;
		}

		if (restoreMode == DebugRestoreMode::kConservative) {
			currentState_->projectionPosScaleX = originalState_.projectionPosScaleX;
			currentState_->projectionPosScaleY = originalState_.projectionPosScaleY;
			UpdateCameraData();
			MarkRenderTargetDirty();
			return;
		}

		const auto applyNativeState = [&]() {
			currentState_->projectionPosScaleX = originalState_.projectionPosScaleX;
			currentState_->projectionPosScaleY = originalState_.projectionPosScaleY;

			auto& runtimeData = currentState_->GetRuntimeData();
			runtimeData.dynamicResolutionPreviousWidthRatio = 1.0f;
			runtimeData.dynamicResolutionPreviousHeightRatio = 1.0f;
			runtimeData.dynamicResolutionWidthRatio = 1.0f;
			runtimeData.dynamicResolutionHeightRatio = 1.0f;
			runtimeData.dynamicResolutionLock = 1;
		};

		applyNativeState();
		if (auto* renderer = RE::BSGraphics::Renderer::GetSingleton()) {
			renderer->UpdateViewPort(0, 0, true);
			applyNativeState();
		}
		UpdateCameraData();
		applyNativeState();
		UpdateCameraData();
		applyNativeState();
		MarkRenderTargetDirty();

		if (currentState_->frameCount >= lastRestoreStateLogFrame_ + 120) {
			lastRestoreStateLogFrame_ = currentState_->frameCount;
			const auto& restoredData = currentState_->GetRuntimeData();
			logger::info(
				"DragonScale downstream native state: frame={}, ratio={:.4f}x{:.4f}, previous={:.4f}x{:.4f}, lock={}, jitterScale={:.7f},{:.7f}",
				currentState_->frameCount,
				restoredData.dynamicResolutionWidthRatio,
				restoredData.dynamicResolutionHeightRatio,
				restoredData.dynamicResolutionPreviousWidthRatio,
				restoredData.dynamicResolutionPreviousHeightRatio,
				static_cast<std::int32_t>(restoredData.dynamicResolutionLock),
				currentState_->projectionPosScaleX,
				currentState_->projectionPosScaleY);
		}
	}

	void Upscaler::EnsureDynamicResolutionEnabled(std::string_view a_reason)
	{
		if (!IsUpscalingEnabled() || !IsFsrPipelineMode()) {
			return;
		}

		if (auto* dynamicResolution = RE::GetINISetting("bEnableAutoDynamicResolution:Display")) {
			if (!dynamicResolution->data.b) {
				dynamicResolution->data.b = true;
				if (!dynamicResolutionSettingForcedLogged_) {
					logger::info("Forced Skyrim dynamic resolution on for DragonScale upscaling ({})", a_reason);
					dynamicResolutionSettingForcedLogged_ = true;
				}
			}
		} else if (!dynamicResolutionSettingMissingLogged_) {
			logger::warn("Could not find bEnableAutoDynamicResolution:Display; Skyrim may ignore DragonScale render ratios");
			dynamicResolutionSettingMissingLogged_ = true;
		}

		static bool clampLogged = false;
		if (auto* clampOffset = RE::GetINISetting("fDRClampOffset:Display")) {
			if (std::abs(clampOffset->data.f) > 0.0001f) {
				clampOffset->data.f = 0.0f;
				if (!clampLogged) {
					logger::info("Set fDRClampOffset:Display to 0.0 for DragonScale render scaling");
					clampLogged = true;
				}
			}
		}
	}

	void Upscaler::LogDynamicResolutionState(std::string_view a_reason, const RE::BSGraphics::State& a_state) noexcept
	{
		if (lastDynamicResolutionLogFrame_ != 0 && a_state.frameCount < lastDynamicResolutionLogFrame_ + 120) {
			return;
		}

		lastDynamicResolutionLogFrame_ = a_state.frameCount;
		const auto& runtimeData = a_state.GetRuntimeData();
		const auto dynamicResolutionLock = static_cast<std::int32_t>(runtimeData.dynamicResolutionLock);
		logger::info(
			"DragonScale DRS {}: frame={}, ratio={:.4f}x{:.4f}, previous={:.4f}x{:.4f}, lock={}, jitterScale={:.7f},{:.7f}",
			std::string(a_reason),
			a_state.frameCount,
			runtimeData.dynamicResolutionWidthRatio,
			runtimeData.dynamicResolutionHeightRatio,
			runtimeData.dynamicResolutionPreviousWidthRatio,
			runtimeData.dynamicResolutionPreviousHeightRatio,
			dynamicResolutionLock,
			a_state.projectionPosScaleX,
			a_state.projectionPosScaleY);
	}

	void Upscaler::MarkRenderTargetDirty() noexcept
	{
		if (auto* shadowState = RE::BSGraphics::RendererShadowState::GetSingleton()) {
			shadowState->GetRuntimeData().stateUpdateFlags |= RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET;
			shadowState->GetRuntimeData().stateUpdateFlags |= RE::BSGraphics::ShaderFlags::DIRTY_VIEWPORT;
		}
	}

	void Upscaler::RefreshEnbDetection(std::string_view a_reason)
	{
		const auto& settings = Config::GetSingleton().GetSettings();
		const auto previousDetected = enbDetected_;
		const auto& detection = EnbDetector::GetSingleton().Refresh(settings.compatibility.enbMode, a_reason);
		enbDetected_ = detection.detected;
		strictExtentValidation_ = detection.compatibleMode || detection.detected;
		if (!previousDetected && enbDetected_) {
			Fsr2Backend::GetSingleton().ResetHistory();
			logger::info("Resetting FSR2 history because ENB became active");
		}
	}

	bool Upscaler::ShouldResetHistory(const ResourceSnapshot&) noexcept
	{
		const bool sizeChanged =
			lastDisplayWidth_ != currentPlan_.displayWidth ||
			lastDisplayHeight_ != currentPlan_.displayHeight ||
			lastRenderWidth_ != currentPlan_.renderWidth ||
			lastRenderHeight_ != currentPlan_.renderHeight;

		lastDisplayWidth_ = currentPlan_.displayWidth;
		lastDisplayHeight_ = currentPlan_.displayHeight;
		lastRenderWidth_ = currentPlan_.renderWidth;
		lastRenderHeight_ = currentPlan_.renderHeight;

		const auto& settings = Config::GetSingleton().GetSettings();
		const bool loadingReset = settings.upscaling.resetOnLoadingMenu && MenuStateTracker::GetSingleton().ConsumeResetRequest();
		if (sizeChanged || loadingReset) {
			logger::info("Resetting FSR2 history: sizeChanged={}, loadingMenuClosed={}", sizeChanged, loadingReset);
		}
		return sizeChanged || loadingReset;
	}

	void Upscaler::LogMipBias(const RenderPlan& a_plan) noexcept
	{
		const auto& settings = Config::GetSingleton().GetSettings().upscaling;
		if (!settings.mipBiasEnabled) {
			return;
		}

		const auto computed = settings.mipBiasAuto ? std::clamp(std::log2((std::max)(a_plan.renderScale, 0.01f)), -8.0f, 0.0f) : settings.mipBiasValue;
		if (std::abs(computed - lastLoggedMipBias_) < 0.01f) {
			return;
		}

		lastLoggedMipBias_ = computed;
		logger::info("DragonScale mip bias target={:.2f} ({})", computed, settings.mipBiasAuto ? "Auto" : "Manual");
	}
}
