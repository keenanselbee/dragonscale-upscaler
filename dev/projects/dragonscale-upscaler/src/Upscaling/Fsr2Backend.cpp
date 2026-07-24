#include "Upscaling/Fsr2Backend.h"

#include "Config.h"
#include "Diagnostics/Diagnostics.h"

#ifdef DRAGONSCALE_WITH_FIDELITYFX_SDK
#	include <FidelityFX/host/backends/dx11/ffx_dx11.h>
#	include <backends/shared/ffx_shader_blobs.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cwchar>
#include <cstring>
#include <string>

namespace
{
#if defined(DRAGONSCALE_WITH_FIDELITYFX_SDK) && defined(DRAGONSCALE_WITH_FIDELITYFX_STATIC_LIBS)
	extern "C" HRESULT ffxGetLastCreateComputeShaderHRESULTDX11();

	constexpr float kPi = 3.14159265359f;
	constexpr float kDefaultFrameTimeMilliseconds = 16.6667f;
	constexpr float kDefaultCameraNear = 0.1f;
	constexpr float kDefaultCameraFar = 100000.0f;
	constexpr float kDefaultVerticalFovRadians = 1.04719758f;

	[[nodiscard]] float ReadRelocatedFloat(REL::RelocationID a_id, std::ptrdiff_t a_offset, float a_fallback) noexcept
	{
		const auto baseAddress = a_id.address();
		if (baseAddress == 0) {
			return a_fallback;
		}

		const auto address = baseAddress + static_cast<std::uintptr_t>(a_offset);
		const auto value = *reinterpret_cast<float*>(address);
		return std::isfinite(value) && value > 0.0f ? value : a_fallback;
	}

	[[nodiscard]] float GetFrameTimeMilliseconds() noexcept
	{
		const auto deltaSeconds = ReadRelocatedFloat(REL::RelocationID(523660, 410199), 0, kDefaultFrameTimeMilliseconds / 1000.0f);
		return deltaSeconds * 1000.0f;
	}

	[[nodiscard]] float GetCameraNear() noexcept
	{
		return ReadRelocatedFloat(REL::RelocationID(517032, 403540), 0x40, kDefaultCameraNear);
	}

	[[nodiscard]] float GetCameraFar() noexcept
	{
		return ReadRelocatedFloat(REL::RelocationID(517032, 403540), 0x44, kDefaultCameraFar);
	}

	[[nodiscard]] float GetVerticalFovRadians(std::uint32_t a_displayWidth, std::uint32_t a_displayHeight, float a_configuredHorizontalFovDegrees) noexcept
	{
		if (a_displayWidth == 0 || a_displayHeight == 0) {
			return kDefaultVerticalFovRadians;
		}

		const auto aspectRatio = static_cast<float>(a_displayWidth) / static_cast<float>(a_displayHeight);
		const auto horizontalFovDegrees = a_configuredHorizontalFovDegrees == 0.0f ?
			ReadRelocatedFloat(REL::RelocationID(513786, 388785), 0, 75.0f) :
			a_configuredHorizontalFovDegrees;
		if (horizontalFovDegrees <= 1.0f || horizontalFovDegrees >= 179.0f) {
			return kDefaultVerticalFovRadians;
		}

		const auto horizontalFovRadians = horizontalFovDegrees * (kPi / 180.0f);
		const auto verticalFovRadians = 2.0f * std::atan(std::tan(horizontalFovRadians * 0.5f) / aspectRatio);

		return std::isfinite(verticalFovRadians) && verticalFovRadians > 0.0f ? verticalFovRadians : kDefaultVerticalFovRadians;
	}

	[[nodiscard]] bool IsHdrFormat(DXGI_FORMAT a_format) noexcept
	{
		return a_format == DXGI_FORMAT_R16G16B16A16_FLOAT ||
		       a_format == DXGI_FORMAT_R11G11B10_FLOAT ||
		       a_format == DXGI_FORMAT_R10G10B10A2_UNORM;
	}

	[[nodiscard]] const char* DxgiFormatName(DXGI_FORMAT a_format) noexcept
	{
		switch (a_format) {
		case DXGI_FORMAT_R16G16B16A16_FLOAT:
			return "R16G16B16A16_FLOAT";
		case DXGI_FORMAT_R11G11B10_FLOAT:
			return "R11G11B10_FLOAT";
		case DXGI_FORMAT_R10G10B10A2_UNORM:
			return "R10G10B10A2_UNORM";
		case DXGI_FORMAT_R8G8B8A8_UNORM:
			return "R8G8B8A8_UNORM";
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
			return "R8G8B8A8_UNORM_SRGB";
		case DXGI_FORMAT_B8G8R8A8_UNORM:
			return "B8G8R8A8_UNORM";
		case DXGI_FORMAT_R32G32_FLOAT:
			return "R32G32_FLOAT";
		case DXGI_FORMAT_R32_FLOAT:
			return "R32_FLOAT";
		case DXGI_FORMAT_R24G8_TYPELESS:
			return "R24G8_TYPELESS";
		default:
			return "Unknown";
		}
	}

