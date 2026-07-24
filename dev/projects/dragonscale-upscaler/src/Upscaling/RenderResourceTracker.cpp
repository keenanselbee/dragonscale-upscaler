#include "Upscaling/RenderResourceTracker.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace
{
	[[nodiscard]] const char* SourceName(DragonScale::Upscaling::ResourceSource a_source) noexcept
	{
		using DragonScale::Upscaling::ResourceSource;
		switch (a_source) {
		case ResourceSource::kRendererSlot:
			return "renderer";
		case ResourceSource::kTextureDiscoveryFallback:
			return "texture-discovery";
		case ResourceSource::kOwned:
			return "owned";
		case ResourceSource::kImageSpace:
			return "image-space";
		case ResourceSource::kMissing:
		default:
			return "missing";
		}
	}

	void FillDescription(DragonScale::Upscaling::TextureView& a_view) noexcept
	{
		if (!a_view.texture) {
			return;
		}

		D3D11_TEXTURE2D_DESC description{};
		a_view.texture->GetDesc(&description);
		a_view.format = description.Format;
		a_view.width = description.Width;
		a_view.height = description.Height;
		a_view.bindFlags = description.BindFlags;
	}

	[[nodiscard]] DragonScale::Upscaling::TextureView FromRenderTarget(RE::BSGraphics::RenderTargetData& a_target) noexcept
	{
		DragonScale::Upscaling::TextureView view;
		view.texture = a_target.texture;
		view.srv = a_target.SRV;
		view.uav = a_target.UAV;
		view.rtv = a_target.RTV;
		view.source = a_target.texture ? DragonScale::Upscaling::ResourceSource::kRendererSlot : DragonScale::Upscaling::ResourceSource::kMissing;
		FillDescription(view);
		return view;
	}

	[[nodiscard]] DragonScale::Upscaling::TextureView FromRenderTargetCopy(RE::BSGraphics::RenderTargetData& a_target) noexcept
	{
		DragonScale::Upscaling::TextureView view;
		view.texture = a_target.textureCopy;
		view.srv = a_target.SRVCopy;
		view.source = a_target.textureCopy ? DragonScale::Upscaling::ResourceSource::kRendererSlot : DragonScale::Upscaling::ResourceSource::kMissing;
		FillDescription(view);
		return view;
	}

	[[nodiscard]] DragonScale::Upscaling::TextureView FromShaderResourceView(ID3D11ShaderResourceView* a_srv) noexcept
	{
		DragonScale::Upscaling::TextureView view;
		view.srv = a_srv;
		view.source = a_srv ? DragonScale::Upscaling::ResourceSource::kImageSpace : DragonScale::Upscaling::ResourceSource::kMissing;
		if (!a_srv) {
			return view;
		}

		ID3D11Resource* resource = nullptr;
		a_srv->GetResource(&resource);
		if (!resource) {
			return view;
		}

		ID3D11Texture2D* texture = nullptr;
		if (SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&texture)))) {
			view.texture = texture;
			FillDescription(view);
			texture->Release();
		}
		resource->Release();
		return view;
	}

	[[nodiscard]] DragonScale::Upscaling::TextureView FromRenderTargetView(ID3D11RenderTargetView* a_rtv) noexcept
	{
		DragonScale::Upscaling::TextureView view;
		view.rtv = a_rtv;
		view.source = a_rtv ? DragonScale::Upscaling::ResourceSource::kImageSpace : DragonScale::Upscaling::ResourceSource::kMissing;
		if (!a_rtv) {
			return view;
		}

		ID3D11Resource* resource = nullptr;
		a_rtv->GetResource(&resource);
		if (!resource) {
			return view;
		}

		ID3D11Texture2D* texture = nullptr;
		if (SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&texture)))) {
			view.texture = texture;
			FillDescription(view);
			texture->Release();
		}
		resource->Release();
		return view;
	}

	[[nodiscard]] DragonScale::Upscaling::TextureView FromDepthStencil(RE::BSGraphics::DepthStencilData& a_target) noexcept
	{
		DragonScale::Upscaling::TextureView view;
		view.texture = a_target.texture;
		view.srv = a_target.depthSRV;
		view.dsv = a_target.views[0];
		view.source = a_target.texture ? DragonScale::Upscaling::ResourceSource::kRendererSlot : DragonScale::Upscaling::ResourceSource::kMissing;
		FillDescription(view);
		return view;
	}

	[[nodiscard]] std::uintptr_t Mix(std::uintptr_t a_seed, std::uintptr_t a_value) noexcept
	{
		return a_seed ^ (a_value + 0x9E3779B97F4A7C15ull + (a_seed << 6) + (a_seed >> 2));
	}

	[[nodiscard]] std::uintptr_t Signature(const DragonScale::Upscaling::TextureView& a_view) noexcept
	{
		std::uintptr_t value = reinterpret_cast<std::uintptr_t>(a_view.texture);
		value = Mix(value, reinterpret_cast<std::uintptr_t>(a_view.srv));
		value = Mix(value, reinterpret_cast<std::uintptr_t>(a_view.uav));
		value = Mix(value, static_cast<std::uintptr_t>(a_view.format));
		value = Mix(value, static_cast<std::uintptr_t>(a_view.width));
		value = Mix(value, static_cast<std::uintptr_t>(a_view.height));
		value = Mix(value, static_cast<std::uintptr_t>(a_view.bindFlags));
		return value;
	}

	[[nodiscard]] std::uintptr_t Signature(const DragonScale::Upscaling::ViewportSnapshot& a_viewport) noexcept
	{
		if (!a_viewport.valid) {
			return 0;
		}

		auto signature = static_cast<std::uintptr_t>(std::lround(a_viewport.width));
		signature = Mix(signature, static_cast<std::uintptr_t>(std::lround(a_viewport.height)));
		signature = Mix(signature, static_cast<std::uintptr_t>(std::lround(a_viewport.left)));
		signature = Mix(signature, static_cast<std::uintptr_t>(std::lround(a_viewport.top)));
		return signature;
	}

	void LogTexture(std::string_view a_name, const DragonScale::Upscaling::TextureView& a_view)
	{
		logger::info(
			"{} resource: source={}, texture={}, srv={}, uav={}, rtv={}, dsv={}, {}x{}, format={}, bind=0x{:X}",
			a_name,
			SourceName(a_view.source),
			static_cast<const void*>(a_view.texture),
			static_cast<const void*>(a_view.srv),
			static_cast<const void*>(a_view.uav),
			static_cast<const void*>(a_view.rtv),
			static_cast<const void*>(a_view.dsv),
			a_view.width,
			a_view.height,
			static_cast<std::uint32_t>(a_view.format),
			a_view.bindFlags);
	}

	void CapturePipelineResources(DragonScale::Upscaling::ResourceSnapshot& a_snapshot)
	{
		auto* context = a_snapshot.context;
		if (!context) {
			return;
		}

		D3D11_VIEWPORT viewport{};
		UINT viewportCount = 1;
		context->RSGetViewports(&viewportCount, &viewport);
		if (viewportCount > 0) {
			a_snapshot.viewport.valid = true;
			a_snapshot.viewport.left = viewport.TopLeftX;
			a_snapshot.viewport.top = viewport.TopLeftY;
			a_snapshot.viewport.width = viewport.Width;
			a_snapshot.viewport.height = viewport.Height;
			a_snapshot.actualRenderWidth = static_cast<std::uint32_t>((std::max)(0l, std::lround(viewport.Width)));
			a_snapshot.actualRenderHeight = static_cast<std::uint32_t>((std::max)(0l, std::lround(viewport.Height)));
		}

		D3D11_RECT scissor{};
		UINT scissorCount = 1;
		context->RSGetScissorRects(&scissorCount, &scissor);
		if (scissorCount > 0) {
			a_snapshot.scissor.valid = true;
			a_snapshot.scissor.left = scissor.left;
			a_snapshot.scissor.top = scissor.top;
			a_snapshot.scissor.right = scissor.right;
			a_snapshot.scissor.bottom = scissor.bottom;
		}

		ID3D11ShaderResourceView* colorSrv = nullptr;
		context->PSGetShaderResources(0, 1, &colorSrv);
		if (colorSrv) {
			a_snapshot.pipeline.colorSrvRef.Attach(colorSrv);
			a_snapshot.pipeline.color = FromShaderResourceView(colorSrv);
			a_snapshot.pipeline.colorSrvRef.CopyTo(&a_snapshot.pipeline.color.srv);
			if (a_snapshot.pipeline.color.srv) {
				a_snapshot.pipeline.color.srv->Release();
			}
			a_snapshot.pipeline.colorTextureRef = a_snapshot.pipeline.color.texture;
		}

		ID3D11RenderTargetView* outputRtv = nullptr;
		ID3D11DepthStencilView* depthStencil = nullptr;
		context->OMGetRenderTargets(1, &outputRtv, &depthStencil);
		if (outputRtv) {
			a_snapshot.pipeline.outputRtvRef.Attach(outputRtv);
			a_snapshot.pipeline.output = FromRenderTargetView(outputRtv);
			a_snapshot.pipeline.outputRtvRef.CopyTo(&a_snapshot.pipeline.output.rtv);
			if (a_snapshot.pipeline.output.rtv) {
				a_snapshot.pipeline.output.rtv->Release();
			}
			a_snapshot.pipeline.outputTextureRef = a_snapshot.pipeline.output.texture;
		}
		if (depthStencil) {
			a_snapshot.pipeline.depthStencilRef.Attach(depthStencil);
		}
	}
}

