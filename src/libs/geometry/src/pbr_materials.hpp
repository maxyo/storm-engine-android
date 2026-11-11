// Caribbean Legend only: attaches the sidecar PBR materials of a model (resource\models\<model>.json) to its GEOS.
// Every rejected object is logged with the reason and keeps its legacy material.
#pragma once

#include "dx9render.h"
#include "geos.h"

namespace storm::geometry
{
void AttachPbrMaterials(GEOS &geo, const char *model, VDX9RENDER &render);
}
