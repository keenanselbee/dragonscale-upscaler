#include "Config.h"

#include <REX/W32/KERNEL32.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace
{
	[[nodiscard]] std::string Trim(std::string value)
	{
		const auto first = value.find_first_not_of(" \t\r\n");
		if (first == std::string::npos) {
			return {};
		}

		const auto last = value.find_last_not_of(" \t\r\n");
		return value.substr(first, last - first + 1);
	}

	[[nodiscard]] std::string Lower(std::string value)
	{
		std::ranges::transform(value, value.begin(), [](unsigned char ch) {
			return static_cast<char>(std::tolower(ch));
		});
		return value;
	}

	[[nodiscard]] std::string ReadString(
		const std::filesystem::path& path,
		const char* section,
		const char* key,
		const char* fallback)
	{
		std::array<char, 256> buffer{};
		const auto pathString = path.string();
		REX::W32::GetPrivateProfileStringA(section, key, fallback, buffer.data(), static_cast<std::uint32_t>(buffer.size()), pathString.c_str());
		return Trim(buffer.data());
	}

	[[nodiscard]] bool ReadBool(
		const std::filesystem::path& path,
		const char* section,
		const char* key,
		bool fallback)
	{
		auto value = Lower(ReadString(path, section, key, fallback ? "1" : "0"));
		if (value == "1" || value == "true" || value == "yes" || value == "on") {
			return true;
		}
		if (value == "0" || value == "false" || value == "no" || value == "off") {
			return false;
		}
		return fallback;
	}

	[[nodiscard]] float ReadFloat(
		const std::filesystem::path& path,
		const char* section,
		const char* key,
		float fallback)
	{
		const auto value = ReadString(path, section, key, "");
		if (value.empty()) {
			return fallback;
		}

		float parsed = fallback;
		const auto* first = value.data();
		const auto* last = first + value.size();
		if (std::from_chars(first, last, parsed).ec == std::errc{}) {
			return parsed;
		}

		return fallback;
	}

	[[nodiscard]] float ReadFovDegrees(
		const std::filesystem::path& path,
		const char* section,
		const char* key,
		float fallback)
	{
		const auto value = ReadString(path, section, key, "");
		if (value.empty()) {
			return fallback;
		}

		const auto normalized = Lower(value);
		if (normalized == "auto") {
			return 0.0f;
		}

		float parsed = fallback;
		const auto* first = value.data();
		const auto* last = first + value.size();
		if (std::from_chars(first, last, parsed).ec != std::errc{}) {
			return fallback;
		}

		if (parsed == 0.0f) {
			return 0.0f;
		}

		return std::clamp(parsed, 1.0f, 179.0f);
	}

	void ReadMipBias(
		const std::filesystem::path& path,
		const char* section,
		const char* key,
		DragonScale::UpscalingSettings& settings)
	{
		const auto value = ReadString(path, section, key, settings.mipBias.c_str());
		const auto normalized = Lower(value);
		settings.mipBias = value.empty() ? "Auto" : value;
		if (normalized == "auto") {
			settings.mipBiasEnabled = true;
			settings.mipBiasAuto = true;
			settings.mipBiasValue = 0.0f;
			return;
		}
		if (normalized == "off" || normalized == "disabled" || normalized == "0") {
			settings.mipBiasEnabled = false;
			settings.mipBiasAuto = false;
			settings.mipBiasValue = 0.0f;
			settings.mipBias = "Off";
			return;
		}

		float parsed = 0.0f;
		const auto* first = value.data();
		const auto* last = first + value.size();
		if (std::from_chars(first, last, parsed).ec == std::errc{}) {
			settings.mipBiasEnabled = true;
			settings.mipBiasAuto = false;
			settings.mipBiasValue = std::clamp(parsed, -8.0f, 0.0f);
			settings.mipBias = std::to_string(settings.mipBiasValue);
			return;
		}

		settings.mipBiasEnabled = true;
		settings.mipBiasAuto = true;
		settings.mipBiasValue = 0.0f;
		settings.mipBias = "Auto";
	}

	[[nodiscard]] std::uint32_t ReadUInt32(
		const std::filesystem::path& path,
		const char* section,
		const char* key,
		std::uint32_t fallback)
	{
		const auto value = ReadString(path, section, key, "");
		if (value.empty()) {
			return fallback;
		}

		std::uint32_t parsed = fallback;
		const auto* first = value.data();
		const auto* last = first + value.size();
		if (std::from_chars(first, last, parsed).ec == std::errc{}) {
			return parsed;
		}

		return fallback;
	}

	[[nodiscard]] DragonScale::UpscaleMethod ReadMethod(
		const std::filesystem::path& path,
		const char* section,
		const char* key,
		DragonScale::UpscaleMethod fallback)
	{
		const auto fallbackValue = std::string(DragonScale::ToString(fallback));
		const auto value = Lower(ReadString(path, section, key, fallbackValue.c_str()));
		if (value == "none" || value == "off" || value == "0") {
			return DragonScale::UpscaleMethod::kNone;
		}
		if (value == "fsr" || value == "fsr2" || value == "fsr2.0" || value == "fsr2.1" || value == "fsr2.2" || value == "fsr2.3") {
			return DragonScale::UpscaleMethod::kFsr2;
		}
		if (value == "fsr3" || value == "fsr31" || value == "fsr3.1" || value == "1") {
			logger::warn("Upscaling method '{}' is a legacy alias; using FSR2", value);
			return DragonScale::UpscaleMethod::kFsr2;
		}
		return fallback;
	}

	[[nodiscard]] DragonScale::QualityMode ReadQuality(
		const std::filesystem::path& path,
		const char* section,
		const char* key,
		DragonScale::QualityMode fallback)
	{
		const auto fallbackValue = std::string(DragonScale::ToString(fallback));
		const auto value = Lower(ReadString(path, section, key, fallbackValue.c_str()));
		if (value == "native" || value == "nativeaa" || value == "native_aa" || value == "0") {
			return DragonScale::QualityMode::kNativeAA;
		}
		if (value == "quality" || value == "1") {
			return DragonScale::QualityMode::kQuality;
		}
		if (value == "balanced" || value == "2") {
			return DragonScale::QualityMode::kBalanced;
		}
		if (value == "performance" || value == "3") {
			return DragonScale::QualityMode::kPerformance;
		}
		if (value == "ultraperformance" || value == "ultra_performance" || value == "4") {
			return DragonScale::QualityMode::kUltraPerformance;
		}
		return fallback;
	}

	[[nodiscard]] DragonScale::EnbMode ReadEnbMode(
		const std::filesystem::path& path,
		const char* section,
		const char* key,
		DragonScale::EnbMode fallback)
	{
		const auto value = Lower(ReadString(path, section, key, "Auto"));
		if (value == "auto" || value == "0") {
			return DragonScale::EnbMode::kAuto;
		}
		if (value == "forcecompatible" || value == "force_compatible" || value == "compatible" || value == "1") {
			return DragonScale::EnbMode::kForceCompatible;
		}
		if (value == "off" || value == "disabled" || value == "2") {
			return DragonScale::EnbMode::kOff;
		}
		return fallback;
	}

	[[nodiscard]] DragonScale::OverlayPosition ReadOverlayPosition(
		const std::filesystem::path& path,
		const char* section,
		const char* key,
		DragonScale::OverlayPosition fallback)
	{
		const auto value = Lower(ReadString(path, section, key, "TopLeft"));
		if (value == "topleft" || value == "top_left" || value == "0") {
			return DragonScale::OverlayPosition::kTopLeft;
		}
		return fallback;
	}

	[[nodiscard]] DragonScale::FsrShaderModelOverride ReadFsrShaderModel(
		const std::filesystem::path& path,
		const char* section,
		const char* key,
		DragonScale::FsrShaderModelOverride fallback)
	{
		const auto value = Lower(ReadString(path, section, key, "5.1"));
		if (value == "auto" || value == "0") {
			return DragonScale::FsrShaderModelOverride::kAuto;
		}
		if (value == "5.1" || value == "51" || value == "sm5.1" || value == "sm51" || value == "1") {
			return DragonScale::FsrShaderModelOverride::k51;
		}
		if (value == "6.0" || value == "60" || value == "sm6.0" || value == "sm60" || value == "2") {
			return DragonScale::FsrShaderModelOverride::k60;
		}
		if (value == "6.1" || value == "61" || value == "sm6.1" || value == "sm61" || value == "3") {
			return DragonScale::FsrShaderModelOverride::k61;
		}
		if (value == "6.2" || value == "62" || value == "sm6.2" || value == "sm62" || value == "4") {
			return DragonScale::FsrShaderModelOverride::k62;
		}
		if (value == "6.3" || value == "63" || value == "sm6.3" || value == "sm63" || value == "5") {
			return DragonScale::FsrShaderModelOverride::k63;
		}
		if (value == "6.4" || value == "64" || value == "sm6.4" || value == "sm64" || value == "6") {
			return DragonScale::FsrShaderModelOverride::k64;
		}
		if (value == "6.5" || value == "65" || value == "sm6.5" || value == "sm65" || value == "7") {
			return DragonScale::FsrShaderModelOverride::k65;
		}
		if (value == "6.6" || value == "66" || value == "sm6.6" || value == "sm66" || value == "8") {
			return DragonScale::FsrShaderModelOverride::k66;
		}
		if (value == "6.7" || value == "67" || value == "sm6.7" || value == "sm67" || value == "9") {
			return DragonScale::FsrShaderModelOverride::k67;
		}
		return fallback;
	}

	[[nodiscard]] DragonScale::FsrTriState ReadTriState(
		const std::filesystem::path& path,
		const char* section,
		const char* key,
		DragonScale::FsrTriState fallback)
	{
		const auto value = Lower(ReadString(path, section, key, "Auto"));
		if (value == "auto") {
			return DragonScale::FsrTriState::kAuto;
		}
		if (value == "1" || value == "true" || value == "yes" || value == "on" || value == "enabled") {
			return DragonScale::FsrTriState::kEnabled;
		}
		if (value == "0" || value == "false" || value == "no" || value == "off" || value == "disabled") {
			return DragonScale::FsrTriState::kDisabled;
		}
		return fallback;
	}

	[[nodiscard]] DragonScale::DebugPipelineMode ReadDebugPipelineMode(
		const std::filesystem::path& path,
		const char* section,
		const char* key,
		DragonScale::DebugPipelineMode fallback)
	{
		const auto value = Lower(ReadString(path, section, key, DragonScale::ToString(fallback).data()));
		if (value == "captureonly" || value == "capture_only" || value == "capture" || value == "0") {
			return DragonScale::DebugPipelineMode::kCaptureOnly;
		}
		if (value == "copythrough" || value == "copy_through" || value == "copy" || value == "1") {
			return DragonScale::DebugPipelineMode::kCopyThrough;
		}
		if (value == "fsr2" || value == "fsr" || value == "2") {
			return DragonScale::DebugPipelineMode::kFsr2;
		}
		if (value == "scalecopy" || value == "scale_copy" || value == "bilinear" || value == "bilinearcopy" || value == "3") {
			return DragonScale::DebugPipelineMode::kScaleCopy;
		}
		return fallback;
	}

	[[nodiscard]] DragonScale::DebugSourceMode ReadDebugSourceMode(
		const std::filesystem::path& path,
		const char* section,
		const char* key,
		DragonScale::DebugSourceMode fallback)
	{
		const auto value = Lower(ReadString(path, section, key, DragonScale::ToString(fallback).data()));
		if (value == "auto" || value == "0") {
			return DragonScale::DebugSourceMode::kAuto;
		}
		if (value == "rendererslot" || value == "renderer_slot" || value == "renderer" || value == "1") {
			return DragonScale::DebugSourceMode::kRendererSlot;
		}
		if (value == "imagespace" || value == "image_space" || value == "pipeline" || value == "2") {
			return DragonScale::DebugSourceMode::kImageSpace;
		}
		return fallback;
	}

	[[nodiscard]] DragonScale::DebugOutputMode ReadDebugOutputMode(
		const std::filesystem::path& path,
		const char* section,
		const char* key,
		DragonScale::DebugOutputMode fallback)
	{
		const auto value = Lower(ReadString(path, section, key, DragonScale::ToString(fallback).data()));
		if (value == "ownedcopyback" || value == "owned_copy_back" || value == "owned" || value == "copyback" || value == "0") {
			return DragonScale::DebugOutputMode::kOwnedCopyBack;
		}
		if (value == "directmain" || value == "direct_main" || value == "direct" || value == "1") {
			return DragonScale::DebugOutputMode::kDirectMain;
		}
		return fallback;
	}

	[[nodiscard]] DragonScale::DebugRestoreMode ReadDebugRestoreMode(
		const std::filesystem::path& path,
		const char* section,
		const char* key,
		DragonScale::DebugRestoreMode fallback)
	{
		const auto value = Lower(ReadString(path, section, key, DragonScale::ToString(fallback).data()));
		if (value == "none" || value == "off" || value == "0") {
			return DragonScale::DebugRestoreMode::kNone;
		}
		if (value == "conservative" || value == "safe" || value == "1") {
			return DragonScale::DebugRestoreMode::kConservative;
		}
		if (value == "nativeimmediately" || value == "native_immediately" || value == "immediate" || value == "2") {
			return DragonScale::DebugRestoreMode::kNativeImmediately;
		}
		if (value == "nativeafterimagespace" || value == "native_after_imagespace" || value == "afterimagespace" || value == "after_image_space" || value == "3") {
			return DragonScale::DebugRestoreMode::kNativeAfterImageSpace;
		}
		return fallback;
	}
}

