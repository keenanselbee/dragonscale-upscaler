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
