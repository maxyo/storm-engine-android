//============================================================================================
//    Spirenkov Maxim, 2001
//--------------------------------------------------------------------------------------------
//    Sea Dogs II
//--------------------------------------------------------------------------------------------
//    WdmRenderObject
//--------------------------------------------------------------------------------------------
//
//============================================================================================

#pragma once

#include "attributes.h"
#include "renderer.h"

class WdmRenderObject
{
    // --------------------------------------------------------------------------------------------
    // Construction, destruction
    // --------------------------------------------------------------------------------------------
  public:
    WdmRenderObject();
    virtual ~WdmRenderObject();

    // Calculations
    virtual void Update(float dltTime){};
    // Rendering
    virtual void PRender(VRenderer *rs){};

    virtual void MRender(VRenderer *rs){};

    virtual void LRender(VRenderer *rs){};

    static void DeleteAllObjects();

    bool killMe;
    bool isEnablePause;

    // --------------------------------------------------------------------------------------------
    // Encapsulation
    // --------------------------------------------------------------------------------------------
  private:
    WdmRenderObject *prevObject;
    WdmRenderObject *nextObject;
    static WdmRenderObject *firstObject;
};