	[[nodiscard]] std::uint32_t BuildFsr2Flags(const DragonScale::UpscalingSettings& a_settings, DXGI_FORMAT a_outputFormat, bool& a_detectedHdr, bool& a_enableHdr) noexcept
	{
		a_detectedHdr = IsHdrFormat(a_outputFormat);
		a_enableHdr =
			a_settings.fsrHdr == DragonScale::FsrTriState::kEnabled ||
			(a_settings.fsrHdr == DragonScale::FsrTriState::kAuto && a_detectedHdr);

		std::uint32_t flags = 0;
		if (a_settings.fsrAutoExposure) {
			flags |= FFX_FSR2_ENABLE_AUTO_EXPOSURE;
		}
		if (a_settings.fsrDepthInverted) {
			flags |= FFX_FSR2_ENABLE_DEPTH_INVERTED;
		}
		if (a_settings.fsrDebug) {
			flags |= FFX_FSR2_ENABLE_DEBUG_CHECKING;
		}
		if (a_enableHdr) {
			flags |= FFX_FSR2_ENABLE_HIGH_DYNAMIC_RANGE;
		}
		return flags;
	}

	[[nodiscard]] std::string WideToAscii(const wchar_t* a_text)
	{
		if (!a_text) {
			return {};
		}

		std::string result;
		while (*a_text) {
			const auto ch = *a_text++;
			result.push_back(ch >= 0 && ch <= 0x7F ? static_cast<char>(ch) : '?');
		}
		return result;
	}

	[[nodiscard]] std::uint32_t ReadShaderBlobMagic(const FfxShaderBlob& a_blob) noexcept
	{
		if (!a_blob.data || a_blob.size < sizeof(std::uint32_t)) {
			return 0;
		}

		std::uint32_t magic = 0;
		std::memcpy(&magic, a_blob.data, sizeof(magic));
		return magic;
	}

	[[nodiscard]] const char* ShaderModelName(FfxShaderModel a_shaderModel) noexcept
	{
		switch (a_shaderModel) {
		case FFX_SHADER_MODEL_5_1:
			return "5.1";
		case FFX_SHADER_MODEL_6_0:
			return "6.0";
		case FFX_SHADER_MODEL_6_1:
			return "6.1";
		case FFX_SHADER_MODEL_6_2:
			return "6.2";
		case FFX_SHADER_MODEL_6_3:
			return "6.3";
		case FFX_SHADER_MODEL_6_4:
			return "6.4";
		case FFX_SHADER_MODEL_6_5:
			return "6.5";
		case FFX_SHADER_MODEL_6_6:
			return "6.6";
		case FFX_SHADER_MODEL_6_7:
			return "6.7";
		default:
			return "Unknown";
		}
	}

	[[nodiscard]] FfxShaderModel ToFfxShaderModel(DragonScale::FsrShaderModelOverride a_shaderModel) noexcept
	{
		switch (a_shaderModel) {
		case DragonScale::FsrShaderModelOverride::k51:
			return FFX_SHADER_MODEL_5_1;
		case DragonScale::FsrShaderModelOverride::k60:
			return FFX_SHADER_MODEL_6_0;
		case DragonScale::FsrShaderModelOverride::k61:
			return FFX_SHADER_MODEL_6_1;
		case DragonScale::FsrShaderModelOverride::k62:
			return FFX_SHADER_MODEL_6_2;
		case DragonScale::FsrShaderModelOverride::k63:
			return FFX_SHADER_MODEL_6_3;
		case DragonScale::FsrShaderModelOverride::k64:
			return FFX_SHADER_MODEL_6_4;
		case DragonScale::FsrShaderModelOverride::k65:
			return FFX_SHADER_MODEL_6_5;
		case DragonScale::FsrShaderModelOverride::k66:
			return FFX_SHADER_MODEL_6_6;
		case DragonScale::FsrShaderModelOverride::k67:
			return FFX_SHADER_MODEL_6_7;
		case DragonScale::FsrShaderModelOverride::kAuto:
		default:
			return FFX_SHADER_MODEL_5_1;
		}
	}

	void LogFidelityFxMessage(std::uint32_t a_type, const wchar_t* a_message)
	{
		const auto message = WideToAscii(a_message);
		if (a_type == FFX_MESSAGE_TYPE_ERROR) {
			logger::error("FidelityFX message: {}", message);
		} else {
			logger::warn("FidelityFX message: {}", message);
		}
	}

	void FsrContextMessageCallback(FfxMsgType a_type, const wchar_t* a_message)
	{
		LogFidelityFxMessage(static_cast<std::uint32_t>(a_type), a_message);
	}

	void LogTextureDescription(const char* a_name, ID3D11Texture2D* a_texture)
	{
		if (!a_texture) {
			logger::info("{} texture: null", a_name);
			return;
		}

		D3D11_TEXTURE2D_DESC description{};
		a_texture->GetDesc(&description);
		logger::info(
			"{} texture: {}x{}, format={}, bind=0x{:X}, misc=0x{:X}, mips={}, array={}, samples={}x{}",
			a_name,
			description.Width,
			description.Height,
			static_cast<std::uint32_t>(description.Format),
			description.BindFlags,
			description.MiscFlags,
			description.MipLevels,
			description.ArraySize,
			description.SampleDesc.Count,
			description.SampleDesc.Quality);
	}

