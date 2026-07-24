#pragma once

#include "Upscaling/UpscaleTypes.h"

struct ID3D11BlendState;
struct ID3D11Buffer;
struct ID3D11DepthStencilState;
struct ID3D11PixelShader;
struct ID3D11RasterizerState;
struct ID3D11SamplerState;
struct ID3D11VertexShader;

namespace DragonScale::Upscaling
{
	struct AuxiliaryValidationResult
	{
		bool enabled = false;
		bool requiredReady = false;
		bool underwaterReady = false;
		std::string reason;
	};

	class AuxiliaryUpscaler
	{
	public:
		[[nodiscard]] static AuxiliaryUpscaler& GetSingleton();

		[[nodiscard]] AuxiliaryValidationResult Validate(
			const ResourceSnapshot& a_snapshot,
			std::uint32_t a_displayWidth,
			std::uint32_t a_displayHeight) const;
		[[nodiscard]] bool Repair(
			const ResourceSnapshot& a_snapshot,
			std::uint32_t a_displayWidth,
			std::uint32_t a_displayHeight,
			std::uint32_t a_renderWidth,
			std::uint32_t a_renderHeight,
			const JitterOffset& a_jitter);
		void Release() noexcept;

	private:
		AuxiliaryUpscaler() = default;

		struct AuxConstants
		{
			float displayWidth = 0.0f;
			float displayHeight = 0.0f;
			float renderWidth = 0.0f;
			float renderHeight = 0.0f;
			float jitterX = 0.0f;
			float jitterY = 0.0f;
			float targetWidth = 0.0f;
			float targetHeight = 0.0f;
		};

		template <class T>
		void SafeRelease(T*& a_value) noexcept
		{
			if (a_value) {
				a_value->Release();
				a_value = nullptr;
			}
		}

		[[nodiscard]] bool EnsurePipeline(ID3D11Device* a_device);
		[[nodiscard]] bool EnsureCopyResource(ID3D11Device* a_device, const TextureView& a_source, TextureView& a_copy);
		[[nodiscard]] const TextureView* PrepareCopy(ID3D11DeviceContext* a_context, const TextureView& a_source, const TextureView& a_sourceCopy, TextureView& a_ownedCopy);
		[[nodiscard]] bool RepairDepthRefraction(
			ID3D11DeviceContext* a_context,
			const ResourceSnapshot& a_snapshot,
			const TextureView& a_refractionSource,
			const AuxConstants& a_constants);
		[[nodiscard]] bool RepairUnderwaterMask(
			ID3D11DeviceContext* a_context,
			const ResourceSnapshot& a_snapshot,
			const TextureView& a_underwaterSource,
			const AuxConstants& a_constants);

		ID3D11Device* device_ = nullptr;
		ID3D11VertexShader* fullscreenVertexShader_ = nullptr;
		ID3D11PixelShader* depthRefractionPixelShader_ = nullptr;
		ID3D11PixelShader* underwaterMaskPixelShader_ = nullptr;
		ID3D11Buffer* constantBuffer_ = nullptr;
		ID3D11SamplerState* linearSampler_ = nullptr;
		ID3D11BlendState* opaqueBlendState_ = nullptr;
		ID3D11RasterizerState* rasterizerState_ = nullptr;
		ID3D11DepthStencilState* depthWriteState_ = nullptr;
		TextureView refractionCopy_;
		TextureView underwaterCopy_;
	};
}
