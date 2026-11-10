# Screens: game interfaces in the portable UI

The engine describes every open game interface (a menu screen) as an `InterfaceModel`
(`include/storm_ui/interface_model.h`): the interface id and its widgets in reading order.
`Host::SetInterface` routes the model to a *screen*: C++ code here that turns the widgets
into the props of a pack screen (`<s-…>`, application repository
`mobile-ui/design/screens/<name>/`) and component events back into engine actions.

```
engine ──InterfaceModel──▶ Router ──admission──▶ ported Screen ─┐
                                   └─(unknown id)─▶ generic layout ─┤─▶ props ─▶ <s-…> in the pack
engine ◀──────Action {screen, opening, node, index, verb, value}──┘◀─ events (id, verb, value)
```

| File | Role |
|---|---|
| `screen.h` | the contracts: `Screen`, `NodeRule`, `Admission`, `Bindings`, `Router`, `StateConsumer`, `Surface` |
| `registry.cpp` | ported screens by interface id (every other interface gets the generic layout), state consumers |
| `router.cpp` | admission, shared extras («Прочее» sheet, note, reading sheet), event dispatch, busy marks, throttled sends, `EngineCommand` |
| `controls.*` | shared view builders: buttons and their roles, action rows, control rows (toggle, segment, slider, stepper, field) |
| `generic.cpp` | the generic auto-layout for interfaces without a ported screen |
| `trade_crew.cpp` | crew hire: the trade layout with one always-open row |
| `trade_shipyard.cpp` | shipyard: catalogue, ship card, the game's windows as sheets; sends the engine's `yard@` capability tokens, one per model revision |
| `trade_commerce.cpp` | reference ported screens: store, contraband, cannons, items trade, storage, chests — one layout, a column table per interface |
| `extra_games.cpp` | tavern dice and cards (`boal_dice`, `boal_cards`): the felt from the script images and facts the engine exports (`ANDROID_TABLE_GAME`) |
| `extra_goods.cpp` | the treasurer's purchase list (`goodstransfer`): rows the engine exports (`ANDROID_GOODS_ORDER`), a standing order saved as the amount settles (`order` action) |
| `extra_repair.cpp` | self repair (`repair`): hull, masts, sails as amounts sent as the game's arrow presses, materials per amount |
| `extra_transfer.cpp` | loot and exchange (`transfer_main`): cargo by pack arrows, crew tab, the game's windows as modals / a sheet; the question window's script mode from `ANDROID_TRANSFER`; dangerous answers held |
| `extra_sails.cpp` | sail emblems (`sailsgerald`): the emblem ribbon (`SCROLL_GERALD` rows with texture paths) and one contextual primary |
| `extra_alchemy.cpp` | alchemy (`alchemy`): recipe rows the engine exports (`ANDROID_ALCHEMY_RECIPE`, `choose`), the components and N batches crafted by one guarded `craft` action (`ANDROID_ALCHEMY`) |
| `hero_ship.cpp` | the ship (`ship`): overview, crew, hold over the one stock screen; the game's windows (drop overboard, cannons by side, shares, the morale and exchange questions) as our controls; facts from the adapter `android_hero.c` (`ANDROID_SHIP`, `ANDROID_SHARES`) |
| `dialogue.cpp` | state channel `dialogue`: speech paged by `<ui-pages>` (pure rules in `../text_pages.h`), replies in two columns, the answer editor (text through the IME bridge, or an amount on the number panel), the menu button (`pause` to the platform host) |
| `menu_game_over.cpp` | state channel `gameOver` (`{kind: sea\|land\|defeat, language}` or `null`, engine xinterface `info_handler.cpp`): the game-over screen |
| `maps.cpp` | the game's maps (`mapview`, `mapbest`, `papermap`): c-map-view (pan, pinch, zoom; the game's markers), a tap is `point` in the picture's base units, the colony sheet from the game's colony window, `release` closes it |
| `engine_notice.cpp` | state channel `engineNotice` (`{kind, subject}`): what the engine could not do, told as a toast (`video_unavailable`: no video decoder on this platform yet) |
| `launcher.cpp` | the application's own screens (launcher, download library, about) from the platform host's `launcher` channel; the contract is at the top of the file |

