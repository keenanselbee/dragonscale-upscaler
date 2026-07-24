#include "Upscaling/AuxiliaryUpscaler.h"

#include "Config.h"
#include "Upscaling/D3D11StateGuard.h"

#include <d3dcompiler.h>

#include <algorithm>
#include <cstring>
#include <format>

namespace
{
	constexpr const char* kFullscreenVertexShader = R"(
struct VSOutput
{
	float4 position : SV_Position;
	float2 uv : TEXCOORD0;
};

VSOutput main(uint vertexID : SV_VertexID)
{
	float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
	VSOutput output;
	output.position = float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
	output.uv = uv;
	return output;
}
)";

	constexpr const char* kDepthRefractionPixelShader = R"(
struct PSInput
{
	float4 position : SV_Position;
	float2 uv : TEXCOORD0;
};

struct PSOutput
{
	float4 refractionNormals : SV_Target0;
	float saoCameraZ : SV_Target1;
	float depth : SV_Depth;
};

cbuffer AuxConstants : register(b0)
{
	float2 DisplaySize;
	float2 RenderSize;
	float2 Jitter;
	float2 TargetSize;
};

SamplerState LinearClamp : register(s0);
Texture2D<float4> RefractionNormals : register(t0);
Texture2D<float> DepthTex : register(t1);

float2 GetSourceUv(float2 outputUv)
{
	float2 sourceScale = RenderSize / max(DisplaySize, float2(1.0f, 1.0f));
	float2 uv = outputUv * sourceScale - (Jitter / max(DisplaySize, float2(1.0f, 1.0f)));
	float2 maxUv = max((RenderSize - 1.0f) / max(DisplaySize, float2(1.0f, 1.0f)), float2(0.0f, 0.0f));
	return clamp(uv, float2(0.0f, 0.0f), maxUv);
}

PSOutput main(PSInput input)
{
	float2 uv = GetSourceUv(input.uv);
	float depth = DepthTex.SampleLevel(LinearClamp, uv, 0);

	PSOutput output;
	output.refractionNormals = RefractionNormals.SampleLevel(LinearClamp, uv, 0);
	output.saoCameraZ = depth;
	output.depth = depth;
	return output;
}
)";

	constexpr const char* kUnderwaterMaskPixelShader = R"(
struct PSInput
{
	float4 position : SV_Position;
	float2 uv : TEXCOORD0;
};

struct PSOutput
{
	float underwaterMask : SV_Target0;
};

cbuffer AuxConstants : register(b0)
{
	float2 DisplaySize;
	float2 RenderSize;
	float2 Jitter;
	float2 TargetSize;
};

SamplerState LinearClamp : register(s0);
Texture2D<float> UnderwaterMask : register(t0);

float2 GetSourceUv(float2 outputUv)
{
	float2 sourceScale = RenderSize / max(DisplaySize, float2(1.0f, 1.0f));
	float2 uv = outputUv * sourceScale - (Jitter / max(DisplaySize, float2(1.0f, 1.0f)));
	float2 maxUv = max((RenderSize - 1.0f) / max(DisplaySize, float2(1.0f, 1.0f)), float2(0.0f, 0.0f));
	return clamp(uv, float2(0.0f, 0.0f), maxUv);
}

