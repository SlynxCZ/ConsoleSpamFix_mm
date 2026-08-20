# ConsoleSpamFix

**Metamod plugin that blocks chat from CS2 clients which are not fully in game — the "Console" advertising spam.**

## The problem

Advertising bots connect to a CS2 server with real, Steam-validated accounts, get
far enough that the server calls `ClientPutInServer` and builds a player
controller, then let the signon handshake fall over and never finish it:

```
CServerSideClientBase::Connect( name='<advert>.com', userid=16, fake=0, ... )
SV:  "<advert>.com<16><[U:1:745005247]><>" STEAM USERID validated
ClientPutInServer create new player controller [<advert>.com]
Client 16 signon state SIGNONSTATE_CONNECTED -> SIGNONSTATE_NEW
SV:  Forcing client reconnect (SIGNONSTATE_NEW) for client '<advert>.com'
SV:  Forcing client reconnect (SIGNONSTATE_CONNECTED) for client '<advert>.com'
...
```

The controller entity is torn down again, but the net channel stays open, so the
client can keep pushing string commands at the server while owning no entity at
all. When a `say` arrives from such a slot, the game looks up the sender, finds
nothing, and takes its console branch:

```
[All Chat][Console (0)]: 20 free commends for your CS2 profile at <advert>.com
L 08/19/2026 - 13:06:24: "Console<0>" say "20 free commends for your CS2 profile at <advert>.com"
```

That is why the message renders as **Console** rather than the bot's name, and
why kicking players named "Console" or filtering the URL never catches it —
nobody is actually named that, and the filter is not what is running.

It also takes the server's own chat plugins down on the way through. A CSSharp
`say` listener that touches the controller throws on the invalid entity:

```
System.InvalidOperationException: Entity is not valid
   at CounterStrikeSharp.API.Core.CCSPlayerController.get_AuthorizedSteamID()
   at IksAdmin.Main.OnSay(CCSPlayerController player, CommandInfo commandInfo)
```

```
System.Collections.Generic.KeyNotFoundException: The given key
'CounterStrikeSharp.API.Core.CCSPlayerController' was not present in the dictionary.
   at MeowCheckCheats.MeowCheckCheats.@ӣ(CCSPlayerController @ӓ, CommandInfo @Ӕ)
```

A listener that throws never returns its `HookResult`, so it never gets to block
anything. The exception is not a side effect of the spam — it is what clears the
way for it. Any anti-advertising, gag or mute plugin on the server is bypassed
by being crashed rather than beaten.

## What the game actually does

This is not inferred from the symptoms — it is in `libserver.so`. The `say`
ConCommand callback (`sub_17F3F20`) decompiles to:

```c
v2 = ResolvePlayerFromContext(ctx);      // sub_178BE20
if (v2)  Host_Say(v2, args, 0, 0, 0);    // normal path
else     Host_Say(0,  args, 0, 0, 0);    // <-- the hole
```

and the resolve it calls (`sub_178BE20`) is:

```c
v1 = *(_DWORD *)(ctx + 4);               // ctx.GetPlayerSlot()
if (v1 >= 0 && gpGlobals && v1 < gpGlobals->maxClients)
    return GetEntityInstance(entitySystem, v1 + 1);
else
    return 0;
```

Handed a null player, `Host_Say` (`sub_17F3440`) takes its console branch: the
name defaults to the literal string `"Console"`, the userid is a hardcoded `0`,
and the log line comes from a **separate format string** — `"Console<0>" say
"%s"` — that exists in the binary purely for this case, alongside the normal
`"%s<%i><%s><%s>" say "%s"`.

So the chat line is not a spoof and not a mislabel. It is the game's documented
fallback for "the slot this command came from has no entity", reached by a client
that arranges to keep its net channel while losing its player controller.

Two things follow. Steam auth is not the discriminator — these bots pass it, and
`IsClientFullyAuthenticated()` is true for them; CounterStrikeSharp's
`AuthorizedSteamID` only reads null because `RunAuthChecks` never authorizes a
slot its own bookkeeping does not consider connected. And `say_team`
(`sub_17F3E50`) has no such else branch, it simply returns — only `say` reaches
the console path.

## The fix

One SourceHook hook on `ICvar::DispatchConCommand`, which is where every console
command the engine dispatches passes through, client commands included, with the
originating player slot in the command context.