namespace DragonScale
{
	Config& Config::GetSingleton()
	{
		static Config singleton;
		return singleton;
	}

	void Config::Load()
	{
		EnsureDefaultFile();

		settings_.upscaling.enabled = ReadBool(path_, "Upscaling", "Enabled", settings_.upscaling.enabled);
		settings_.upscaling.method = ReadMethod(path_, "Upscaling", "Method", settings_.upscaling.method);
		settings_.upscaling.quality = ReadQuality(path_, "Upscaling", "Quality", settings_.upscaling.quality);
		settings_.upscaling.sharpness = std::clamp(ReadFloat(path_, "Upscaling", "Sharpness", settings_.upscaling.sharpness), 0.0f, 1.0f);
		ReadMipBias(path_, "Upscaling", "MipBias", settings_.upscaling);
		settings_.upscaling.fsrFov = ReadFovDegrees(path_, "Upscaling", "FsrFov", settings_.upscaling.fsrFov);
		settings_.upscaling.fsrForceShaderModel = ReadFsrShaderModel(path_, "Upscaling", "FsrForceShaderModel", settings_.upscaling.fsrForceShaderModel);
		settings_.upscaling.fsrAllowFP16 = ReadBool(path_, "Upscaling", "FsrAllowFP16", settings_.upscaling.fsrAllowFP16);
		settings_.upscaling.fsrAllowWave64 = ReadBool(path_, "Upscaling", "FsrAllowWave64", settings_.upscaling.fsrAllowWave64);
		settings_.upscaling.fsrHdr = ReadTriState(path_, "Upscaling", "FsrHdr", settings_.upscaling.fsrHdr);
		settings_.upscaling.fsrAutoExposure = ReadBool(path_, "Upscaling", "FsrAutoExposure", settings_.upscaling.fsrAutoExposure);
		settings_.upscaling.fsrDepthInverted = ReadBool(path_, "Upscaling", "FsrDepthInverted", settings_.upscaling.fsrDepthInverted);
		settings_.upscaling.fsrDebug = ReadBool(path_, "Upscaling", "FsrDebug", settings_.upscaling.fsrDebug);
		settings_.upscaling.nativeAA = ReadBool(path_, "Upscaling", "NativeAA", settings_.upscaling.nativeAA);
		settings_.upscaling.resetOnLoadingMenu = ReadBool(path_, "Upscaling", "ResetOnLoadingMenu", settings_.upscaling.resetOnLoadingMenu);
		settings_.upscaling.useTextureDiscoveryFallback = ReadBool(path_, "Upscaling", "UseTextureDiscoveryFallback", settings_.upscaling.useTextureDiscoveryFallback);
		settings_.upscaling.encodeReactiveMasks = ReadBool(path_, "Upscaling", "EncodeReactiveMasks", settings_.upscaling.encodeReactiveMasks);

		settings_.compatibility.enbMode = ReadEnbMode(path_, "Compatibility", "ENBMode", settings_.compatibility.enbMode);
		settings_.compatibility.preferEngineRenderHooks = ReadBool(path_, "Compatibility", "PreferEngineRenderHooks", settings_.compatibility.preferEngineRenderHooks);
		settings_.compatibility.patchScissorRects = ReadBool(path_, "Compatibility", "PatchScissorRects", settings_.compatibility.patchScissorRects);
		settings_.compatibility.patchFaceGenNative = ReadBool(path_, "Compatibility", "PatchFaceGenNative", settings_.compatibility.patchFaceGenNative);
		settings_.compatibility.patchPrecipitationNative = ReadBool(path_, "Compatibility", "PatchPrecipitationNative", settings_.compatibility.patchPrecipitationNative);
		settings_.compatibility.forceFXAAOff = ReadBool(path_, "Compatibility", "ForceFXAAOff", settings_.compatibility.forceFXAAOff);
		settings_.compatibility.repairAuxiliaryBuffers = ReadBool(path_, "Compatibility", "RepairAuxiliaryBuffers", settings_.compatibility.repairAuxiliaryBuffers);
		settings_.compatibility.repairDepth = ReadBool(path_, "Compatibility", "RepairDepth", settings_.compatibility.repairDepth);
		settings_.compatibility.repairSAOCameraZ = ReadBool(path_, "Compatibility", "RepairSAOCameraZ", settings_.compatibility.repairSAOCameraZ);
		settings_.compatibility.repairRefractionNormals = ReadBool(path_, "Compatibility", "RepairRefractionNormals", settings_.compatibility.repairRefractionNormals);
		settings_.compatibility.repairUnderwaterMask = ReadBool(path_, "Compatibility", "RepairUnderwaterMask", settings_.compatibility.repairUnderwaterMask);
		settings_.compatibility.preserveMainAlpha = ReadBool(path_, "Compatibility", "PreserveMainAlpha", settings_.compatibility.preserveMainAlpha);
		settings_.compatibility.patchRenderTargetAccess = ReadBool(path_, "Compatibility", "PatchRenderTargetAccess", settings_.compatibility.patchRenderTargetAccess);
		settings_.compatibility.useDirectMainOutput = ReadBool(path_, "Compatibility", "UseDirectMainOutput", settings_.compatibility.useDirectMainOutput);
		settings_.compatibility.reShadeMode = ReadString(path_, "Compatibility", "ReShadeMode", settings_.compatibility.reShadeMode.c_str());

		settings_.debug.logLevel = ReadString(path_, "Debug", "LogLevel", settings_.debug.logLevel.c_str());
		settings_.debug.pipelineMode = ReadDebugPipelineMode(path_, "Debug", "PipelineMode", settings_.debug.pipelineMode);
		settings_.debug.sourceMode = ReadDebugSourceMode(path_, "Debug", "SourceMode", settings_.debug.sourceMode);
		settings_.debug.outputMode = ReadDebugOutputMode(path_, "Debug", "OutputMode", settings_.debug.outputMode);
		settings_.debug.restoreMode = ReadDebugRestoreMode(path_, "Debug", "RestoreMode", settings_.debug.restoreMode);
		settings_.debug.dumpFrame = ReadBool(path_, "Debug", "DumpFrame", settings_.debug.dumpFrame);
		settings_.debug.overlay = ReadBool(path_, "Debug", "Overlay", settings_.debug.overlay);
		settings_.debug.overlayPosition = ReadOverlayPosition(path_, "Debug", "OverlayPosition", settings_.debug.overlayPosition);
		settings_.debug.overlayScale = std::clamp(ReadFloat(path_, "Debug", "OverlayScale", settings_.debug.overlayScale), 0.5f, 4.0f);
		settings_.debug.overlayGpuTiming = ReadBool(path_, "Debug", "OverlayGpuTiming", settings_.debug.overlayGpuTiming);
		settings_.debug.overlayLogInterval = std::clamp<std::uint32_t>(ReadUInt32(path_, "Debug", "OverlayLogInterval", settings_.debug.overlayLogInterval), 100, 60000);
		settings_.debug.overlayToggleHotkey = ReadString(path_, "Debug", "OverlayToggleHotkey", settings_.debug.overlayToggleHotkey.c_str());

		logger::info("Loaded config from {}", path_.string());
		logger::info(
			"Upscaling enabled={}, method={}, quality={}, sharpness={:.2f}",
			settings_.upscaling.enabled,
			ToString(settings_.upscaling.method),
			ToString(settings_.upscaling.quality),
			settings_.upscaling.sharpness);
		logger::info(
			"Upscaling resource settings: mipBias={}, resetOnLoadingMenu={}, textureDiscoveryFallback={}, encodeReactiveMasks={}",
			settings_.upscaling.mipBias,
			settings_.upscaling.resetOnLoadingMenu,
			settings_.upscaling.useTextureDiscoveryFallback,
			settings_.upscaling.encodeReactiveMasks);
		if (settings_.upscaling.fsrFov == 0.0f) {
			logger::info("FSR camera FOV source=Auto");
		} else {
			logger::info("FSR camera FOV source={:.1f} horizontal degrees", settings_.upscaling.fsrFov);
		}
		logger::info(
			"FSR DX11 settings: shaderModel={}, allowFP16={}, allowWave64={}, hdr={}, autoExposure={}, depthInverted={}, debug={}",
			ToString(settings_.upscaling.fsrForceShaderModel),
			settings_.upscaling.fsrAllowFP16,
			settings_.upscaling.fsrAllowWave64,
			ToString(settings_.upscaling.fsrHdr),
			settings_.upscaling.fsrAutoExposure,
			settings_.upscaling.fsrDepthInverted,
			settings_.upscaling.fsrDebug);
		logger::info(
			"Compatibility settings: enbMode={}, preferEngineRenderHooks={}, patchScissorRects={}, patchFaceGenNative={}, patchPrecipitationNative={}, forceFXAAOff={}, repairAuxiliaryBuffers={}, repairDepth={}, repairSAOCameraZ={}, repairRefractionNormals={}, repairUnderwaterMask={}, preserveMainAlpha={}, patchRenderTargetAccess={}, useDirectMainOutput={}, reShadeMode={}",
			static_cast<std::uint32_t>(settings_.compatibility.enbMode),
			settings_.compatibility.preferEngineRenderHooks,
			settings_.compatibility.patchScissorRects,
			settings_.compatibility.patchFaceGenNative,
			settings_.compatibility.patchPrecipitationNative,
			settings_.compatibility.forceFXAAOff,
			settings_.compatibility.repairAuxiliaryBuffers,
			settings_.compatibility.repairDepth,
			settings_.compatibility.repairSAOCameraZ,
			settings_.compatibility.repairRefractionNormals,
			settings_.compatibility.repairUnderwaterMask,
			settings_.compatibility.preserveMainAlpha,
			settings_.compatibility.patchRenderTargetAccess,
			settings_.compatibility.useDirectMainOutput,
			settings_.compatibility.reShadeMode);
		logger::info(
			"Debug pipeline settings: pipelineMode={}, sourceMode={}, outputMode={}, restoreMode={}, dumpFrame={}",
			ToString(settings_.debug.pipelineMode),
			ToString(settings_.debug.sourceMode),
			ToString(settings_.debug.outputMode),
			ToString(settings_.debug.restoreMode),
			settings_.debug.dumpFrame);
		if (settings_.upscaling.method == UpscaleMethod::kFsr2 &&
			(settings_.upscaling.nativeAA || settings_.upscaling.quality == QualityMode::kNativeAA)) {
			logger::warn("NativeAA is not supported by FSR2 yet; using the Quality preset");
		}
	}

