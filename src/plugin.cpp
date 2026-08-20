// Author: Michal Přikryl (Slynx) <github.com/SlynxCZ>

#include "plugin.h"

#include "module.hpp"
#include "utils.hpp"
#include "eiface.h"
#include "entityinstance.h"
#include "entitysystem.h"
#include "interfaces/interfaces.h"
#include "playerslot.h"
#include "schemasystem/schemasystem.h"

#include <cstdio>

#define VERSION_STRING SEMVER " @ " GITHUB_SHA
#define BUILD_TIMESTAMP __DATE__ " " __TIME__

using namespace DynLibUtils;

Plugin g_Plugin;
PLUGIN_EXPOSE(Plugin, g_Plugin);

CGameEntitySystem* GameEntitySystem();

// void ICvar::DispatchConCommand(ConCommandRef cmd, const CCommandContext& ctx, const CCommand& args)
SH_DECL_HOOK3_void(ICvar, DispatchConCommand, SH_NOATTRIB, 0, ConCommandRef, const CCommandContext&, const CCommand&);

// How long a slot stays quiet in the log after we print a line for it.
static constexpr std::chrono::seconds kReportInterval{5};

bool Plugin::Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool late)
{
	PLUGIN_SAVEVARS();

	GET_V_IFACE_CURRENT(GetServerFactory, g_pSource2Server, ISource2Server, SOURCE2SERVER_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetEngineFactory, g_pEngineServer, IVEngineServer2, SOURCE2ENGINETOSERVER_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetEngineFactory, g_pCVar, ICvar, CVAR_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetEngineFactory, g_pSchemaSystem, ISchemaSystem, SCHEMASYSTEM_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetEngineFactory, g_pGameResourceServiceServer, IGameResourceService, GAMERESOURCESERVICESERVER_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetEngineFactory, g_pNetworkServerService, INetworkServerService, NETWORKSERVERSERVICE_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetFileSystemFactory, g_pFullFileSystem, IFileSystem, FILESYSTEM_INTERFACE_VERSION);

	// Plain virtual hook on an interface metamod hands us at load time, so
	// unlike the signature-scanning fixes this one needs no deferral and is
	// safe to install late -- the point is being able to drop it onto a server
	// that is being spammed right now without restarting it.
	m_iDispatchConCommandHookID = SH_ADD_HOOK(ICvar, DispatchConCommand, g_pCVar, SH_MEMBER(this, &Plugin::Hook_DispatchConCommand), false);
	if (!m_iDispatchConCommandHookID)
	{
		std::snprintf(error, maxlen, "Failed to create hook for ICvar::DispatchConCommand");
		return false;
	}

	return true;
}

bool Plugin::Unload(char* error, size_t maxlen)
{
	SH_REMOVE_HOOK_ID(m_iDispatchConCommandHookID);
	m_iDispatchConCommandHookID = 0;

	return true;
}

///////////////////////////////////////

// What the advertising bots exploit:
//
// They connect with a real, Steam-validated account, get far enough that the
// server calls ClientPutInServer and builds a player controller, then let the
// signon handshake fall over ("Forcing client reconnect" on repeat) and simply
// never finish it. The controller entity is torn down again, but the net
// channel stays open -- so the client can still push string commands at the
// server while owning no entity at all.
//
// When a `say` arrives from such a slot, the game's chat handler looks up the
// sender, finds nothing, and falls back to entity index 0. That is why the
// message renders as "Console" in chat and logs as "Console<0>" say "..."
// instead of the bot's actual name and userid, and why banning the name or
// filtering the URL never catches it.
//
// It also takes the server's own chat plugins down with it: a CSSharp `say`
// listener that touches the controller (AuthorizedSteamID, a per-player
// dictionary) throws on the invalid entity, so it never returns its HookResult
// and never gets to block anything. The crash is not a side effect, it is what
// clears the way for the message.
//
// So the fix is to answer the question the game itself got wrong, one step
// earlier: does this slot have a live player controller? If not, the command
// never reaches the game.