The design rules are GENERIC-LAYOUT (pipeline, type → component, button roles, templates,
verbs, §8 admission and extras) and the V2 mockups of the design canvas.

## Adding a screen

1. **Pack.** `mobile-ui/design/screens/<name>/spec.json` (tag `s-<name>`, one prop per
   part of the view, plus `"extras": {"type": "json"}`), `template.rml` built only from
   shared `<c-…>` components and the screen parts (`s-extras`, `s-note`, `s-control`,
   `s-block`, `s-actions`), `style.rcss` with layout only (flex, sizes in dp or %, colours
   as tokens). The root is `<c-screen id="header" key="header" … actions="{{ json(extras.actions) }}">`
   and ends with `<s-extras id="extras" extras="{{ json(extras) }}"/>`; put
   `<s-note id="extras-note" text="{{extras.note}}"/>` under the main block. Nothing may
   assume 915 × 412: the viewport is the real dp size of the display.
2. **C++.** A class derived from `Screen` in `src/screens/<zone>_<name>.cpp`:
   - `Component()` → `"s-<name>"`;
   - `Nodes()` → the KNOWN list: every node the design draws with the type it expects
     (`"a|b"` alternatives, `NAME*` prefix); `required = true` marks REQUIRED;
   - `Build(admitted, bindings)` → the props (JSON) for the pack screen. Read widgets with
     `admitted.One/All/Label`; bind every interactive element by its element id:
     `bindings.Forward(id, widget, "press")`, `bindings.On(id, verb, handler)` and inside a
     handler `bindings.Send(widget, verb, value)`, `SendLatest` (drags), `Back(exit)`,
     `EditText` / `AskText` (the screen gets the typed text), `Read`, `Rebuild()` for local
     state kept in members, `Platform(verb)` for a request to the platform host (`pause`),
     `SendHost(node, value)` for a setting of the application itself (an `Action` without a
     screen that the platform host keeps; `app-settings` = the renderer options). A screen
     with a modal of its own returns true from `OwnsBack()` and closes it in `Back()`: the
     back key then goes there first (`Host::WantsBack`). Mark events that are
     expected but need nothing with `bindings.Ignore(id, verb)`.
   Add the file to `CMakeLists.txt`.
3. **Registry.** Add `{"<interface id>", Create…}` to `kPorted` in `registry.cpp`. Until
   then the generic layout draws the interface; the platform host draws none.
4. **Tests.** Record a model (below), add it to `tests/models/`, extend
   `tests/screens_check.cpp` (admission, the props that matter, every action mapping).

Element ids: an event carries the id of the nearest ancestor that has one. Components
inside lists report under the list's id with their own verb (`goods row 3`,
`goods change 150`); header parts report under `header` (`section ship`,
`subject-next`), its buttons as `header-back`, `header-close`.

Verbs and what the engine receives (`EngineCommand`, today's queue line
`widget:<screen>\t<node>\t<index>\t<wire>`):

| verb | value | wire |
|---|---|---|
| `press`, `left`, `right`, any other | empty | the verb |
| `select`, `activate`, `open`, … | row identity | `verb@<opening>:<row id>` |
| `toggle` | `1` / `0` | the value |
| `edit` | the text | the value |
| `set` | slider position | `set@<opening>:<position>` |
| any, with `guard` | — | `guard@<opening>:<guard row>\|<wire>` |
| `back` without a node | — | `back` |

## What the router does for every screen

- **Admission.** All REQUIRED nodes present with their type → the ported screen. Otherwise
  the generic layout draws the interface and an error names the missing nodes
  (`Router::Rejection()`, logged once per node set, a toast in debug hosts).
- **Extras.** Widgets a ported screen does not know: buttons, choices and controls become
  rows of the «Прочее» sheet under the header «⋯»; texts become one parchment note.
  Logged once per (screen, node set): `screen store extras=B_GUILD,GUILD_NOTE`.
- **Busy until answered (R22).** An event that sent something marks its element
  (`bindings.Pending(id)`); the mark goes with the next model revision or after 1.5 s.
- **Incremental update.** Every revision sets new props on the same mounted screen; the
  component tree is patched in place, so scroll offsets and pressed parts survive.