	const Settings& Config::GetSettings() const noexcept
	{
		return settings_;
	}

	const std::filesystem::path& Config::GetPath() const noexcept
	{
		return path_;
	}

	void Config::EnsureDefaultFile() const
	{
		if (std::filesystem::exists(path_)) {
			return;
		}

		std::filesystem::create_directories(path_.parent_path());

		std::ofstream file(path_);
		file
			<< "[Upscaling]\n\n"
			<< "; Enable DragonScale's render hooks and upscaling path.\n"
			<< "; 0 = Disabled, 1 = Enabled.\n"
			<< "Enabled = 1\n\n"
			<< "; Upscaling method. The first implementation path supports FSR2 upscaling only.\n"
			<< "; Valid values: None, FSR2.\n"
			<< "Method = FSR2\n\n"
			<< "; Upscaling quality preset.\n"
			<< "; Valid values: NativeAA, Quality, Balanced, Performance, UltraPerformance.\n"
			<< "Quality = Quality\n\n"
			<< "; FSR sharpening strength.\n"
			<< "; Valid range: 0.0-1.0.\n"
			<< "Sharpness = 0.0\n\n"
			<< "; Texture mip LOD bias. Auto follows the selected render scale; Off disables DragonScale biasing.\n"
			<< "; Valid values: Auto, Off, or a value from -8.0 to 0.0.\n"
			<< "MipBias = Auto\n\n"
			<< "; FSR camera FOV in Skyrim/game horizontal degrees. DragonScale converts numeric values to vertical FOV for FSR.\n"
			<< "; Use 0 or Auto to read Skyrim's active vertical FOV.\n"
			<< "FsrFov = Auto\n\n"
			<< "; Conservative DX11 shader path for FSR context creation.\n"
			<< "; Valid values: Auto, 5.1, 6.0, 6.1, 6.2, 6.3, 6.4, 6.5, 6.6, 6.7.\n"
			<< "FsrForceShaderModel = 5.1\n\n"
			<< "; Allow FSR FP16 shader permutations.\n"
			<< "; 0 = Disabled, 1 = Enabled.\n"
			<< "FsrAllowFP16 = 0\n\n"
			<< "; Allow FSR wave64 shader permutations when the backend reports support.\n"
			<< "; 0 = Disabled, 1 = Enabled.\n"
			<< "FsrAllowWave64 = 0\n\n"
			<< "; FSR HDR context flag. Auto follows the current render-window format.\n"
			<< "; Valid values: Auto, 0, 1.\n"
			<< "FsrHdr = Auto\n\n"
			<< "; Enable FSR auto exposure.\n"
			<< "; 0 = Disabled, 1 = Enabled.\n"
			<< "FsrAutoExposure = 0\n\n"
			<< "; Tell FSR the input depth buffer is inverted.\n"
			<< "; 0 = Disabled, 1 = Enabled.\n"
			<< "FsrDepthInverted = 0\n\n"
			<< "; Enable FidelityFX runtime debug checks and messages.\n"
			<< "; 0 = Disabled, 1 = Enabled.\n"
			<< "FsrDebug = 0\n\n"
			<< "; Native-resolution antialiasing is reserved for a later backend; FSR2 uses Quality when this is enabled.\n"
			<< "; 0 = Disabled, 1 = Enabled.\n"
			<< "NativeAA = 0\n\n"
			<< "; Reset FSR history when Skyrim closes the loading menu.\n"
			<< "; 0 = Disabled, 1 = Enabled.\n"
			<< "ResetOnLoadingMenu = 1\n\n"
			<< "; Enable renderer-slot resource discovery with a future texture-discovery fallback path.\n"
			<< "; 0 = Disabled, 1 = Enabled.\n"
			<< "UseTextureDiscoveryFallback = 1\n\n"
			<< "; Encode reactive and transparency/composition masks when source masks are available.\n"
			<< "; 0 = Disabled, 1 = Enabled.\n"
			<< "EncodeReactiveMasks = 1\n\n"
			<< "\n"
			<< "[Compatibility]\n\n"
			<< "; ENB compatibility mode.\n"
			<< "; Valid values: Auto, ForceCompatible, Off.\n"
			<< "ENBMode = Auto\n\n"
			<< "; Prefer Skyrim render-target hooks instead of wrapping the swap chain.\n"
			<< "; 0 = Disabled, 1 = Enabled.\n"
			<< "PreferEngineRenderHooks = 1\n\n"
			<< "; Clamp scissor rectangles to DragonScale's active render size when compatibility patching is available.\n"
			<< "; 0 = Disabled, 1 = Enabled.\n"
			<< "PatchScissorRects = 1\n\n"
			<< "; Keep facegen/customization texture generation at native resolution when compatibility patching is available.\n"
			<< "; 0 = Disabled, 1 = Enabled.\n"
			<< "PatchFaceGenNative = 1\n\n"
			<< "; Keep precipitation rendering at native resolution when compatibility patching is available.\n"
			<< "; 0 = Disabled, 1 = Enabled.\n"
			<< "PatchPrecipitationNative = 1\n\n"
			<< "; Force Skyrim FXAA off while DragonScale is loaded.\n"
			<< "; 0 = Disabled, 1 = Enabled.\n"
			<< "ForceFXAAOff = 1\n\n"
			<< "; Repair full-resolution auxiliary buffers after successful upscaling.\n"
			<< "; Disable individual passes to isolate ENB/lighting artifacts.\n"
			<< "; 0 = Disabled, 1 = Enabled.\n"
			<< "RepairAuxiliaryBuffers = 0\n"
			<< "RepairDepth = 0\n"
			<< "RepairSAOCameraZ = 0\n"
			<< "RepairRefractionNormals = 0\n"
			<< "RepairUnderwaterMask = 0\n\n"
			<< "; Preserve the main scene alpha channel after FSR writes the upscaled color.\n"
			<< "; This protects downstream Skyrim/ENB image-space passes that reuse MAIN alpha data.\n"
			<< "; 0 = Disabled, 1 = Enabled.\n"
			<< "PreserveMainAlpha = 1\n\n"
			<< "; Add UAV/copy access to render targets DragonScale needs when Skyrim creates them.\n"
			<< "; Requires a game restart after changing this value.\n"
			<< "; 0 = Disabled, 1 = Enabled.\n"
			<< "PatchRenderTargetAccess = 0\n\n"
			<< "; Let FSR write directly to Skyrim's main scene target when UAV access exists.\n"
			<< "; The safer default uses an owned output texture and explicit copy-back path.\n"
			<< "; 0 = Disabled, 1 = Enabled.\n"
			<< "UseDirectMainOutput = 0\n\n"
			<< "; ReShade runtime integration mode. Reserved for a later pass.\n"
			<< "; Valid values: Off, BeforeUpscale, AfterUpscale, UnderUI.\n"
			<< "ReShadeMode = Off\n\n"
			<< "\n"
			<< "[Debug]\n\n"
			<< "; SKSE plugin logging verbosity.\n"
			<< "; Valid values: Off, Info, Verbose.\n"
			<< "LogLevel = Info\n\n"
			<< "; Development pipeline mode for isolating rendering issues.\n"
			<< "; CaptureOnly logs resources and lets vanilla rendering continue.\n"
			<< "; CopyThrough copies the selected source through DragonScale-owned textures without FSR2.\n"
			<< "; ScaleCopy uses the dynamic render size but replaces FSR2 with a simple bilinear upscale.\n"
			<< "; FSR2 runs the real upscaler path.\n"
			<< "; Valid values: CaptureOnly, CopyThrough, ScaleCopy, FSR2.\n"
			<< "PipelineMode = CopyThrough\n\n"
			<< "; Select which captured color/output resources DragonScale uses.\n"
			<< "; Valid values: Auto, RendererSlot, ImageSpace.\n"
			<< "SourceMode = Auto\n\n"
			<< "; Select the output path.\n"
			<< "; OwnedCopyBack is the safest diagnostic path. DirectMain requires target UAV access.\n"
			<< "; Valid values: OwnedCopyBack, DirectMain.\n"
			<< "OutputMode = OwnedCopyBack\n\n"
			<< "; Select how DragonScale restores downstream Skyrim render state.\n"
			<< "; Valid values: None, Conservative, NativeImmediately, NativeAfterImageSpace.\n"
			<< "RestoreMode = Conservative\n\n"
			<< "; Reserved frame dump switch for deep diagnostics.\n"
			<< "; 0 = Disabled, 1 = Enabled.\n"
			<< "DumpFrame = 0\n\n"
			<< "; Show the development diagnostics overlay at startup.\n"
			<< "; 0 = Hidden, 1 = Visible.\n"
			<< "Overlay = 0\n\n"
			<< "; Keyboard shortcut that toggles the diagnostics overlay at runtime.\n"
			<< "; Use None to disable. Examples: Pause, Ctrl+Shift+F12, F10, Alt+F11.\n"
			<< "OverlayToggleHotkey = Pause\n\n"
			<< "; Diagnostics overlay position.\n"
			<< "; Valid values: TopLeft.\n"
			<< "OverlayPosition = TopLeft\n\n"
			<< "; Diagnostics overlay text scale.\n"
			<< "; Valid range: 0.5-4.0.\n"
			<< "OverlayScale = 1.0\n\n"
			<< "; Enable non-blocking D3D11 timestamp queries for FSR dispatch GPU timing.\n"
			<< "; 0 = Disabled, 1 = Enabled.\n"
			<< "OverlayGpuTiming = 1\n\n"
			<< "; Milliseconds between diagnostics log lines while overlay or verbose logging is active.\n"
			<< "; Valid range: 100-60000.\n"
			<< "OverlayLogInterval = 1000\n";
	}