PSOutput main(PSInput input)
{
	PSOutput output;
	output.underwaterMask = UnderwaterMask.SampleLevel(LinearClamp, GetSourceUv(input.uv), 0);
	return output;
}
)";

	template <class T>
	void SafeRelease(T*& a_value) noexcept
	{
		if (a_value) {
			a_value->Release();
			a_value = nullptr;
		}
	}

	[[nodiscard]] bool Covers(const DragonScale::Upscaling::TextureView& a_view, std::uint32_t a_width, std::uint32_t a_height) noexcept
	{
		return a_view.HasTexture() && a_view.width >= a_width && a_view.height >= a_height;
	}

	[[nodiscard]] bool CopyCompatible(const DragonScale::Upscaling::TextureView& a_source, const DragonScale::Upscaling::TextureView& a_copy) noexcept
	{
		return a_source.HasTexture() &&
		       a_copy.HasTexture() &&
		       a_source.width == a_copy.width &&
		       a_source.height == a_copy.height;
	}

	[[nodiscard]] DragonScale::Upscaling::TextureView OwnedView(
		ID3D11Texture2D* a_texture,
		ID3D11ShaderResourceView* a_srv,
		DXGI_FORMAT a_format,
		std::uint32_t a_width,
		std::uint32_t a_height,
		std::uint32_t a_bindFlags) noexcept
	{
		DragonScale::Upscaling::TextureView view;
		view.texture = a_texture;
		view.srv = a_srv;
		view.format = a_format;
		view.width = a_width;
		view.height = a_height;
		view.bindFlags = a_bindFlags;
		view.source = DragonScale::Upscaling::ResourceSource::kOwned;
		return view;
	}

	[[nodiscard]] bool CompileShader(
		ID3D11Device* a_device,
		const char* a_source,
		const char* a_name,
		const char* a_target,
		ID3DBlob** a_blob)
	{
		ID3DBlob* errorBlob = nullptr;
		const auto result = D3DCompile(
			a_source,
			std::strlen(a_source),
			a_name,
			nullptr,
			nullptr,
			"main",
			a_target,
			0,
			0,
			a_blob,
			&errorBlob);
		if (FAILED(result) || !*a_blob) {
			if (errorBlob) {
				logger::error("Failed to compile {}: {}", a_name, static_cast<const char*>(errorBlob->GetBufferPointer()));
			} else {
				logger::error("Failed to compile {}: 0x{:X}", a_name, static_cast<std::uint32_t>(result));
			}
			SafeRelease(errorBlob);
			return false;
		}

		SafeRelease(errorBlob);
		(void)a_device;
		return true;
	}
}

namespace DragonScale::Upscaling
{
	AuxiliaryUpscaler& AuxiliaryUpscaler::GetSingleton()
	{
		static AuxiliaryUpscaler singleton;
		return singleton;
	}

	AuxiliaryValidationResult AuxiliaryUpscaler::Validate(
		const ResourceSnapshot& a_snapshot,
		std::uint32_t a_displayWidth,
		std::uint32_t a_displayHeight) const
	{
		AuxiliaryValidationResult result;
		const auto& compatibility = DragonScale::Config::GetSingleton().GetSettings().compatibility;
		if (!compatibility.repairAuxiliaryBuffers) {
			result.reason = "auxiliary repair disabled";
			return result;
		}

		const bool repairMainAuxiliary = compatibility.repairDepth || compatibility.repairSAOCameraZ || compatibility.repairRefractionNormals;
		if (!repairMainAuxiliary && !compatibility.repairUnderwaterMask) {
			result.reason = "all auxiliary repair passes disabled";
			return result;
		}

		result.enabled = true;
		const auto halfDisplayWidth = (std::max<std::uint32_t>)(1, (a_displayWidth + 1) / 2);
		const auto halfDisplayHeight = (std::max<std::uint32_t>)(1, (a_displayHeight + 1) / 2);

		if (!a_snapshot.context || !a_snapshot.device) {
			result.reason = "D3D11 device/context unavailable";
			return result;
		}
		const bool needsDepthSource = repairMainAuxiliary || compatibility.repairUnderwaterMask;
		if (needsDepthSource && (!Covers(a_snapshot.depth, a_displayWidth, a_displayHeight) || !a_snapshot.depth.dsv)) {
			result.reason = std::format("main depth target unavailable or too small: {}x{}", a_snapshot.depth.width, a_snapshot.depth.height);
			return result;
		}
		if (needsDepthSource && (!Covers(a_snapshot.depthCopy, a_displayWidth, a_displayHeight) || !a_snapshot.depthCopy.srv)) {
			result.reason = std::format("main depth copy unavailable or too small: {}x{}", a_snapshot.depthCopy.width, a_snapshot.depthCopy.height);
			return result;
		}
		if (repairMainAuxiliary && (!Covers(a_snapshot.refractionNormals, a_displayWidth, a_displayHeight) || !a_snapshot.refractionNormals.rtv)) {
			result.reason = std::format("refraction normals target unavailable or too small: {}x{}", a_snapshot.refractionNormals.width, a_snapshot.refractionNormals.height);
			return result;
		}
		if (compatibility.repairSAOCameraZ && (!Covers(a_snapshot.saoCameraZ, a_displayWidth, a_displayHeight) || !a_snapshot.saoCameraZ.rtv)) {
			result.reason = std::format("SAO camera-Z target unavailable or too small: {}x{}", a_snapshot.saoCameraZ.width, a_snapshot.saoCameraZ.height);
			return result;
		}

		result.requiredReady = true;
		result.underwaterReady = compatibility.repairUnderwaterMask &&
			Covers(a_snapshot.underwaterMask, halfDisplayWidth, halfDisplayHeight) &&
			a_snapshot.underwaterMask.rtv != nullptr &&
			((a_snapshot.underwaterMaskCopy.HasSRV() && CopyCompatible(a_snapshot.underwaterMask, a_snapshot.underwaterMaskCopy)) ||
			 a_snapshot.underwaterMask.format != DXGI_FORMAT_UNKNOWN);
		return result;
	}

