#pragma once

#include "Config.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace DragonScale::Diagnostics
{
	enum class Status : std::uint32_t
	{
		kDisabled = 0,
		kWaiting,
		kCreatingContext,
		kRunning,
		kFailed,
		kNativeRestored
	};

	enum class CpuTimerKind : std::uint32_t
	{
		kConfigure = 0,
		kUpscale,
		kFsrDispatch
	};

	struct FrameStats
	{
		Status status = Status::kDisabled;
		std::uint32_t displayWidth = 0;
		std::uint32_t displayHeight = 0;
		std::uint32_t renderWidth = 0;
		std::uint32_t renderHeight = 0;
		std::uint64_t frameID = 0;
		QualityMode quality = QualityMode::kQuality;
		float renderScale = 1.0f;
		float gameFrameMs = 0.0f;
		float dragonscaleCpuMs = 0.0f;
		float configureCpuMs = 0.0f;
		float upscaleCpuMs = 0.0f;
		float fsrCpuMs = 0.0f;
		float fsrGpuMs = 0.0f;
		bool fsrGpuMsValid = false;
		bool fsrGpuPending = false;
		std::int32_t lastResult = 0;
		std::uint32_t lastFailureDisplayWidth = 0;
		std::uint32_t lastFailureDisplayHeight = 0;
		std::uint32_t lastFailureRenderWidth = 0;
		std::uint32_t lastFailureRenderHeight = 0;
		std::int32_t lastFailureFormat = 0;
		std::uint32_t lastFailureFlags = 0;
		std::string statusDetail;
		std::string lastFailure;
	};

	class DiagnosticsManager
	{
	public:
		[[nodiscard]] static DiagnosticsManager& GetSingleton();

		void Configure(const Settings& a_settings);
		void BeginFrame(std::uint64_t a_frameID);
		void SetStatus(Status a_status, std::string_view a_detail = {});
		void SetResolution(
			std::uint32_t a_displayWidth,
			std::uint32_t a_displayHeight,
			std::uint32_t a_renderWidth,
			std::uint32_t a_renderHeight,
			QualityMode a_quality,
			float a_renderScale);
		void SetFsrFailure(
			std::int32_t a_result,
			std::string_view a_detail,
			std::uint32_t a_displayWidth,
			std::uint32_t a_displayHeight,
			std::uint32_t a_renderWidth,
			std::uint32_t a_renderHeight,
			std::int32_t a_format,
			std::uint32_t a_flags);
		void RecordCpuTime(CpuTimerKind a_kind, float a_ms);
		void SetFsrGpuTime(float a_ms);
		void SetFsrGpuPending();
		void SetFsrGpuUnavailable();
		void UpdateHotkey();
		void MaybeLog();

		[[nodiscard]] const FrameStats& GetStats() const noexcept;
		[[nodiscard]] std::vector<std::string> BuildLines() const;
		[[nodiscard]] bool OverlayEnabled() const noexcept;
		[[nodiscard]] bool GpuTimingEnabled() const noexcept;
		[[nodiscard]] OverlayPosition OverlayPositionSetting() const noexcept;
		[[nodiscard]] float OverlayScale() const noexcept;

	private:
		class RollingAverage
		{
		public:
			void Add(float a_value) noexcept;
			[[nodiscard]] float Average() const noexcept;

		private:
			std::array<float, 60> samples_{};
			std::size_t index_ = 0;
			std::size_t count_ = 0;
		};

		struct Hotkey
		{
			bool enabled = false;
			bool ctrl = false;
			bool shift = false;
			bool alt = false;
			std::uint32_t key = 0;
			std::string display = "None";
		};

		DiagnosticsManager() = default;

		[[nodiscard]] bool ShouldLog() const;
		[[nodiscard]] Hotkey ParseHotkey(std::string_view a_value) const;
		[[nodiscard]] bool IsHotkeyDown(const Hotkey& a_hotkey) const;

		FrameStats stats_;
		RollingAverage configureAverage_;
		RollingAverage upscaleAverage_;
		RollingAverage fsrAverage_;
		std::chrono::steady_clock::time_point lastFrameTime_{};
		std::chrono::steady_clock::time_point lastLogTime_{};
		bool overlayEnabled_ = false;
		bool overlayVisible_ = false;
		bool gpuTimingEnabled_ = true;
		bool verboseLogging_ = false;
		bool hotkeyWasDown_ = false;
		OverlayPosition overlayPosition_ = OverlayPosition::kTopLeft;
		float overlayScale_ = 1.0f;
		std::uint32_t overlayLogIntervalMs_ = 1000;
		Hotkey overlayToggleHotkey_;
	};

	class ScopedCpuTimer
	{
	public:
		explicit ScopedCpuTimer(CpuTimerKind a_kind) noexcept;
		~ScopedCpuTimer();

		ScopedCpuTimer(const ScopedCpuTimer&) = delete;
		ScopedCpuTimer& operator=(const ScopedCpuTimer&) = delete;

	private:
		CpuTimerKind kind_;
		std::chrono::steady_clock::time_point start_;
	};

	[[nodiscard]] std::string_view ToString(Status a_status) noexcept;
}
