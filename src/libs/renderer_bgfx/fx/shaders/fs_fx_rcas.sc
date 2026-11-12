$input v_uv
// FSR 1 RCAS: robust contrast-adaptive sharpening at the display size (fx_fsr.sh), after EASU or on a native
// picture. FSR_RCAS_DENOISE keeps the dither and fine noise from being sharpened up.
#include "fx_fsr.sh"

AF4 FsrRcasLoadF(ASU2 p) { return fxFsrTexel(p); }
void FsrRcasInputF(inout AF1 r, inout AF1 g, inout AF1 b) {}
#define FSR_RCAS_F 1
#define FSR_RCAS_DENOISE 1
#include "ffx_fsr1.h"

void main()
{
    AF3 c;
    FsrRcasF(c.r, c.g, c.b, fxFsrPixel(v_uv, u_fxFsr[3].zw), fxFsrCon(0));
    gl_FragColor = vec4(c, 1.0);
}