- **Nothing is dropped quietly.** Malformed models, a missing pack screen, an event
  without a binding and a state channel without a consumer are error log lines and, with
  `Config::show_errors`, toasts.

## UI strings

Screens never contain text for the player. Texts the game sends (captions, names,
prices) pass through; everything else is a key of the pack's string table
(`mobile-ui/design/strings/<lang>.json`, one table per shipped language):

- C++: `bindings.Text("trade.price")`; a key kept in a table goes through
  `StringKey("trade.store")` so the pack build still sees it;
- templates: `{{ tr('common.next') }}`.

The table of the interface's game language is used (`InterfaceModel::language`). To add a
string: add the key to every `strings/<lang>.json` (the design build fails if one
language lacks it or if a used key is undefined), keep the zone prefix (`trade.`,
`crew.`, `common.` for shared words). A language without tables and a key missing at run
time are errors in the log (and toasts in debug hosts); English or the key itself is
shown, never nothing. The word lists in `controls.cpp` (`kExitLabels`,
`kConfirmLabels`, `kDangerWords`) that recognise button roles are matching data, not UI
text. They are a marked C++ table (not part of the pack) holding the words of all six
languages (ru, en, de, es, fr, it); add a language's words there together with its string
table. `Roles` in `tests/screens_check.cpp` has one case per language.

## Game pictures in rows

Widgets name pictures by group and picture of the game's `pictures.ini`
(`ImageRef{kind "atlas", group, image}`). `PictureOf(image)` (controls.h) turns that into
the row image `{kind: "item", texture: "atlas:GROUP/Picture"}`; `<ui-game-image>` resolves
it at run time through `<game root>/RESOURCE/INI/interfaces/pictures.ini`
(`picture_atlas.h`) to the texture and rectangle. Nothing of the game is bundled; without
a game root the neutral placeholder stays.

A picture the engine names by file (a map, an interlude picture, a map item) comes as
`ImageRef{kind "texture", image "interfaces/maps/kayman", uv}`; `TextureOf(image)` (controls.h)
gives `{texture, uv}` for `<ui-game-image texture uv [fit="contain|cover"]>`, and `MapViewOf`
the props of `c-map-view` for a scrolled picture (`map` widget and its `map_marker` rows).

## State channels

`Host::SetState(channel, json)` carries the state of the game outside interfaces. A zone
implements `StateConsumer` (`Channel()`, `OnState(json, router)`) and registers it in
`CreateStateConsumers()`. `dialogue.cpp` turns its state into an `InterfaceModel` of its
own and lets a ported screen draw it. The HUD channels are consumed in `../hud`; the
platform host's `appSettings` (its renderer options) is read by the settings screen.

## The HUD

`src/hud` draws the in-game HUD under game interfaces, from state channels:

```
engine ──state JSON──▶ consumer (states.cpp) ──typed States──▶ Scene::Build ──View──▶ mount (ui_host.cpp)
engine ◀──"<verb>:<value>" (Action, screen "hud")── Hud::Send ◀── Input handlers ◀── component events
```

