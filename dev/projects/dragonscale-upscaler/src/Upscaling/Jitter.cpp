#include "Upscaling/Jitter.h"

#include <algorithm>
#include <cmath>

namespace
{
	[[nodiscard]] float Halton(std::int32_t index, std::int32_t base) noexcept
	{
		float f = 1.0f;
		float result = 0.0f;

		while (index > 0) {
			f /= static_cast<float>(base);
			result += f * static_cast<float>(index % base);
			index = static_cast<std::int32_t>(std::floor(static_cast<float>(index) / static_cast<float>(base)));
		}

		return result;
	}
}

namespace DragonScale::Upscaling
{
	std::int32_t GetJitterPhaseCount(std::int32_t a_renderWidth, std::int32_t a_displayWidth) noexcept
	{
		const auto renderWidth = (std::max)(a_renderWidth, 1);
		const auto displayWidth = (std::max)(a_displayWidth, 1);
		const auto ratio = static_cast<float>(displayWidth) / static_cast<float>(renderWidth);
		return (std::max)(1, static_cast<std::int32_t>(8.0f * ratio * ratio));
	}

	JitterOffset GetJitterOffset(std::uint32_t a_frameIndex, std::int32_t a_phaseCount) noexcept
	{
		const auto phaseCount = (std::max)(a_phaseCount, 1);
		const auto index = static_cast<std::int32_t>(a_frameIndex % static_cast<std::uint32_t>(phaseCount)) + 1;

		return {
			Halton(index, 2) - 0.5f,
			Halton(index, 3) - 0.5f
		};
	}
}