	bool AuxiliaryUpscaler::Repair(
		const ResourceSnapshot& a_snapshot,
		std::uint32_t a_displayWidth,
		std::uint32_t a_displayHeight,
		std::uint32_t a_renderWidth,
		std::uint32_t a_renderHeight,
		const JitterOffset& a_jitter)
	{
		const auto validation = Validate(a_snapshot, a_displayWidth, a_displayHeight);
		if (!validation.enabled) {
			logger::info("Auxiliary repair skipped: {}", validation.reason);
			return true;
		}
		if (!validation.requiredReady) {
			logger::warn("Auxiliary repair skipped: {}", validation.reason);
			return false;
		}
		if (!EnsurePipeline(a_snapshot.device)) {
			return false;
		}

		D3D11StateGuard stateGuard(a_snapshot.context);

		ID3D11RenderTargetView* nullRTVs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
		ID3D11ShaderResourceView* nullSRVs[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT]{};
		ID3D11UnorderedAccessView* nullUAVs[D3D11_PS_CS_UAV_REGISTER_COUNT]{};
		a_snapshot.context->OMSetRenderTargets(static_cast<UINT>(std::size(nullRTVs)), nullRTVs, nullptr);
		a_snapshot.context->PSSetShaderResources(0, static_cast<UINT>(std::size(nullSRVs)), nullSRVs);
		a_snapshot.context->CSSetShaderResources(0, static_cast<UINT>(std::size(nullSRVs)), nullSRVs);
		a_snapshot.context->CSSetUnorderedAccessViews(0, static_cast<UINT>(std::size(nullUAVs)), nullUAVs, nullptr);

		AuxConstants constants;
		constants.displayWidth = static_cast<float>(a_displayWidth);
		constants.displayHeight = static_cast<float>(a_displayHeight);
		constants.renderWidth = static_cast<float>(a_renderWidth);
		constants.renderHeight = static_cast<float>(a_renderHeight);
		constants.jitterX = a_jitter.x;
		constants.jitterY = a_jitter.y;

		const auto& compatibility = DragonScale::Config::GetSingleton().GetSettings().compatibility;
		const bool repairMainAuxiliary = compatibility.repairDepth || compatibility.repairSAOCameraZ || compatibility.repairRefractionNormals;
		if (repairMainAuxiliary) {
			auto* refractionSource = PrepareCopy(a_snapshot.context, a_snapshot.refractionNormals, a_snapshot.refractionNormalsCopy, refractionCopy_);
			if (!refractionSource || !refractionSource->HasSRV()) {
				logger::warn("Auxiliary repair skipped: refraction copy source unavailable");
				return false;
			}

			if (!RepairDepthRefraction(a_snapshot.context, a_snapshot, *refractionSource, constants)) {
				return false;
			}
		}

		if (validation.underwaterReady) {
			auto* underwaterSource = PrepareCopy(a_snapshot.context, a_snapshot.underwaterMask, a_snapshot.underwaterMaskCopy, underwaterCopy_);
			if (underwaterSource && underwaterSource->HasSRV()) {
				(void)RepairUnderwaterMask(a_snapshot.context, a_snapshot, *underwaterSource, constants);
			}
		}

		return true;
	}

