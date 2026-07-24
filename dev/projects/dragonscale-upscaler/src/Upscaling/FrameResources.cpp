#include "Upscaling/FrameResources.h"

#include "Upscaling/D3D11StateGuard.h"

#include <d3dcompiler.h>

#include <array>
#include <cstring>

namespace
{
	constexpr const char* kEncodeInputsShader = R"(
cbuffer EncodeConstants : register(b0)
{
	float2 RenderSize;
	float EncodeMasks;
	float pad0;
};

Texture2D<float2> TemporalAAMask : register(t0);
Texture2D<float4> NormalsWaterMask : register(t1);
Texture2D<float2> MotionVectorMask : register(t2);
Texture2D<float> DepthMask : register(t3);

RWTexture2D<float> ReactiveMask : register(u0);
RWTexture2D<float> TransparencyCompositionMask : register(u1);
RWTexture2D<float> DepthOutput : register(u2);

[numthreads(8, 8, 1)]
void main(uint3 dispatchID : SV_DispatchThreadID)
{
	if (dispatchID.x >= (uint)RenderSize.x || dispatchID.y >= (uint)RenderSize.y) {
		return;
	}

	uint2 pixel = dispatchID.xy;
	DepthOutput[pixel] = DepthMask[pixel];

	float reactive = 0.0f;
	float transparency = 0.0f;
	if (EncodeMasks > 0.5f) {
		float2 taa = TemporalAAMask[pixel];
		float4 normalsWater = NormalsWaterMask[pixel];
		reactive = saturate(taa.x * 0.1f + taa.y);
		transparency = saturate(normalsWater.z);
	}

	ReactiveMask[pixel] = reactive;
	TransparencyCompositionMask[pixel] = transparency;
}
)";

	constexpr const char* kPreserveOutputAlphaShader = R"(
cbuffer AlphaPreserveConstants : register(b0)
{
	float2 DisplaySize;
	float2 RenderSize;
};

Texture2D<float4> SourceColor : register(t0);
RWTexture2D<float4> OutputColor : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 dispatchID : SV_DispatchThreadID)
{
	if (dispatchID.x >= (uint)DisplaySize.x || dispatchID.y >= (uint)DisplaySize.y) {
		return;
	}

	float2 displaySize = max(DisplaySize, float2(1.0f, 1.0f));
	float2 renderSizeFloat = max(RenderSize, float2(1.0f, 1.0f));
	uint2 renderSize = uint2(renderSizeFloat);
	float2 uv = (float2(dispatchID.xy) + 0.5f) / displaySize;
	uint2 sourcePixel = min(uint2(uv * renderSizeFloat), renderSize - 1);

	float4 output = OutputColor[dispatchID.xy];
	output.a = SourceColor.Load(int3(sourcePixel, 0)).a;
	OutputColor[dispatchID.xy] = output;
}
)";

	constexpr const char* kScaleCopyShader = R"(
cbuffer ScaleCopyConstants : register(b0)
{
	float2 DisplaySize;
	float2 RenderSize;
};

