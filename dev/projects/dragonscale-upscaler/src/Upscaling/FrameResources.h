#pragma once

#include "Upscaling/UpscaleTypes.h"

struct ID3D11Buffer;
struct ID3D11ComputeShader;

namespace DragonScale::Upscaling
{
	class FrameResources
	{
	public:
		FrameResources() = default;
		~FrameResources();

		FrameResources(const FrameResources&) = delete;
		FrameResources(FrameResources&&) = delete;
		FrameResources& operator=(const FrameResources&) = delete;
		FrameResources& operator=(FrameResources&&) = delete;

		[[nodiscard]] bool Prepare(
			const ResourceSnapshot& a_snapshot,
			std::uint32_t a_displayWidth,
			std::uint32_t a_displayHeight,
			std::uint32_t a_renderWidth,
			std::uint32_t a_renderHeight,
			bool a_encodeReactiveMasks);
		[[nodiscard]] bool PrepareCopyThrough(
			const ResourceSnapshot& a_snapshot,
			std::uint32_t a_displayWidth,
			std::uint32_t a_displayHeight,
			std::uint32_t a_renderWidth,
			std::uint32_t a_renderHeight);
		[[nodiscard]] const PreparedFrameResources& GetPrepared() const noexcept;
		[[nodiscard]] bool RecreatedThisFrame() const noexcept;
		[[nodiscard]] bool CopyInputToOutput(std::uint32_t a_width, std::uint32_t a_height);
		[[nodiscard]] bool ScaleInputToOutput(std::uint32_t a_displayWidth, std::uint32_t a_displayHeight, std::uint32_t a_renderWidth, std::uint32_t a_renderHeight);
		[[nodiscard]] bool PreserveOutputAlpha(std::uint32_t a_displayWidth, std::uint32_t a_displayHeight, std::uint32_t a_renderWidth, std::uint32_t a_renderHeight);
		void CopyOutputToGameTarget(const ResourceSnapshot& a_snapshot);
		void Release() noexcept;

	private:
		struct EncodeConstants
		{
			float renderWidth = 0.0f;
			float renderHeight = 0.0f;
			float encodeMasks = 0.0f;
			float pad0 = 0.0f;
		};

		struct AlphaPreserveConstants
		{
			float displayWidth = 0.0f;
			float displayHeight = 0.0f;
			float renderWidth = 0.0f;
			float renderHeight = 0.0f;
		};

		template <class T>
		void SafeRelease(T*& a_value) noexcept
		{
			if (a_value) {
				a_value->Release();
				a_value = nullptr;
			}
		}

		[[nodiscard]] bool EnsureResources(
			const ResourceSnapshot& a_snapshot,
			std::uint32_t a_displayWidth,
			std::uint32_t a_displayHeight,
			std::uint32_t a_renderWidth,
			std::uint32_t a_renderHeight);
		[[nodiscard]] bool EnsureCopyThroughResources(
			const ResourceSnapshot& a_snapshot,
			std::uint32_t a_displayWidth,
			std::uint32_t a_displayHeight,
			std::uint32_t a_renderWidth,
			std::uint32_t a_renderHeight);
		[[nodiscard]] bool EnsureEncodeResources(ID3D11Device* a_device);
		[[nodiscard]] bool CreateColorResource(ID3D11Device* a_device, DXGI_FORMAT a_format, std::uint32_t a_width, std::uint32_t a_height, bool a_createUav, TextureView& a_view);
		[[nodiscard]] bool CreateMaskResource(ID3D11Device* a_device, TextureView& a_view);
		[[nodiscard]] bool CreateDepthResource(ID3D11Device* a_device, std::uint32_t a_width, std::uint32_t a_height, TextureView& a_view);
		[[nodiscard]] bool CopyRenderRegion(ID3D11DeviceContext* a_context, ID3D11Texture2D* a_destination, const TextureView& a_source, std::uint32_t a_width, std::uint32_t a_height);
		[[nodiscard]] bool EncodeInputs(const ResourceSnapshot& a_snapshot, std::uint32_t a_renderWidth, std::uint32_t a_renderHeight, bool a_encodeReactiveMasks);
		PreparedFrameResources prepared_;
		ID3D11Device* device_ = nullptr;
		ID3D11ComputeShader* encodeInputsShader_ = nullptr;
		ID3D11ComputeShader* scaleCopyShader_ = nullptr;
		ID3D11ComputeShader* preserveAlphaShader_ = nullptr;
		ID3D11Buffer* encodeConstants_ = nullptr;
		ID3D11Buffer* preserveAlphaConstants_ = nullptr;
		DXGI_FORMAT colorFormat_ = DXGI_FORMAT_UNKNOWN;
		DXGI_FORMAT motionVectorFormat_ = DXGI_FORMAT_UNKNOWN;
		std::uint32_t displayWidth_ = 0;
		std::uint32_t displayHeight_ = 0;
		std::uint32_t renderWidth_ = 0;
		std::uint32_t renderHeight_ = 0;
		bool recreatedThisFrame_ = false;
		bool alphaPreserveLogged_ = false;
	};
}