	float GetUpscaleRatio(QualityMode a_quality) noexcept
	{
		switch (a_quality) {
		case QualityMode::kNativeAA:
			return 1.0f;
		case QualityMode::kQuality:
			return 1.5f;
		case QualityMode::kBalanced:
			return 1.7f;
		case QualityMode::kPerformance:
			return 2.0f;
		case QualityMode::kUltraPerformance:
			return 3.0f;
		default:
			return 1.5f;
		}
	}

	std::string_view ToString(UpscaleMethod a_method) noexcept
	{
		switch (a_method) {
		case UpscaleMethod::kNone:
			return "None";
		case UpscaleMethod::kFsr2:
			return "FSR2";
		default:
			return "Unknown";
		}
	}

	std::string_view ToString(QualityMode a_quality) noexcept
	{
		switch (a_quality) {
		case QualityMode::kNativeAA:
			return "NativeAA";
		case QualityMode::kQuality:
			return "Quality";
		case QualityMode::kBalanced:
			return "Balanced";
		case QualityMode::kPerformance:
			return "Performance";
		case QualityMode::kUltraPerformance:
			return "UltraPerformance";
		default:
			return "Unknown";
		}
	}

	std::string_view ToString(OverlayPosition a_position) noexcept
	{
		switch (a_position) {
		case OverlayPosition::kTopLeft:
			return "TopLeft";
		default:
			return "Unknown";
		}
	}

