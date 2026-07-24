#include "Upscaling/DisplayTweaksDetector.h"

#include <REX/W32/KERNEL32.h>

#include <array>
#include <charconv>
#include <filesystem>
#include <format>

namespace DragonScale::Upscaling
{
	namespace
	{
		constexpr auto kDisplayTweaksIni = "Data\\SKSE\\Plugins\\SSEDisplayTweaks.ini";
		constexpr auto kDisplayTweaksCustomIni = "Data\\SKSE\\Plugins\\SSEDisplayTweaks_custom.ini";

		[[nodiscard]] std::wstring ModulePath(REX::W32::HMODULE a_module)
		{
			std::array<wchar_t, MAX_PATH> buffer{};
			const auto length = REX::W32::GetModuleFileNameW(a_module, buffer.data(), static_cast<std::uint32_t>(buffer.size()));
			if (length == 0) {
				return {};
			}

			return std::wstring(buffer.data(), buffer.data() + length);
		}

		[[nodiscard]] std::string NarrowPath(const std::wstring& a_path)
		{
			std::string result;
			result.reserve(a_path.size());
			for (const auto ch : a_path) {
				result.push_back(ch <= 0x7F ? static_cast<char>(ch) : '?');
			}
			return result;
		}

		[[nodiscard]] std::string Trim(std::string a_value)
		{
			const auto first = a_value.find_first_not_of(" \t\r\n");
			if (first == std::string::npos) {
				return {};
			}

			const auto last = a_value.find_last_not_of(" \t\r\n");
			return a_value.substr(first, last - first + 1);
		}

		[[nodiscard]] std::string ReadProfileString(
			const char* a_section,
			const char* a_key,
			const char* a_fallback)
		{
			std::array<char, 256> base{};
			REX::W32::GetPrivateProfileStringA(
				a_section,
				a_key,
				a_fallback,
				base.data(),
				static_cast<std::uint32_t>(base.size()),
				kDisplayTweaksIni);

			std::array<char, 256> custom{};
			REX::W32::GetPrivateProfileStringA(
				a_section,
				a_key,
				base.data(),
				custom.data(),
				static_cast<std::uint32_t>(custom.size()),
				kDisplayTweaksCustomIni);

			return Trim(custom.data());
		}

		[[nodiscard]] bool ReadProfileBool(const char* a_section, const char* a_key, bool a_fallback)
		{
			const auto value = ReadProfileString(a_section, a_key, a_fallback ? "true" : "false");
			if (_stricmp(value.c_str(), "1") == 0 || _stricmp(value.c_str(), "true") == 0 || _stricmp(value.c_str(), "yes") == 0 ||
				_stricmp(value.c_str(), "on") == 0) {
				return true;
			}
			if (_stricmp(value.c_str(), "0") == 0 || _stricmp(value.c_str(), "false") == 0 || _stricmp(value.c_str(), "no") == 0 ||
				_stricmp(value.c_str(), "off") == 0) {
				return false;
			}
			return a_fallback;
		}

		[[nodiscard]] float ReadProfileFloat(const char* a_section, const char* a_key, float a_fallback)
		{
			const auto value = ReadProfileString(a_section, a_key, "");
			if (value.empty()) {
				return a_fallback;
			}

			float parsed = a_fallback;
			const auto* first = value.data();
			const auto* last = first + value.size();
			if (std::from_chars(first, last, parsed).ec == std::errc{}) {
				return parsed;
			}

			return a_fallback;
		}

		[[nodiscard]] bool HasProfileValue(const char* a_section, const char* a_key)
		{
			std::array<char, 8> buffer{};
			constexpr auto sentinel = "__ds__";

			REX::W32::GetPrivateProfileStringA(
				a_section,
				a_key,
				sentinel,
				buffer.data(),
				static_cast<std::uint32_t>(buffer.size()),
				kDisplayTweaksCustomIni);
			if (std::string_view(buffer.data()) != sentinel) {
				return true;
			}

			REX::W32::GetPrivateProfileStringA(
				a_section,
				a_key,
				sentinel,
				buffer.data(),
				static_cast<std::uint32_t>(buffer.size()),
				kDisplayTweaksIni);
			return std::string_view(buffer.data()) != sentinel;
		}

		[[nodiscard]] std::string BuildSignature(const DisplayTweaksDetection& a_detection)
		{
			return std::format(
				"{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{:.3f}|{:.3f}|{}|{}",
				a_detection.moduleLoaded,
				a_detection.iniPresent,
				a_detection.customIniPresent,
				a_detection.borderlessUpscale,
				a_detection.disableBufferResizing,
				a_detection.disableTargetResizing,
				a_detection.enableVSync,
				a_detection.enableTearing,
				a_detection.osdEnabled,
				a_detection.osdScaleToWindow,
				a_detection.hasResolutionOverride,
				a_detection.hasResolutionScale,
				a_detection.resolutionScale,
				a_detection.framerateLimit,
				a_detection.resolution,
				a_detection.swapEffect);
		}
	}