	[[nodiscard]] FfxResource GetFfxResource(ID3D11Resource* a_resource, [[maybe_unused]] wchar_t const* a_name, FfxResourceStates a_state = FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ)
	{
		if (!a_resource) {
			return {};
		}

		FfxResource resource{};
		resource.resource = reinterpret_cast<void*>(a_resource);
		resource.state = a_state;
		resource.description = GetFfxResourceDescriptionDX11(a_resource);

#ifdef _DEBUG
		if (a_name) {
			wcscpy_s(resource.name, a_name);
		}
#endif

		return resource;
	}

	[[nodiscard]] FfxDevice GetDx11Device(ID3D11Device* a_device)
	{
		return ffxGetDeviceDX11_Fsr31(a_device);
	}
#endif
}

namespace DragonScale::Upscaling
{
	Fsr2Backend& Fsr2Backend::GetSingleton()
	{
		static Fsr2Backend singleton;
		return singleton;
	}

	void Fsr2Backend::Load()
	{
		if (checkedAvailability_) {
			return;
		}

		checkedAvailability_ = true;

		available_ = false;

#ifdef DRAGONSCALE_WITH_FIDELITYFX_SDK
		sdkAvailable_ = true;
		staticLibsAvailable_ = false;
		available_ = false;

#	ifdef DRAGONSCALE_WITH_FIDELITYFX_STATIC_LIBS
		staticLibsAvailable_ = true;
		available_ = true;
		Diagnostics::DiagnosticsManager::GetSingleton().SetStatus(Diagnostics::Status::kWaiting);
		logger::info(
			"FidelityFX SDK DX11 static libraries are linked; FSR2 {}.{}.{} context will be created on first active frame",
			FFX_FSR2_VERSION_MAJOR,
			FFX_FSR2_VERSION_MINOR,
			FFX_FSR2_VERSION_PATCH);
#	else
		logger::info(
			"FidelityFX SDK DX11 headers are available; build static libraries before creating the FSR2 {}.{}.{} context",
			FFX_FSR2_VERSION_MAJOR,
			FFX_FSR2_VERSION_MINOR,
			FFX_FSR2_VERSION_PATCH);
#	endif
#else
		sdkAvailable_ = false;
		staticLibsAvailable_ = false;
		Diagnostics::DiagnosticsManager::GetSingleton().SetStatus(Diagnostics::Status::kDisabled);
		logger::warn("FidelityFX SDK DX11 headers are not available; FSR2 dispatch remains disabled");
#endif
	}

	void Fsr2Backend::OnDataLoaded()
	{
		Load();
	}

	bool Fsr2Backend::IsAvailable() const noexcept
	{
		return available_ && !contextCreateDisabled_;
	}