	std::string_view ToString(FsrShaderModelOverride a_shaderModel) noexcept
	{
		switch (a_shaderModel) {
		case FsrShaderModelOverride::kAuto:
			return "Auto";
		case FsrShaderModelOverride::k51:
			return "5.1";
		case FsrShaderModelOverride::k60:
			return "6.0";
		case FsrShaderModelOverride::k61:
			return "6.1";
		case FsrShaderModelOverride::k62:
			return "6.2";
		case FsrShaderModelOverride::k63:
			return "6.3";
		case FsrShaderModelOverride::k64:
			return "6.4";
		case FsrShaderModelOverride::k65:
			return "6.5";
		case FsrShaderModelOverride::k66:
			return "6.6";
		case FsrShaderModelOverride::k67:
			return "6.7";
		default:
			return "Unknown";
		}
	}

	std::string_view ToString(FsrTriState a_value) noexcept
	{
		switch (a_value) {
		case FsrTriState::kAuto:
			return "Auto";
		case FsrTriState::kDisabled:
			return "Disabled";
		case FsrTriState::kEnabled:
			return "Enabled";
		default:
			return "Unknown";
		}
	}

	std::string_view ToString(DebugPipelineMode a_value) noexcept
	{
		switch (a_value) {
		case DebugPipelineMode::kCaptureOnly:
			return "CaptureOnly";
		case DebugPipelineMode::kCopyThrough:
			return "CopyThrough";
		case DebugPipelineMode::kFsr2:
			return "FSR2";
		case DebugPipelineMode::kScaleCopy:
			return "ScaleCopy";
		default:
			return "Unknown";
		}
	}

	std::string_view ToString(DebugSourceMode a_value) noexcept
	{
		switch (a_value) {
		case DebugSourceMode::kAuto:
			return "Auto";
		case DebugSourceMode::kRendererSlot:
			return "RendererSlot";
		case DebugSourceMode::kImageSpace:
			return "ImageSpace";
		default:
			return "Unknown";
		}
	}

	std::string_view ToString(DebugOutputMode a_value) noexcept
	{
		switch (a_value) {
		case DebugOutputMode::kOwnedCopyBack:
			return "OwnedCopyBack";
		case DebugOutputMode::kDirectMain:
			return "DirectMain";
		default:
			return "Unknown";
		}
	}

	std::string_view ToString(DebugRestoreMode a_value) noexcept
	{
		switch (a_value) {
		case DebugRestoreMode::kNone:
			return "None";
		case DebugRestoreMode::kConservative:
			return "Conservative";
		case DebugRestoreMode::kNativeImmediately:
			return "NativeImmediately";
		case DebugRestoreMode::kNativeAfterImageSpace:
			return "NativeAfterImageSpace";
		default:
			return "Unknown";
		}
	}
}
