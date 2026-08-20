#ifndef _INCLUDE_CONSOLE_SPAM_FIX_PLUGIN_SLYNX_H_
#define _INCLUDE_CONSOLE_SPAM_FIX_PLUGIN_SLYNX_H_
#ifdef _WIN32
#pragma once
#endif

#include "inetchannel.h"
#include "ISmmPlugin.h"

// Redirects SH_GLOB_SHPTR/SH_GLOB_PLUGPTR onto a private, plugin-owned
// SourceHook engine (vendor/sourcehook) instead of metamod's shared
// g_SHPtr/g_PLID -- must come after ISmmPlugin.h (which is what defines the
// defaults this overrides) and before any SH_DECL_HOOK*/SH_ADD_*HOOK usage.
#include "sourcehook/sourcehook_metamod_override.h"

#include "const.h"
#include "tier1/convar.h"

#include <chrono>
#include <cstdint>

class Plugin final : public ISmmPlugin
{
public:
	bool Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool late) override;
	bool Unload(char* error, size_t maxlen) override;

private: // Hooks
	void CCvar_DispatchConCommand(ConCommandRef cmd, const CCommandContext& ctx, const CCommand& args);

	int m_iDispatchConCommandHookID = 0;

private:
	const char* GetAuthor() override;
	const char* GetName() override;
	const char* GetDescription() override;
	const char* GetURL() override;
	const char* GetLicense() override;
	const char* GetVersion() override;
	const char* GetDate() override;
	const char* GetLogTag() override;
};

extern Plugin g_Plugin;

PLUGIN_GLOBALVARS();

#endif // _INCLUDE_CONSOLE_SPAM_FIX_PLUGIN_SLYNX_H_