Texture2D<float4> SourceColor : register(t0);
RWTexture2D<float4> OutputColor : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 dispatchID : SV_DispatchThreadID)
{
	if (dispatchID.x >= (uint)DisplaySize.x || dispatchID.y >= (uint)DisplaySize.y) {
		return;
	}

	float2 displaySize = max(DisplaySize, float2(1.0f, 1.0f));
	float2 renderSize = max(RenderSize, float2(1.0f, 1.0f));
	float2 sourcePosition = ((float2(dispatchID.xy) + 0.5f) / displaySize) * renderSize - 0.5f;
	float2 sourceBaseFloat = floor(sourcePosition);
	float2 sourceFraction = saturate(sourcePosition - sourceBaseFloat);
	int2 sourceBase = int2(sourceBaseFloat);
	int2 maxPixel = int2(renderSize) - 1;

	int2 p00 = clamp(sourceBase, int2(0, 0), maxPixel);
	int2 p10 = clamp(sourceBase + int2(1, 0), int2(0, 0), maxPixel);
	int2 p01 = clamp(sourceBase + int2(0, 1), int2(0, 0), maxPixel);
	int2 p11 = clamp(sourceBase + int2(1, 1), int2(0, 0), maxPixel);

	float4 c00 = SourceColor.Load(int3(p00, 0));
	float4 c10 = SourceColor.Load(int3(p10, 0));
	float4 c01 = SourceColor.Load(int3(p01, 0));
	float4 c11 = SourceColor.Load(int3(p11, 0));
	float4 cx0 = lerp(c00, c10, sourceFraction.x);
	float4 cx1 = lerp(c01, c11, sourceFraction.x);
	OutputColor[dispatchID.xy] = lerp(cx0, cx1, sourceFraction.y);
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

	class ScopedOutputMergerTargets
	{
	public:
		explicit ScopedOutputMergerTargets(ID3D11DeviceContext* a_context) :
			context_(a_context)
		{
			if (!context_) {
				return;
			}

			context_->OMGetRenderTargets(static_cast<UINT>(std::size(renderTargets_)), renderTargets_, &depthStencil_);
			context_->OMSetRenderTargets(0, nullptr, nullptr);
		}

		~ScopedOutputMergerTargets()
		{
			if (!context_) {
				return;
			}

			context_->OMSetRenderTargets(static_cast<UINT>(std::size(renderTargets_)), renderTargets_, depthStencil_);
			for (auto*& renderTarget : renderTargets_) {
				SafeRelease(renderTarget);
			}
			SafeRelease(depthStencil_);
		}

		ScopedOutputMergerTargets(const ScopedOutputMergerTargets&) = delete;
		ScopedOutputMergerTargets& operator=(const ScopedOutputMergerTargets&) = delete;

	private:
		ID3D11DeviceContext* context_ = nullptr;
		ID3D11RenderTargetView* renderTargets_[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
		ID3D11DepthStencilView* depthStencil_ = nullptr;
	};

	[[nodiscard]] bool ViewAliasesResource(ID3D11View* a_view, ID3D11Resource* a_first, ID3D11Resource* a_second) noexcept
	{
		if (!a_view || (!a_first && !a_second)) {
			return false;
		}

		ID3D11Resource* resource = nullptr;
		a_view->GetResource(&resource);
		const bool aliases = resource && (resource == a_first || resource == a_second);
		SafeRelease(resource);
		return aliases;
	}

	template <class ClearStage>
	void ClearAliasedShaderResourceStage(ClearStage&& a_clearStage, ID3D11Resource* a_first, ID3D11Resource* a_second)
	{
		std::array<ID3D11ShaderResourceView*, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT> views{};
		a_clearStage(true, views.data(), static_cast<UINT>(views.size()));

		bool changed = false;
		for (auto*& view : views) {
			if (ViewAliasesResource(view, a_first, a_second)) {
				SafeRelease(view);
				changed = true;
			}
		}

		if (changed) {
			a_clearStage(false, views.data(), static_cast<UINT>(views.size()));
		}

		for (auto*& view : views) {
			SafeRelease(view);
		}
	}

	void UnbindAliasedCopyViews(ID3D11DeviceContext* a_context, ID3D11Resource* a_destination, ID3D11Resource* a_source)
	{
		if (!a_context || (!a_destination && !a_source)) {
			return;
		}

		ClearAliasedShaderResourceStage(
			[&](bool a_get, ID3D11ShaderResourceView** a_views, UINT a_count) {
				if (a_get) {
					a_context->VSGetShaderResources(0, a_count, a_views);
				} else {
					a_context->VSSetShaderResources(0, a_count, a_views);
				}
			},
			a_destination,
			a_source);
		ClearAliasedShaderResourceStage(
			[&](bool a_get, ID3D11ShaderResourceView** a_views, UINT a_count) {
				if (a_get) {
					a_context->HSGetShaderResources(0, a_count, a_views);
				} else {
					a_context->HSSetShaderResources(0, a_count, a_views);
				}
			},
			a_destination,
			a_source);
		ClearAliasedShaderResourceStage(
			[&](bool a_get, ID3D11ShaderResourceView** a_views, UINT a_count) {
				if (a_get) {
					a_context->DSGetShaderResources(0, a_count, a_views);
				} else {
					a_context->DSSetShaderResources(0, a_count, a_views);
				}
			},
			a_destination,
			a_source);
		ClearAliasedShaderResourceStage(
			[&](bool a_get, ID3D11ShaderResourceView** a_views, UINT a_count) {
				if (a_get) {
					a_context->GSGetShaderResources(0, a_count, a_views);
				} else {
					a_context->GSSetShaderResources(0, a_count, a_views);
				}
			},
			a_destination,
			a_source);
		ClearAliasedShaderResourceStage(
			[&](bool a_get, ID3D11ShaderResourceView** a_views, UINT a_count) {
				if (a_get) {
					a_context->PSGetShaderResources(0, a_count, a_views);
				} else {
					a_context->PSSetShaderResources(0, a_count, a_views);
				}
			},
			a_destination,
			a_source);
		ClearAliasedShaderResourceStage(
			[&](bool a_get, ID3D11ShaderResourceView** a_views, UINT a_count) {
				if (a_get) {
					a_context->CSGetShaderResources(0, a_count, a_views);
				} else {
					a_context->CSSetShaderResources(0, a_count, a_views);
				}
			},
			a_destination,
			a_source);

		std::array<ID3D11UnorderedAccessView*, D3D11_PS_CS_UAV_REGISTER_COUNT> uavs{};
		a_context->CSGetUnorderedAccessViews(0, static_cast<UINT>(uavs.size()), uavs.data());
		bool changed = false;
		for (auto*& uav : uavs) {
			if (ViewAliasesResource(uav, a_destination, a_source)) {
				SafeRelease(uav);
				changed = true;
			}
		}
		if (changed) {
			a_context->CSSetUnorderedAccessViews(0, static_cast<UINT>(uavs.size()), uavs.data(), nullptr);
		}
		for (auto*& uav : uavs) {
			SafeRelease(uav);
		}
	}

	[[nodiscard]] bool IsColorFormatUsable(DXGI_FORMAT a_format) noexcept
	{
		return a_format != DXGI_FORMAT_UNKNOWN &&
		       a_format != DXGI_FORMAT_R24G8_TYPELESS &&
		       a_format != DXGI_FORMAT_R32_TYPELESS &&
		       a_format != DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
	}

	[[nodiscard]] DragonScale::Upscaling::TextureView OwnedView(
		ID3D11Texture2D* a_texture,
		ID3D11ShaderResourceView* a_srv,
		ID3D11UnorderedAccessView* a_uav,
		DXGI_FORMAT a_format,
		std::uint32_t a_width,
		std::uint32_t a_height,
		std::uint32_t a_bindFlags) noexcept
	{
		DragonScale::Upscaling::TextureView view;
		view.texture = a_texture;
		view.srv = a_srv;
		view.uav = a_uav;
		view.format = a_format;
		view.width = a_width;
		view.height = a_height;
		view.bindFlags = a_bindFlags;
		view.source = DragonScale::Upscaling::ResourceSource::kOwned;
		return view;
	}

	[[nodiscard]] bool CompileComputeShader(
		ID3D11Device* a_device,
		const char* a_source,
		const char* a_name,
		ID3D11ComputeShader*& a_shader)
	{
		if (!a_device || a_shader) {
			return a_shader != nullptr;
		}

		ID3DBlob* shaderBlob = nullptr;
		ID3DBlob* errorBlob = nullptr;
		const auto compileResult = D3DCompile(
			a_source,
			std::strlen(a_source),
			a_name,
			nullptr,
			nullptr,
			"main",
			"cs_5_0",
			0,
			0,
			&shaderBlob,
			&errorBlob);
		if (FAILED(compileResult) || !shaderBlob) {
			if (errorBlob) {
				logger::error("Failed to compile {}: {}", a_name, static_cast<const char*>(errorBlob->GetBufferPointer()));
			} else {
				logger::error("Failed to compile {}: 0x{:X}", a_name, static_cast<std::uint32_t>(compileResult));
			}
			SafeRelease(shaderBlob);
			SafeRelease(errorBlob);
			return false;
		}

		const auto shaderResult = a_device->CreateComputeShader(shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), nullptr, &a_shader);
		SafeRelease(shaderBlob);
		SafeRelease(errorBlob);
		if (FAILED(shaderResult)) {
			logger::error("Failed to create {}: 0x{:X}", a_name, static_cast<std::uint32_t>(shaderResult));
			return false;
		}

		return true;
	}
}

