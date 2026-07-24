#include "Diagnostics/OverlayRenderer.h"

#include "Diagnostics/Diagnostics.h"

#include <d3d11.h>
#include <d3dcompiler.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace
{
	template <class T>
	void SafeRelease(T*& a_value) noexcept
	{
		if (a_value) {
			a_value->Release();
			a_value = nullptr;
		}
	}

	[[nodiscard]] bool GetRenderTargetSize(ID3D11RenderTargetView* a_renderTarget, std::uint32_t& a_width, std::uint32_t& a_height) noexcept
	{
		if (!a_renderTarget) {
			return false;
		}

		ID3D11Resource* resource = nullptr;
		a_renderTarget->GetResource(&resource);
		if (!resource) {
			return false;
		}

		ID3D11Texture2D* texture = nullptr;
		const auto result = resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&texture));
		SafeRelease(resource);
		if (FAILED(result) || !texture) {
			return false;
		}

		D3D11_TEXTURE2D_DESC description{};
		texture->GetDesc(&description);
		SafeRelease(texture);

		a_width = (std::max)(description.Width, 1u);
		a_height = (std::max)(description.Height, 1u);
		return true;
	}

	[[nodiscard]] std::array<std::uint8_t, 7> GetGlyphRows(char a_char) noexcept
	{
		switch (a_char) {
		case 'A': return { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 };
		case 'B': return { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E };
		case 'C': return { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E };
		case 'D': return { 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E };
		case 'E': return { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F };
		case 'F': return { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 };
		case 'G': return { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F };
		case 'H': return { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 };
		case 'I': return { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E };
		case 'J': return { 0x01, 0x01, 0x01, 0x01, 0x11, 0x11, 0x0E };
		case 'K': return { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 };
		case 'L': return { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F };
		case 'M': return { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 };
		case 'N': return { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 };
		case 'O': return { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E };
		case 'P': return { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 };
		case 'Q': return { 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D };
		case 'R': return { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 };
		case 'S': return { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E };
		case 'T': return { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 };
		case 'U': return { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E };
		case 'V': return { 0x11, 0x11, 0x11, 0x11, 0x0A, 0x0A, 0x04 };
		case 'W': return { 0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A };
		case 'X': return { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 };
		case 'Y': return { 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04 };
		case 'Z': return { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F };
		case '0': return { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E };
		case '1': return { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E };
		case '2': return { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F };
		case '3': return { 0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E };
		case '4': return { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 };
		case '5': return { 0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E };
		case '6': return { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E };
		case '7': return { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 };
		case '8': return { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E };
		case '9': return { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C };
		case ':': return { 0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00 };
		case '.': return { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C };
		case '-': return { 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00 };
		case '>': return { 0x10, 0x08, 0x04, 0x02, 0x04, 0x08, 0x10 };
		case '|': return { 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 };
		case '/': return { 0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10 };
		case '%': return { 0x19, 0x19, 0x02, 0x04, 0x08, 0x13, 0x13 };
		case ' ': return { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
		default: return { 0x1F, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04 };
		}
	}
}

namespace DragonScale::Diagnostics
{
	OverlayRenderer& OverlayRenderer::GetSingleton()
	{
		static OverlayRenderer singleton;
		return singleton;
	}

