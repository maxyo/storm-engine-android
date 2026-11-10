Run `python3 tests/mobile_goods_transfer/run.py`. Compiles `mobile_goods_transfer.hpp` (the export of
the treasurer's purchase list, goodstransfer: one row per good, the selected ship, the `open` action
and the mouse point that opens a good's amount window in GoodsTransfer.c) with sanitizers. The
xinterface glue (`mobile_goods_transfer.inc`) reads the script variables and is Android-only.
