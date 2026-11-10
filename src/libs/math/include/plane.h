#pragma once

// A clip plane as the renderer and the collision queries exchange it:
// a point p is in front of the plane when Nx*p.x + Ny*p.y + Nz*p.z > D.
struct PLANE
{
    float Nx;
    float Ny;
    float Nz;
    float D;
};