namespace DragonScale::Upscaling
{
	FrameResources::~FrameResources()
	{
		Release();
	}

	bool FrameResources::Prepare(
		const ResourceSnapshot& a_snapshot,
		std::uint32_t a_displayWidth,
		std::uint32_t a_displayHeight,
		std::uint32_t a_renderWidth,
		std::uint32_t a_renderHeight,
		bool a_encodeReactiveMasks)
	{
		recreatedThisFrame_ = false;
		if (!a_snapshot.HasRequiredInputs()) {
			logger::warn(
				"Frame resource preparation skipped; requiredReady={}, colorSRV={}, depthSRV={}, motionSRV={}, outputTexture={}",
				a_snapshot.HasRequiredInputs(),
				static_cast<const void*>(a_snapshot.color.srv),
				static_cast<const void*>(a_snapshot.depth.srv),
				static_cast<const void*>(a_snapshot.motionVectors.srv),
				static_cast<const void*>(a_snapshot.output.texture));
			return false;
		}

		if (!EnsureResources(a_snapshot, a_displayWidth, a_displayHeight, a_renderWidth, a_renderHeight)) {
			return false;
		}

		auto* context = a_snapshot.context;
		D3D11StateGuard stateGuard(context);
		{
			ScopedOutputMergerTargets outputMergerScope(context);
			if (!CopyRenderRegion(context, prepared_.inputColor.texture, a_snapshot.color, a_renderWidth, a_renderHeight) ||
				!CopyRenderRegion(context, prepared_.motionVectors.texture, a_snapshot.motionVectors, a_renderWidth, a_renderHeight)) {
				return false;
			}
		}

		if (!EncodeInputs(a_snapshot, a_renderWidth, a_renderHeight, a_encodeReactiveMasks)) {
			return false;
		}

		return true;
	}

	bool FrameResources::PrepareCopyThrough(
		const ResourceSnapshot& a_snapshot,
		std::uint32_t a_displayWidth,
		std::uint32_t a_displayHeight,
		std::uint32_t a_renderWidth,
		std::uint32_t a_renderHeight)
	{
		recreatedThisFrame_ = false;
		if (!a_snapshot.device || !a_snapshot.context || !a_snapshot.color.HasTexture() || !a_snapshot.output.HasTexture()) {
			logger::warn(
				"Copy-through preparation skipped; device={}, context={}, colorTexture={}, outputTexture={}",
				static_cast<const void*>(a_snapshot.device),
				static_cast<const void*>(a_snapshot.context),
				static_cast<const void*>(a_snapshot.color.texture),
				static_cast<const void*>(a_snapshot.output.texture));
			return false;
		}

		if (!EnsureCopyThroughResources(a_snapshot, a_displayWidth, a_displayHeight, a_renderWidth, a_renderHeight)) {
			return false;
		}

		D3D11StateGuard stateGuard(a_snapshot.context);
		ScopedOutputMergerTargets outputMergerScope(a_snapshot.context);
		return CopyRenderRegion(a_snapshot.context, prepared_.inputColor.texture, a_snapshot.color, a_renderWidth, a_renderHeight);
	}

