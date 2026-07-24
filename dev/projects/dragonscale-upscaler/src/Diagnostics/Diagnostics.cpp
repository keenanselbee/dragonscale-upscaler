#include "Diagnostics/Diagnostics.h"

#include <REX/W32/USER32.h>

#include <algorithm>
#include <charconv>
#include <cctype>
#include <iomanip>
#include <numeric>
#include <sstream>
#include <system_error>

namespace
{
	constexpr std::uint32_t kVkTab = 0x09;
	constexpr std::uint32_t kVkShift = 0x10;
	constexpr std::uint32_t kVkControl = 0x11;
	constexpr std::uint32_t kVkAlt = 0x12;
	constexpr std::uint32_t kVkPause = 0x13;
	constexpr std::uint32_t kVkEscape = 0x1B;
	constexpr std::uint32_t kVkSpace = 0x20;
	constexpr std::uint32_t kVkPageUp = 0x21;
	constexpr std::uint32_t kVkPageDown = 0x22;
	constexpr std::uint32_t kVkEnd = 0x23;
	constexpr std::uint32_t kVkHome = 0x24;
	constexpr std::uint32_t kVkPrintScreen = 0x2C;
	constexpr std::uint32_t kVkInsert = 0x2D;
	constexpr std::uint32_t kVkDelete = 0x2E;
	constexpr std::uint32_t kVkF1 = 0x70;

	[[nodiscard]] std::string Lower(std::string value)
	{
		std::ranges::transform(value, value.begin(), [](unsigned char ch) {
			return static_cast<char>(std::tolower(ch));
		});
		return value;
	}

	[[nodiscard]] std::string Trim(std::string_view value)
	{
		const auto first = value.find_first_not_of(" \t\r\n");
		if (first == std::string_view::npos) {
			return {};
		}

		const auto last = value.find_last_not_of(" \t\r\n");
		return std::string(value.substr(first, last - first + 1));
	}

	[[nodiscard]] std::string CompactLower(std::string_view value)
	{
		std::string result;
		result.reserve(value.size());
		for (const auto ch : value) {
			if (std::isspace(static_cast<unsigned char>(ch)) || ch == '_' || ch == '-') {
				continue;
			}
			result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
		}
		return result;
	}

	[[nodiscard]] bool IsDisabledHotkey(std::string_view value)
	{
		const auto compact = CompactLower(value);
		return compact.empty() || compact == "none" || compact == "off" || compact == "disabled" || compact == "0";
	}

	[[nodiscard]] bool ParseUInt(std::string_view value, std::uint32_t& result)
	{
		result = 0;
		const auto* first = value.data();
		const auto* last = first + value.size();
		return std::from_chars(first, last, result).ec == std::errc{};
	}

	[[nodiscard]] std::uint32_t KeyFromToken(std::string_view value)
	{
		const auto token = CompactLower(value);
		if (token.empty()) {
			return 0;
		}

		if (token.size() == 1) {
			const auto ch = token[0];
			if (ch >= 'a' && ch <= 'z') {
				return static_cast<std::uint32_t>('A' + (ch - 'a'));
			}
			if (ch >= '0' && ch <= '9') {
				return static_cast<std::uint32_t>(ch);
			}
		}

		if (token.starts_with("f") && token.size() > 1) {
			std::uint32_t functionKey = 0;
			if (ParseUInt(std::string_view(token).substr(1), functionKey) && functionKey >= 1 && functionKey <= 24) {
				return kVkF1 + functionKey - 1;
			}
		}

		if (token == "esc" || token == "escape") {
			return kVkEscape;
		}
		if (token == "insert" || token == "ins") {
			return kVkInsert;
		}
		if (token == "delete" || token == "del") {
			return kVkDelete;
		}
		if (token == "home") {
			return kVkHome;
		}
		if (token == "end") {
			return kVkEnd;
		}
		if (token == "pageup" || token == "pgup") {
			return kVkPageUp;
		}
		if (token == "pagedown" || token == "pgdn") {
			return kVkPageDown;
		}
		if (token == "space") {
			return kVkSpace;
		}
		if (token == "tab") {
			return kVkTab;
		}
		if (token == "pause") {
			return kVkPause;
		}
		if (token == "printscreen" || token == "prtsc") {
			return kVkPrintScreen;
		}

		return 0;
	}

	[[nodiscard]] bool IsVirtualKeyDown(std::uint32_t key) noexcept
	{
		return (REX::W32::GetKeyState(static_cast<std::int32_t>(key)) & 0x8000) != 0;
	}

	[[nodiscard]] std::string FormatMilliseconds(float value, int precision)
	{
		std::ostringstream stream;
		stream << std::fixed << std::setprecision(precision) << value << " ms";
		return stream.str();
	}

