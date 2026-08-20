//====== Copyright �, Valve Corporation, All rights reserved. =======
//
// Purpose: CEconItem, a shared object for econ items
//
//=============================================================================

#ifndef CSERVERSIDECLIENT_H
#define CSERVERSIDECLIENT_H
#ifdef _WIN32
#pragma once
#endif

#include <utlstring.h>
#include <eiface.h>
#include <inetchannel.h>
#include <steam/steamclientpublic.h>
#include <netadr.h>
#include <networkbasetypes.pb.h>
#include <networksystem/inetworksystem.h>
#include <network_connection.pb.h>

class INetMessage;
class CNetworkGameServerBase;
class CNetworkGameServer;
class CUtlSlot;

enum CopiedLockState_t : int32
{
	CLS_NOCOPY = 0,
	CLS_UNLOCKED = 1,
	CLS_LOCKED_BY_COPYING_THREAD = 2,
};

template <class MUTEX, CopiedLockState_t L = CLS_UNLOCKED>
class CCopyableLock : public MUTEX
{
	typedef MUTEX BaseClass;

public:
	// ...
};

class CUtlSignaller_Base
{
public:
	using Delegate_t = CUtlDelegate<void(CUtlSlot*)>;

	CUtlSignaller_Base(const Delegate_t& other) :
		m_SlotDeletionDelegate(other) {
	}
	CUtlSignaller_Base(Delegate_t&& other) :
		m_SlotDeletionDelegate(Move(other)) {
	}

private:
	Delegate_t m_SlotDeletionDelegate;
};

class CUtlSlot
{
public:
	using MTElement_t = CUtlSignaller_Base*;

	CUtlSlot() :
		m_ConnectedSignallers(0, 1) {
	}

private:
	CCopyableLock<CThreadFastMutex> m_Mutex;
	CUtlVector<MTElement_t> m_ConnectedSignallers;
};

class CServerSideClientBase : public CUtlSlot, public INetworkChannelNotify, public INetworkMessageProcessingPreFilter
{
public:
	virtual ~CServerSideClientBase() = 0;

public:
	CPlayerSlot              GetPlayerSlot() const { return m_nClientSlot; }
	CPlayerUserId            GetUserID() const { return m_UserID; }
	CEntityIndex             GetEntityIndex() const { return m_nEntityIndex; }
	CSteamID                 GetClientSteamID() const { return m_SteamID; }

	virtual void             Connect(int socket, const char* pszName, int nUserID, INetChannel* pNetChannel, uint8 nConnectionTypeFlags, uint32 uChallengeNumber) = 0; // bool bFakePlayer = !nConnectionTypeFlags || (nConnectionTypeFlags & 8) != 0;
	virtual void             Inactivate(const char* pszAddons) = 0;
	virtual void             Reactivate(CPlayerSlot nSlot) = 0;
	virtual void             SetServer(CNetworkGameServer* pNetServer) = 0;
	virtual void             Reconnect() = 0;
	virtual void             Disconnect(ENetworkDisconnectionReason reason, const char* pszInternalReason) = 0;
	virtual bool             CheckConnect() = 0;
	virtual void             Create(CPlayerSlot& nSlot, CSteamID nSteamID, const char* pszName) = 0;
	virtual void             SetRate(int nRate) = 0;
	virtual void             SetUpdateRate(float fUpdateRate) = 0;
	virtual int              GetRate() = 0;

	virtual void             Clear() = 0;

	virtual bool             ExecuteStringCommand(const void* msg) = 0; // "false" trigger an anti spam counter to kick a client.
	virtual bool             SendNetMessage(const CNetMessage* pData, NetChannelBufType_t bufType = BUF_DEFAULT) = 0;

	// "Client %d(%s) tried to send a RebroadcastSourceId msg.\n"
	virtual bool             FilterMessage(const CNetMessage* pData, INetChannel* pChannel) = 0; // On Windows, this function is in a separate virtual table

public:
	virtual void             ClientPrintf(PRINTF_FORMAT_STRING const char*, ...) = 0;

	bool                     IsConnected() const { return m_nSignonState >= SIGNONSTATE_CONNECTED; }
	bool                     IsInGame() const { return m_nSignonState == SIGNONSTATE_FULL; }
	bool                     IsSpawned() const { return m_nSignonState >= SIGNONSTATE_NEW; }
	bool                     IsActive() const { return m_nSignonState == SIGNONSTATE_FULL; }

	virtual bool             IsFakeClient()  const = 0;
	virtual bool             IsHumanPlayer() const = 0;

	// Is an actual human player or splitscreen player (not a bot and not a HLTV slot)
	virtual bool             IsHearingClient(CPlayerSlot nSlot) const = 0;
	virtual bool             IsProximityHearingClient()  const = 0;
	virtual bool             IsLowViolenceClient()  const = 0;

	virtual bool             IsSplitScreenUser()  const = 0;

public: // Message Handlers
	virtual bool             ProcessTick(const void* msg) = 0;
	virtual bool             ProcessStringCmd(const void* msg) = 0;

public:
	virtual bool             ApplyConVars(const void* list) = 0;

private:
	virtual bool             unk_28() = 0;

public:
	virtual bool             ProcessSpawnGroup_LoadCompleted(const void* msg) = 0;
	virtual bool             ProcessClientInfo(const void* msg) = 0;
	virtual bool             ProcessBaselineAck(const void* msg) = 0;
	virtual bool             ProcessLoadingProgress(const void* msg) = 0;
	virtual bool             ProcessSplitPlayerConnect(const void* msg) = 0;
	virtual bool             ProcessSplitPlayerDisconnect(const void* msg) = 0;
	virtual bool             ProcessCmdKeyValues(const void* msg) = 0;

private:
	virtual bool             unk_36() = 0;
	virtual bool             unk_37() = 0;

public:
	virtual bool             ProcessMove(const void* msg) = 0;
	virtual bool             ProcessVoiceData(const void* msg) = 0;
	virtual bool             ProcessRespondCvarValue(const void* msg) = 0;