	bool Fsr2Backend::Dispatch(const UpscaleInputs& a_inputs)
	{
		if (!available_) {
			if (!missingSdkLogged_) {
				logger::warn("FSR2 dispatch skipped because the FidelityFX backend binding is incomplete");
				missingSdkLogged_ = true;
			}
			Diagnostics::DiagnosticsManager::GetSingleton().SetStatus(Diagnostics::Status::kDisabled);
			return false;
		}

		if (!EnsureContext(a_inputs)) {
			return false;
		}

#if defined(DRAGONSCALE_WITH_FIDELITYFX_SDK) && defined(DRAGONSCALE_WITH_FIDELITYFX_STATIC_LIBS)
		auto* device = a_inputs.device;
		auto* context = a_inputs.context;

		if (!device || !context || !a_inputs.color.texture || !a_inputs.color.srv || !a_inputs.depth.texture || !a_inputs.depth.srv ||
			!a_inputs.motionVectors.texture || !a_inputs.motionVectors.srv || !a_inputs.output.texture || !a_inputs.output.uav) {
			if (!missingResourcesLogged_) {
				logger::warn(
					"FSR2 dispatch skipped because one or more prepared D3D11 resources are missing: device={}, context={}, color={}, colorSRV={}, depth={}, depthSRV={}, motion={}, motionSRV={}, output={}, outputUAV={}",
					static_cast<const void*>(device),
					static_cast<const void*>(context),
					static_cast<const void*>(a_inputs.color.texture),
					static_cast<const void*>(a_inputs.color.srv),
					static_cast<const void*>(a_inputs.depth.texture),
					static_cast<const void*>(a_inputs.depth.srv),
					static_cast<const void*>(a_inputs.motionVectors.texture),
					static_cast<const void*>(a_inputs.motionVectors.srv),
					static_cast<const void*>(a_inputs.output.texture),
					static_cast<const void*>(a_inputs.output.uav));
				missingResourcesLogged_ = true;
			}
			return false;
		}

		missingResourcesLogged_ = false;
		InitializeGpuTiming(device);

		const auto sharpness = std::clamp(a_inputs.sharpness, 0.0f, 1.0f);

		FfxFsr2DispatchDescription dispatchParameters{};
		dispatchParameters.commandList = ffxGetCommandListDX11(context);
		dispatchParameters.color = GetFfxResource(reinterpret_cast<ID3D11Resource*>(a_inputs.color.texture), L"DragonScale_FSR2_InputColor");
		dispatchParameters.depth = GetFfxResource(reinterpret_cast<ID3D11Resource*>(a_inputs.depth.texture), L"DragonScale_FSR2_InputDepth");
		dispatchParameters.motionVectors = GetFfxResource(reinterpret_cast<ID3D11Resource*>(a_inputs.motionVectors.texture), L"DragonScale_FSR2_InputMotionVectors");
		dispatchParameters.exposure = {};
		dispatchParameters.reactive = GetFfxResource(reinterpret_cast<ID3D11Resource*>(a_inputs.reactiveMask.texture), L"DragonScale_FSR2_InputReactiveMap");
		dispatchParameters.transparencyAndComposition = GetFfxResource(reinterpret_cast<ID3D11Resource*>(a_inputs.transparencyCompositionMask.texture), L"DragonScale_FSR2_TransparencyCompositionMap");
		dispatchParameters.output = GetFfxResource(
			reinterpret_cast<ID3D11Resource*>(a_inputs.output.texture),
			L"DragonScale_FSR2_OutputColor",
			FFX_RESOURCE_STATE_UNORDERED_ACCESS);
		dispatchParameters.jitterOffset.x = -a_inputs.jitter.x;
		dispatchParameters.jitterOffset.y = -a_inputs.jitter.y;
		dispatchParameters.motionVectorScale.x = static_cast<float>(a_inputs.renderWidth);
		dispatchParameters.motionVectorScale.y = static_cast<float>(a_inputs.renderHeight);
		dispatchParameters.renderSize.width = a_inputs.renderWidth;
		dispatchParameters.renderSize.height = a_inputs.renderHeight;
		dispatchParameters.enableSharpening = sharpness > 0.0f;
		dispatchParameters.sharpness = sharpness;
		dispatchParameters.frameTimeDelta = GetFrameTimeMilliseconds();
		dispatchParameters.preExposure = 1.0f;
		const auto cameraFov = GetVerticalFovRadians(
			a_inputs.displayWidth,
			a_inputs.displayHeight,
			DragonScale::Config::GetSingleton().GetSettings().upscaling.fsrFov);
		const bool cameraFovChanged = lastCameraFovRadians_ != 0.0f && std::abs(cameraFov - lastCameraFovRadians_) > 0.001f;
		dispatchParameters.reset = resetDispatch_ || a_inputs.reset || cameraFovChanged;
		dispatchParameters.cameraNear = GetCameraNear();
		dispatchParameters.cameraFar = GetCameraFar();
		dispatchParameters.cameraFovAngleVertical = cameraFov;
		dispatchParameters.viewSpaceToMetersFactor = 0.01428222656f;
		dispatchParameters.enableAutoReactive = false;
		if (cameraFovChanged) {
			logger::info("Resetting FSR2 history because camera FOV changed: {:.4f} -> {:.4f}", lastCameraFovRadians_, cameraFov);
		}

		BeginGpuTiming(context);
		FfxErrorCode result = FFX_ERROR_BACKEND_API_ERROR;
		try {
			Diagnostics::ScopedCpuTimer timer(Diagnostics::CpuTimerKind::kFsrDispatch);
			result = ffxFsr2ContextDispatch(&fsrContext_, &dispatchParameters);
		} catch (...) {
			EndGpuTiming(context);
			available_ = false;
			Diagnostics::DiagnosticsManager::GetSingleton().SetFsrFailure(
				static_cast<std::int32_t>(FFX_ERROR_BACKEND_API_ERROR),
				"FSR2 dispatch threw an exception",
				a_inputs.displayWidth,
				a_inputs.displayHeight,
				a_inputs.renderWidth,
				a_inputs.renderHeight,
				0,
				0);
			logger::critical("FidelityFX FSR2 dispatch threw an exception; disabling FSR for this session");
			DestroyContext();
			return false;
		}
		EndGpuTiming(context);

		if (result != FFX_OK) {
			if (!dispatchFailureLogged_) {
				logger::critical("FidelityFX FSR2 dispatch failed: {}; disabling FSR for this session", static_cast<std::int32_t>(result));
				dispatchFailureLogged_ = true;
			}
			available_ = false;
			Diagnostics::DiagnosticsManager::GetSingleton().SetFsrFailure(
				static_cast<std::int32_t>(result),
				"FSR2 dispatch failed",
				a_inputs.displayWidth,
				a_inputs.displayHeight,
				a_inputs.renderWidth,
				a_inputs.renderHeight,
				0,
				0);
			DestroyContext();
			return false;
		}

		resetDispatch_ = false;
		lastCameraFovRadians_ = cameraFov;
		dispatchFailureLogged_ = false;
		Diagnostics::DiagnosticsManager::GetSingleton().SetStatus(Diagnostics::Status::kRunning);

		logger::debug(
			"Dispatched FSR2 upscale: display={}x{}, render={}x{}, jitter=({}, {}), sharpness={:.2f}, fovVertical={:.4f}, reset={}",
			a_inputs.displayWidth,
			a_inputs.displayHeight,
			a_inputs.renderWidth,
			a_inputs.renderHeight,
			a_inputs.jitter.x,
			a_inputs.jitter.y,
			sharpness,
			cameraFov,
			dispatchParameters.reset);

		return true;
#else
		return false;
#endif
	}

