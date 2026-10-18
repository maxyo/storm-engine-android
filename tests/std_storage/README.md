# Standard storage contracts

Run from the engine worktree with existing read-only dependency sources:

```sh
python3 tests/std_storage/run.py \
  --build-dir build/std-storage/current \
  --dependency-sources /path/to/android-ci/sources \
  --glm-source /path/to/glm
```

Add `--ndk /path/to/android-ndk-r30` to compile the actual
Android ARM64 `entity_manager.cpp`, `entity_container_cache.cpp`, `core_impl.cpp`,
`compiler.cpp` and `internal_functions.cpp` consumers. Outputs are written only to
the chosen build directory; no dependencies, devices, shared caches or existing engine builds are
changed. `--source-root /path/to/baseline` runs the same harness against an
unmodified checkout.

The ASan/UBSan host test links the real EntityManager and EntityContainerCache.
A real VMA factory creates small Entity implementations through the public
`CreateEntity` API. Assertions cover deferred destructor order, index reuse after
both LIFO passes, generation checks, repeated/stale erase, failed `Init()`,
deletions enqueued by a destructor, layer/cache cleanup, `ForEachEntity`, `EraseAll`
reset and 10,000-element growth. No manager internals or replacement storage are
exposed to the test.

`named_event.cpp` runs the real named S_EVENTMSG constructor used by
`CoreImpl::PostEvent` and script `FUNC_POSTEVENT`, with an owned MESSAGE payload,
timer and invalidation. The unmodified baseline intentionally fails this case
with ASan `alloc-dealloc-mismatch`: the name is allocated with `new char[len]`
and was destroyed with scalar delete. Its only assignments in production are
the constructor's array allocation and the default constructor's nullptr; no
consumer reassigns the name. The prerequisite fix pairs it with `delete[]`.
Use `--contract entity_manager` for the passing baseline manager comparison or
`--contract named_event` to reproduce the baseline ownership failure.

`postevents.cpp` tests the real POSTEVENTS_LIST before and after the storage
migration (the pre-migration comparison includes the array-deletion prerequisite).
It checks named events, actual MESSAGE integer/string payloads, 4,096-element
growth, borrowed event pointers across append/erase, preserved insertion order,
middle/head/tail deletion, invalidation, repeat Release and reuse after Release.
The known `RemoveInvalidated` index-reset behavior is deliberately asserted:
adjacent invalidated entries at the front may need a second pass. Event
scheduling is not changed. Release still destroys objects in insertion order.
The owning vector moves only unique_ptrs, so event object addresses stay stable.
There are no copies of POSTEVENTS_LIST in production; ownership is now enforced
by unique_ptr instead of permitting an unsafe implicit shallow copy. `Add` takes
ownership immediately, including if vector growth throws. The previous failed
realloc path had no recoverable contract. The event queue is runtime-only;
save/load code does not serialize its allocation storage.

The migration changes allocation layout from PLF blocks to contiguous indices.
No references to those indices escape EntityManager, and the element is a
trivially copied uint32_t. Both allocators may throw on growth; this change does
not add an out-of-memory recovery contract. Vector clear retains capacity until
manager destruction; IDs, entity destruction, layer order, cache order and the
public API are unchanged. The index stacks are runtime state and are not written
by serialization paths.

These checks do not link or run the full engine or validate gameplay on a device.
