// Author: Michal Přikryl (Slynx) <github.com/SlynxCZ>

#include "plugin.h"

#include "CServerSideClient.h"

#include "module.hpp"
#include "utils.hpp"

#include "eiface.h"
#include "iserver.h"
#include "playerslot.h"
#include "utlvector.h"
#include "interfaces/interfaces.h"

#include <cstdio>

#define VERSION_STRING SEMVER " @ " GITHUB_SHA
#define BUILD_TIMESTAMP __DATE__ " " __TIME__

using namespace DynLibUtils;

Plugin g_Plugin;
PLUGIN_EXPOSE(Plugin, g_Plugin);

CUtlVector<CServerSideClient*>* GetClientList();
CServerSideClient* GetClientBySlot(CPlayerSlot slot);

SH_DECL_HOOK3_void(ICvar, DispatchConCommand, SH_NOATTRIB, 0, ConCommandRef, const CCommandContext&, const CCommand&);

bool Plugin::Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool late)
{
	PLUGIN_SAVEVARS();
	SH_METAMOD_OVERRIDE_SAVEVARS(id);

	GET_V_IFACE_CURRENT(GetServerFactory, g_pSource2Server, ISource2Server, SOURCE2SERVER_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetEngineFactory, g_pEngineServer, IVEngineServer2, SOURCE2ENGINETOSERVER_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetEngineFactory, g_pCVar, ICvar, CVAR_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetEngineFactory, g_pNetworkServerService, INetworkServerService, NETWORKSERVERSERVICE_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetFileSystemFactory, g_pFullFileSystem, IFileSystem, FILESYSTEM_INTERFACE_VERSION);

	// https://github.com/Source2ZE/CS2Fixes/commit/e5a3557cfb4c6b1a648d784bcc5c1f4622e477ec
	m_iDispatchConCommandHookID = SH_ADD_HOOK(ICvar, DispatchConCommand, g_pCVar, SH_MEMBER(this, &Plugin::CCvar_DispatchConCommand), false);
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

	return true;
}

void Plugin::CCvar_DispatchConCommand(ConCommandRef cmd, const CCommandContext& ctx, const CCommand& args)
{
	CGlobalVars* pGlobalVars = g_pEngineServer->GetServerGlobals();
	if (!pGlobalVars)
		RETURN_META(MRES_IGNORED);

	CPlayerSlot commandPlayerSlot = ctx.GetPlayerSlot();

	bool bSay = !V_stricmp(args.Arg(0), "say");
	bool bTeamSay = !V_stricmp(args.Arg(0), "say_team");

	if (commandPlayerSlot.Get() != -1 && (bSay || bTeamSay))
	{
		CServerSideClient* pClient = GetClientBySlot(commandPlayerSlot);

		// Block chat messages from players not fully ingame, can be interpreted as console messages
		if (!pClient || !pClient->IsActive())
		{
			META_LOG(this, "Blocked chat message from user ID %i not fully in game\n", g_pEngineServer->GetPlayerUserId(commandPlayerSlot).Get());
			RETURN_META(MRES_SUPERCEDE);
		}
	}

	RETURN_META(MRES_IGNORED);
}

CUtlVector<CServerSideClient*>* GetClientList()
{
	CNetworkGameServerBase* pServer = g_pNetworkServerService->GetIGameServer();
	if (!pServer)
		return nullptr;

	return CMemory(pServer).Offset(0x248).RCast<CUtlVector<CServerSideClient*>*>();
}

CServerSideClient* GetClientBySlot(CPlayerSlot slot)
{
	CUtlVector<CServerSideClient*>* pClients = GetClientList();
	if (!pClients)
		return nullptr;

	const int index = slot.Get();
	if (index < 0 || index >= pClients->Count())
		return nullptr;

	return pClients->Element(index);
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
	return "Blocks chat from clients that are not fully in game";
}

const char* Plugin::GetName()
{
	return "Console spam fix";
}

const char* Plugin::GetURL()
{
	return "https://slynxdev.cz";
}