	void Fsr2Backend::ResetHistory() noexcept
	{
		resetDispatch_ = true;
	}

	void Fsr2Backend::Destroy() noexcept
	{
		DestroyContext();
	}

	bool Fsr2Backend::EnsureContext(const UpscaleInputs& a_inputs)
	{
#if defined(DRAGONSCALE_WITH_FIDELITYFX_SDK) && defined(DRAGONSCALE_WITH_FIDELITYFX_STATIC_LIBS)
		if (contextCreateDisabled_) {
			return false;
		}

		if (a_inputs.displayWidth == 0 || a_inputs.displayHeight == 0 || a_inputs.renderWidth == 0 || a_inputs.renderHeight == 0) {
			logger::warn("FSR2 context creation skipped because frame dimensions are incomplete");
			return false;
		}

		const auto& upscalingSettings = DragonScale::Config::GetSingleton().GetSettings().upscaling;
		const auto renderWindowFormat = a_inputs.output.format;
		bool detectedHdr = false;
		bool enableHdr = false;
		const auto effectiveFlags = BuildFsr2Flags(upscalingSettings, renderWindowFormat, detectedHdr, enableHdr);

		if (contextCreated_ &&
			contextDisplayWidth_ == a_inputs.displayWidth &&
			contextDisplayHeight_ == a_inputs.displayHeight &&
			contextRenderWidth_ == a_inputs.renderWidth &&
			contextRenderHeight_ == a_inputs.renderHeight &&
			contextOutputFormat_ == renderWindowFormat &&
			contextFlags_ == effectiveFlags) {
			return true;
		}

		if (upscalingSettings.fsrHdr == DragonScale::FsrTriState::kDisabled && detectedHdr) {
			logger::warn(
				"FsrHdr=0 is forcing an HDR scene color target through an LDR FSR2 context; set FsrHdr=Auto unless testing. format={} ({})",
				static_cast<std::uint32_t>(renderWindowFormat),
				DxgiFormatName(renderWindowFormat));
		}

		Diagnostics::DiagnosticsManager::GetSingleton().SetStatus(Diagnostics::Status::kCreatingContext);

		if (contextCreated_ || fsrScratchBuffer_) {
			DestroyContext();
		}

		auto* device = a_inputs.device;
		auto* context = a_inputs.context;
		if (!device || !context) {
			logger::warn("FSR2 context creation skipped because the D3D11 device or context is not available");
			return false;
		}

		constexpr std::uint32_t contextCount = 1;
		const auto scratchBufferSize = ffxGetScratchMemorySizeDX11(contextCount);
		fsrScratchBuffer_ = std::calloc(1, scratchBufferSize);
		if (!fsrScratchBuffer_) {
			logger::critical("Failed to allocate FidelityFX DX11 scratch memory");
			return false;
		}

		const auto ffxDevice = GetDx11Device(device);
		const auto interfaceResult = ffxGetInterfaceDX11(&fsrInterface_, ffxDevice, fsrScratchBuffer_, scratchBufferSize, contextCount);
		if (interfaceResult != FFX_OK) {
			contextCreateDisabled_ = true;
			available_ = false;
			Diagnostics::DiagnosticsManager::GetSingleton().SetFsrFailure(
				static_cast<std::int32_t>(interfaceResult),
				"FSR2 DX11 backend interface initialization failed",
				a_inputs.displayWidth,
				a_inputs.displayHeight,
				a_inputs.renderWidth,
				a_inputs.renderHeight,
				0,
				0);
			logger::critical("Failed to initialize FidelityFX DX11 backend interface");
			DestroyContext();
			return false;
		}

		originalGetDeviceCapabilities_ = fsrInterface_.fpGetDeviceCapabilities;
		originalCreatePipeline_ = fsrInterface_.fpCreatePipeline;
		fsrInterface_.fpGetDeviceCapabilities = GetDeviceCapabilitiesOverride;
		fsrInterface_.fpCreatePipeline = CreatePipelineOverride;

		FfxFsr2ContextDescription contextDescription{};
		contextDescription.maxRenderSize.width = a_inputs.renderWidth;
		contextDescription.maxRenderSize.height = a_inputs.renderHeight;
		contextDescription.displaySize.width = a_inputs.displayWidth;
		contextDescription.displaySize.height = a_inputs.displayHeight;
		contextDescription.flags = effectiveFlags;
		contextDescription.fpMessage = FsrContextMessageCallback;

		LogTextureDescription("FSR prepared color", a_inputs.color.texture);
		LogTextureDescription("FSR prepared motion vectors", a_inputs.motionVectors.texture);
		LogTextureDescription("FSR prepared typed depth", a_inputs.depth.texture);
		LogTextureDescription("FSR prepared output", a_inputs.output.texture);

		contextDescription.backendInterface = fsrInterface_;

		logger::info(
			"Creating FidelityFX FSR2 context: display={}x{}, render={}x{}, renderWindowFormat={} ({}), flags=0x{:X}, hdr={}, hdrMode={}, detectedHdr={}, autoExposure={}, depthInverted={}, debug={}, fovVertical={:.4f}",
			a_inputs.displayWidth,
			a_inputs.displayHeight,
			a_inputs.renderWidth,
			a_inputs.renderHeight,
			static_cast<std::uint32_t>(renderWindowFormat),
			DxgiFormatName(renderWindowFormat),
			contextDescription.flags,
			enableHdr,
			DragonScale::ToString(upscalingSettings.fsrHdr),
			detectedHdr,
			upscalingSettings.fsrAutoExposure,
			upscalingSettings.fsrDepthInverted,
			upscalingSettings.fsrDebug,
			GetVerticalFovRadians(a_inputs.displayWidth, a_inputs.displayHeight, upscalingSettings.fsrFov));

		FfxErrorCode createResult = FFX_ERROR_BACKEND_API_ERROR;
		try {
			createResult = ffxFsr2ContextCreate(&fsrContext_, &contextDescription);
		} catch (...) {
			contextCreateDisabled_ = true;
			available_ = false;
			Diagnostics::DiagnosticsManager::GetSingleton().SetFsrFailure(
				static_cast<std::int32_t>(FFX_ERROR_BACKEND_API_ERROR),
				"FSR2 context creation threw an exception",
				a_inputs.displayWidth,
				a_inputs.displayHeight,
				a_inputs.renderWidth,
				a_inputs.renderHeight,
				static_cast<std::int32_t>(renderWindowFormat),
				contextDescription.flags);
			if (!contextCreateFailureLogged_) {
				logger::critical(
					"FidelityFX FSR2 context creation threw an exception; disabling FSR for this session. display={}x{}, render={}x{}, format={}, flags={}",
					a_inputs.displayWidth,
					a_inputs.displayHeight,
					a_inputs.renderWidth,
					a_inputs.renderHeight,
					static_cast<std::int32_t>(renderWindowFormat),
					contextDescription.flags);
				contextCreateFailureLogged_ = true;
			}
			DestroyContext();
			return false;
		}

		if (createResult != FFX_OK) {
			contextCreateDisabled_ = true;
			available_ = false;
			Diagnostics::DiagnosticsManager::GetSingleton().SetFsrFailure(
				static_cast<std::int32_t>(createResult),
				"FSR2 context creation failed",
				a_inputs.displayWidth,
				a_inputs.displayHeight,
				a_inputs.renderWidth,
				a_inputs.renderHeight,
				static_cast<std::int32_t>(renderWindowFormat),
				contextDescription.flags);
			if (!contextCreateFailureLogged_) {
				logger::critical(
					"Failed to create FidelityFX FSR2 context: result={}, display={}x{}, render={}x{}, format={}, flags={}",
					static_cast<std::int32_t>(createResult),
					a_inputs.displayWidth,
					a_inputs.displayHeight,
					a_inputs.renderWidth,
					a_inputs.renderHeight,
					static_cast<std::int32_t>(renderWindowFormat),
					contextDescription.flags);
				contextCreateFailureLogged_ = true;
			}
			DestroyContext();
			return false;
		}

		contextCreated_ = true;
		contextDisplayWidth_ = a_inputs.displayWidth;
		contextDisplayHeight_ = a_inputs.displayHeight;
		contextRenderWidth_ = a_inputs.renderWidth;
		contextRenderHeight_ = a_inputs.renderHeight;
		contextOutputFormat_ = renderWindowFormat;
		contextFlags_ = contextDescription.flags;
		contextHdr_ = enableHdr;
		resetDispatch_ = true;
		Diagnostics::DiagnosticsManager::GetSingleton().SetStatus(Diagnostics::Status::kWaiting);

		logger::info(
			"Created FidelityFX FSR2 context: display={}x{}, render={}x{}, format={} ({}), flags=0x{:X}, hdr={}",
			contextDisplayWidth_,
			contextDisplayHeight_,
			contextRenderWidth_,
			contextRenderHeight_,
			static_cast<std::int32_t>(renderWindowFormat),
			DxgiFormatName(renderWindowFormat),
			contextDescription.flags,
			contextHdr_);

		return true;
#else
		return false;
#endif
	}