	[[nodiscard]] std::string FormatPercent(float value)
	{
		std::ostringstream stream;
		stream << std::fixed << std::setprecision(1) << value << "%";
		return stream.str();
	}
}

namespace DragonScale::Diagnostics
{
	DiagnosticsManager& DiagnosticsManager::GetSingleton()
	{
		static DiagnosticsManager singleton;
		return singleton;
	}

	void DiagnosticsManager::Configure(const Settings& a_settings)
	{
		overlayEnabled_ = a_settings.debug.overlay;
		overlayVisible_ = overlayEnabled_;
		gpuTimingEnabled_ = a_settings.debug.overlayGpuTiming;
		overlayPosition_ = a_settings.debug.overlayPosition;
		overlayScale_ = a_settings.debug.overlayScale;
		overlayLogIntervalMs_ = a_settings.debug.overlayLogInterval;
		verboseLogging_ = Lower(a_settings.debug.logLevel) == "verbose";
		overlayToggleHotkey_ = ParseHotkey(a_settings.debug.overlayToggleHotkey);
		hotkeyWasDown_ = false;

		if (!overlayToggleHotkey_.enabled && !IsDisabledHotkey(a_settings.debug.overlayToggleHotkey)) {
			logger::warn("Diagnostics overlay hotkey '{}' is invalid; hotkey toggle disabled", a_settings.debug.overlayToggleHotkey);
		}
		logger::info(
			"Diagnostics overlay startup visibility={}, toggle hotkey={}",
			overlayVisible_,
			overlayToggleHotkey_.display);
	}

	void DiagnosticsManager::BeginFrame(std::uint64_t a_frameID)
	{
		const auto now = std::chrono::steady_clock::now();
		if (lastFrameTime_.time_since_epoch().count() != 0) {
			stats_.gameFrameMs = std::chrono::duration<float, std::milli>(now - lastFrameTime_).count();
		}
		lastFrameTime_ = now;
		stats_.frameID = a_frameID;
	}

	void DiagnosticsManager::SetStatus(Status a_status, std::string_view a_detail)
	{
		stats_.status = a_status;
		stats_.statusDetail = std::string(a_detail);
	}

	void DiagnosticsManager::SetResolution(
		std::uint32_t a_displayWidth,
		std::uint32_t a_displayHeight,
		std::uint32_t a_renderWidth,
		std::uint32_t a_renderHeight,
		QualityMode a_quality,
		float a_renderScale)
	{
		stats_.displayWidth = a_displayWidth;
		stats_.displayHeight = a_displayHeight;
		stats_.renderWidth = a_renderWidth;
		stats_.renderHeight = a_renderHeight;
		stats_.quality = a_quality;
		stats_.renderScale = a_renderScale;
	}

	void DiagnosticsManager::SetFsrFailure(
		std::int32_t a_result,
		std::string_view a_detail,
		std::uint32_t a_displayWidth,
		std::uint32_t a_displayHeight,
		std::uint32_t a_renderWidth,
		std::uint32_t a_renderHeight,
		std::int32_t a_format,
		std::uint32_t a_flags)
	{
		stats_.status = Status::kFailed;
		stats_.lastResult = a_result;
		stats_.statusDetail = std::string(a_detail);
		stats_.lastFailure = std::string(a_detail);
		stats_.lastFailureDisplayWidth = a_displayWidth;
		stats_.lastFailureDisplayHeight = a_displayHeight;
		stats_.lastFailureRenderWidth = a_renderWidth;
		stats_.lastFailureRenderHeight = a_renderHeight;
		stats_.lastFailureFormat = a_format;
		stats_.lastFailureFlags = a_flags;
	}

	void DiagnosticsManager::RecordCpuTime(CpuTimerKind a_kind, float a_ms)
	{
		switch (a_kind) {
		case CpuTimerKind::kConfigure:
			configureAverage_.Add(a_ms);
			break;
		case CpuTimerKind::kUpscale:
			upscaleAverage_.Add(a_ms);
			break;
		case CpuTimerKind::kFsrDispatch:
			fsrAverage_.Add(a_ms);
			break;
		}

		stats_.configureCpuMs = configureAverage_.Average();
		stats_.upscaleCpuMs = upscaleAverage_.Average();
		stats_.fsrCpuMs = fsrAverage_.Average();
		stats_.dragonscaleCpuMs = stats_.configureCpuMs + stats_.upscaleCpuMs;
	}

	void DiagnosticsManager::SetFsrGpuTime(float a_ms)
	{
		stats_.fsrGpuMs = a_ms;
		stats_.fsrGpuMsValid = true;
		stats_.fsrGpuPending = false;
	}