	void AuxiliaryUpscaler::Release() noexcept
	{
		SafeRelease(fullscreenVertexShader_);
		SafeRelease(depthRefractionPixelShader_);
		SafeRelease(underwaterMaskPixelShader_);
		SafeRelease(constantBuffer_);
		SafeRelease(linearSampler_);
		SafeRelease(opaqueBlendState_);
		SafeRelease(rasterizerState_);
		SafeRelease(depthWriteState_);
		SafeRelease(refractionCopy_.srv);
		SafeRelease(refractionCopy_.texture);
		SafeRelease(underwaterCopy_.srv);
		SafeRelease(underwaterCopy_.texture);
		refractionCopy_ = {};
		underwaterCopy_ = {};
		device_ = nullptr;
	}

	bool AuxiliaryUpscaler::EnsurePipeline(ID3D11Device* a_device)
	{
		if (!a_device) {
			return false;
		}
		if (device_ == a_device &&
			fullscreenVertexShader_ &&
			depthRefractionPixelShader_ &&
			underwaterMaskPixelShader_ &&
			constantBuffer_ &&
			linearSampler_ &&
			opaqueBlendState_ &&
			rasterizerState_ &&
			depthWriteState_) {
			return true;
		}

		Release();
		device_ = a_device;

		ID3DBlob* vertexBlob = nullptr;
		ID3DBlob* pixelBlob = nullptr;
		if (!CompileShader(a_device, kFullscreenVertexShader, "DragonScale_AuxiliaryVS", "vs_5_0", &vertexBlob) ||
			FAILED(a_device->CreateVertexShader(vertexBlob->GetBufferPointer(), vertexBlob->GetBufferSize(), nullptr, &fullscreenVertexShader_))) {
			SafeRelease(vertexBlob);
			Release();
			return false;
		}
		SafeRelease(vertexBlob);

		if (!CompileShader(a_device, kDepthRefractionPixelShader, "DragonScale_DepthRefractionPS", "ps_5_0", &pixelBlob) ||
			FAILED(a_device->CreatePixelShader(pixelBlob->GetBufferPointer(), pixelBlob->GetBufferSize(), nullptr, &depthRefractionPixelShader_))) {
			SafeRelease(pixelBlob);
			Release();
			return false;
		}
		SafeRelease(pixelBlob);

		if (!CompileShader(a_device, kUnderwaterMaskPixelShader, "DragonScale_UnderwaterMaskPS", "ps_5_0", &pixelBlob) ||
			FAILED(a_device->CreatePixelShader(pixelBlob->GetBufferPointer(), pixelBlob->GetBufferSize(), nullptr, &underwaterMaskPixelShader_))) {
			SafeRelease(pixelBlob);
			Release();
			return false;
		}
		SafeRelease(pixelBlob);

		D3D11_BUFFER_DESC bufferDescription{};
		bufferDescription.ByteWidth = sizeof(AuxConstants);
		bufferDescription.Usage = D3D11_USAGE_DEFAULT;
		bufferDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		if (FAILED(a_device->CreateBuffer(&bufferDescription, nullptr, &constantBuffer_))) {
			logger::error("Failed to create DragonScale auxiliary constant buffer");
			Release();
			return false;
		}

		D3D11_SAMPLER_DESC samplerDescription{};
		samplerDescription.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		samplerDescription.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDescription.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDescription.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDescription.MaxLOD = D3D11_FLOAT32_MAX;
		if (FAILED(a_device->CreateSamplerState(&samplerDescription, &linearSampler_))) {
			logger::error("Failed to create DragonScale auxiliary sampler");
			Release();
			return false;
		}

		D3D11_BLEND_DESC blendDescription{};
		blendDescription.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		blendDescription.RenderTarget[1].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		if (FAILED(a_device->CreateBlendState(&blendDescription, &opaqueBlendState_))) {
			logger::error("Failed to create DragonScale auxiliary blend state");
			Release();
			return false;
		}

		D3D11_RASTERIZER_DESC rasterizerDescription{};
		rasterizerDescription.FillMode = D3D11_FILL_SOLID;
		rasterizerDescription.CullMode = D3D11_CULL_NONE;
		rasterizerDescription.DepthClipEnable = TRUE;
		if (FAILED(a_device->CreateRasterizerState(&rasterizerDescription, &rasterizerState_))) {
			logger::error("Failed to create DragonScale auxiliary rasterizer state");
			Release();
			return false;
		}

		D3D11_DEPTH_STENCIL_DESC depthDescription{};
		depthDescription.DepthEnable = TRUE;
		depthDescription.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
		depthDescription.DepthFunc = D3D11_COMPARISON_ALWAYS;
		if (FAILED(a_device->CreateDepthStencilState(&depthDescription, &depthWriteState_))) {
			logger::error("Failed to create DragonScale auxiliary depth state");
			Release();
			return false;
		}

		logger::info("Created DragonScale auxiliary repair pipeline");
		return true;
	}

