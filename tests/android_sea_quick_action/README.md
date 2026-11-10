# Sea quick action contract checks

Run `python3 tests/android_sea_quick_action/run.py` on the host (ASan/UBSan).

`ILogAndActions` offers one quick action at sea: Map, Moor, Board, LandTroops or Reload.
`readSeaQuickAction` (battle_interface) binds it to the main ship and to the identity of
its target; `SeaQuickActionState` publishes `{schemaVersion, activity, revision, available,
action, label}` (state channel `seaQuickAction`) and takes
`seaquick:1\t<activity>\t<revision>\tack\t<0|1>` and `…\tactivate\t<action>`.

The native icon is hidden only while the touch control of exactly this revision says it is
drawn (a 750 ms lease, renewed by repeating the acknowledgement). A tap is accepted once
per revision, for the covered action only; another target, owner or Activity makes a new
revision, so duplicate and stale taps do nothing.
