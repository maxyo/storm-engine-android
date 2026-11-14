# Far LODs

Islands, ships and outdoor locations are drawn with fewer triangles where they are far: every object of the model is
split into spatial clusters, every cluster has a chain of simplified levels, and each frame each cluster is drawn at the
coarsest level whose error stays under the preset's pixels (`fx::Settings::far_lod_pixels`: «Низкая» 2, «Средняя»
1.5, «Высокая» 1; graphics option `fx_far_lod` in tenths of a pixel, 0 = off). The levels are made from the player's
own `.gm` files on first load, by [meshoptimizer](https://github.com/zeux/meshoptimizer) 1.3 (MIT,
`third_party/licenses/meshoptimizer-MIT.txt`), and kept in the cache; nothing of the game is shipped.

## The levels (far_lod.hpp, no engine dependencies)

- **Clusters:** the object's triangles in meshoptimizer's spatial order, cut every ~1536 triangles. Triangles that tie in
  the depth test (`DepthTiePairs`: coplanar, overlapping; decals a few millimetres off their surface) go to one cluster.
- **Levels:** level 0 is the cluster's own triangles; level 1..5 are simplified within 0.05, 0.2, 0.8, 3.2, 12.8 m
  (`meshopt_simplifyWithAttributes`, normal and uv0 as attributes, Permissive), then the parts smaller than the error
  that touch no seam are dropped. A level that lost a seam edge is made again with that edge's triangle locked. A level that would keep more than 90 % of the previous one is that one.
- **Only indices:** every level draws the object's own vertices. Vertex buffers, the baked colours (`.col`, by file
  vertex number), lightmap uvs, the collision and its BSP are untouched.
- **No cracks:** a vertex whose position another cluster or another object of the model also uses is locked, so
  neighbours at any two levels still share their edges. Tie triangles are never simplified and come last in every
  level, in the file's order (the game's less-or-equal depth test keeps the later one on top).
- **Cache form:** the cluster table and the indices (`meshopt_encodeIndexBuffer`, ~2-3 bytes per triangle of level 0
  for all levels together). `Decode` checks every range and index against the model.

## In the engine (geometry/src/geom_far_lod.*)

- **Which models:** `far_lod::Wanted`: `islands\`, `ships\`, and `locations\` models of at least 50 m radius
  (outdoor places), never models with animated vertices. A model whose layout fails `CheckMeshLayout` gets none
  (warning).
- **Making them:** on first load a background bake (`fx::PostBake`) reads the cache entry (texture cache directory,
  kind `STMFLOD1`, key: the objects, the triangles and every vertex's position, normal and uv0 — not its colour), or
  builds the levels on the bakes' threads (`fx::ParallelRanges`: nice 10, thermal pause, cancelled when the model is
  released) and writes them. Until then the model is drawn whole; `far LOD: <model>: ...` in the log says when they
  arrive and how, and every failure is an error in the log with the model drawn whole.
- **Choosing:** `NODER::Draw` gives the view in the node's space (`storm/render/far_lod.hpp`): camera, view direction,
  scale, pixels per unit of the target being drawn (its viewport and projection; orthographic too) and the depth range.
  A level's error in pixels is its error over the distance to the cluster's sphere, widened to the screen's corner, so
  turning the camera never changes a level. A cluster goes coarser only when the next level is 1.25× under the limit.
- **Drawing:** an object of one cluster draws its level's range of the model's LOD index buffer. An object of several
  clusters draws a list composed for the chosen levels (nearest cluster first) in an index buffer of its own per pass
  (scene, sea reflection, sea cube faces), recomposed only when a level changes; a depth slice of the island
  (`ISLAND::Realize`) draws only the run of clusters that reaches into its depth range. A buffer is written at most
  once a frame (bgfx applies updates before the frame's draws): a second, different choice in the same frame and pass
  draws that object whole and counts as a conflict.
- **Counters:** the effects profile line and the scene sweep's renderer window: `far_lod_<main|refl|cube>_kt`
  (drawn) and `_full_kt` (what those objects have), composes, compose kB, conflicts, `island_slices`, models pending.

## Checks

- `python3 tests/far_lod/run.py --build-dir build/far-lod`: host check on synthetic models (seconds): bumpy grids
  meeting at a seam, a decal pair, a flat grid, a broken layout; the result must not depend on the threads.
- `python3 tools/game_checks/far_lod/run.py <game directory>`: every model of the game that gets far LODs (game-data
  check, before a release; ~10 min). Both check, per object and cluster (`tests/far_lod/verify.hpp`): level 0 is
  exactly the object's triangles, levels use only their cluster's vertices, every edge shared with another cluster or
  object at level 0 is still drawn at every level, tie triangles stay last and in order, Encode/Decode round trip;
  the game check prints the triangles kept per level, encoded bytes and build time by folder.

Bump `kVersion` whenever `Build` or `Encode` changes their result: every cache entry is made again.
