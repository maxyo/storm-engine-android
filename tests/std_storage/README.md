# Standard storage contracts

Run from the engine worktree with existing read-only dependency sources:

```sh
python3 tests/std_storage/run.py \
  --build-dir build/std-storage/current \
  --dependency-sources /path/to/android-ci/sources
```

Add `--ndk /path/to/android-ndk-r30 --glm-source /path/to/glm` to compile the actual
Android ARM64 `entity_manager.cpp`, `entity_container_cache.cpp`, `core_impl.cpp`
and `compiler.cpp` consumers. Outputs are written only to the chosen build
directory; no dependencies, devices, shared caches or existing engine builds are
changed. `--source-root /path/to/baseline` runs the same harness against an
unmodified checkout.

The ASan/UBSan host test links the real EntityManager and EntityContainerCache.
A real VMA factory creates small Entity implementations through the public
`CreateEntity` API. Assertions cover deferred destructor order, index reuse after
both LIFO passes, generation checks, repeated/stale erase, failed `Init()`,
deletions enqueued by a destructor, layer/cache cleanup, `ForEachEntity`, `EraseAll`
reset and 10,000-element growth. No manager internals or replacement storage are
exposed to the test.

The migration changes allocation layout from PLF blocks to contiguous indices.
No references to those indices escape EntityManager, and the element is a
trivially copied uint32_t. Both allocators may throw on growth; this change does
not add an out-of-memory recovery contract. Vector clear retains capacity until
manager destruction; IDs, entity destruction, layer order, cache order and the
public API are unchanged. The index stacks are runtime state and are not written
by serialization paths.

These checks do not link or run the full engine or validate gameplay on a device.
