#pragma once

#include <cstdint>

namespace DragonScale::Upscaling
{
	struct JitterOffset
	{
		float x = 0.0f;
		float y = 0.0f;
	};

	[[nodiscard]] std::int32_t GetJitterPhaseCount(std::int32_t a_renderWidth, std::int32_t a_displayWidth) noexcept;
	[[nodiscard]] JitterOffset GetJitterOffset(std::uint32_t a_frameIndex, std::int32_t a_phaseCount) noexcept;
}
