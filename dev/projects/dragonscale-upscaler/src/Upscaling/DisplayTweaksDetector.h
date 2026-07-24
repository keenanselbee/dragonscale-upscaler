#pragma once

#include <string>
#include <string_view>

namespace DragonScale::Upscaling
{
	struct DisplayTweaksDetection
	{
		bool moduleLoaded = false;
		bool iniPresent = false;
		bool customIniPresent = false;
		bool borderlessUpscale = false;
		bool disableBufferResizing = false;
		bool disableTargetResizing = false;
		bool enableVSync = true;
		bool enableTearing = false;
		bool osdEnabled = false;
		bool osdScaleToWindow = true;
		bool hasResolutionOverride = false;
		bool hasResolutionScale = false;
		float resolutionScale = -1.0f;
		float framerateLimit = -1.0f;
		std::string resolution;
		std::string swapEffect = "auto";
		std::wstring modulePath;
	};

	class DisplayTweaksDetector
	{
	public:
		[[nodiscard]] static DisplayTweaksDetector& GetSingleton();

		[[nodiscard]] const DisplayTweaksDetection& Refresh(std::string_view a_reason);
		[[nodiscard]] const DisplayTweaksDetection& Get() const noexcept;

	private:
		DisplayTweaksDetection detection_;
		std::string lastSignature_;
		bool loggedAbsent_ = false;
	};
}