	void Fsr2Backend::DestroyContext() noexcept
	{
#if defined(DRAGONSCALE_WITH_FIDELITYFX_SDK) && defined(DRAGONSCALE_WITH_FIDELITYFX_STATIC_LIBS)
		if (contextCreated_) {
			try {
				const auto result = ffxFsr2ContextDestroy(&fsrContext_);
				if (result != FFX_OK) {
					logger::warn("Failed to destroy FidelityFX FSR2 context cleanly: {}", static_cast<std::int32_t>(result));
				}
			} catch (...) {
				logger::warn("FidelityFX FSR2 context destruction threw an exception");
			}
		}

		if (fsrScratchBuffer_) {
			std::free(fsrScratchBuffer_);
			fsrScratchBuffer_ = nullptr;
		}

		ShutdownGpuTiming();

		std::memset(&fsrContext_, 0, sizeof(fsrContext_));
		std::memset(&fsrInterface_, 0, sizeof(fsrInterface_));
		originalGetDeviceCapabilities_ = nullptr;
		originalCreatePipeline_ = nullptr;
		contextCreated_ = false;
		resetDispatch_ = true;
		contextDisplayWidth_ = 0;
		contextDisplayHeight_ = 0;
		contextRenderWidth_ = 0;
		contextRenderHeight_ = 0;
		contextOutputFormat_ = DXGI_FORMAT_UNKNOWN;
		contextFlags_ = 0;
		contextHdr_ = false;
		lastCameraFovRadians_ = 0.0f;
#endif
	}

#ifdef DRAGONSCALE_WITH_FIDELITYFX_SDK
	void Fsr2Backend::InitializeGpuTiming(ID3D11Device* a_device)
	{
#if defined(DRAGONSCALE_WITH_FIDELITYFX_STATIC_LIBS)
		auto& diagnostics = Diagnostics::DiagnosticsManager::GetSingleton();
		if (!diagnostics.GpuTimingEnabled()) {
			ShutdownGpuTiming();
			diagnostics.SetFsrGpuUnavailable();
			return;
		}

		if (!a_device || gpuTimingFailed_) {
			diagnostics.SetFsrGpuUnavailable();
			return;
		}

		if (gpuTimingInitialized_ && gpuTimingDevice_ == a_device) {
			return;
		}

		ShutdownGpuTiming();

		D3D11_QUERY_DESC disjointDescription{};
		disjointDescription.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;

		D3D11_QUERY_DESC timestampDescription{};
		timestampDescription.Query = D3D11_QUERY_TIMESTAMP;

		for (auto& query : gpuTimingQueries_) {
			if (FAILED(a_device->CreateQuery(&disjointDescription, &query.disjoint)) ||
				FAILED(a_device->CreateQuery(&timestampDescription, &query.start)) ||
				FAILED(a_device->CreateQuery(&timestampDescription, &query.end))) {
				ShutdownGpuTiming();
				gpuTimingFailed_ = true;
				diagnostics.SetFsrGpuUnavailable();
				if (!gpuTimingFailureLogged_) {
					logger::warn("Failed to create D3D11 timestamp queries; FSR GPU timing is disabled");
					gpuTimingFailureLogged_ = true;
				}
				return;
			}
		}

		gpuTimingDevice_ = a_device;
		gpuTimingInitialized_ = true;
		gpuTimingIndex_ = 0;
#else
		(void)a_device;
#endif
	}

