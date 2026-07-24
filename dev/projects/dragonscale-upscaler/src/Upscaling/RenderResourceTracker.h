#pragma once

#include "Upscaling/UpscaleTypes.h"

#include <chrono>

namespace DragonScale::Upscaling
{
	class RenderResourceTracker
	{
	public:
		[[nodiscard]] static RenderResourceTracker& GetSingleton();

		[[nodiscard]] ResourceSnapshot Capture(std::uint64_t a_frameID);
		void Configure(bool a_useTextureDiscoveryFallback) noexcept;
		void ResetLogState() noexcept;

	private:
		RenderResourceTracker() = default;

		void LogSnapshot(const ResourceSnapshot& a_snapshot);

		bool useTextureDiscoveryFallback_ = true;
		bool textureDiscoveryNoticeLogged_ = false;
		std::uintptr_t lastSignature_ = 0;
		std::chrono::steady_clock::time_point lastSnapshotLogTime_{};
	};
}
