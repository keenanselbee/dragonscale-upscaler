#pragma once

#include <atomic>

namespace DragonScale::Upscaling
{
	class MenuStateTracker : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
	{
	public:
		[[nodiscard]] static MenuStateTracker& GetSingleton();

		bool Register();
		[[nodiscard]] bool ConsumeResetRequest() noexcept;
		[[nodiscard]] bool IsMainOrLoadingMenuOpen() const noexcept;
		[[nodiscard]] bool IsMenuOnlyFrame() const noexcept;

		RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override;

	private:
		MenuStateTracker() = default;

		std::atomic_bool registered_ = false;
		std::atomic_bool mainMenuOpen_ = false;
		std::atomic_bool loadingMenuOpen_ = false;
		std::atomic_bool mapMenuOpen_ = false;
		std::atomic_bool magicMenuOpen_ = false;
		std::atomic_bool statsMenuOpen_ = false;
		std::atomic_bool resetRequested_ = false;
	};
}
