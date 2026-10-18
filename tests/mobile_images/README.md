# Native mobile widget images (schema 2)

The engine owns resource lookup, atlas resolution and CPU image decoding. The app
receives immutable opaque handles, never paths or renderer texture pointers. No
commercial textures belong in this repository or APK. Test user resources only
from their local installation, as shown below.

## Additive snapshot contract

The existing `showMobileWidgets(byte[])` JSON fields remain available unchanged.
Top-level additions are `schemaVersion:2`, `screenRevision` (new interface lifetime,
including reopening the same screen), `revision` (changed serialized content), and
`language` (the current game's `VSTRSERVICE::GetLanguage()` value, e.g. `russian` or
`english`; empty if not initialized). This is authoritative game state, not the
Android locale or a list of available translations. Game labels remain localized
UTF-8 unchanged. App chrome may map known language names to its own translation
catalog, with an explicit fallback; do not infer installed translations from old
language.ini registry entries.

TABLE remains `type:"choice"`, with its old tab-joined `label`, zero-based `index`
and `value:"1"` for selected / `"0"` otherwise. Additions:

- `rowId`: exact `trN.UserData.ID`, or empty when the script supplies no ID.
- `cells`: ordered objects `{column,text,colorArgb,images}`; columns are zero-based.
- `images`: array of `{handle,imageRevision,width,height,tintArgb}` references.
- `actions`: permitted `select` and, only when the node's actual native command
  configuration allows it, `activate`.

Ordinary PICTURE nodes export `type:"image"` and `images` only after the app exposes
both optional image methods (old forms otherwise render unknown types as blank rows). They retain source path
metadata through INI loads and runtime picture changes. Video and GPU-only sources
are intentionally omitted. `SAVEIMG` retains its old `type:"save"` behavior.
Dimensions are zero when unknown before decoding; the callback gives actual output
size. `tintArgb` is separate native vertex tint metadata; returned RGBA is untinted.

Only enabled actions may be dispatched. Existing ordinary button/picture `press`
uses the existing action path. For a TABLE request use the existing widget-action
transport with `value="select@<screenRevision>:<rowId>"` or
`value="activate@<screenRevision>:<rowId>"`. Keep its node and zero-based index.
Empty row ID means an empty suffix after `:`. Do not parse labels for semantics.
Legacy unversioned `select` remains supported for old apps.

`activate` first selects, then queues an internal continuation. XINTERFACE resolves
the node again after the selection callback, checks screen epoch, row ID/index and
selection, and invokes the configured activate/double-click command. This matters
for Ship cargo: selection alone does not open the quantity window. The app must
not send a separate select followed by an assumed activation. Back/cancel and modal
visibility remain the game's existing native commands.

## Optional Java image methods

Old apps may omit both methods; absence is detected without recurring JNI exception
logging and does not prevent the old bridge loading.

```java
long[] pollMobileImageRequest(); // null if empty
// Exact array: requestId, screenRevision, handle, imageRevision, maxEdge
void onMobileImage(long requestId, long screenRevision, long handle,
                   long imageRevision, int width, int height,
                   byte[] straightRGBA, String error);
```

IDs must be positive; `maxEdge` is 1..512. Output rows are top-left-origin, tightly
packed `width*4` bytes in **R,G,B,A** order, straight alpha. Copy to an Android bitmap
using explicit RGBA-to-ARGB conversion (do not assume native ByteBuffer integer
order). The app may premultiply once when creating the bitmap. An error has zero
width/height, empty bytes and a nonempty ASCII message. Successful error text is
empty. Validate request ID + screenRevision + handle + imageRevision when binding;
a changed snapshot revision alone must not discard an otherwise current portrait.
Callbacks originate on the game thread: post to the Activity UI thread, then
recheck the binding/Activity lifecycle there before changing a View. Never retain
an Activity through an image request. On destroy, clear Java requests and bindings.

Native uses one weak Activity reference, refreshes optional method IDs when the
Activity changes, deletes local JNI refs per call, and drops replies for a different
Activity generation or screen lifetime. Screen close clears queued IO/descriptors/
cache, releases the weak reference and invalidates running worker results.