	const PreparedFrameResources& FrameResources::GetPrepared() const noexcept
	{
		return prepared_;
	}

	bool FrameResources::RecreatedThisFrame() const noexcept
	{
		return recreatedThisFrame_;
	}

	bool FrameResources::CopyInputToOutput(std::uint32_t a_width, std::uint32_t a_height)
	{
		if (!device_ || !prepared_.inputColor.texture || !prepared_.outputColor.texture) {
			return false;
		}

		ID3D11DeviceContext* context = nullptr;
		device_->GetImmediateContext(&context);
		if (!context) {
			return false;
		}

		bool copied = false;
		{
			D3D11StateGuard stateGuard(context);
			ScopedOutputMergerTargets outputMergerScope(context);
			copied = CopyRenderRegion(context, prepared_.outputColor.texture, prepared_.inputColor, a_width, a_height);
		}
		context->Release();
		return copied;
	}

	bool FrameResources::ScaleInputToOutput(
		std::uint32_t a_displayWidth,
		std::uint32_t a_displayHeight,
		std::uint32_t a_renderWidth,
		std::uint32_t a_renderHeight)
	{
		if (a_displayWidth == 0 || a_displayHeight == 0 || a_renderWidth == 0 || a_renderHeight == 0) {
			return false;
		}
		if (!device_ || !prepared_.inputColor.HasSRV() || !prepared_.outputColor.HasUAV() || !scaleCopyShader_ || !preserveAlphaConstants_) {
			logger::warn(
				"DragonScale ScaleCopy unavailable: device={}, inputSRV={}, outputUAV={}, shader={}, constants={}",
				static_cast<const void*>(device_),
				static_cast<const void*>(prepared_.inputColor.srv),
				static_cast<const void*>(prepared_.outputColor.uav),
				static_cast<const void*>(scaleCopyShader_),
				static_cast<const void*>(preserveAlphaConstants_));
			return false;
		}

		ID3D11DeviceContext* context = nullptr;
		device_->GetImmediateContext(&context);
		if (!context) {
			return false;
		}

		{
			D3D11StateGuard stateGuard(context);
			ScopedOutputMergerTargets outputMergerScope(context);
			UnbindAliasedCopyViews(context, prepared_.outputColor.texture, prepared_.inputColor.texture);

			const AlphaPreserveConstants constants{
				static_cast<float>(a_displayWidth),
				static_cast<float>(a_displayHeight),
				static_cast<float>(a_renderWidth),
				static_cast<float>(a_renderHeight)
			};
			context->UpdateSubresource(preserveAlphaConstants_, 0, nullptr, &constants, 0, 0);

			ID3D11ShaderResourceView* srvs[1]{ prepared_.inputColor.srv };
			ID3D11UnorderedAccessView* uavs[1]{ prepared_.outputColor.uav };
			context->CSSetShader(scaleCopyShader_, nullptr, 0);
			context->CSSetShaderResources(0, static_cast<UINT>(std::size(srvs)), srvs);
			context->CSSetUnorderedAccessViews(0, static_cast<UINT>(std::size(uavs)), uavs, nullptr);
			context->CSSetConstantBuffers(0, 1, &preserveAlphaConstants_);
			context->Dispatch((a_displayWidth + 7u) / 8u, (a_displayHeight + 7u) / 8u, 1);

			ID3D11ShaderResourceView* nullSRVs[1]{};
			ID3D11UnorderedAccessView* nullUAVs[1]{};
			ID3D11Buffer* nullBuffer = nullptr;
			context->CSSetShaderResources(0, static_cast<UINT>(std::size(nullSRVs)), nullSRVs);
			context->CSSetUnorderedAccessViews(0, static_cast<UINT>(std::size(nullUAVs)), nullUAVs, nullptr);
			context->CSSetConstantBuffers(0, 1, &nullBuffer);
			context->CSSetShader(nullptr, nullptr, 0);
		}

		context->Release();
		return true;
	}

