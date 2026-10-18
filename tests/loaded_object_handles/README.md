# Loaded script objects and runtime entity handles

Run with existing read-only dependency sources:

```sh
python3 tests/loaded_object_handles/run.py \
  --dependency-sources /path/to/android-ci/sources \
  --glm-source /path/to/glm
```

`--source-root /path/to/baseline` checks the same contract against another
checkout. All generated code, executables and sanitizer outputs use a temporary
directory. No full engine build, native libraries or device are required.

The save format serializes script `VAR_OBJECT` attributes and a legacy 64-bit
runtime entity ID. Native entities themselves are not serialized: state loading
erases them, then script `OnLoad` recreates the required scene entities using
`CreateEntity`. The legacy ID must still be consumed for byte compatibility,
but cannot be used as a binding in the new scene. Generation counters protect
handles within one process; they cannot validate handles read from another
process's save file.

The ASan/UBSan test reproduces an ID collision between two independent real
`EntityManager` instances. It loads the persisted object and proves that a
string group command cannot reach an unrelated receiver expecting a numeric
message code. Object attributes, object references, attribute references and
object-array elements survive restoration. It then explicitly recreates the
group receiver with the same attributes, as script `CreateEntity` does, and
checks message delivery and visibility through both restored reference types.
The receiver fixture uses the group string protocol; it does not substitute for
an on-device test of the full location AI.

To keep this check bounded, the runner extracts the complete production bodies
of `ReadVariable`, `ReadAttributesData`, `ReadData`, `ReadVDword` and `ReadString`
from `compiler.cpp`. Only their class qualifier is renamed to `SaveReader`.
The surrounding fixture supplies a buffer, codec and diagnostics. It links the
real `DATA`, `ATTRIBUTES`, `VarTable`, `EntityManager` and `MESSAGE` implementations.
The temporary copy of `data.cpp` replaces its unused `core_impl.h` include to
avoid instantiating the inline full-engine singleton. No production method body
is replaced or reimplemented.

In the stock script lifecycle, location AI saves group relations to attributes
and deletes `CharactersGroups` on land exit. It recreates the entity and loads
those saved attributes on the next location entry. Consequently a sea save can
legitimately contain group attributes without a live group entity. Loading such
an object must preserve its saved data without manufacturing a runtime entity.
