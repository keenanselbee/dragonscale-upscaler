#include "Upscaling/MenuStateTracker.h"

namespace DragonScale::Upscaling
{
	MenuStateTracker& MenuStateTracker::GetSingleton()
	{
		static MenuStateTracker singleton;
		return singleton;
	}

	bool MenuStateTracker::Register()
	{
		bool expected = false;
		if (!registered_.compare_exchange_strong(expected, true)) {
			return true;
		}

		const auto ui = RE::UI::GetSingleton();
		if (!ui) {
			registered_ = false;
			logger::warn("Menu state tracker could not register because UI is unavailable");
			return false;
		}

		const auto source = ui->GetEventSource<RE::MenuOpenCloseEvent>();
		if (!source) {
			registered_ = false;
			logger::warn("Menu state tracker could not register because MenuOpenCloseEvent source is unavailable");
			return false;
		}

		source->AddEventSink(this);
		logger::info("Registered menu state tracker");
		return true;
	}

	bool MenuStateTracker::ConsumeResetRequest() noexcept
	{
		return resetRequested_.exchange(false, std::memory_order_acq_rel);
	}

	bool MenuStateTracker::IsMainOrLoadingMenuOpen() const noexcept
	{
		return mainMenuOpen_.load(std::memory_order_acquire) || loadingMenuOpen_.load(std::memory_order_acquire);
	}

	bool MenuStateTracker::IsMenuOnlyFrame() const noexcept
	{
		return IsMainOrLoadingMenuOpen() ||
		       mapMenuOpen_.load(std::memory_order_acquire) ||
		       magicMenuOpen_.load(std::memory_order_acquire) ||
		       statsMenuOpen_.load(std::memory_order_acquire);
	}

	RE::BSEventNotifyControl MenuStateTracker::ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
	{
		if (!a_event) {
			return RE::BSEventNotifyControl::kContinue;
		}

		const auto opening = a_event->opening;
		if (a_event->menuName == "Main Menu") {
			mainMenuOpen_.store(opening, std::memory_order_release);
		} else if (a_event->menuName == "Loading Menu") {
			loadingMenuOpen_.store(opening, std::memory_order_release);
			if (!opening) {
				resetRequested_.store(true, std::memory_order_release);
			}
		} else if (a_event->menuName == "MapMenu") {
			mapMenuOpen_.store(opening, std::memory_order_release);
		} else if (a_event->menuName == "MagicMenu") {
			magicMenuOpen_.store(opening, std::memory_order_release);
		} else if (a_event->menuName == "StatsMenu") {
			statsMenuOpen_.store(opening, std::memory_order_release);
		}

		return RE::BSEventNotifyControl::kContinue;
	}
}
