#include "Config.h"
#include "Diagnostics/Diagnostics.h"
#include "Upscaling/Upscaler.h"

static void SKSEMessageHandler(SKSE::MessagingInterface::Message* message)
{
    switch (message->type) {
    case SKSE::MessagingInterface::kPostLoad:
        DragonScale::Config::GetSingleton().Load();
        DragonScale::Diagnostics::DiagnosticsManager::GetSingleton().Configure(DragonScale::Config::GetSingleton().GetSettings());
        DragonScale::Upscaling::Upscaler::GetSingleton().Load();
        break;
    case SKSE::MessagingInterface::kPostPostLoad:
        DragonScale::Upscaling::Upscaler::GetSingleton().InstallHooks();
        break;
    case SKSE::MessagingInterface::kDataLoaded:
        DragonScale::Upscaling::Upscaler::GetSingleton().OnDataLoaded();
        break;
    }
}

extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Load(const SKSE::LoadInterface* a_skse)
{
    REL::Module::reset();

    auto g_messaging = reinterpret_cast<SKSE::MessagingInterface*>(a_skse->QueryInterface(SKSE::LoadInterface::kMessaging));

    if (!g_messaging) {
        logger::critical("Failed to load messaging interface! This error is fatal, plugin will not load.");
        return false;
    }

    logger::info("{} v{}"sv, Plugin::NAME, Plugin::VERSION.string());

    SKSE::Init(a_skse);
    SKSE::AllocTrampoline(1 << 12);

    g_messaging->RegisterListener("SKSE", SKSEMessageHandler);

    return true;
}
