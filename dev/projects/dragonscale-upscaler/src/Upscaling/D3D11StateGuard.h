#pragma once

#include <d3d11.h>

#include <array>
#include <cstdint>

namespace DragonScale::Upscaling
{
	class D3D11StateGuard
	{
	public:
		explicit D3D11StateGuard(ID3D11DeviceContext* a_context);
		~D3D11StateGuard();

		D3D11StateGuard(const D3D11StateGuard&) = delete;
		D3D11StateGuard(D3D11StateGuard&&) = delete;
		D3D11StateGuard& operator=(const D3D11StateGuard&) = delete;
		D3D11StateGuard& operator=(D3D11StateGuard&&) = delete;

	private:
		template <class T>
		static void SafeRelease(T*& a_value) noexcept
		{
			if (a_value) {
				a_value->Release();
				a_value = nullptr;
			}
		}

		ID3D11DeviceContext* context_ = nullptr;

		ID3D11InputLayout* inputLayout_ = nullptr;
		D3D11_PRIMITIVE_TOPOLOGY primitiveTopology_ = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
		std::array<ID3D11Buffer*, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT> vertexBuffers_{};
		std::array<UINT, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT> vertexStrides_{};
		std::array<UINT, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT> vertexOffsets_{};
		ID3D11Buffer* indexBuffer_ = nullptr;
		DXGI_FORMAT indexFormat_ = DXGI_FORMAT_UNKNOWN;
		UINT indexOffset_ = 0;

		ID3D11VertexShader* vertexShader_ = nullptr;
		std::array<ID3D11Buffer*, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT> vertexConstantBuffers_{};

		ID3D11PixelShader* pixelShader_ = nullptr;
		std::array<ID3D11ShaderResourceView*, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT> pixelSRVs_{};
		std::array<ID3D11SamplerState*, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT> pixelSamplers_{};
		std::array<ID3D11Buffer*, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT> pixelConstantBuffers_{};

		ID3D11ComputeShader* computeShader_ = nullptr;
		std::array<ID3D11ShaderResourceView*, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT> computeSRVs_{};
		std::array<ID3D11UnorderedAccessView*, D3D11_PS_CS_UAV_REGISTER_COUNT> computeUAVs_{};
		std::array<ID3D11SamplerState*, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT> computeSamplers_{};
		std::array<ID3D11Buffer*, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT> computeConstantBuffers_{};

		std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> renderTargets_{};
		ID3D11DepthStencilView* depthStencil_ = nullptr;
		ID3D11BlendState* blendState_ = nullptr;
		std::array<float, 4> blendFactor_{};
		UINT sampleMask_ = 0;
		ID3D11DepthStencilState* depthStencilState_ = nullptr;
		UINT stencilRef_ = 0;

		ID3D11RasterizerState* rasterizerState_ = nullptr;
		std::array<D3D11_VIEWPORT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> viewports_{};
		UINT viewportCount_ = 0;
		std::array<D3D11_RECT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> scissors_{};
		UINT scissorCount_ = 0;
	};
}