bool Plugin::IsDeadSlot(int slot)
{
	CGameEntitySystem* pEntitySystem = GameEntitySystem();

	// No entity system means no map is running, and nobody has a controller --
	// but that is also the state where we know the least, so say nothing.
	if (!pEntitySystem)
		return false;

	// Entity indices 1..ABSOLUTE_PLAYER_LIMIT are reserved for player
	// controllers, so an empty one is an empty slot rather than a recycled
	// index belonging to some unrelated entity.
	CEntityInstance* pController = pEntitySystem->GetEntityInstance(CEntityIndex(slot + 1));
	if (!pController)
		return true;

	// Instance still listed but its identity is already gone: the same
	// half-destroyed state, caught one step later.
	return pController->m_pEntity == nullptr;
}

static bool IsGuardedCommand(const char* pszCommand)
{
	if (!pszCommand)
		return false;

	// Only the commands that reach other players. Everything else a
	// controller-less client sends is either harmless or part of a connection
	// attempt we have no business breaking.
	return V_stricmp(pszCommand, "say") == 0
		|| V_stricmp(pszCommand, "say_team") == 0
		|| V_stricmp(pszCommand, "callvote") == 0;
}

void Plugin::Hook_DispatchConCommand(ConCommandRef cmd, const CCommandContext& ctx, const CCommand& args)
{
	const int slot = ctx.GetPlayerSlot().Get();

	// Negative slot is the real server console -- rcon, server.cfg, another
	// plugin. Those are legitimate and stay untouched. This is also the check
	// that keeps the hook off the hot path for everything but client commands.
	if (slot < 0 || slot >= ABSOLUTE_PLAYER_LIMIT)
		RETURN_META(MRES_IGNORED);

	if (args.ArgC() < 1 || !IsGuardedCommand(args.Arg(0)))
		RETURN_META(MRES_IGNORED);

	if (!IsDeadSlot(slot))
		RETURN_META(MRES_IGNORED);

	ReportBlock(slot, args);

	RETURN_META(MRES_SUPERCEDE);
}

void Plugin::ReportBlock(int slot, const CCommand& args)
{
	SlotReport& report = m_Reports[slot];

	const auto tNow = std::chrono::steady_clock::now();

	if (report.bSeen && tNow - report.tLastPrint < kReportInterval)
	{
		++report.nSuppressed;
		return;
	}

	const char* pszNetworkID = g_pEngineServer->GetPlayerNetworkIDString(CPlayerSlot(slot));

	META_CONPRINTF("Blocked \"%s\" from slot %d (%s) -- no player controller\n",
				   args.GetCommandString(),
				   slot,
				   pszNetworkID ? pszNetworkID : "unknown");

	if (report.nSuppressed)
		META_CONPRINTF("... and %u more from slot %d in the last %lld seconds\n",
					   report.nSuppressed,
					   slot,
					   static_cast<long long>(kReportInterval.count()));

	report.tLastPrint = tNow;
	report.nSuppressed = 0;
	report.bSeen = true;
}

///////////////////////////////////////

CGameEntitySystem* GameEntitySystem()
{
	// CGameResourceService::SetEntityResourceManifest
	// str server_entities
	return *CMemory(g_pGameResourceServiceServer).Offset(WIN_LINUX(0x58, 0x50)).RCast<CGameEntitySystem**>();
}

///////////////////////////////////////
const char* Plugin::GetLicense()
{
	return "GPLv3";
}

const char* Plugin::GetVersion()
{
	return VERSION_STRING;
}

const char* Plugin::GetDate()
{
	return BUILD_TIMESTAMP;
}

const char* Plugin::GetLogTag()
{
	return "ConsoleSpamFix";
}

const char* Plugin::GetAuthor()
{
	return "Slynx (˙·٠● S l y n x ●٠·˙)";
}

const char* Plugin::GetDescription()
{
	return "Blocks chat from clients that own no player controller";
}

const char* Plugin::GetName()
{
	return "Console spam fix";
}

const char* Plugin::GetURL()
{
	return "https://slynxdev.cz";
}