## Resource and memory bounds

- Engine VFS only, on the game thread. Match renderer TX-first path semantics,
  falling back to the named raw TGA/DDS file. No Java filesystem duplicate.
- No texture creation, GPU readback, renderer locks or resource polling every200ms.
  Snapshots register metadata; actual IO happens only for requested cache misses.
- At most4 Java request polls/frame,16 queued requests,1 reader and1 CPU worker.
  A frame opens at most2 candidate paths and reads at most128KiB. This bounds IO
  volume, not device-specific filesystem latency; runtime performance still needs
  measurement. Worker jobs contain owned encoded bytes or immutable CPU pixels.
-16MiB combined source/output LRU; at most4096 immutable descriptors per screen.
  Encoded file<=16MiB, decoded source<=16MiB, dimensions<=8192 and <=4M pixels;
  output<=512² RGBA. A running worker can temporarily pin an evicted source.
  Worst-case pixel buffers are bounded separately from the LRU: cached16MiB +
  encoded16MiB + decoded16MiB + output1MiB. Descriptor/JSON overhead is additional.
- Repeated missing/unsupported resources are negative-cached for that screen.
  User texture files are treated as immutable during a screen lifetime. Reopen the
  screen to retry a changed/replaced file. Handles are never reused across screens.
- TX: A8R8G8B8, X8R8G8B8, RGB565, A1R5G5B5, A4R4G4B4, L8, BC1/BC2/BC3
  (DXT1/3/5). Validate the declared mip chain; decode mip zero. Reject palette,
  cubemap, DXT2/4 premultiplied and unsupported formats.
- DDS: legacy BC1/2/3 with complete declared blocks/mips, or single-mip uncompressed
  RGB masks with explicit row pitch. Reject DX10, volume/cube, non-contiguous or
  overlapping masks and unsupported uncompressed mip chains.
- TGA:24/32-bit truecolor, raw/RLE, all horizontal/vertical origin combinations.
  Reject indexed/interleaved forms and RLE overrun/truncation.
- Atlas rectangles resolve from XSERVICE metadata without texture reference counts.
  UV crop preserves top-left orientation and optional flips; floor/ceil covers
  native half-texel inset edges. Resize uses bounded nearest-neighbor sampling,
  preserving straight alpha and exact icon colors (no artificial opaque matte).

## Validation

```sh
python3 tests/mobile_images/run.py
python3 tests/mobile_images/run.py --compiler clang++ # requires Clang sanitizer runtimes
python3 tests/mobile_images/run.py --local-texture /path/to/user/goods.tga.tx
python3 tests/mobile_images/compile_android.py --reference-build /path/to/existing/engine-build
```

The standalone host test compiles production decoder/cache/schema with ASan/UBSan.
It checks channels, alpha endpoints/interpolation, padded DDS rows, BC edge blocks,
TGA origins/RLE, truncation/mips, crop/flips/half-texel coverage, output bounds,
immutable handles, epoch invalidation, descriptor cap and combined LRU eviction.
Optional local raw-TX validation compares every output byte with a separate Python
reference decoder and prints its hash. Neither assets nor decoded output are kept.
The ARM64 checker reads a Ninja compile command without building/modifying its
reference directory; all new objects are compiled in a temporary directory.

This is a bounded TABLE/PICTURE stage. SCROLLIMAGE/VIMAGESCROLL carousel images,
video textures, GPU-pointer/save-thumbnail images, and game-specific semantic
progress bars require separate adapters. A native object build and host tests do
not establish Android UI or runtime acceptance. Integration must verify Character
portrait/icons, actual Ship cargo activate→quantity→cancel, rapid screen switching,
Activity recreation, stale responses, old-app compatibility and memory/IO behavior
with real user textures. Do not enable an unsupported UI action to fill a gap.

If a host Clang installation lacks sanitizer runtimes, `--no-sanitize` allows an
explicit compiler-only second run; retain a separate sanitized GCC/Clang run.