	virtual bool             ProcessPacketStart(const void* msg) = 0;
	virtual bool             ProcessPacketEnd(const void* msg) = 0;
	virtual bool             ProcessConnectionClosed(const void* msg) = 0;
	virtual bool             ProcessConnectionCrashed(const void* msg) = 0;

public:
	virtual bool             ProcessChangeSplitscreenUser(const void* msg) = 0;

private:
	virtual bool             unk_47() = 0;
	virtual bool             unk_48() = 0;
	virtual bool             unk_49() = 0;

public:
	virtual void             ConnectionStart(INetChannel* pNetChannel) = 0;

private: // SpawnGroup something.
	virtual void             unk_51() = 0;
	virtual void             unk_52() = 0;

public:
	virtual void             ExecuteDelayedCall(void*) = 0;

	virtual bool             UpdateAcknowledgedFramecount(int tick) = 0;
	void                     ForceFullUpdate()
	{
		// For some reason, it doesn't work.
		// UpdateAcknowledgedFramecount(-1);
		m_nDeltaTick = -1;
	}

	virtual bool             ShouldSendMessages() = 0;
	virtual void             UpdateSendState() = 0;

	virtual const void* GetPlayerInfo() const = 0;

	virtual void             UpdateUserSettings() = 0;
	virtual void             ResetUserSettings() = 0;

private:
	virtual void             unk_60() = 0;

public:
	virtual void             SendSignonData() = 0;
	virtual void             SpawnPlayer() = 0;
	virtual void             ActivatePlayer() = 0;

	virtual void             SetName(const char* name) = 0;
	virtual void             SetUserCVar(const char* cvar, const char* value) = 0;

	virtual void             FreeBaselines() = 0;

	virtual CServerSideClientBase* GetSplitScreenOwner() { return m_pAttachedTo; }

	virtual int              GetNumPlayers() = 0;

	virtual void             ShouldReceiveStringTableUserData() = 0;

private:
	virtual void             unk_70(CPlayerSlot nSlot) = 0;
	virtual void             unk_71() = 0;
	virtual void             unk_72() = 0;

public:
	virtual int              GetHltvLastSendTick() = 0;

private:
	virtual void             unk_74() = 0;
	virtual void             unk_75() = 0;
	virtual void             unk_76() = 0;

public:
	virtual void             Await() = 0;

	virtual void             MarkToKick() = 0;
	virtual void             UnmarkToKick() = 0;

	virtual bool             ProcessSignonStateMsg(int state) = 0;
	virtual void             PerformDisconnection(ENetworkDisconnectionReason reason) = 0;

public:
	CUtlString m_UserIDString;
	CUtlString m_Name;
	CPlayerSlot m_nClientSlot;
	CEntityIndex m_nEntityIndex;
	CNetworkGameServerBase* m_Server;
	INetChannel* m_NetChannel;
	uint8 m_nUnkVariable;
	bool m_bMarkedToKick;
	SignonState_t m_nSignonState;
	bool m_bSplitScreenUser;
	bool m_bSplitAllowFastDisconnect;
	int m_nSplitScreenPlayerSlot;
	CServerSideClientBase* m_SplitScreenUsers[4];
	CServerSideClientBase* m_pAttachedTo;
	bool m_bSplitPlayerDisconnecting;
	int m_UnkVariable172;
	bool m_bFakePlayer;
	bool m_bSendingSnapshot;
	[[maybe_unused]] char pad6[0x5];
	CPlayerUserId m_UserID = -1;
	bool m_bReceivedPacket;
	CSteamID m_SteamID;
	CSteamID m_UnkSteamID;
	CSteamID m_UnkSteamID2;
	CSteamID m_nFriendsID;
	ns_address m_nAddr;
	ns_address m_nAddr2;
	KeyValues* m_ConVars;
	bool m_bUnk0;

private:
	[[maybe_unused]] char pad273[0x28];

public:
	bool m_bConVarsChanged;
	bool m_bIsHLTV;

private:
	[[maybe_unused]] char pad29[0xB];

public:
	uint32 m_nSendtableCRC;
	uint32 m_uChallengeNumber;
	int m_nSignonTick;
	int m_nDeltaTick;
	int m_UnkVariable3;
	int m_nStringTableAckTick;
};

//#ifdef __linux__
//COMPILE_TIME_ASSERT(offsetof(CServerSideClientBase, m_nEntityIndex) == 76);
//COMPILE_TIME_ASSERT(offsetof(CServerSideClientBase, m_nSignonState) == 100);
//COMPILE_TIME_ASSERT(offsetof(CServerSideClientBase, m_SteamID) == 171);
//COMPILE_TIME_ASSERT(offsetof(CServerSideClientBase, m_nDeltaTick) == 348);
//#else
//COMPILE_TIME_ASSERT(offsetof(CServerSideClientBase, m_nEntityIndex) == 68);
//COMPILE_TIME_ASSERT(offsetof(CServerSideClientBase, m_nSignonState) == 92);
//COMPILE_TIME_ASSERT(offsetof(CServerSideClientBase, m_SteamID) == 163);
//COMPILE_TIME_ASSERT(offsetof(CServerSideClientBase, m_nDeltaTick) == 340);
//#endif

// class CServerSideClient: public CServerSideClientBase, CUtlSlot, INetworkChannelNotify, INetworkMessageProcessingPreFilter
class CServerSideClient : public CServerSideClientBase
{
public:
	virtual ~CServerSideClient() = 0;
};

#endif // CSERVERSIDECLIENT_H
