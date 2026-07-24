#include "Upscaling/D3D11StateGuard.h"

namespace DragonScale::Upscaling
{
	D3D11StateGuard::D3D11StateGuard(ID3D11DeviceContext* a_context) :
		context_(a_context)
	{
		if (!context_) {
			return;
		}

		context_->IAGetInputLayout(&inputLayout_);
		context_->IAGetPrimitiveTopology(&primitiveTopology_);
		context_->IAGetVertexBuffers(
			0,
			static_cast<UINT>(vertexBuffers_.size()),
			vertexBuffers_.data(),
			vertexStrides_.data(),
			vertexOffsets_.data());
		context_->IAGetIndexBuffer(&indexBuffer_, &indexFormat_, &indexOffset_);

		context_->VSGetShader(&vertexShader_, nullptr, nullptr);
		context_->VSGetConstantBuffers(0, static_cast<UINT>(vertexConstantBuffers_.size()), vertexConstantBuffers_.data());

		context_->PSGetShader(&pixelShader_, nullptr, nullptr);
		context_->PSGetShaderResources(0, static_cast<UINT>(pixelSRVs_.size()), pixelSRVs_.data());
		context_->PSGetSamplers(0, static_cast<UINT>(pixelSamplers_.size()), pixelSamplers_.data());
		context_->PSGetConstantBuffers(0, static_cast<UINT>(pixelConstantBuffers_.size()), pixelConstantBuffers_.data());

		context_->CSGetShader(&computeShader_, nullptr, nullptr);
		context_->CSGetShaderResources(0, static_cast<UINT>(computeSRVs_.size()), computeSRVs_.data());
		context_->CSGetUnorderedAccessViews(0, static_cast<UINT>(computeUAVs_.size()), computeUAVs_.data());
		context_->CSGetSamplers(0, static_cast<UINT>(computeSamplers_.size()), computeSamplers_.data());
		context_->CSGetConstantBuffers(0, static_cast<UINT>(computeConstantBuffers_.size()), computeConstantBuffers_.data());

		context_->OMGetRenderTargets(static_cast<UINT>(renderTargets_.size()), renderTargets_.data(), &depthStencil_);
		context_->OMGetBlendState(&blendState_, blendFactor_.data(), &sampleMask_);
		context_->OMGetDepthStencilState(&depthStencilState_, &stencilRef_);

		context_->RSGetState(&rasterizerState_);
		viewportCount_ = static_cast<UINT>(viewports_.size());
		context_->RSGetViewports(&viewportCount_, viewports_.data());
		scissorCount_ = static_cast<UINT>(scissors_.size());
		context_->RSGetScissorRects(&scissorCount_, scissors_.data());
	}

	D3D11StateGuard::~D3D11StateGuard()
	{
		if (!context_) {
			return;
		}

		context_->IASetInputLayout(inputLayout_);
		context_->IASetPrimitiveTopology(primitiveTopology_);
		context_->IASetVertexBuffers(
			0,
			static_cast<UINT>(vertexBuffers_.size()),
			vertexBuffers_.data(),
			vertexStrides_.data(),
			vertexOffsets_.data());
		context_->IASetIndexBuffer(indexBuffer_, indexFormat_, indexOffset_);

		context_->VSSetShader(vertexShader_, nullptr, 0);
		context_->VSSetConstantBuffers(0, static_cast<UINT>(vertexConstantBuffers_.size()), vertexConstantBuffers_.data());

		context_->PSSetShader(pixelShader_, nullptr, 0);
		context_->PSSetShaderResources(0, static_cast<UINT>(pixelSRVs_.size()), pixelSRVs_.data());
		context_->PSSetSamplers(0, static_cast<UINT>(pixelSamplers_.size()), pixelSamplers_.data());
		context_->PSSetConstantBuffers(0, static_cast<UINT>(pixelConstantBuffers_.size()), pixelConstantBuffers_.data());

		context_->CSSetShader(computeShader_, nullptr, 0);
		context_->CSSetShaderResources(0, static_cast<UINT>(computeSRVs_.size()), computeSRVs_.data());
		context_->CSSetUnorderedAccessViews(0, static_cast<UINT>(computeUAVs_.size()), computeUAVs_.data(), nullptr);
		context_->CSSetSamplers(0, static_cast<UINT>(computeSamplers_.size()), computeSamplers_.data());
		context_->CSSetConstantBuffers(0, static_cast<UINT>(computeConstantBuffers_.size()), computeConstantBuffers_.data());

		context_->OMSetRenderTargets(static_cast<UINT>(renderTargets_.size()), renderTargets_.data(), depthStencil_);
		context_->OMSetBlendState(blendState_, blendFactor_.data(), sampleMask_);
		context_->OMSetDepthStencilState(depthStencilState_, stencilRef_);

		context_->RSSetState(rasterizerState_);
		if (viewportCount_ > 0) {
			context_->RSSetViewports(viewportCount_, viewports_.data());
		}
		if (scissorCount_ > 0) {
			context_->RSSetScissorRects(scissorCount_, scissors_.data());
		}

		SafeRelease(inputLayout_);
		for (auto*& value : vertexBuffers_) {
			SafeRelease(value);
		}
		SafeRelease(indexBuffer_);
		SafeRelease(vertexShader_);
		for (auto*& value : vertexConstantBuffers_) {
			SafeRelease(value);
		}
		SafeRelease(pixelShader_);
		for (auto*& value : pixelSRVs_) {
			SafeRelease(value);
		}
		for (auto*& value : pixelSamplers_) {
			SafeRelease(value);
		}
		for (auto*& value : pixelConstantBuffers_) {
			SafeRelease(value);
		}
		SafeRelease(computeShader_);
		for (auto*& value : computeSRVs_) {
			SafeRelease(value);
		}
		for (auto*& value : computeUAVs_) {
			SafeRelease(value);
		}
		for (auto*& value : computeSamplers_) {
			SafeRelease(value);
		}
		for (auto*& value : computeConstantBuffers_) {
			SafeRelease(value);
		}
		for (auto*& value : renderTargets_) {
			SafeRelease(value);
		}
		SafeRelease(depthStencil_);
		SafeRelease(blendState_);
		SafeRelease(depthStencilState_);
		SafeRelease(rasterizerState_);
	}
}
