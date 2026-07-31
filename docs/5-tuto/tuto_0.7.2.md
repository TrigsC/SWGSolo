# Tutorial 0.7.2 — Why `pcall` didn't save you: safe Lua↔C++ object wrapping

**Release:** F_0.7.2 — doctor/entertainer buffer player-chat crash fix.
**Core principle:** In Core3's Lua bindings, constructing a typed wrapper
(`AiAgent(pObj)`) around the wrong concrete object type is not a recoverable
error — it `abort()`s the process. Gate every wrap with a cast-free predicate,
and expose type-specific reads on the *base* Lua class via a native safe cast.

---

## 1. The bug, precisely

The Smart Doctor Buffer's chat handler tested whether the speaker was a sim
bot. The helper (`bin/scripts/screenplays/custom/smartDoctorBuffer.lua`) did:

```lua
local ok, isSimBot = pcall(function()
    return AiAgent(pObj):isSimPlayerBot()   -- pObj can be a real PLAYER
end)
```

`AiAgent(...)` is a Lua global registered by `Luna<LuaAiAgent>::Register`
(`DirectorManager.cpp:894`). Constructing it runs `LuaAiAgent::_setObject`
(`LuaAiAgent.cpp:174`):

```cpp
auto obj = dynamic_cast<AiAgent*>(_getRealSceneObject());
if (realObject != obj) realObject = obj;
E3_ASSERT(!_getRealSceneObject() || realObject != nullptr);   // line 182
```

For a **player**, `_getRealSceneObject()` is non-null but the
`dynamic_cast<AiAgent*>` is `nullptr` (players aren't `AiAgent`s), so the
assertion is `false || false` → `E3_ASSERT` → `sys::e3_assert_internal` →
`abort()` → SIGABRT. That is exactly the backtrace the crash produced, routed
through `ChatManager::broadcastChatMessage` → the `SPATIALCHATSENT` (event 50)
`ScreenPlayObserver` → the Lua chat handler.

## 2. Why the `pcall` was useless

Lua's `pcall` catches **Lua errors** (including C++ exceptions the binding
translates into Lua errors). `E3_ASSERT` does not raise — it calls `abort()`,
a POSIX process signal. There is no stack to unwind, no error object, nothing
for `pcall` to trap. **A `pcall` around a call that can `abort()` is
decorative.** The three nested `lua_pcallk` frames in the backtrace were all
bypassed.

Corollary for release builds: without `DYNAMIC_CAST_LUAOBJECTS`, `_setObject`
uses `static_cast<AiAgent*>(lua_touserdata(...))` — no assert, but a mis-typed
pointer whose later `getSimPlayerBot()` call is undefined behavior. The debug
assert was surfacing a genuine latent bug, not inventing one.

## 3. The fix — two layers

### C++: put the read on the base class, cast safely

`SceneObject` already declares a native, cast-free downcast:
`AiAgent asAiAgent()` (`SceneObject.idl:1577`) — it returns `this` for an
`AiAgent` and `nullptr` for anything else, with no `dynamic_cast` assertion.
We mirror `isSimPlayerBot` onto `LuaCreatureObject` using it
(`LuaCreatureObject.cpp:800`):

```cpp
int LuaCreatureObject::isSimPlayerBot(lua_State* L) {
    AiAgent* agent = realObject != nullptr ? realObject->asAiAgent() : nullptr;
    lua_pushboolean(L, agent != nullptr && agent->getSimPlayerBot());
    return 1;
}
```

Registered next to `isAiAgent` (`LuaCreatureObject.cpp:99`), declared
non-static in the header (`LuaCreatureObject.h:81`) because `Luna` binds an
instance-member pointer and the body reads the instance member `realObject`.
Now any creature can be asked "are you a sim bot?" without ever constructing
an `AiAgent()` wrapper.

### Lua: gate the wrap with a cast-free predicate

Every caller now checks type before wrapping. `smartDoctorBuffer.lua:246`:

```lua
local ok, isSimBot = pcall(function()
    if not SceneObject(pObj):isCreatureObject() then return false end
    return CreatureObject(pObj):isSimPlayerBot()
end)
```

`SceneObject(pObj)` never asserts (any scene object *is* a `SceneObject`), and
`isCreatureObject()` / `isAiAgent()` are plain virtual bool reads. The same
pattern hardens `smart_entertainer_helper.lua:34` (musician/dancer buffers)
and `aiGlobalChatHandler.lua:487`, where the legacy
`LuaAiAgent(pTarget):healCreatureTarget(pPlayer)` is now guarded by
`SceneObject(pTarget):isAiAgent()`.

Behavior is unchanged for genuine bots: an `AiAgent` still resolves through
`asAiAgent()` to the same `getSimPlayerBot()` flag (`SimPlayerManager.cpp:12651`
sets it on hunters). Real players and non-creatures now return `false` and take
the normal negotiation path instead of killing the zone.

## 4. The transferable rule

When you reach across the Lua↔C++ boundary:

1. **Never** construct a typed wrapper (`AiAgent`, `LuaAiAgent`, `BuildingObject`,
   …) around an object you haven't proven is that type. The wrap itself is the
   dangerous operation.
2. Prove type with a **cast-free virtual predicate** on the base wrapper:
   `isCreatureObject()`, `isAiAgent()`, `isPlayerCreature()`.
3. If a read lives only on a derived Lua class, **mirror it onto the base
   class** using the engine's native `as<Type>()` safe cast, so scripts never
   need the risky wrapper at all.
4. Do not rely on `pcall` for safety here — it cannot catch `abort()`.

The already-correct model in the tree was `ai_registry.lua:110`
(`SceneObject(pCreature):isAiAgent()`); F_0.7.2 simply made the buffer scripts
follow it and gave them a safe `isSimPlayerBot` to call.
