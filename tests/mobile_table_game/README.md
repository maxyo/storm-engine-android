Run `python3 tests/mobile_table_game/run.py`. Compiles `mobile_table_game.hpp` (the export of the
tavern mini-games boal_dice / boal_cards: script images, script strings, script facts) with
sanitizers and checks the widgets and the reason naming missing facts. The xinterface glue
(`mobile_table_game.inc`) reads the script variables and is Android-only.