| File | Role |
|---|---|
| `hud.h` | the contracts: `States`, `View`, `Input`, `Scene`, `SceneEntry`, `Mount`, `Hud`, the layout |
| `hud.cpp` | **the migration switch** `kScenes` (gameplay mode → scene), visibility, event dispatch, the event log |
| `states.cpp` | one consumer per channel: the engine's JSON checked once, kept typed; bad data is rejected aloud |
| `land.cpp` | the land scene: explore and combat sets, the action sheet, the sword-move wheel |
| `spyglass.cpp` | the spyglass card (`<s-hud-spyglass>`): laid by the Hud as the view's `overlay` over any scene, and alone where no scene owns the mode; acknowledged per drawn frame (`spyglass:<generation>`) so the engine hides its own info |
| `world_map.cpp` | the world-map scene: fleet, compass (a tap shows the details beside it), helm slider, sail lever (held SailUp/SailDown), camera, menu, the map's commands |
| `sea.cpp` | the sea scene: sailing and naval battle, fleet, minimap (the engine's radar in its disc), sail lever, helm slider, guns and ammunition fan, quick action, orders sheet |

Channels (JSON as the engine publishes it unless noted):

| channel | shape | state |
|---|---|---|
| `gameplay` | `{schemaVersion: 1, revision, mode: explore\|melee\|sailing\|naval\|worldmap\|unknown, language, inputOwner, analogMovement, movementGeneration, capabilities: […]}` | `Gameplay` |
| `combat` | `{schemaVersion: 2, gameplayRevision, activityGeneration, playerId, mode, actions: {name: generation}, interactKind, pistol: {state, progress 0…20}}`; a command carries the activity and the action's own generation, so an action appearing or going away (`interact` as people walk past) fences only itself | `Combat` |
| `vitals` | `{health, energy, reload: 0…1, loaded, capacity, effects: [poison\|drunk\|healing], potions, officers: [{id, name, face, health}], dash?: 0…1}` or `null` | `Vitals` |
| `worldInfo` | a JSON string: the engine's location lines, `\n` between them | `States::world_info` |
| `contextAction` | a JSON string: the script's quick action name, `""` = none | `States::context_action` |
| `landCommands` | `{schemaVersion: 1, sceneEpoch, revision, activityGeneration, available, active, complete, stage, pendingLabel, selectedItemId, canBack, canClose, items: [{id, kind, label, source, enabled, cooldown}]}` or `null` | `LandCommands` |
| `spyglass` | `{schemaVersion: 1, revision, on, generation, target: null \| {kind: ship\|fort, name, type, class, hull, sails, crew, cannons, cannonsMax, charge, nation, sail, speed, distance, captain, cannonType, portrait, skills: {…}}}`, fields the telescope cannot show left out | `Spyglass` |
| `ship` | the engine's ship telemetry (`android_ship_telemetry.hpp`): `{schemaVersion: 1, gameplayRevision, sequence, hudRevision, valid, …own ship, fleet: […], navigator…, shipSpeedKnots, windSpeed, cannonCharge, timeScale, abilities: [{id, state: ready|active|recovery|blocked, fill}], ammo, ammoCount, ammoCounts, sailState, canSetSails, dateTimeText, locationText}` | `Ship` |
| `seaOrders` | `{schemaVersion: 1, sceneEpoch, revision, activityGeneration, available, active, complete, stage: commands\|options\|targets, recipientId, selectedItemId, pendingLabel, canBack, canClose, recipients: […], items: [{id, kind, label, source, enabled, cooldown}]}` or `null` | `SeaOrders` |
| `seaQuickAction` | `{schemaVersion: 1, activity, revision, available, action: Map\|Moor\|Board\|LandTroops\|Reload, label}` | `SeaQuick` |
| `worldMap` | the engine's world-map frame (`android_worldmap.hpp`): `{schemaVersion: 1, revision, activity, gameplayRevision, available, mask, commandsReady, shipsVisible, instrumentsVisible, date, hour, windForce, windAngle, heading, speed, morale, foodDays, rumDays, coordinates, flag, stormState, ships: […], commands: [{id, label}]}` | `WorldMap` |
| `presentation` | written by the platform host: `{screen, loading, cinematic, dialogue, live}` | `Presentation` |

The HUD is visible when a scene draws the gameplay mode, `presentation` reports a free game
(no screen, loading, cutscene or dialogue of the platform host) and no interface of the UI
is mounted. While `live` is false it is drawn but takes no input, and the scene gives up
what it holds (`Released`, `Suspended`).

A scene builds a `View`: the layout scene of the pack's `hud-layout.json`, the live props of
its elements by id (an element left out is not mounted), an optional sheet (a pack part,
modal or not), elements to dim. The mount patches elements in place, so an element that the
old and the new view share keeps its pointer (the stick survives explore ↔ combat). Layout
props come first, the scene's props on top; a prop the scene stops giving returns to its
default. `Input::On(id, verb, handler)` binds component events; an event without a binding,
a layout scene or element the pack lacks, and a channel carrying something else than its
contract are errors (log, toast in debug hosts), never quiet drops.

Input goes out with `Hud::Send(verb, value, slot)`: the engine command is `<verb>:<value>`
(`move`, `combat`, `landcommands`, `context`; at sea `hold` (held controls: helm, spyglass),
`sails`, `gameplay`, `seaquick`, `orders`, `shiphud`, `navigator`; on the world map `hold` and `worldmap`); a `slot` marks a latest-value input (stick
position, coverage acknowledgement) of which a queueing host keeps only the newest. Three
verbs are for the platform host: `shortcut` (`mainmenu`, `character`), `presence`
(`shown` / `hidden`) and `pause` (its pause menu; the dialogue's menu button, through
`Bindings::Platform`). Sequences, owners and revisions are formed here; the platform adds
nothing.

Touches over the game are decided per pointer at pointer-down: `Host::WantsTouch(x, y)` is
true on an interactive HUD element (what the behaviours report as `Regions`), anywhere
while a modal sheet, an interface or a plain document is shown, and false elsewhere, so the
game keeps the rest (camera look). It is answered from the layout of the last frame and is
thread-safe. The land move stick floats: its layout element is the zone (anchor and size in
`hud-layout.json`, drawn under every other element), the stick rests at `restLeft` /
`restBottom` and appears under the thumb that lands anywhere in the zone where no other
control is hit; plates that only show are `pointer-events: none`, so they do not shield
it. A layout element may add a behaviour: `"behavior": "hold"` (reports `down` /
`up`) or `"swipe"` (a drag picks a move; `data` → `data-swipe-*`).

Remaining fallbacks, each logged and tested: a context action name the land table lacks
keeps its button with the generic icon (info line, once); a land menu the engine marks as
not drawable, or with a kind this runtime does not know, stays the engine's own menu (error
line, no coverage acknowledged); `storm_ui_shot` without a `presentation` state uses an
idle game in front and says so.