	void Fsr2Backend::ShutdownGpuTiming() noexcept
	{
		for (auto& query : gpuTimingQueries_) {
			if (query.disjoint) {
				query.disjoint->Release();
				query.disjoint = nullptr;
			}
			if (query.start) {
				query.start->Release();
				query.start = nullptr;
			}
			if (query.end) {
				query.end->Release();
				query.end = nullptr;
			}
			query.issued = false;
		}

		gpuTimingDevice_ = nullptr;
		gpuTimingInitialized_ = false;
		gpuTimingIndex_ = 0;
	}

	void Fsr2Backend::ResolveGpuTiming(ID3D11DeviceContext* a_context, GpuTimingQuery& a_query)
	{
		if (!a_context || !a_query.issued) {
			return;
		}

		D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjointData{};
		const auto disjointResult = a_context->GetData(a_query.disjoint, &disjointData, sizeof(disjointData), 0);
		if (disjointResult != S_OK) {
			Diagnostics::DiagnosticsManager::GetSingleton().SetFsrGpuPending();
			a_query.issued = false;
			return;
		}

		std::uint64_t start = 0;
		std::uint64_t end = 0;
		const auto startResult = a_context->GetData(a_query.start, &start, sizeof(start), 0);
		const auto endResult = a_context->GetData(a_query.end, &end, sizeof(end), 0);
		if (startResult == S_OK && endResult == S_OK && !disjointData.Disjoint && disjointData.Frequency != 0 && end >= start) {
			const auto elapsedMs = static_cast<float>(static_cast<double>(end - start) * 1000.0 / static_cast<double>(disjointData.Frequency));
			Diagnostics::DiagnosticsManager::GetSingleton().SetFsrGpuTime(elapsedMs);
		} else {
			Diagnostics::DiagnosticsManager::GetSingleton().SetFsrGpuUnavailable();
		}

		a_query.issued = false;
	}

	void Fsr2Backend::BeginGpuTiming(ID3D11DeviceContext* a_context)
	{
		if (!gpuTimingInitialized_ || !a_context) {
			return;
		}

		auto& query = gpuTimingQueries_[gpuTimingIndex_];
		ResolveGpuTiming(a_context, query);
		a_context->Begin(query.disjoint);
		a_context->End(query.start);
	}

	void Fsr2Backend::EndGpuTiming(ID3D11DeviceContext* a_context)
	{
		if (!gpuTimingInitialized_ || !a_context) {
			return;
		}

		auto& query = gpuTimingQueries_[gpuTimingIndex_];
		a_context->End(query.end);
		a_context->End(query.disjoint);
		query.issued = true;
		gpuTimingIndex_ = (gpuTimingIndex_ + 1) % static_cast<std::uint32_t>(gpuTimingQueries_.size());
		Diagnostics::DiagnosticsManager::GetSingleton().SetFsrGpuPending();
	}

