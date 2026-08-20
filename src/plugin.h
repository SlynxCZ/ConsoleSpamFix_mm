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
//
// CAVEAT, and it is a real one here: the single hook this plugin installs is
// a VTABLE hook on ICvar::DispatchConCommand, and other plugins on the same
// server hook that exact slot through metamod's shared engine -- e.g.
// CounterStrikeSharp (SH_ADD_HOOK_MEMFUNC on the same method), and CS2Fixes
// and source2toolkit via KHook. Two independent ISourceHook instances
// patching one vtable slot do not coordinate: whichever patched last is what
// the slot points at, each treats whatever it found there as "the original",
// and MRES_SUPERCEDE only suppresses its own engine's chain. So whether this
// plugin's block lands before or after a CounterStrikeSharp say listener is
// decided by plugin load order rather than by SourceHook, and unloading
// either side restores a vtable slot the other may have re-patched since.
// See README.md for the whole picture.
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
	// Every console command the engine dispatches comes through here, client
	// commands included, with the originating slot in the context -- which is
	// the one place we can drop a chat line before the game ever tries to
	// resolve who sent it. See the body in plugin.cpp for what it drops.
	void Hook_DispatchConCommand(ConCommandRef cmd, const CCommandContext& ctx, const CCommand& args);

	// A slot whose player controller entity is gone is the "zombie" state this
	// plugin exists for. Deliberately fail-open: anything we cannot positively
	// identify as a dead slot is treated as a real player, so a wrong guess
	// costs one spam line rather than the whole server's chat.
	static bool IsDeadSlot(int slot);

	void ReportBlock(int slot, const CCommand& args);

	int m_iDispatchConCommandHookID = 0;

	// Blocking is per message, but logging must not be -- the flood is the
	// whole point of the attack, and mirroring it into the server log just
	// moves the spam somewhere else.
	struct SlotReport
	{
		std::chrono::steady_clock::time_point tLastPrint{};
		uint32_t nSuppressed = 0;
		bool bSeen = false;
	};

	SlotReport m_Reports[ABSOLUTE_PLAYER_LIMIT];

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