	void DiagnosticsManager::SetFsrGpuPending()
	{
		stats_.fsrGpuMsValid = false;
		stats_.fsrGpuPending = true;
	}

	void DiagnosticsManager::SetFsrGpuUnavailable()
	{
		stats_.fsrGpuMsValid = false;
		stats_.fsrGpuPending = false;
	}

	void DiagnosticsManager::UpdateHotkey()
	{
		if (!overlayToggleHotkey_.enabled) {
			hotkeyWasDown_ = false;
			return;
		}

		const auto hotkeyDown = IsHotkeyDown(overlayToggleHotkey_);
		if (hotkeyDown && !hotkeyWasDown_) {
			overlayVisible_ = !overlayVisible_;
			logger::info("Diagnostics overlay toggled {}", overlayVisible_ ? "on" : "off");
		}
		hotkeyWasDown_ = hotkeyDown;
	}

	void DiagnosticsManager::MaybeLog()
	{
		if (!ShouldLog()) {
			return;
		}

		const auto now = std::chrono::steady_clock::now();
		if (lastLogTime_.time_since_epoch().count() != 0 &&
			std::chrono::duration_cast<std::chrono::milliseconds>(now - lastLogTime_).count() < overlayLogIntervalMs_) {
			return;
		}

		lastLogTime_ = now;

		const auto lines = BuildLines();
		if (lines.size() >= 3) {
			logger::info("{} | {} | {}", lines[0], lines[1], lines[2]);
		}
		if (lines.size() >= 4) {
			logger::info("{}", lines[3]);
		}
	}

	const FrameStats& DiagnosticsManager::GetStats() const noexcept
	{
		return stats_;
	}

	std::vector<std::string> DiagnosticsManager::BuildLines() const
	{
		std::vector<std::string> lines;
		lines.reserve(4);

		switch (stats_.status) {
		case Status::kRunning:
			if (!stats_.statusDetail.empty() && stats_.statusDetail != "FSR2") {
				lines.emplace_back("DragonScale: " + stats_.statusDetail + " RUNNING");
			} else {
				lines.emplace_back("DragonScale: FSR2 RUNNING");
			}
			break;
		case Status::kFailed:
			lines.emplace_back("DragonScale: FSR2 FAILED");
			break;
		case Status::kNativeRestored:
			lines.emplace_back("DragonScale: FSR2 FAILED, native restored");
			break;
		case Status::kCreatingContext:
			lines.emplace_back("DragonScale: FSR2 creating context");
			break;
		case Status::kWaiting:
			lines.emplace_back("DragonScale: FSR2 waiting");
			break;
		case Status::kDisabled:
		default:
			lines.emplace_back("DragonScale: disabled");
			break;
		}

		const auto renderWidth = stats_.renderWidth != 0 ? stats_.renderWidth : stats_.displayWidth;
		const auto renderHeight = stats_.renderHeight != 0 ? stats_.renderHeight : stats_.displayHeight;
		const auto scale = stats_.renderScale > 0.0f ? stats_.renderScale * 100.0f : 100.0f;

		std::ostringstream resolutionLine;
		resolutionLine
			<< renderWidth << "x" << renderHeight
			<< " -> "
			<< stats_.displayWidth << "x" << stats_.displayHeight
			<< " | " << ToString(stats_.quality)
			<< " | " << FormatPercent(scale);
		lines.push_back(resolutionLine.str());

		std::ostringstream timingLine;
		timingLine
			<< "Game " << FormatMilliseconds(stats_.gameFrameMs, 1)
			<< " | DS CPU " << FormatMilliseconds(stats_.dragonscaleCpuMs, 2)
			<< " | FSR CPU " << FormatMilliseconds(stats_.fsrCpuMs, 2)
			<< " | FSR GPU ";
		if (stats_.fsrGpuMsValid) {
			timingLine << FormatMilliseconds(stats_.fsrGpuMs, 2);
		} else if (stats_.fsrGpuPending) {
			timingLine << "pending";
		} else {
			timingLine << "n/a";
		}
		lines.push_back(timingLine.str());

		if (!stats_.lastFailure.empty() &&
			(stats_.status == Status::kFailed || stats_.status == Status::kNativeRestored)) {
			std::ostringstream failureLine;
			failureLine << "Last: " << stats_.lastFailure;
			if (stats_.lastResult != 0) {
				failureLine << " (" << stats_.lastResult << ")";
			}
			if (stats_.lastFailureDisplayWidth != 0 || stats_.lastFailureRenderWidth != 0 ||
				stats_.lastFailureFormat != 0 || stats_.lastFailureFlags != 0) {
				failureLine
					<< " | display=" << stats_.lastFailureDisplayWidth << "x" << stats_.lastFailureDisplayHeight
					<< " render=" << stats_.lastFailureRenderWidth << "x" << stats_.lastFailureRenderHeight
					<< " format=" << stats_.lastFailureFormat
					<< " flags=" << stats_.lastFailureFlags;
			}
			lines.push_back(failureLine.str());
		} else if (!stats_.statusDetail.empty() &&
				   (stats_.status == Status::kWaiting || stats_.status == Status::kCreatingContext)) {
			std::ostringstream statusLine;
			statusLine << "State: " << stats_.statusDetail;
			lines.push_back(statusLine.str());
		}

		return lines;
	}