	DisplayTweaksDetector& DisplayTweaksDetector::GetSingleton()
	{
		static DisplayTweaksDetector singleton;
		return singleton;
	}

	const DisplayTweaksDetection& DisplayTweaksDetector::Refresh(std::string_view a_reason)
	{
		DisplayTweaksDetection updated;

		if (auto* module = REX::W32::GetModuleHandleW(L"SSEDisplayTweaks.dll")) {
			updated.moduleLoaded = true;
			updated.modulePath = ModulePath(module);
		}

		updated.iniPresent = std::filesystem::exists(kDisplayTweaksIni);
		updated.customIniPresent = std::filesystem::exists(kDisplayTweaksCustomIni);

		if (updated.iniPresent || updated.customIniPresent) {
			updated.borderlessUpscale = ReadProfileBool("Render", "BorderlessUpscale", false);
			updated.disableBufferResizing = ReadProfileBool("Render", "DisableBufferResizing", false);
			updated.disableTargetResizing = ReadProfileBool("Render", "DisableTargetResizing", false);
			updated.enableVSync = ReadProfileBool("Render", "EnableVSync", true);
			updated.enableTearing = ReadProfileBool("Render", "EnableTearing", false);
			updated.osdEnabled = ReadProfileBool("OSD", "Enable", false);
			updated.osdScaleToWindow = ReadProfileBool("OSD", "ScaleToWindow", true);
			updated.resolution = ReadProfileString("Render", "Resolution", "");
			updated.hasResolutionOverride = !updated.resolution.empty() && updated.resolution != "-1 -1";
			updated.resolutionScale = ReadProfileFloat("Render", "ResolutionScale", -1.0f);
			updated.hasResolutionScale = HasProfileValue("Render", "ResolutionScale") && updated.resolutionScale > 0.0f;
			updated.framerateLimit = ReadProfileFloat("Render", "FramerateLimit", -1.0f);
			updated.swapEffect = ReadProfileString("Render", "SwapEffect", "auto");
			if (updated.swapEffect.empty()) {
				updated.swapEffect = "auto";
			}
		}

		const auto signature = BuildSignature(updated);
		if (signature != lastSignature_) {
			if (updated.moduleLoaded || updated.iniPresent || updated.customIniPresent) {
				logger::info(
					"SSE Display Tweaks compatibility: moduleLoaded={}, iniPresent={}, customIniPresent={}, reason={}, module={}",
					updated.moduleLoaded,
					updated.iniPresent,
					updated.customIniPresent,
					a_reason,
					updated.modulePath.empty() ? std::string("<unknown>") : NarrowPath(updated.modulePath));
				logger::info(
					"SSE Display Tweaks render settings: BorderlessUpscale={}, Resolution={}, ResolutionScale={:.3f}, DisableBufferResizing={}, DisableTargetResizing={}, SwapEffect={}, EnableVSync={}, EnableTearing={}, FramerateLimit={:.1f}",
					updated.borderlessUpscale,
					updated.hasResolutionOverride ? updated.resolution : "<default>",
					updated.resolutionScale,
					updated.disableBufferResizing,
					updated.disableTargetResizing,
					updated.swapEffect,
					updated.enableVSync,
					updated.enableTearing,
					updated.framerateLimit);
				logger::info(
					"SSE Display Tweaks OSD settings: enabled={}, scaleToWindow={}",
					updated.osdEnabled,
					updated.osdScaleToWindow);

				if (updated.borderlessUpscale || updated.hasResolutionOverride || updated.hasResolutionScale) {
					logger::warn(
						"SSE Display Tweaks window/resolution scaling is active. DragonScale can coexist with it, but prefer one render-scaling owner while diagnosing zoom, crop, or extent mismatch issues.");
				}
				if (updated.osdEnabled) {
					logger::info("SSE Display Tweaks OSD is enabled; DragonScale diagnostics overlay may overlap it while debugging");
				}
				if (updated.enableTearing) {
					logger::info("SSE Display Tweaks tearing support is enabled; DragonScale does not modify Present flags");
				}
			} else if (!loggedAbsent_) {
				logger::info("SSE Display Tweaks not detected");
				loggedAbsent_ = true;
			}

			detection_ = std::move(updated);
			lastSignature_ = signature;
		}

		return detection_;
	}

	const DisplayTweaksDetection& DisplayTweaksDetector::Get() const noexcept
	{
		return detection_;
	}
}