	bool FrameResources::PreserveOutputAlpha(
		std::uint32_t a_displayWidth,
		std::uint32_t a_displayHeight,
		std::uint32_t a_renderWidth,
		std::uint32_t a_renderHeight)
	{
		if (a_displayWidth == 0 || a_displayHeight == 0 || a_renderWidth == 0 || a_renderHeight == 0) {
			return false;
		}
		if (!device_ ||
			!prepared_.inputColor.HasSRV() ||
			!prepared_.outputColor.HasUAV() ||
			!preserveAlphaShader_ ||
			!preserveAlphaConstants_) {
			if (!alphaPreserveLogged_) {
				logger::warn(
					"DragonScale main alpha preservation unavailable: device={}, inputSRV={}, outputUAV={}, shader={}, constants={}",
					static_cast<const void*>(device_),
					static_cast<const void*>(prepared_.inputColor.srv),
					static_cast<const void*>(prepared_.outputColor.uav),
					static_cast<const void*>(preserveAlphaShader_),
					static_cast<const void*>(preserveAlphaConstants_));
				alphaPreserveLogged_ = true;
			}
			return false;
		}

		ID3D11DeviceContext* context = nullptr;
		device_->GetImmediateContext(&context);
		if (!context) {
			return false;
		}

		{
			D3D11StateGuard stateGuard(context);
			ScopedOutputMergerTargets outputMergerScope(context);
			UnbindAliasedCopyViews(context, prepared_.outputColor.texture, prepared_.inputColor.texture);

			const AlphaPreserveConstants constants{
				static_cast<float>(a_displayWidth),
				static_cast<float>(a_displayHeight),
				static_cast<float>(a_renderWidth),
				static_cast<float>(a_renderHeight)
			};
			context->UpdateSubresource(preserveAlphaConstants_, 0, nullptr, &constants, 0, 0);

			ID3D11ShaderResourceView* srvs[1]{ prepared_.inputColor.srv };
			ID3D11UnorderedAccessView* uavs[1]{ prepared_.outputColor.uav };
			context->CSSetShader(preserveAlphaShader_, nullptr, 0);
			context->CSSetShaderResources(0, static_cast<UINT>(std::size(srvs)), srvs);
			context->CSSetUnorderedAccessViews(0, static_cast<UINT>(std::size(uavs)), uavs, nullptr);
			context->CSSetConstantBuffers(0, 1, &preserveAlphaConstants_);
			context->Dispatch((a_displayWidth + 7u) / 8u, (a_displayHeight + 7u) / 8u, 1);

			ID3D11ShaderResourceView* nullSRVs[1]{};
			ID3D11UnorderedAccessView* nullUAVs[1]{};
			ID3D11Buffer* nullBuffer = nullptr;
			context->CSSetShaderResources(0, static_cast<UINT>(std::size(nullSRVs)), nullSRVs);
			context->CSSetUnorderedAccessViews(0, static_cast<UINT>(std::size(nullUAVs)), nullUAVs, nullptr);
			context->CSSetConstantBuffers(0, 1, &nullBuffer);
			context->CSSetShader(nullptr, nullptr, 0);
		}

		context->Release();
		if (!alphaPreserveLogged_) {
			logger::info(
				"DragonScale main alpha preservation enabled: {}x{} -> {}x{}",
				a_renderWidth,
				a_renderHeight,
				a_displayWidth,
				a_displayHeight);
			alphaPreserveLogged_ = true;
		}
		return true;
	}

	void FrameResources::CopyOutputToGameTarget(const ResourceSnapshot& a_snapshot)
	{
		if (!a_snapshot.context || !a_snapshot.output.texture || !prepared_.outputColor.texture) {
			return;
		}

		D3D11StateGuard stateGuard(a_snapshot.context);
		ScopedOutputMergerTargets outputMergerScope(a_snapshot.context);
		UnbindAliasedCopyViews(a_snapshot.context, a_snapshot.output.texture, prepared_.outputColor.texture);
		a_snapshot.context->CopyResource(a_snapshot.output.texture, prepared_.outputColor.texture);
	}

	void FrameResources::Release() noexcept
	{
		SafeRelease(prepared_.inputColor.srv);
		SafeRelease(prepared_.inputColor.uav);
		SafeRelease(prepared_.inputColor.texture);
		SafeRelease(prepared_.outputColor.srv);
		SafeRelease(prepared_.outputColor.uav);
		SafeRelease(prepared_.outputColor.texture);
		SafeRelease(prepared_.typedDepth.srv);
		SafeRelease(prepared_.typedDepth.uav);
		SafeRelease(prepared_.typedDepth.texture);
		SafeRelease(prepared_.motionVectors.srv);
		SafeRelease(prepared_.motionVectors.uav);
		SafeRelease(prepared_.motionVectors.texture);
		SafeRelease(prepared_.reactiveMask.srv);
		SafeRelease(prepared_.reactiveMask.uav);
		SafeRelease(prepared_.reactiveMask.texture);
		SafeRelease(prepared_.transparencyCompositionMask.srv);
		SafeRelease(prepared_.transparencyCompositionMask.uav);
		SafeRelease(prepared_.transparencyCompositionMask.texture);
		SafeRelease(encodeInputsShader_);
		SafeRelease(scaleCopyShader_);
		SafeRelease(preserveAlphaShader_);
		SafeRelease(encodeConstants_);
		SafeRelease(preserveAlphaConstants_);
		device_ = nullptr;
		colorFormat_ = DXGI_FORMAT_UNKNOWN;
		motionVectorFormat_ = DXGI_FORMAT_UNKNOWN;
		displayWidth_ = 0;
		displayHeight_ = 0;
		renderWidth_ = 0;
		renderHeight_ = 0;
		recreatedThisFrame_ = false;
		alphaPreserveLogged_ = false;
	}