	bool AuxiliaryUpscaler::EnsureCopyResource(ID3D11Device* a_device, const TextureView& a_source, TextureView& a_copy)
	{
		if (!a_device || !a_source.HasTexture() || a_source.format == DXGI_FORMAT_UNKNOWN) {
			return false;
		}

		if (a_copy.HasSRV() &&
			a_copy.width == a_source.width &&
			a_copy.height == a_source.height &&
			a_copy.format == a_source.format) {
			return true;
		}

		SafeRelease(a_copy.srv);
		SafeRelease(a_copy.texture);
		a_copy = {};

		D3D11_TEXTURE2D_DESC textureDescription{};
		textureDescription.Width = a_source.width;
		textureDescription.Height = a_source.height;
		textureDescription.MipLevels = 1;
		textureDescription.ArraySize = 1;
		textureDescription.Format = a_source.format;
		textureDescription.SampleDesc.Count = 1;
		textureDescription.Usage = D3D11_USAGE_DEFAULT;
		textureDescription.BindFlags = D3D11_BIND_SHADER_RESOURCE;

		ID3D11Texture2D* texture = nullptr;
		ID3D11ShaderResourceView* srv = nullptr;
		if (FAILED(a_device->CreateTexture2D(&textureDescription, nullptr, &texture)) ||
			FAILED(a_device->CreateShaderResourceView(texture, nullptr, &srv))) {
			SafeRelease(srv);
			SafeRelease(texture);
			logger::error("Failed to create DragonScale auxiliary copy: {}x{}, format={}", a_source.width, a_source.height, static_cast<std::uint32_t>(a_source.format));
			return false;
		}

		a_copy = OwnedView(texture, srv, a_source.format, a_source.width, a_source.height, D3D11_BIND_SHADER_RESOURCE);
		return true;
	}

	const TextureView* AuxiliaryUpscaler::PrepareCopy(ID3D11DeviceContext* a_context, const TextureView& a_source, const TextureView& a_sourceCopy, TextureView& a_ownedCopy)
	{
		if (!a_context || !a_source.HasTexture()) {
			return nullptr;
		}

		if (a_sourceCopy.HasSRV() && CopyCompatible(a_source, a_sourceCopy)) {
			a_context->CopyResource(a_sourceCopy.texture, a_source.texture);
			return &a_sourceCopy;
		}

		if (!EnsureCopyResource(device_, a_source, a_ownedCopy)) {
			return nullptr;
		}

		a_context->CopyResource(a_ownedCopy.texture, a_source.texture);
		return &a_ownedCopy;
	}

