/* The implementation of voxelizer.h (karimnaaji/voxelizer, MIT; pinned in ../CMakeLists.txt). Only its
 * triangle/box overlap test (Akenine-Möller) is used, by ao_volume.cpp: the library's own grid builders keep every
 * voxel in a 4096-bucket hash table with one allocation each and step the grid by float accumulation, which is
 * too slow for a bake on entering a location and not exactly aligned to our cell grid. */
#define VOXELIZER_IMPLEMENTATION
#include "voxelizer.h"

/* C entry for ao_volume.cpp: the library's triangle type lives in its implementation section. */
int storm_vx_triangle_box_overlap(const float centre[3], const float half[3], const float a[3], const float b[3],
                                  const float c[3])
{
    vx_vertex_t box_centre, box_half;
    vx_triangle_t triangle;
    memcpy(box_centre.v, centre, sizeof(box_centre.v));
    memcpy(box_half.v, half, sizeof(box_half.v));
    memcpy(triangle.p1.v, a, sizeof(triangle.p1.v));
    memcpy(triangle.p2.v, b, sizeof(triangle.p2.v));
    memcpy(triangle.p3.v, c, sizeof(triangle.p3.v));
    return vx__triangle_box_overlap(box_centre, box_half, triangle);
}