	bool FrameResources::EnsureResources(
		const ResourceSnapshot& a_snapshot,
		std::uint32_t a_displayWidth,
		std::uint32_t a_displayHeight,
		std::uint32_t a_renderWidth,
		std::uint32_t a_renderHeight)
	{
		if (!a_snapshot.device || !IsColorFormatUsable(a_snapshot.color.format) || !IsColorFormatUsable(a_snapshot.motionVectors.format)) {
			logger::warn(
				"Cannot create frame resources; device={}, colorFormat={}, motionVectorFormat={}",
				static_cast<const void*>(a_snapshot.device),
				static_cast<std::uint32_t>(a_snapshot.color.format),
				static_cast<std::uint32_t>(a_snapshot.motionVectors.format));
			return false;
		}

		if (device_ == a_snapshot.device &&
			colorFormat_ == a_snapshot.color.format &&
			motionVectorFormat_ == a_snapshot.motionVectors.format &&
			displayWidth_ == a_displayWidth &&
			displayHeight_ == a_displayHeight &&
			renderWidth_ == a_renderWidth &&
			renderHeight_ == a_renderHeight &&
			prepared_.inputColor.HasSRV() &&
			prepared_.outputColor.HasUAV() &&
			prepared_.typedDepth.HasUAV() &&
			prepared_.motionVectors.HasSRV() &&
			prepared_.reactiveMask.HasUAV() &&
			prepared_.transparencyCompositionMask.HasUAV()) {
			return true;
		}

		Release();
		recreatedThisFrame_ = true;
		device_ = a_snapshot.device;
		colorFormat_ = a_snapshot.color.format;
		motionVectorFormat_ = a_snapshot.motionVectors.format;
		displayWidth_ = a_displayWidth;
		displayHeight_ = a_displayHeight;
		renderWidth_ = a_renderWidth;
		renderHeight_ = a_renderHeight;

		if (!CreateColorResource(device_, colorFormat_, renderWidth_, renderHeight_, false, prepared_.inputColor) ||
			!CreateColorResource(device_, colorFormat_, displayWidth_, displayHeight_, true, prepared_.outputColor) ||
			!CreateColorResource(device_, motionVectorFormat_, renderWidth_, renderHeight_, false, prepared_.motionVectors) ||
			!CreateDepthResource(device_, renderWidth_, renderHeight_, prepared_.typedDepth) ||
			!CreateMaskResource(device_, prepared_.reactiveMask) ||
			!CreateMaskResource(device_, prepared_.transparencyCompositionMask) ||
			!EnsureEncodeResources(device_)) {
			Release();
			return false;
		}

		logger::info(
			"Created frame resources: display={}x{}, render={}x{}, colorFormat={}, motionVectorFormat={}",
			displayWidth_,
			displayHeight_,
			renderWidth_,
			renderHeight_,
			static_cast<std::uint32_t>(colorFormat_),
			static_cast<std::uint32_t>(motionVectorFormat_));
		return true;
	}

	bool FrameResources::EnsureCopyThroughResources(
		const ResourceSnapshot& a_snapshot,
		std::uint32_t a_displayWidth,
		std::uint32_t a_displayHeight,
		std::uint32_t a_renderWidth,
		std::uint32_t a_renderHeight)
	{
		if (!a_snapshot.device || !IsColorFormatUsable(a_snapshot.color.format)) {
			logger::warn(
				"Cannot create copy-through resources; device={}, colorFormat={}",
				static_cast<const void*>(a_snapshot.device),
				static_cast<std::uint32_t>(a_snapshot.color.format));
			return false;
		}

		if (device_ == a_snapshot.device &&
			colorFormat_ == a_snapshot.color.format &&
			motionVectorFormat_ == DXGI_FORMAT_UNKNOWN &&
			displayWidth_ == a_displayWidth &&
			displayHeight_ == a_displayHeight &&
			renderWidth_ == a_renderWidth &&
			renderHeight_ == a_renderHeight &&
			prepared_.inputColor.HasSRV() &&
			prepared_.outputColor.HasTexture()) {
			return true;
		}

		Release();
		recreatedThisFrame_ = true;
		device_ = a_snapshot.device;
		colorFormat_ = a_snapshot.color.format;
		motionVectorFormat_ = DXGI_FORMAT_UNKNOWN;
		displayWidth_ = a_displayWidth;
		displayHeight_ = a_displayHeight;
		renderWidth_ = a_renderWidth;
		renderHeight_ = a_renderHeight;

		if (!CreateColorResource(device_, colorFormat_, renderWidth_, renderHeight_, false, prepared_.inputColor) ||
			!CreateColorResource(device_, colorFormat_, displayWidth_, displayHeight_, false, prepared_.outputColor)) {
			Release();
			return false;
		}

		logger::info(
			"Created copy-through resources: display={}x{}, render={}x{}, colorFormat={}",
			displayWidth_,
			displayHeight_,
			renderWidth_,
			renderHeight_,
			static_cast<std::uint32_t>(colorFormat_));
		return true;
	}

	bool FrameResources::EnsureEncodeResources(ID3D11Device* a_device)
	{
		if (encodeInputsShader_ && scaleCopyShader_ && preserveAlphaShader_ && encodeConstants_ && preserveAlphaConstants_) {
			return true;
		}

		if (!CompileComputeShader(a_device, kEncodeInputsShader, "DragonScale_EncodeInputsCS", encodeInputsShader_) ||
			!CompileComputeShader(a_device, kScaleCopyShader, "DragonScale_ScaleCopyCS", scaleCopyShader_) ||
			!CompileComputeShader(a_device, kPreserveOutputAlphaShader, "DragonScale_PreserveOutputAlphaCS", preserveAlphaShader_)) {
			return false;
		}

		if (!encodeConstants_) {
			D3D11_BUFFER_DESC bufferDescription{};
			bufferDescription.ByteWidth = sizeof(EncodeConstants);
			bufferDescription.Usage = D3D11_USAGE_DEFAULT;
			bufferDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			if (FAILED(a_device->CreateBuffer(&bufferDescription, nullptr, &encodeConstants_))) {
				logger::error("Failed to create DragonScale input encoder constant buffer");
				return false;
			}
		}
		if (!preserveAlphaConstants_) {
			D3D11_BUFFER_DESC bufferDescription{};
			bufferDescription.ByteWidth = sizeof(AlphaPreserveConstants);
			bufferDescription.Usage = D3D11_USAGE_DEFAULT;
			bufferDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			if (FAILED(a_device->CreateBuffer(&bufferDescription, nullptr, &preserveAlphaConstants_))) {
				logger::error("Failed to create DragonScale alpha-preserve constant buffer");
				return false;
			}
		}

		return true;
	}

