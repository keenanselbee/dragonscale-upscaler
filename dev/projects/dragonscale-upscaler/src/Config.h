#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace DragonScale
{
	enum class UpscaleMethod : std::uint32_t
	{
		kNone = 0,
		kFsr2 = 1
	};

	enum class QualityMode : std::uint32_t
	{
		kNativeAA = 0,
		kQuality = 1,
		kBalanced = 2,
		kPerformance = 3,
		kUltraPerformance = 4
	};

	enum class EnbMode : std::uint32_t
	{
		kAuto = 0,
		kForceCompatible = 1,
		kOff = 2
	};

	enum class OverlayPosition : std::uint32_t
	{
		kTopLeft = 0
	};

	enum class FsrShaderModelOverride : std::uint32_t
	{
		kAuto = 0,
		k51 = 1,
		k60 = 2,
		k61 = 3,
		k62 = 4,
		k63 = 5,
		k64 = 6,
		k65 = 7,
		k66 = 8,
		k67 = 9
	};

	enum class FsrTriState : std::uint32_t
	{
		kAuto = 0,
		kDisabled = 1,
		kEnabled = 2
	};

	enum class DebugPipelineMode : std::uint32_t
	{
		kCaptureOnly = 0,
		kCopyThrough = 1,
		kFsr2 = 2,
		kScaleCopy = 3
	};

	enum class DebugSourceMode : std::uint32_t
	{
		kAuto = 0,
		kRendererSlot = 1,
		kImageSpace = 2
	};

	enum class DebugOutputMode : std::uint32_t
	{
		kOwnedCopyBack = 0,
		kDirectMain = 1
	};

	enum class DebugRestoreMode : std::uint32_t
	{
		kNone = 0,
		kConservative = 1,
		kNativeImmediately = 2,
		kNativeAfterImageSpace = 3
	};

	struct UpscalingSettings
	{
		bool enabled = true;
		UpscaleMethod method = UpscaleMethod::kFsr2;
		QualityMode quality = QualityMode::kQuality;
		float sharpness = 0.0f;
		std::string mipBias = "Auto";
		bool mipBiasEnabled = true;
		bool mipBiasAuto = true;
		float mipBiasValue = 0.0f;
		float fsrFov = 0.0f;
		FsrShaderModelOverride fsrForceShaderModel = FsrShaderModelOverride::k51;
		bool fsrAllowFP16 = false;
		bool fsrAllowWave64 = false;
		FsrTriState fsrHdr = FsrTriState::kAuto;
		bool fsrAutoExposure = false;
		bool fsrDepthInverted = false;
		bool fsrDebug = false;
		bool nativeAA = false;
		bool resetOnLoadingMenu = true;
		bool useTextureDiscoveryFallback = true;
		bool encodeReactiveMasks = true;
	};

	struct CompatibilitySettings
	{
		EnbMode enbMode = EnbMode::kAuto;
		bool preferEngineRenderHooks = true;
		bool patchScissorRects = true;
		bool patchFaceGenNative = true;
		bool patchPrecipitationNative = true;
		bool forceFXAAOff = true;
		bool repairAuxiliaryBuffers = false;
		bool repairDepth = false;
		bool repairSAOCameraZ = false;
		bool repairRefractionNormals = false;
		bool repairUnderwaterMask = false;
		bool preserveMainAlpha = true;
		bool patchRenderTargetAccess = false;
		bool useDirectMainOutput = false;
		std::string reShadeMode = "Off";
	};

	struct DebugSettings
	{
		std::string logLevel = "Info";
		DebugPipelineMode pipelineMode = DebugPipelineMode::kCopyThrough;
		DebugSourceMode sourceMode = DebugSourceMode::kAuto;
		DebugOutputMode outputMode = DebugOutputMode::kOwnedCopyBack;
		DebugRestoreMode restoreMode = DebugRestoreMode::kConservative;
		bool dumpFrame = false;
		bool overlay = false;
		OverlayPosition overlayPosition = OverlayPosition::kTopLeft;
		float overlayScale = 1.0f;
		bool overlayGpuTiming = true;
		std::uint32_t overlayLogInterval = 1000;
		std::string overlayToggleHotkey = "Pause";
	};

	struct Settings
	{
		UpscalingSettings upscaling;
		CompatibilitySettings compatibility;
		DebugSettings debug;
	};

	class Config
	{
	public:
		[[nodiscard]] static Config& GetSingleton();

		void Load();

		[[nodiscard]] const Settings& GetSettings() const noexcept;
		[[nodiscard]] const std::filesystem::path& GetPath() const noexcept;

	private:
		Config() = default;

		void EnsureDefaultFile() const;

		Settings settings_;
		std::filesystem::path path_{ "Data\\SKSE\\Plugins\\dragonscale-upscaler.ini" };
	};

	[[nodiscard]] float GetUpscaleRatio(QualityMode a_quality) noexcept;
	[[nodiscard]] std::string_view ToString(UpscaleMethod a_method) noexcept;
	[[nodiscard]] std::string_view ToString(QualityMode a_quality) noexcept;
	[[nodiscard]] std::string_view ToString(OverlayPosition a_position) noexcept;
	[[nodiscard]] std::string_view ToString(FsrShaderModelOverride a_shaderModel) noexcept;
	[[nodiscard]] std::string_view ToString(FsrTriState a_value) noexcept;
	[[nodiscard]] std::string_view ToString(DebugPipelineMode a_value) noexcept;
	[[nodiscard]] std::string_view ToString(DebugSourceMode a_value) noexcept;
	[[nodiscard]] std::string_view ToString(DebugOutputMode a_value) noexcept;
	[[nodiscard]] std::string_view ToString(DebugRestoreMode a_value) noexcept;
}
