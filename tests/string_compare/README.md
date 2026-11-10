# Case-insensitive strings

```sh
python3 tools/ci/run_host_tests.py --only string_compare
```

Builds and runs `src/libs/util/testsuite/string_compare.cpp` (Catch2) under ASan/UBSan. Besides
ASCII case folding it covers cp1251 and UTF-8 Cyrillic bytes: they are compared byte for byte,
never folded, ordered as unsigned char after ASCII (as `std::char_traits<char>`), and hashed
consistently with `iStrComparator`. `storm::FoldCase` replaces `std::toupper(char)`, which is
undefined for the negative values those bytes have as signed char.