	bool FrameResources::CreateColorResource(ID3D11Device* a_device, DXGI_FORMAT a_format, std::uint32_t a_width, std::uint32_t a_height, bool a_createUav, TextureView& a_view)
	{
		const std::uint32_t bindFlags = D3D11_BIND_SHADER_RESOURCE | (a_createUav ? D3D11_BIND_UNORDERED_ACCESS : 0);

		D3D11_TEXTURE2D_DESC textureDescription{};
		textureDescription.Width = a_width;
		textureDescription.Height = a_height;
		textureDescription.MipLevels = 1;
		textureDescription.ArraySize = 1;
		textureDescription.Format = a_format;
		textureDescription.SampleDesc.Count = 1;
		textureDescription.Usage = D3D11_USAGE_DEFAULT;
		textureDescription.BindFlags = bindFlags;

		ID3D11Texture2D* texture = nullptr;
		ID3D11ShaderResourceView* srv = nullptr;
		ID3D11UnorderedAccessView* uav = nullptr;
		if (FAILED(a_device->CreateTexture2D(&textureDescription, nullptr, &texture)) ||
			FAILED(a_device->CreateShaderResourceView(texture, nullptr, &srv)) ||
			(a_createUav && FAILED(a_device->CreateUnorderedAccessView(texture, nullptr, &uav)))) {
			SafeRelease(uav);
			SafeRelease(srv);
			SafeRelease(texture);
			logger::error("Failed to create owned texture resource: {}x{}, format={}, uav={}", a_width, a_height, static_cast<std::uint32_t>(a_format), a_createUav);
			return false;
		}

		a_view = OwnedView(texture, srv, uav, a_format, a_width, a_height, bindFlags);
		return true;
	}

	bool FrameResources::CreateMaskResource(ID3D11Device* a_device, TextureView& a_view)
	{
		constexpr auto format = DXGI_FORMAT_R8_UNORM;
		constexpr std::uint32_t bindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

		D3D11_TEXTURE2D_DESC textureDescription{};
		textureDescription.Width = renderWidth_;
		textureDescription.Height = renderHeight_;
		textureDescription.MipLevels = 1;
		textureDescription.ArraySize = 1;
		textureDescription.Format = format;
		textureDescription.SampleDesc.Count = 1;
		textureDescription.Usage = D3D11_USAGE_DEFAULT;
		textureDescription.BindFlags = bindFlags;

		ID3D11Texture2D* texture = nullptr;
		ID3D11ShaderResourceView* srv = nullptr;
		ID3D11UnorderedAccessView* uav = nullptr;
		if (FAILED(a_device->CreateTexture2D(&textureDescription, nullptr, &texture)) ||
			FAILED(a_device->CreateShaderResourceView(texture, nullptr, &srv)) ||
			FAILED(a_device->CreateUnorderedAccessView(texture, nullptr, &uav))) {
			SafeRelease(uav);
			SafeRelease(srv);
			SafeRelease(texture);
			logger::error("Failed to create owned mask resource: {}x{}", renderWidth_, renderHeight_);
			return false;
		}

		a_view = OwnedView(texture, srv, uav, format, renderWidth_, renderHeight_, bindFlags);
		return true;
	}

	bool FrameResources::CreateDepthResource(ID3D11Device* a_device, std::uint32_t a_width, std::uint32_t a_height, TextureView& a_view)
	{
		constexpr auto format = DXGI_FORMAT_R32_FLOAT;
		constexpr std::uint32_t bindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

		D3D11_TEXTURE2D_DESC textureDescription{};
		textureDescription.Width = a_width;
		textureDescription.Height = a_height;
		textureDescription.MipLevels = 1;
		textureDescription.ArraySize = 1;
		textureDescription.Format = format;
		textureDescription.SampleDesc.Count = 1;
		textureDescription.Usage = D3D11_USAGE_DEFAULT;
		textureDescription.BindFlags = bindFlags;

		ID3D11Texture2D* texture = nullptr;
		ID3D11ShaderResourceView* srv = nullptr;
		ID3D11UnorderedAccessView* uav = nullptr;
		if (FAILED(a_device->CreateTexture2D(&textureDescription, nullptr, &texture)) ||
			FAILED(a_device->CreateShaderResourceView(texture, nullptr, &srv)) ||
			FAILED(a_device->CreateUnorderedAccessView(texture, nullptr, &uav))) {
			SafeRelease(uav);
			SafeRelease(srv);
			SafeRelease(texture);
			logger::error("Failed to create owned typed depth resource: {}x{}", a_width, a_height);
			return false;
		}

		a_view = OwnedView(texture, srv, uav, format, a_width, a_height, bindFlags);
		return true;
	}