	void OverlayRenderer::Render()
	{
		auto& diagnostics = DiagnosticsManager::GetSingleton();
		if (!diagnostics.OverlayEnabled() || disabledAfterFailure_) {
			return;
		}

		const auto renderer = RE::BSGraphics::Renderer::GetSingleton();
		if (!renderer) {
			return;
		}

		auto& runtimeData = renderer->GetRuntimeData();
		auto* device = reinterpret_cast<ID3D11Device*>(runtimeData.forwarder);
		auto* context = reinterpret_cast<ID3D11DeviceContext*>(runtimeData.context);
		if (!device || !context) {
			return;
		}

		if (!EnsureResources(device)) {
			return;
		}

		ID3D11RenderTargetView* renderTarget = nullptr;
		ID3D11DepthStencilView* currentDepthStencil = nullptr;
		context->OMGetRenderTargets(1, &renderTarget, &currentDepthStencil);
		SafeRelease(currentDepthStencil);
		if (!renderTarget) {
			return;
		}

		std::uint32_t width = 0;
		std::uint32_t height = 0;
		if (!GetRenderTargetSize(renderTarget, width, height)) {
			SafeRelease(renderTarget);
			return;
		}

		BuildVertices(width, height);
		if (vertices_.empty()) {
			SafeRelease(renderTarget);
			return;
		}

		D3D11_MAPPED_SUBRESOURCE mapped{};
		if (FAILED(context->Map(vertexBuffer_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
			SafeRelease(renderTarget);
			DisableAfterFailure();
			return;
		}

		std::memcpy(mapped.pData, vertices_.data(), vertices_.size() * sizeof(Vertex));
		context->Unmap(vertexBuffer_, 0);

		ID3D11InputLayout* oldInputLayout = nullptr;
		ID3D11Buffer* oldVertexBuffer = nullptr;
		UINT oldStride = 0;
		UINT oldOffset = 0;
		D3D11_PRIMITIVE_TOPOLOGY oldTopology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
		ID3D11VertexShader* oldVertexShader = nullptr;
		ID3D11PixelShader* oldPixelShader = nullptr;
		ID3D11BlendState* oldBlendState = nullptr;
		FLOAT oldBlendFactor[4]{};
		UINT oldSampleMask = 0;
		ID3D11DepthStencilState* oldDepthStencilState = nullptr;
		UINT oldStencilRef = 0;
		ID3D11RasterizerState* oldRasterizerState = nullptr;
		ID3D11RenderTargetView* oldRenderTargets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
		ID3D11DepthStencilView* oldDepthStencilView = nullptr;
		UINT oldViewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
		D3D11_VIEWPORT oldViewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
		UINT oldScissorCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
		D3D11_RECT oldScissors[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};

		context->IAGetInputLayout(&oldInputLayout);
		context->IAGetVertexBuffers(0, 1, &oldVertexBuffer, &oldStride, &oldOffset);
		context->IAGetPrimitiveTopology(&oldTopology);
		context->VSGetShader(&oldVertexShader, nullptr, nullptr);
		context->PSGetShader(&oldPixelShader, nullptr, nullptr);
		context->OMGetBlendState(&oldBlendState, oldBlendFactor, &oldSampleMask);
		context->OMGetDepthStencilState(&oldDepthStencilState, &oldStencilRef);
		context->RSGetState(&oldRasterizerState);
		context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, oldRenderTargets, &oldDepthStencilView);
		context->RSGetViewports(&oldViewportCount, oldViewports);
		context->RSGetScissorRects(&oldScissorCount, oldScissors);

		D3D11_VIEWPORT viewport{};
		viewport.Width = static_cast<float>(width);
		viewport.Height = static_cast<float>(height);
		viewport.MaxDepth = 1.0f;

		const UINT stride = sizeof(Vertex);
		const UINT offset = 0;
		context->OMSetRenderTargets(1, &renderTarget, nullptr);
		context->OMSetBlendState(blendState_, nullptr, 0xFFFFFFFF);
		context->OMSetDepthStencilState(depthStencilState_, 0);
		context->RSSetState(rasterizerState_);
		context->RSSetViewports(1, &viewport);
		context->IASetInputLayout(inputLayout_);
		context->IASetVertexBuffers(0, 1, &vertexBuffer_, &stride, &offset);
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		context->VSSetShader(vertexShader_, nullptr, 0);
		context->PSSetShader(pixelShader_, nullptr, 0);
		context->Draw(static_cast<UINT>(vertices_.size()), 0);

		context->IASetInputLayout(oldInputLayout);
		context->IASetVertexBuffers(0, 1, &oldVertexBuffer, &oldStride, &oldOffset);
		context->IASetPrimitiveTopology(oldTopology);
		context->VSSetShader(oldVertexShader, nullptr, 0);
		context->PSSetShader(oldPixelShader, nullptr, 0);
		context->OMSetBlendState(oldBlendState, oldBlendFactor, oldSampleMask);
		context->OMSetDepthStencilState(oldDepthStencilState, oldStencilRef);
		context->RSSetState(oldRasterizerState);
		context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, oldRenderTargets, oldDepthStencilView);
		context->RSSetViewports(oldViewportCount, oldViewports);
		context->RSSetScissorRects(oldScissorCount, oldScissors);

		SafeRelease(oldInputLayout);
		SafeRelease(oldVertexBuffer);
		SafeRelease(oldVertexShader);
		SafeRelease(oldPixelShader);
		SafeRelease(oldBlendState);
		SafeRelease(oldDepthStencilState);
		SafeRelease(oldRasterizerState);
		for (auto*& oldRenderTarget : oldRenderTargets) {
			SafeRelease(oldRenderTarget);
		}
		SafeRelease(oldDepthStencilView);
		SafeRelease(renderTarget);
	}