namespace DragonScale::Upscaling
{
	RenderResourceTracker& RenderResourceTracker::GetSingleton()
	{
		static RenderResourceTracker singleton;
		return singleton;
	}

	void RenderResourceTracker::Configure(bool a_useTextureDiscoveryFallback) noexcept
	{
		useTextureDiscoveryFallback_ = a_useTextureDiscoveryFallback;
	}

	ResourceSnapshot RenderResourceTracker::Capture(std::uint64_t a_frameID)
	{
		ResourceSnapshot snapshot;
		snapshot.frameID = a_frameID;

		const auto renderer = RE::BSGraphics::Renderer::GetSingleton();
		if (!renderer) {
			LogSnapshot(snapshot);
			return snapshot;
		}

		auto& runtimeData = renderer->GetRuntimeData();
		auto& depthData = renderer->GetDepthStencilData();

		snapshot.device = reinterpret_cast<ID3D11Device*>(runtimeData.forwarder);
		snapshot.context = reinterpret_cast<ID3D11DeviceContext*>(runtimeData.context);
		snapshot.color = FromRenderTarget(runtimeData.renderTargets[RE::RENDER_TARGETS::kMAIN]);
		snapshot.output = snapshot.color;
		snapshot.rendererColor = snapshot.color;
		snapshot.rendererOutput = snapshot.output;
		snapshot.depth = FromDepthStencil(depthData.depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN]);
		snapshot.depthCopy = FromDepthStencil(depthData.depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN_COPY]);
		snapshot.mainCopy = FromRenderTarget(runtimeData.renderTargets[RE::RENDER_TARGETS::kMAIN_COPY]);
		snapshot.motionVectors = FromRenderTarget(runtimeData.renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR]);
		snapshot.temporalAAMask = FromRenderTarget(runtimeData.renderTargets[RE::RENDER_TARGETS::kTEMPORAL_AA_MASK]);
		snapshot.normalsWaterMask = FromRenderTarget(runtimeData.renderTargets[RE::RENDER_TARGETS::kNORMAL_TAAMASK_SSRMASK]);
		snapshot.refractionNormals = FromRenderTarget(runtimeData.renderTargets[RE::RENDER_TARGETS::kREFRACTION_NORMALS]);
		snapshot.refractionNormalsCopy = FromRenderTargetCopy(runtimeData.renderTargets[RE::RENDER_TARGETS::kREFRACTION_NORMALS]);
		snapshot.saoCameraZ = FromRenderTarget(runtimeData.renderTargets[RE::RENDER_TARGETS::kSAO_CAMERAZ]);
		snapshot.underwaterMask = FromRenderTarget(runtimeData.renderTargets[RE::RENDER_TARGETS::kUNDERWATER_MASK]);
		snapshot.underwaterMaskCopy = FromRenderTargetCopy(runtimeData.renderTargets[RE::RENDER_TARGETS::kUNDERWATER_MASK]);
		CapturePipelineResources(snapshot);

		const auto liveMinimumWidth = snapshot.actualRenderWidth != 0 ? snapshot.actualRenderWidth : snapshot.color.width;
		const auto liveMinimumHeight = snapshot.actualRenderHeight != 0 ? snapshot.actualRenderHeight : snapshot.color.height;
		if (snapshot.pipeline.HasLiveResources(liveMinimumWidth, liveMinimumHeight)) {
			snapshot.color = snapshot.pipeline.color;
			snapshot.output = snapshot.pipeline.output;
			snapshot.activeSource = ResourceSource::kImageSpace;
		} else {
			snapshot.activeSource = ResourceSource::kRendererSlot;
		}

		if (useTextureDiscoveryFallback_ && !snapshot.HasRequiredInputs() && !textureDiscoveryNoticeLogged_) {
			logger::warn("Texture discovery fallback is enabled, but the first pass uses renderer-slot discovery only; missing required resources will be logged explicitly");
			textureDiscoveryNoticeLogged_ = true;
		}

		LogSnapshot(snapshot);
		return snapshot;
	}

	void RenderResourceTracker::ResetLogState() noexcept
	{
		lastSignature_ = 0;
		lastSnapshotLogTime_ = {};
		textureDiscoveryNoticeLogged_ = false;
	}

	void RenderResourceTracker::LogSnapshot(const ResourceSnapshot& a_snapshot)
	{
		std::uintptr_t signature = reinterpret_cast<std::uintptr_t>(a_snapshot.device);
		signature = Mix(signature, reinterpret_cast<std::uintptr_t>(a_snapshot.context));
		signature = Mix(signature, Signature(a_snapshot.rendererColor));
		signature = Mix(signature, Signature(a_snapshot.rendererOutput));
		signature = Mix(signature, Signature(a_snapshot.color));
		signature = Mix(signature, Signature(a_snapshot.depth));
		signature = Mix(signature, Signature(a_snapshot.depthCopy));
		signature = Mix(signature, Signature(a_snapshot.mainCopy));
		signature = Mix(signature, Signature(a_snapshot.motionVectors));
		signature = Mix(signature, Signature(a_snapshot.temporalAAMask));
		signature = Mix(signature, Signature(a_snapshot.normalsWaterMask));
		signature = Mix(signature, Signature(a_snapshot.refractionNormals));
		signature = Mix(signature, Signature(a_snapshot.refractionNormalsCopy));
		signature = Mix(signature, Signature(a_snapshot.saoCameraZ));
		signature = Mix(signature, Signature(a_snapshot.underwaterMask));
		signature = Mix(signature, Signature(a_snapshot.underwaterMaskCopy));
		signature = Mix(signature, Signature(a_snapshot.pipeline.color));
		signature = Mix(signature, Signature(a_snapshot.pipeline.output));
		signature = Mix(signature, Signature(a_snapshot.viewport));
		signature = Mix(signature, static_cast<std::uintptr_t>(a_snapshot.activeSource));

		if (signature == lastSignature_) {
			return;
		}

		const auto now = std::chrono::steady_clock::now();
		if (lastSnapshotLogTime_.time_since_epoch().count() != 0 &&
			std::chrono::duration_cast<std::chrono::milliseconds>(now - lastSnapshotLogTime_).count() < 1000) {
			lastSignature_ = signature;
			return;
		}

		lastSignature_ = signature;
		lastSnapshotLogTime_ = now;
		logger::info(
			"Render resource snapshot changed: frame={}, requiredReady={}, device={}, context={}, source={}, actualExtent={}x{}, viewport={} {}x{}+{},{}",
			a_snapshot.frameID,
			a_snapshot.HasRequiredInputs(),
			static_cast<const void*>(a_snapshot.device),
			static_cast<const void*>(a_snapshot.context),
			SourceName(a_snapshot.activeSource),
			a_snapshot.actualRenderWidth,
			a_snapshot.actualRenderHeight,
			a_snapshot.viewport.valid,
			a_snapshot.viewport.width,
			a_snapshot.viewport.height,
			a_snapshot.viewport.left,
			a_snapshot.viewport.top);
		LogTexture("Renderer-slot color", a_snapshot.rendererColor);
		LogTexture("Renderer-slot output", a_snapshot.rendererOutput);
		LogTexture("Color", a_snapshot.color);
		LogTexture("Image-space color", a_snapshot.pipeline.color);
		LogTexture("Output", a_snapshot.output);
		LogTexture("Image-space output", a_snapshot.pipeline.output);
		LogTexture("Depth", a_snapshot.depth);
		LogTexture("Depth copy", a_snapshot.depthCopy);
		LogTexture("Main copy", a_snapshot.mainCopy);
		LogTexture("Motion vectors", a_snapshot.motionVectors);
		LogTexture("Temporal AA mask", a_snapshot.temporalAAMask);
		LogTexture("Normals/water mask", a_snapshot.normalsWaterMask);
		LogTexture("Refraction normals", a_snapshot.refractionNormals);
		LogTexture("Refraction normals copy", a_snapshot.refractionNormalsCopy);
		LogTexture("SAO camera Z", a_snapshot.saoCameraZ);
		LogTexture("Underwater mask", a_snapshot.underwaterMask);
		LogTexture("Underwater mask copy", a_snapshot.underwaterMaskCopy);
	}
}
