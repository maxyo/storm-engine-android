# MESSAGE mismatch diagnostics

Run the real `MESSAGE` implementation with existing read-only dependency sources:

```sh
python3 tests/message_diagnostics/run.py \
  --dependency-sources /path/to/android-ci/sources \
  --glm-source /path/to/pinned/glm-1.0.3
```

The host ASan/UBSan check covers expected versus actual format symbols, a
zero-based parameter index, the message format and first integer command code,
empty/exhausted reads, preservation of the cursor after rejection, successful
reads/reset, owned vararg payloads, and context enrichment that still propagates
the original error. It compiles `src/libs/core/src/message.cpp` directly.

Native `CoreImpl::Send_Message` adds the destination entity ID and registered
receiver class when a format error crosses it. Script `SendMessage` adds the
same information plus the script source file/line and collects the script call
stack. These delivery boundaries rethrow the error; they do not substitute
values or continue gameplay after an invalid message. Diagnostic strings are
formatted only on the exceptional path. Message string/pointer payload values
are not included.

The test validates the generic diagnostic path. Reproducing a particular save's
failure still requires the corresponding runtime dispatch; this check does not
claim to reproduce that save or establish the failing game callsite.