	void OverlayRenderer::Shutdown() noexcept
	{
		SafeRelease(vertexShader_);
		SafeRelease(pixelShader_);
		SafeRelease(inputLayout_);
		SafeRelease(vertexBuffer_);
		SafeRelease(blendState_);
		SafeRelease(depthStencilState_);
		SafeRelease(rasterizerState_);
		device_ = nullptr;
		disabledAfterFailure_ = false;
	}

	bool OverlayRenderer::EnsureResources(ID3D11Device* a_device)
	{
		if (!a_device) {
			return false;
		}

		if (device_ == a_device && vertexBuffer_) {
			return true;
		}

		Shutdown();
		device_ = a_device;

		constexpr const char* vertexShaderSource = R"(
struct VSInput { float2 position : POSITION; float4 color : COLOR0; };
struct VSOutput { float4 position : SV_POSITION; float4 color : COLOR0; };
VSOutput main(VSInput input)
{
	VSOutput output;
	output.position = float4(input.position, 0.0f, 1.0f);
	output.color = input.color;
	return output;
}
)";

		constexpr const char* pixelShaderSource = R"(
struct PSInput { float4 position : SV_POSITION; float4 color : COLOR0; };
float4 main(PSInput input) : SV_Target
{
	return input.color;
}
)";

		ID3DBlob* vertexShaderBlob = nullptr;
		ID3DBlob* pixelShaderBlob = nullptr;
		ID3DBlob* errorBlob = nullptr;

		if (FAILED(D3DCompile(vertexShaderSource, std::strlen(vertexShaderSource), nullptr, nullptr, nullptr, "main", "vs_4_0", 0, 0, &vertexShaderBlob, &errorBlob)) ||
			FAILED(D3DCompile(pixelShaderSource, std::strlen(pixelShaderSource), nullptr, nullptr, nullptr, "main", "ps_4_0", 0, 0, &pixelShaderBlob, &errorBlob))) {
			SafeRelease(vertexShaderBlob);
			SafeRelease(pixelShaderBlob);
			SafeRelease(errorBlob);
			DisableAfterFailure();
			return false;
		}

		const D3D11_INPUT_ELEMENT_DESC inputElements[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 }
		};

		const auto shaderResult =
			a_device->CreateVertexShader(vertexShaderBlob->GetBufferPointer(), vertexShaderBlob->GetBufferSize(), nullptr, &vertexShader_) |
			a_device->CreatePixelShader(pixelShaderBlob->GetBufferPointer(), pixelShaderBlob->GetBufferSize(), nullptr, &pixelShader_) |
			a_device->CreateInputLayout(inputElements, static_cast<UINT>(std::size(inputElements)), vertexShaderBlob->GetBufferPointer(), vertexShaderBlob->GetBufferSize(), &inputLayout_);

		SafeRelease(vertexShaderBlob);
		SafeRelease(pixelShaderBlob);
		SafeRelease(errorBlob);

		D3D11_BUFFER_DESC vertexBufferDescription{};
		vertexBufferDescription.ByteWidth = static_cast<UINT>(vertexCapacity_ * sizeof(Vertex));
		vertexBufferDescription.Usage = D3D11_USAGE_DYNAMIC;
		vertexBufferDescription.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		vertexBufferDescription.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

		D3D11_BLEND_DESC blendDescription{};
		blendDescription.RenderTarget[0].BlendEnable = TRUE;
		blendDescription.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
		blendDescription.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		blendDescription.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		blendDescription.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
		blendDescription.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
		blendDescription.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		blendDescription.RenderTarget[0].RenderTargetWriteMask =
			D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE;

		D3D11_DEPTH_STENCIL_DESC depthDescription{};
		depthDescription.DepthEnable = FALSE;
		depthDescription.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;

		D3D11_RASTERIZER_DESC rasterizerDescription{};
		rasterizerDescription.FillMode = D3D11_FILL_SOLID;
		rasterizerDescription.CullMode = D3D11_CULL_NONE;
		rasterizerDescription.DepthClipEnable = TRUE;

		if (FAILED(shaderResult) ||
			FAILED(a_device->CreateBuffer(&vertexBufferDescription, nullptr, &vertexBuffer_)) ||
			FAILED(a_device->CreateBlendState(&blendDescription, &blendState_)) ||
			FAILED(a_device->CreateDepthStencilState(&depthDescription, &depthStencilState_)) ||
			FAILED(a_device->CreateRasterizerState(&rasterizerDescription, &rasterizerState_))) {
			DisableAfterFailure();
			return false;
		}

		vertices_.reserve(vertexCapacity_);
		return true;
	}

	void OverlayRenderer::DisableAfterFailure()
	{
		Shutdown();
		disabledAfterFailure_ = true;
		if (!failureLogged_) {
			logger::warn("Diagnostics overlay disabled after a D3D11 resource failure");
			failureLogged_ = true;
		}
	}

	void OverlayRenderer::BuildVertices(std::uint32_t a_width, std::uint32_t a_height)
	{
		targetWidth_ = (std::max)(a_width, 1u);
		targetHeight_ = (std::max)(a_height, 1u);
		vertices_.clear();

		const auto lines = DiagnosticsManager::GetSingleton().BuildLines();
		const auto pixelSize = (std::max)(1.0f, 2.0f * DiagnosticsManager::GetSingleton().OverlayScale());
		const auto lineHeight = pixelSize * 10.0f;
		const float startX = 16.0f;
		const float startY = 16.0f;

		const std::array<float, 4> shadow{ 0.0f, 0.0f, 0.0f, 0.65f };
		const std::array<float, 4> white{ 0.92f, 0.96f, 1.0f, 0.95f };
		const std::array<float, 4> green{ 0.45f, 1.0f, 0.62f, 0.95f };
		const std::array<float, 4> amber{ 1.0f, 0.78f, 0.35f, 0.95f };
		const std::array<float, 4> red{ 1.0f, 0.42f, 0.38f, 0.95f };

		auto statusColor = white;
		switch (DiagnosticsManager::GetSingleton().GetStats().status) {
		case Status::kRunning:
			statusColor = green;
			break;
		case Status::kFailed:
		case Status::kNativeRestored:
			statusColor = red;
			break;
		case Status::kCreatingContext:
		case Status::kWaiting:
			statusColor = amber;
			break;
		default:
			break;
		}

		for (std::size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
			const auto y = startY + static_cast<float>(lineIndex) * lineHeight;
			const auto& color = lineIndex == 0 ? statusColor : white;
			AppendText(lines[lineIndex], startX + pixelSize, y + pixelSize, pixelSize, shadow);
			AppendText(lines[lineIndex], startX, y, pixelSize, color);
		}
	}

	void OverlayRenderer::AppendText(std::string_view a_text, float a_x, float a_y, float a_pixelSize, const std::array<float, 4>& a_color)
	{
		float cursor = a_x;
		for (const char ch : a_text) {
			AppendGlyph(ch, cursor, a_y, a_pixelSize, a_color);
			cursor += a_pixelSize * 6.0f;
			if (vertices_.size() + 6 * 35 >= vertexCapacity_) {
				return;
			}
		}
	}

	void OverlayRenderer::AppendGlyph(char a_char, float a_x, float a_y, float a_pixelSize, const std::array<float, 4>& a_color)
	{
		const auto glyph = GetGlyphRows(static_cast<char>(std::toupper(static_cast<unsigned char>(a_char))));
		for (std::size_t row = 0; row < glyph.size(); ++row) {
			for (std::size_t column = 0; column < 5; ++column) {
				const auto mask = static_cast<std::uint8_t>(1u << (4u - column));
				if ((glyph[row] & mask) != 0) {
					AppendPixel(a_x + static_cast<float>(column) * a_pixelSize, a_y + static_cast<float>(row) * a_pixelSize, a_pixelSize, a_color);
				}
			}
		}
	}

	void OverlayRenderer::AppendPixel(float a_x, float a_y, float a_size, const std::array<float, 4>& a_color)
	{
		AppendQuad(a_x, a_y, a_x + a_size, a_y + a_size, a_color);
	}

	void OverlayRenderer::AppendQuad(float a_left, float a_top, float a_right, float a_bottom, const std::array<float, 4>& a_color)
	{
		if (vertices_.size() + 6 > vertexCapacity_) {
			return;
		}

		const auto left = (a_left / static_cast<float>(targetWidth_)) * 2.0f - 1.0f;
		const auto right = (a_right / static_cast<float>(targetWidth_)) * 2.0f - 1.0f;
		const auto top = 1.0f - (a_top / static_cast<float>(targetHeight_)) * 2.0f;
		const auto bottom = 1.0f - (a_bottom / static_cast<float>(targetHeight_)) * 2.0f;

		const Vertex v0{ left, top, a_color[0], a_color[1], a_color[2], a_color[3] };
		const Vertex v1{ right, top, a_color[0], a_color[1], a_color[2], a_color[3] };
		const Vertex v2{ right, bottom, a_color[0], a_color[1], a_color[2], a_color[3] };
		const Vertex v3{ left, bottom, a_color[0], a_color[1], a_color[2], a_color[3] };

		vertices_.push_back(v0);
		vertices_.push_back(v1);
		vertices_.push_back(v2);
		vertices_.push_back(v0);
		vertices_.push_back(v2);
		vertices_.push_back(v3);
	}
}
