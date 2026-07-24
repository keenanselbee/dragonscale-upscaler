#include "Upscaling/EnbDetector.h"

#include <REX/W32/KERNEL32.h>

#include <array>

namespace
{
	[[nodiscard]] std::string WideToAscii(const wchar_t* a_text)
	{
		if (!a_text) {
			return {};
		}

		std::string result;
		while (*a_text) {
			const auto ch = *a_text++;
			result.push_back(ch >= 0 && ch <= 0x7F ? static_cast<char>(ch) : '?');
		}
		return result;
	}

	[[nodiscard]] std::string GetModulePath(REX::W32::HMODULE a_module)
	{
		if (!a_module) {
			return {};
		}

		std::array<wchar_t, 1024> buffer{};
		const auto length = REX::W32::GetModuleFileNameW(a_module, buffer.data(), static_cast<std::uint32_t>(buffer.size()));
		if (length == 0) {
			return {};
		}

		return WideToAscii(buffer.data());
	}

	[[nodiscard]] bool HasEnbExports(REX::W32::HMODULE a_module) noexcept
	{
		if (!a_module) {
			return false;
		}

		return REX::W32::GetProcAddress(a_module, "ENBGetSDKVersion") != nullptr ||
		       REX::W32::GetProcAddress(a_module, "ENBGetVersion") != nullptr ||
		       REX::W32::GetProcAddress(a_module, "ENBGetGameIdentifier") != nullptr;
	}

	[[nodiscard]] bool SameDetection(const DragonScale::Upscaling::EnbDetection& a_lhs, const DragonScale::Upscaling::EnbDetection& a_rhs)
	{
		return a_lhs.detected == a_rhs.detected &&
		       a_lhs.compatibleMode == a_rhs.compatibleMode &&
		       a_lhs.source == a_rhs.source &&
		       a_lhs.modulePath == a_rhs.modulePath;
	}
}

namespace DragonScale::Upscaling
{
	EnbDetector& EnbDetector::GetSingleton()
	{
		static EnbDetector singleton;
		return singleton;
	}

	const EnbDetection& EnbDetector::Refresh(EnbMode a_mode, std::string_view a_reason)
	{
		auto detection = Detect(a_mode);
		LogIfChanged(detection, a_reason);
		detection_ = std::move(detection);
		return detection_;
	}

	const EnbDetection& EnbDetector::Get() const noexcept
	{
		return detection_;
	}

	EnbDetection EnbDetector::Detect(EnbMode a_mode) const
	{
		EnbDetection detection;
		detection.compatibleMode = a_mode == EnbMode::kForceCompatible;
		if (a_mode == EnbMode::kOff) {
			detection.source = "disabled-by-config";
			return detection;
		}

		if (auto* enbSeries = REX::W32::GetModuleHandleW(L"enbseries.dll"); enbSeries && HasEnbExports(enbSeries)) {
			detection.detected = true;
			detection.compatibleMode = true;
			detection.source = "enbseries.dll exports";
			detection.modulePath = GetModulePath(enbSeries);
			return detection;
		}

		if (auto* d3d11 = REX::W32::GetModuleHandleW(L"d3d11.dll"); d3d11 && HasEnbExports(d3d11)) {
			detection.detected = true;
			detection.compatibleMode = true;
			detection.source = "d3d11.dll wrapper exports";
			detection.modulePath = GetModulePath(d3d11);
			return detection;
		}

		if (a_mode == EnbMode::kForceCompatible) {
			detection.source = "forced-compatible";
			return detection;
		}

		detection.source = "not-detected";
		return detection;
	}

	void EnbDetector::LogIfChanged(const EnbDetection& a_detection, std::string_view a_reason)
	{
		if (logged_ && SameDetection(detection_, a_detection)) {
			return;
		}

		logged_ = true;
		logger::info(
			"ENB detection: detected={}, compatibleMode={}, source={}, module={}, reason={}",
			a_detection.detected,
			a_detection.compatibleMode,
			a_detection.source,
			a_detection.modulePath.empty() ? "<none>" : a_detection.modulePath,
			a_reason);
	}
}
