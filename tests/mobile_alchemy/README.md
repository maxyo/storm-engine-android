Run `python3 tests/mobile_alchemy/run.py`. Compiles `mobile_alchemy.hpp` (the export of the alchemy
screen: the recipe rows of ITEMS_SCROLL without padding, the component facts, the batches the hero and
the pot allow, the pot changes for N batches and the `choose` / `craft` actions of one screen opening)
with sanitizers. The xinterface glue (`mobile_alchemy.inc`) reads the script variables and is
Android-only.