	bool FrameResources::CopyRenderRegion(
		ID3D11DeviceContext* a_context,
		ID3D11Texture2D* a_destination,
		const TextureView& a_source,
		std::uint32_t a_width,
		std::uint32_t a_height)
	{
		if (!a_context || !a_destination || !a_source.texture || a_width == 0 || a_height == 0) {
			return false;
		}

		if (a_source.width < a_width || a_source.height < a_height) {
			logger::warn(
				"Cannot copy render-region resource; source={}x{}, requested={}x{}, format={}",
				a_source.width,
				a_source.height,
				a_width,
				a_height,
				static_cast<std::uint32_t>(a_source.format));
			return false;
		}

		D3D11_BOX sourceBox{};
		sourceBox.left = 0;
		sourceBox.top = 0;
		sourceBox.front = 0;
		sourceBox.right = static_cast<UINT>(a_width);
		sourceBox.bottom = static_cast<UINT>(a_height);
		sourceBox.back = 1;
		UnbindAliasedCopyViews(a_context, a_destination, a_source.texture);
		a_context->CopySubresourceRegion(a_destination, 0, 0, 0, 0, a_source.texture, 0, &sourceBox);
		return true;
	}

	bool FrameResources::EncodeInputs(const ResourceSnapshot& a_snapshot, std::uint32_t a_renderWidth, std::uint32_t a_renderHeight, bool a_encodeReactiveMasks)
	{
		auto* context = a_snapshot.context;
		if (!context || !encodeInputsShader_ || !encodeConstants_) {
			return false;
		}

		ID3D11ComputeShader* oldShader = nullptr;
		ID3D11ShaderResourceView* oldSRVs[4]{};
		ID3D11UnorderedAccessView* oldUAVs[4]{};
		ID3D11Buffer* oldConstantBuffer = nullptr;
		context->CSGetShader(&oldShader, nullptr, nullptr);
		context->CSGetShaderResources(0, static_cast<UINT>(std::size(oldSRVs)), oldSRVs);
		context->CSGetUnorderedAccessViews(0, static_cast<UINT>(std::size(oldUAVs)), oldUAVs);
		context->CSGetConstantBuffers(0, 1, &oldConstantBuffer);

		const bool canEncodeMasks = a_encodeReactiveMasks && a_snapshot.temporalAAMask.HasSRV() && a_snapshot.normalsWaterMask.HasSRV();
		static bool missingOptionalMaskLogged = false;
		if (a_encodeReactiveMasks && !canEncodeMasks && !missingOptionalMaskLogged) {
			logger::info(
				"Optional mask sources are not available; reactive and transparency/composition masks will be cleared");
			missingOptionalMaskLogged = true;
		}

		const EncodeConstants constants{
			static_cast<float>(a_renderWidth),
			static_cast<float>(a_renderHeight),
			canEncodeMasks ? 1.0f : 0.0f,
			0.0f
		};
		context->UpdateSubresource(encodeConstants_, 0, nullptr, &constants, 0, 0);

		ID3D11ShaderResourceView* srvs[4]{
			canEncodeMasks ? a_snapshot.temporalAAMask.srv : nullptr,
			canEncodeMasks ? a_snapshot.normalsWaterMask.srv : nullptr,
			a_snapshot.motionVectors.srv,
			a_snapshot.depth.srv
		};
		ID3D11UnorderedAccessView* uavs[4]{
			prepared_.reactiveMask.uav,
			prepared_.transparencyCompositionMask.uav,
			prepared_.typedDepth.uav,
			nullptr
		};

		context->CSSetShader(encodeInputsShader_, nullptr, 0);
		context->CSSetShaderResources(0, static_cast<UINT>(std::size(srvs)), srvs);
		context->CSSetUnorderedAccessViews(0, static_cast<UINT>(std::size(uavs)), uavs, nullptr);
		context->CSSetConstantBuffers(0, 1, &encodeConstants_);
		context->Dispatch((a_renderWidth + 7) / 8, (a_renderHeight + 7) / 8, 1);

		ID3D11ShaderResourceView* nullSRVs[4]{};
		ID3D11UnorderedAccessView* nullUAVs[4]{};
		ID3D11Buffer* nullBuffer = nullptr;
		context->CSSetShaderResources(0, static_cast<UINT>(std::size(nullSRVs)), nullSRVs);
		context->CSSetUnorderedAccessViews(0, static_cast<UINT>(std::size(nullUAVs)), nullUAVs, nullptr);
		context->CSSetConstantBuffers(0, 1, &nullBuffer);

		context->CSSetShader(oldShader, nullptr, 0);
		context->CSSetShaderResources(0, static_cast<UINT>(std::size(oldSRVs)), oldSRVs);
		context->CSSetUnorderedAccessViews(0, static_cast<UINT>(std::size(oldUAVs)), oldUAVs, nullptr);
		context->CSSetConstantBuffers(0, 1, &oldConstantBuffer);

		SafeRelease(oldShader);
		for (auto*& srv : oldSRVs) {
			SafeRelease(srv);
		}
		for (auto*& uav : oldUAVs) {
			SafeRelease(uav);
		}
		SafeRelease(oldConstantBuffer);

		return true;
	}
}
