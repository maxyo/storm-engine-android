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
| `registry.cpp` | **the migration switch**: ported screens by interface id, ids the platform host still draws, state consumers |
| `router.cpp` | admission, shared extras («Прочее» sheet, note, reading sheet), event dispatch, busy marks, throttled sends, `EngineCommand` |
| `controls.*` | shared view builders: buttons and their roles, action rows, control rows (toggle, segment, slider, stepper, field) |
| `generic.cpp` | the generic auto-layout for interfaces without a ported screen |
| `trade_store.cpp` | reference ported screen: the store |
| `dialogue.cpp` | reference state channel: `dialogue` |

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
     `EditText`, `Read`, `Rebuild()` for local state kept in members. Mark events that are
     expected but need nothing with `bindings.Ignore(id, verb)`.
   Add the file to `CMakeLists.txt`.
3. **Registry.** Add `{"<interface id>", Create…}` to `kPorted` in `registry.cpp` and
   remove the id from `kHostOwned`; delete the old host form in the application.
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

## State channels

`Host::SetState(channel, json)` carries the HUD and telemetry state of the game:
`dialogue`, `gameplay`, `combat`, `ship`, `worldmap`, `landCommands`, `seaOrders`,
`vitals`, `worldInfo`. A zone implements `StateConsumer` (`Channel()`, `OnState(json,
router)`) and registers it in `CreateStateConsumers()`. `dialogue.cpp` is the pattern: it
turns the state into an `InterfaceModel` of its own and lets a ported screen draw it.

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
