# ConsoleSpamFix

**Metamod plugin that blocks chat from CS2 clients which own no player controller — the "Console" advertising spam.**

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
all. When a `say` arrives from such a slot, the game's chat handler looks up the
sender, finds nothing, and falls back to entity index 0:

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

## The fix

One SourceHook hook on `ICvar::DispatchConCommand`, which is where every console
command the engine dispatches passes through, client commands included, with the
originating player slot in the command context.

The plugin answers the question the game itself got wrong, one step earlier: does
this slot have a live player controller? If not, `say`, `say_team` and `callvote`
are superceded and never reach the game. Everything else — every other command,
every command from a slot with a real controller, and anything from the actual
server console — is passed through untouched.

The check is deliberately fail-open. Anything the plugin cannot positively
identify as a dead slot is treated as a real player, so a wrong guess costs one
spam line rather than the server's entire chat.

Blocks are logged, rate-limited per slot to one line every 5 seconds with a
suppressed-message count, so the log does not simply inherit the flood:

```
[ConsoleSpamFix] Blocked "say 20 free commends ..." from slot 16 ([U:1:745005247]) -- no player controller
[ConsoleSpamFix] ... and 43 more from slot 16 in the last 5 seconds
```

## Scope

This stops the spam. It does **not** disconnect the bot — a client parked in the
reconnect loop keeps its slot until the engine times it out. Kicking clients that
never reach `SIGNONSTATE_FULL` needs the client list off `CNetworkGameServerBase`,
which is a version-dependent offset; this plugin stays on interfaces metamod
hands it so it cannot break on a game update.

## Building

Requires `HL2SDKCS2`, `MMSOURCE_DEV` and `CSGO_PROTO` in the environment.

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DCMAKE_C_COMPILER=clang-18 -DCMAKE_CXX_COMPILER=clang++-18
cmake --build build
```

Output lands in `build/addons/`, laid out ready to copy into the server's
`game/csgo/` directory.

Unlike the other plugins in this family, this one uses **metamod's shared
SourceHook**, not a private vendored instance. The hook is a vtable hook on
`ICvar`, and other plugins on the same server hook that same interface — two
independent `ISourceHook` instances patching one vtable slot do not coordinate
their trampolines, so the engine that owns the chain has to be the one everybody
else is already using.
