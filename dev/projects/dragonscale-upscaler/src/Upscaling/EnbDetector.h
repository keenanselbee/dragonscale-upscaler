#pragma once

#include "Config.h"

#include <string>

namespace DragonScale::Upscaling
{
	struct EnbDetection
	{
		bool detected = false;
		bool compatibleMode = false;
		std::string source = "none";
		std::string modulePath;
	};

	class EnbDetector
	{
	public:
		[[nodiscard]] static EnbDetector& GetSingleton();

		[[nodiscard]] const EnbDetection& Refresh(EnbMode a_mode, std::string_view a_reason);
		[[nodiscard]] const EnbDetection& Get() const noexcept;

	private:
		EnbDetector() = default;

		EnbDetection Detect(EnbMode a_mode) const;
		void LogIfChanged(const EnbDetection& a_detection, std::string_view a_reason);

		EnbDetection detection_;
		bool logged_ = false;
	};
}
