#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

struct ID3D11BlendState;
struct ID3D11Buffer;
struct ID3D11DepthStencilState;
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11InputLayout;
struct ID3D11PixelShader;
struct ID3D11RasterizerState;
struct ID3D11RenderTargetView;
struct ID3D11VertexShader;

namespace DragonScale::Diagnostics
{
	class OverlayRenderer
	{
	public:
		[[nodiscard]] static OverlayRenderer& GetSingleton();

		void Render();
		void Shutdown() noexcept;

	private:
		struct Vertex
		{
			float x = 0.0f;
			float y = 0.0f;
			float r = 1.0f;
			float g = 1.0f;
			float b = 1.0f;
			float a = 1.0f;
		};

		OverlayRenderer() = default;

		[[nodiscard]] bool EnsureResources(ID3D11Device* a_device);
		void DisableAfterFailure();
		void BuildVertices(std::uint32_t a_width, std::uint32_t a_height);
		void AppendText(std::string_view a_text, float a_x, float a_y, float a_pixelSize, const std::array<float, 4>& a_color);
		void AppendGlyph(char a_char, float a_x, float a_y, float a_pixelSize, const std::array<float, 4>& a_color);
		void AppendPixel(float a_x, float a_y, float a_size, const std::array<float, 4>& a_color);
		void AppendQuad(float a_left, float a_top, float a_right, float a_bottom, const std::array<float, 4>& a_color);

		ID3D11Device* device_ = nullptr;
		ID3D11VertexShader* vertexShader_ = nullptr;
		ID3D11PixelShader* pixelShader_ = nullptr;
		ID3D11InputLayout* inputLayout_ = nullptr;
		ID3D11Buffer* vertexBuffer_ = nullptr;
		ID3D11BlendState* blendState_ = nullptr;
		ID3D11DepthStencilState* depthStencilState_ = nullptr;
		ID3D11RasterizerState* rasterizerState_ = nullptr;
		std::vector<Vertex> vertices_;
		std::size_t vertexCapacity_ = 131072;
		std::uint32_t targetWidth_ = 1;
		std::uint32_t targetHeight_ = 1;
		bool disabledAfterFailure_ = false;
		bool failureLogged_ = false;
	};
}
