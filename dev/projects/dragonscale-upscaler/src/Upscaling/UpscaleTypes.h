#pragma once

#include "Config.h"
#include "Upscaling/Jitter.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>

namespace DragonScale::Upscaling
{
	enum class ResourceSource : std::uint32_t
	{
		kMissing = 0,
		kRendererSlot = 1,
		kTextureDiscoveryFallback = 2,
		kOwned = 3,
		kImageSpace = 4
	};

	struct TextureView
	{
		ID3D11Texture2D* texture = nullptr;
		ID3D11ShaderResourceView* srv = nullptr;
		ID3D11UnorderedAccessView* uav = nullptr;
		ID3D11RenderTargetView* rtv = nullptr;
		ID3D11DepthStencilView* dsv = nullptr;
		DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
		std::uint32_t width = 0;
		std::uint32_t height = 0;
		std::uint32_t bindFlags = 0;
		ResourceSource source = ResourceSource::kMissing;

		[[nodiscard]] bool HasTexture() const noexcept { return texture != nullptr && width > 0 && height > 0; }
		[[nodiscard]] bool HasSRV() const noexcept { return HasTexture() && srv != nullptr; }
		[[nodiscard]] bool HasUAV() const noexcept { return HasTexture() && uav != nullptr; }
		[[nodiscard]] bool HasRTV() const noexcept { return HasTexture() && rtv != nullptr; }
	};

	struct ViewportSnapshot
	{
		bool valid = false;
		float left = 0.0f;
		float top = 0.0f;
		float width = 0.0f;
		float height = 0.0f;
	};

	struct ScissorSnapshot
	{
		bool valid = false;
		long left = 0;
		long top = 0;
		long right = 0;
		long bottom = 0;
	};

	struct PipelineResourceCapture
	{
		TextureView color;
		TextureView output;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> colorTextureRef;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> colorSrvRef;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> outputTextureRef;
		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> outputRtvRef;
		Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depthStencilRef;

		[[nodiscard]] bool HasLiveResources(std::uint32_t a_minimumWidth, std::uint32_t a_minimumHeight) const noexcept
		{
			return color.HasSRV() &&
			       output.HasTexture() &&
			       color.width >= a_minimumWidth &&
			       color.height >= a_minimumHeight &&
			       output.width >= a_minimumWidth &&
			       output.height >= a_minimumHeight;
		}
	};

	struct ResourceSnapshot
	{
		ID3D11Device* device = nullptr;
		ID3D11DeviceContext* context = nullptr;
		TextureView rendererColor;
		TextureView rendererOutput;
		TextureView color;
		TextureView output;
		TextureView depth;
		TextureView mainCopy;
		TextureView depthCopy;
		TextureView motionVectors;
		TextureView temporalAAMask;
		TextureView normalsWaterMask;
		TextureView refractionNormals;
		TextureView refractionNormalsCopy;
		TextureView saoCameraZ;
		TextureView underwaterMask;
		TextureView underwaterMaskCopy;
		PipelineResourceCapture pipeline;
		ViewportSnapshot viewport;
		ScissorSnapshot scissor;
		ResourceSource activeSource = ResourceSource::kRendererSlot;
		std::uint32_t actualRenderWidth = 0;
		std::uint32_t actualRenderHeight = 0;
		std::uint64_t frameID = 0;

		[[nodiscard]] bool HasRequiredInputs() const noexcept
		{
			return device != nullptr &&
			       context != nullptr &&
			       color.HasTexture() &&
			       output.HasTexture() &&
			       depth.HasSRV() &&
			       motionVectors.HasSRV();
		}
	};

	struct SourceExtentValidationResult
	{
		bool valid = false;
		std::uint32_t width = 0;
		std::uint32_t height = 0;
		std::string reason;
	};

	struct PreparedFrameResources
	{
		TextureView inputColor;
		TextureView outputColor;
		TextureView typedDepth;
		TextureView motionVectors;
		TextureView reactiveMask;
		TextureView transparencyCompositionMask;
	};

	struct UpscaleInputs
	{
		ID3D11Device* device = nullptr;
		ID3D11DeviceContext* context = nullptr;
		TextureView color;
		TextureView output;
		TextureView depth;
		TextureView motionVectors;
		TextureView reactiveMask;
		TextureView transparencyCompositionMask;
		std::uint32_t displayWidth = 0;
		std::uint32_t displayHeight = 0;
		std::uint32_t renderWidth = 0;
		std::uint32_t renderHeight = 0;
		JitterOffset jitter;
		QualityMode quality = QualityMode::kQuality;
		float sharpness = 0.0f;
		bool reset = false;
	};
}