### Porting a scene (sea, world map)

1. Add the channel structs to `States` and a consumer per channel to `states.cpp`
   (`AddStateConsumers`): parse strictly, `hud.Reject(...)`-style errors for bad data.
2. Write `Scene` in `src/hud/<zone>.cpp`: `Build` sets `view.layout` (`sea`, `sea-battle`,
   `world-map`), the props of the elements and the bindings; timed work in `Tick`, coverage
   acknowledgements in `Drawn` (only a drawn frame may hide the engine's own indicators),
   `Released` / `Suspended` for cancelled input and a lost screen, `Holds` when the engine
   reports no mode while something of the scene is open.
3. Add `{"<name>", modes, Create…}` to `kScenes`. From then on `HostOwnsHud(mode)` is false
   and the platform host must not draw that mode's HUD; delete its code there.
4. Pack: elements and their offsets are already in `hud-layout.json`; add `behavior` /
   `data` where a control is held or swiped, and sheets as `screens/hud-…` parts.
5. Tests: synthetic states in `tests/states`, a scene check like `tests/hud_land_check.cpp`
   (recording mount for the scene, the runtime with `tests/pack` for touches).

```sh
build/ui-host/storm_ui_shot --pack <app>/build/ui-pack --size 915x412 --density 2 --out land.png \
    --state gameplay=tests/states/gameplay-explore.json --state combat=tests/states/combat-explore.json \
    --state vitals=tests/states/vitals.json --script "wants 96 329; down 1 96 329; move 1 96 280; wait 0.3"
```

## Trying a screen on the build host

```sh
python3 <app>/mobile-ui/design/build.py                       # the pack
cmake --build build/ui-host && ctest --test-dir build/ui-host --output-on-failure
build/ui-host/storm_ui_shot --pack <app>/build/ui-pack \
    --interface src/libs/ui/tests/models/store-deal.json --size 915x412 --density 2 \
    --out store.png --script "down 1 600 216; up 1 600 216; wait 0.3; shot row.png"
```

`--state dialogue=file.json` feeds a state channel instead; script commands
`interface <json>` (the next revision), `text <answer>` (accept the editor request).
Engine actions are printed as `command widget:store<TAB>TABLE_LIST<TAB>1<TAB>activate@7:…`.
Render at 780×360, 824×412, 961×412, 1024×768 and 1280×800 (`--density 1`) before
calling a layout done. Models in `tests/models` are synthetic; never commit a model
recorded from a game with its texts unless it holds nothing but interface strings.