The gate is the one CS2Fixes settled on in
[e5a3557](https://github.com/Source2ZE/CS2Fixes/commit/e5a3557cfb4c6b1a648d784bcc5c1f4622e477ec)
("Fixed chat messages from not fully ingame players sometimes being treated as
console messages"): is this client fully in the game? If not, `say` and
`say_team` are superceded and never reach the game.

The difference is where the answer comes from. CS2Fixes reads it off its own
`ZEPlayer` bookkeeping — `m_bInGame`, set in `CPlayerManager::OnClientPutInServer`.
There is no bookkeeping to consult here, so the client is asked directly:

```cpp
CServerSideClient* pClient = GetClientBySlot(commandPlayerSlot);

// Block chat messages from players not fully ingame, can be interpreted as console messages
if (!pClient || !pClient->IsActive())
{
    META_LOG(this, "Blocked chat message from user ID %i not fully in game\n",
             g_pEngineServer->GetPlayerUserId(commandPlayerSlot).Get());
    RETURN_META(MRES_SUPERCEDE);
}
```

`CServerSideClient::IsActive()` is `m_nSignonState == SIGNONSTATE_FULL`, and the
client itself comes off the game server rather than out of the entity system:

```cpp
CUtlVector<CServerSideClient*>* GetClientList()
{
    CNetworkGameServerBase* pServer = g_pNetworkServerService->GetIGameServer();
    if (!pServer)
        return nullptr;

    return CMemory(pServer).Offset(kClientListOffset).RCast<CUtlVector<CServerSideClient*>*>();
}
```

Everything else passes through untouched: every other command, every `say` from a
client that is fully in game, and anything from the actual server console
(`GetPlayerSlot()` of `-1` — rcon, `server.cfg`, another plugin).

The other half of that commit matters too and is reproduced here: the command
name is compared with `V_stricmp`, not `V_strcmp`. The engine dispatches `SAY`
to the same ConCommand, so a case-sensitive compare is a free bypass.

### How this differs from CS2Fixes

The two tests are close but not identical. The server log puts
`ClientPutInServer` exactly at the `CONNECTED -> NEW` transition:

```
ClientPutInServer create new player controller [<advert>.com]
Client 16 signon state SIGNONSTATE_CONNECTED -> SIGNONSTATE_NEW
```

so the CS2Fixes test is effectively `m_nSignonState >= SIGNONSTATE_NEW`, while
`IsActive()` is `== SIGNONSTATE_FULL`. This one is therefore **stricter**, which
cuts both ways:

- It catches a bot that gets itself as far as `NEW` or `PRESPAWN` and parks
  there. Such a client passes the CS2Fixes check and fails this one — and the
  logs show these bots do touch `NEW` before falling back.
- It also rejects `SIGNONSTATE_CHANGELEVEL`, which is **7**, i.e. *above*
  `FULL`. That is a real player riding out a map change, and their chat is
  blocked for that window. CS2Fixes does not have this gap. Allowing both states
  explicitly (`FULL || CHANGELEVEL`) would close it.

## Scope

This stops the spam. It does **not** disconnect the bot — a client parked in the
reconnect loop keeps its slot until the engine times it out. That is a slot the
engine reclaims on its own, and kicking it early would mean deciding how long a
legitimately slow-loading player is allowed to take.

Two things here are version-dependent and neither fails loudly:

- `kClientListOffset`, the client list at `CNetworkGameServerBase + 0x248` (584,
  same on both platforms), which CS2Fixes keeps in its gamedata as
  `CNetworkGameServer_ClientList`. If a game update moves it, `GetClientList()`
  returns garbage.
- `src/sdk/CServerSideClient.h`, a hand-reconstructed layout rather than
  something the SDK generates. `IsActive()` reading the right four bytes is an
  assumption; on this build `m_nSignonState` sits at offset 100 on Linux, 92 on
  Windows. If the struct shifts, the plugin silently tests a garbage signon state
  and either blocks every chat message on the server or none of them.

Both are worth re-checking against CS2Fixes after a major game update. A
`static_assert` on `offsetof(CServerSideClientBase, m_nSignonState)` would turn
the second one into a build error rather than a runtime surprise.

Only `say` and `say_team` are guarded, matching CS2Fixes. `callvote` from a
not-in-game client is equally nonsensical and would be a one-line addition to
`CCvar_DispatchConCommand`, but it is not part of the observed attack and is left
alone.

## Building

Requires `HL2SDKCS2`, `MMSOURCE_DEV` and `CSGO_PROTO` in the environment.

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DCMAKE_C_COMPILER=clang-18 -DCMAKE_CXX_COMPILER=clang++-18
cmake --build build
```

Output lands in `build/addons/`, laid out ready to copy into the server's
`game/csgo/` directory.

## SourceHook

Like the rest of the family, this plugin carries its **own private SourceHook**
(`vendor/sourcehook`, wired up via `sourcehook_metamod_override.h`) rather than
using metamod's shared instance.

Be aware of what that costs here specifically, because this plugin is the one
case in the family where it is not free. Its single hook is a **vtable** hook on
`ICvar::DispatchConCommand`, and that exact slot is hooked by other plugins on
the same server through metamod's shared engine — CounterStrikeSharp does
`SH_ADD_HOOK_MEMFUNC` on the same method, CS2Fixes and source2toolkit patch it
via KHook.

Two independent `ISourceHook` instances patching one vtable slot do not
coordinate. Whichever patched last is what the slot points at; each treats
whatever it found there as "the original"; and `MRES_SUPERCEDE` only suppresses
its own engine's chain. Two consequences follow:

- **Ordering is decided by plugin load order, not by SourceHook.** The point of
  blocking `say` here is to get in front of a CounterStrikeSharp listener that
  would otherwise throw on the dead controller (see **The problem** above). If
  this plugin's engine patched the slot first and
  metamod's patched over it afterwards, the CSSharp listener runs first anyway
  and the block never protects it. Load this plugin *after* the ones it needs
  to preempt.
- **Unloading is order-sensitive too.** Whichever engine restores the slot last
  writes back the pointer it captured, which may no longer be what the other
  side expects.

If either bites, the fix is not to change this plugin's hook — it is to put both
sides on one engine, either by going back to metamod's shared SourceHook here or
by having the other side join this one via `sourcehook_metamod_shared.h`.