	bool AuxiliaryUpscaler::RepairDepthRefraction(
		ID3D11DeviceContext* a_context,
		const ResourceSnapshot& a_snapshot,
		const TextureView& a_refractionSource,
		const AuxConstants& a_constants)
	{
		if (!a_context || !a_snapshot.depthCopy.texture || !a_snapshot.depth.texture || !a_snapshot.depth.dsv) {
			return false;
		}

		a_context->CopyResource(a_snapshot.depthCopy.texture, a_snapshot.depth.texture);

		auto constants = a_constants;
		constants.targetWidth = static_cast<float>(a_snapshot.refractionNormals.width);
		constants.targetHeight = static_cast<float>(a_snapshot.refractionNormals.height);
		a_context->UpdateSubresource(constantBuffer_, 0, nullptr, &constants, 0, 0);

		D3D11_VIEWPORT viewport{};
		viewport.Width = constants.displayWidth;
		viewport.Height = constants.displayHeight;
		viewport.MinDepth = 0.0f;
		viewport.MaxDepth = 1.0f;

		ID3D11ShaderResourceView* nullSRVs[3]{};
		a_context->PSSetShaderResources(0, static_cast<UINT>(std::size(nullSRVs)), nullSRVs);
		a_context->IASetInputLayout(nullptr);
		a_context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
		a_context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
		a_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		a_context->VSSetShader(fullscreenVertexShader_, nullptr, 0);
		a_context->PSSetShader(depthRefractionPixelShader_, nullptr, 0);
		a_context->PSSetConstantBuffers(0, 1, &constantBuffer_);
		a_context->PSSetSamplers(0, 1, &linearSampler_);
		a_context->RSSetState(rasterizerState_);
		a_context->RSSetViewports(1, &viewport);
		a_context->OMSetBlendState(opaqueBlendState_, nullptr, 0xFFFFFFFF);
		a_context->OMSetDepthStencilState(depthWriteState_, 0);

		ID3D11ShaderResourceView* srvs[2]{ a_refractionSource.srv, a_snapshot.depthCopy.srv };
		const auto& compatibility = DragonScale::Config::GetSingleton().GetSettings().compatibility;
		ID3D11RenderTargetView* rtvs[2]{
			compatibility.repairRefractionNormals ? a_snapshot.refractionNormals.rtv : nullptr,
			compatibility.repairSAOCameraZ ? a_snapshot.saoCameraZ.rtv : nullptr
		};
		const auto rtvCount = compatibility.repairSAOCameraZ ? 2u : (compatibility.repairRefractionNormals ? 1u : 0u);
		auto* depthTarget = compatibility.repairDepth ? a_snapshot.depth.dsv : nullptr;
		a_context->PSSetShaderResources(0, static_cast<UINT>(std::size(srvs)), srvs);
		a_context->OMSetRenderTargets(rtvCount, rtvCount > 0 ? rtvs : nullptr, depthTarget);
		a_context->Draw(3, 0);
		a_context->PSSetShaderResources(0, static_cast<UINT>(std::size(nullSRVs)), nullSRVs);
		return true;
	}

	bool AuxiliaryUpscaler::RepairUnderwaterMask(
		ID3D11DeviceContext* a_context,
		const ResourceSnapshot& a_snapshot,
		const TextureView& a_underwaterSource,
		const AuxConstants& a_constants)
	{
		if (!a_context || !a_snapshot.underwaterMask.rtv) {
			return false;
		}

		auto constants = a_constants;
		constants.targetWidth = static_cast<float>(a_snapshot.underwaterMask.width);
		constants.targetHeight = static_cast<float>(a_snapshot.underwaterMask.height);
		a_context->UpdateSubresource(constantBuffer_, 0, nullptr, &constants, 0, 0);

		D3D11_VIEWPORT viewport{};
		viewport.Width = constants.targetWidth;
		viewport.Height = constants.targetHeight;
		viewport.MinDepth = 0.0f;
		viewport.MaxDepth = 1.0f;

		ID3D11ShaderResourceView* nullSRVs[3]{};
		a_context->PSSetShaderResources(0, static_cast<UINT>(std::size(nullSRVs)), nullSRVs);
		a_context->IASetInputLayout(nullptr);
		a_context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
		a_context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
		a_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		a_context->VSSetShader(fullscreenVertexShader_, nullptr, 0);
		a_context->PSSetShader(underwaterMaskPixelShader_, nullptr, 0);
		a_context->PSSetConstantBuffers(0, 1, &constantBuffer_);
		a_context->PSSetSamplers(0, 1, &linearSampler_);
		a_context->RSSetState(rasterizerState_);
		a_context->RSSetViewports(1, &viewport);
		a_context->OMSetBlendState(opaqueBlendState_, nullptr, 0xFFFFFFFF);
		a_context->OMSetDepthStencilState(nullptr, 0);

		ID3D11ShaderResourceView* srvs[1]{ a_underwaterSource.srv };
		ID3D11RenderTargetView* rtvs[1]{ a_snapshot.underwaterMask.rtv };
		a_context->PSSetShaderResources(0, static_cast<UINT>(std::size(srvs)), srvs);
		a_context->OMSetRenderTargets(static_cast<UINT>(std::size(rtvs)), rtvs, nullptr);
		a_context->Draw(3, 0);
		a_context->PSSetShaderResources(0, static_cast<UINT>(std::size(nullSRVs)), nullSRVs);
		return true;
	}
}