	FfxErrorCode Fsr2Backend::GetDeviceCapabilitiesOverride(FfxInterface* a_backendInterface, FfxDeviceCapabilities* a_outDeviceCapabilities)
	{
		return GetSingleton().GetDeviceCapabilities(a_backendInterface, a_outDeviceCapabilities);
	}

	FfxErrorCode Fsr2Backend::GetDeviceCapabilities(FfxInterface* a_backendInterface, FfxDeviceCapabilities* a_outDeviceCapabilities)
	{
		if (!originalGetDeviceCapabilities_ || !a_outDeviceCapabilities) {
			return FFX_ERROR_INVALID_POINTER;
		}

		*a_outDeviceCapabilities = {};
		const auto result = originalGetDeviceCapabilities_(a_backendInterface, a_outDeviceCapabilities);
		if (result != FFX_OK) {
			logger::critical("FidelityFX DX11 capability query failed: {}", static_cast<std::int32_t>(result));
			return result;
		}

		const auto original = *a_outDeviceCapabilities;
		const auto& settings = DragonScale::Config::GetSingleton().GetSettings().upscaling;

		if (settings.fsrForceShaderModel != DragonScale::FsrShaderModelOverride::kAuto) {
			a_outDeviceCapabilities->minimumSupportedShaderModel = ToFfxShaderModel(settings.fsrForceShaderModel);
		}
		if (!settings.fsrAllowFP16) {
			a_outDeviceCapabilities->fp16Supported = false;
		}
		if (!settings.fsrAllowWave64) {
			a_outDeviceCapabilities->waveLaneCountMin = 0;
			a_outDeviceCapabilities->waveLaneCountMax = 0;
		}

		logger::info(
			"FidelityFX DX11 capabilities: shaderModel={} -> {}, fp16={} -> {}, wave={}..{} -> {}..{}, raytracing={}",
			ShaderModelName(original.minimumSupportedShaderModel),
			ShaderModelName(a_outDeviceCapabilities->minimumSupportedShaderModel),
			original.fp16Supported,
			a_outDeviceCapabilities->fp16Supported,
			original.waveLaneCountMin,
			original.waveLaneCountMax,
			a_outDeviceCapabilities->waveLaneCountMin,
			a_outDeviceCapabilities->waveLaneCountMax,
			a_outDeviceCapabilities->raytracingSupported);

		return result;
	}

	FfxErrorCode Fsr2Backend::CreatePipelineOverride(
		FfxInterface* a_backendInterface,
		FfxEffect a_effect,
		FfxPass a_pass,
		std::uint32_t a_permutationOptions,
		const FfxPipelineDescription* a_pipelineDescription,
		FfxUInt32 a_effectContextId,
		FfxPipelineState* a_outPipeline)
	{
		return GetSingleton().CreatePipeline(
			a_backendInterface,
			a_effect,
			a_pass,
			a_permutationOptions,
			a_pipelineDescription,
			a_effectContextId,
			a_outPipeline);
	}

	FfxErrorCode Fsr2Backend::CreatePipeline(
		FfxInterface* a_backendInterface,
		FfxEffect a_effect,
		FfxPass a_pass,
		std::uint32_t a_permutationOptions,
		const FfxPipelineDescription* a_pipelineDescription,
		FfxUInt32 a_effectContextId,
		FfxPipelineState* a_outPipeline)
	{
		if (!originalCreatePipeline_) {
			return FFX_ERROR_INVALID_POINTER;
		}

		FfxShaderBlob shaderBlob{};
		const auto blobResult = ffxGetPermutationBlobByIndex(
			a_effect,
			a_pass,
			FFX_BIND_COMPUTE_SHADER_STAGE,
			a_permutationOptions,
			&shaderBlob);
		const auto blobMagic = ReadShaderBlobMagic(shaderBlob);

		const auto result = originalCreatePipeline_(
			a_backendInterface,
			a_effect,
			a_pass,
			a_permutationOptions,
			a_pipelineDescription,
			a_effectContextId,
			a_outPipeline);

		if (result != FFX_OK) {
			const auto pipelineName = a_pipelineDescription ? WideToAscii(a_pipelineDescription->name) : std::string("<null>");
			const auto hresult = ffxGetLastCreateComputeShaderHRESULTDX11();
			logger::critical(
				"FidelityFX DX11 pipeline creation failed: result={}, hresult=0x{:08X}, effect={}, pass={}, permutation=0x{:X}, pipeline={}, blobResult={}, blobSize={}, blobMagic=0x{:08X}, cbv={}, srvTextures={}, uavTextures={}",
				static_cast<std::int32_t>(result),
				static_cast<std::uint32_t>(hresult),
				static_cast<std::uint32_t>(a_effect),
				static_cast<std::uint32_t>(a_pass),
				a_permutationOptions,
				pipelineName,
				static_cast<std::int32_t>(blobResult),
				shaderBlob.size,
				blobMagic,
				shaderBlob.cbvCount,
				shaderBlob.srvTextureCount,
				shaderBlob.uavTextureCount);
		}

		return result;
	}
#endif
}