	bool DiagnosticsManager::OverlayEnabled() const noexcept
	{
		return overlayVisible_;
	}

	bool DiagnosticsManager::GpuTimingEnabled() const noexcept
	{
		return gpuTimingEnabled_;
	}

	OverlayPosition DiagnosticsManager::OverlayPositionSetting() const noexcept
	{
		return overlayPosition_;
	}

	float DiagnosticsManager::OverlayScale() const noexcept
	{
		return overlayScale_;
	}

	bool DiagnosticsManager::ShouldLog() const
	{
		return overlayVisible_ || verboseLogging_;
	}

	DiagnosticsManager::Hotkey DiagnosticsManager::ParseHotkey(std::string_view a_value) const
	{
		Hotkey hotkey;
		const auto original = Trim(a_value);
		hotkey.display = original.empty() ? "None" : original;

		if (IsDisabledHotkey(original)) {
			return hotkey;
		}

		bool hasKey = false;
		bool invalid = false;
		std::size_t start = 0;
		while (start <= original.size()) {
			const auto separator = original.find('+', start);
			const auto tokenView = separator == std::string::npos ?
				std::string_view(original).substr(start) :
				std::string_view(original).substr(start, separator - start);
			const auto token = CompactLower(tokenView);

			if (token.empty()) {
				invalid = true;
				break;
			}

			if (token == "ctrl" || token == "control") {
				hotkey.ctrl = true;
			} else if (token == "shift") {
				hotkey.shift = true;
			} else if (token == "alt" || token == "menu") {
				hotkey.alt = true;
			} else {
				if (hasKey) {
					invalid = true;
					break;
				}

				hotkey.key = KeyFromToken(token);
				hasKey = hotkey.key != 0;
				if (!hasKey) {
					invalid = true;
					break;
				}
			}

			if (separator == std::string::npos) {
				break;
			}
			start = separator + 1;
		}

		if (invalid || !hasKey) {
			return {};
		}

		hotkey.enabled = true;
		return hotkey;
	}

	bool DiagnosticsManager::IsHotkeyDown(const Hotkey& a_hotkey) const
	{
		if (!a_hotkey.enabled || a_hotkey.key == 0) {
			return false;
		}

		if (a_hotkey.ctrl && !IsVirtualKeyDown(kVkControl)) {
			return false;
		}
		if (a_hotkey.shift && !IsVirtualKeyDown(kVkShift)) {
			return false;
		}
		if (a_hotkey.alt && !IsVirtualKeyDown(kVkAlt)) {
			return false;
		}

		return IsVirtualKeyDown(a_hotkey.key);
	}

	void DiagnosticsManager::RollingAverage::Add(float a_value) noexcept
	{
		samples_[index_] = a_value;
		index_ = (index_ + 1) % samples_.size();
		count_ = (std::min)(count_ + 1, samples_.size());
	}

	float DiagnosticsManager::RollingAverage::Average() const noexcept
	{
		if (count_ == 0) {
			return 0.0f;
		}

		const auto sum = std::accumulate(samples_.begin(), samples_.begin() + static_cast<std::ptrdiff_t>(count_), 0.0f);
		return sum / static_cast<float>(count_);
	}

	ScopedCpuTimer::ScopedCpuTimer(CpuTimerKind a_kind) noexcept :
		kind_(a_kind),
		start_(std::chrono::steady_clock::now())
	{}

	ScopedCpuTimer::~ScopedCpuTimer()
	{
		const auto elapsed = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start_).count();
		DiagnosticsManager::GetSingleton().RecordCpuTime(kind_, elapsed);
	}

	std::string_view ToString(Status a_status) noexcept
	{
		switch (a_status) {
		case Status::kDisabled:
			return "Disabled";
		case Status::kWaiting:
			return "Waiting";
		case Status::kCreatingContext:
			return "CreatingContext";
		case Status::kRunning:
			return "Running";
		case Status::kFailed:
			return "Failed";
		case Status::kNativeRestored:
			return "NativeRestored";
		default:
			return "Unknown";
		}
	}
}
