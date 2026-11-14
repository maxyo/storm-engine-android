//============================================================================================
//    Spirenkov Maxim, 2001
//--------------------------------------------------------------------------------------------
//    Sea Dogs II
//--------------------------------------------------------------------------------------------
//    WdmCamera
//--------------------------------------------------------------------------------------------
//
//============================================================================================

#pragma once

#include "renderer.h"

class WdmCamera
{
    // --------------------------------------------------------------------------------------------
    // Construction, destruction
    // --------------------------------------------------------------------------------------------
  public:
    WdmCamera();
    virtual ~WdmCamera();

    // --------------------------------------------------------------------------------------------
    // Moving the camera
    // --------------------------------------------------------------------------------------------
  public:
    void Init(float defAy, float defHeight = -1.0f);
    void Move(float dltTime, VRenderer *rs);

    // --------------------------------------------------------------------------------------------
    // Control functions
    // --------------------------------------------------------------------------------------------
  protected:
    virtual void CtrlProcess(float dltTime) = 0;
    virtual float MoveLeftRight(float dltTime) = 0;
    virtual float MoveUpDown(float dltTime) = 0;
    virtual float RotLeftRight(float dltTime) = 0;
    virtual float ZoomInOut(float dltTime) = 0;
    virtual bool CurrentFreeMode() = 0;
    virtual bool GetHightHeight(float &height) = 0;

    //--------------------------------------------------------------------------------------------
    //
    //--------------------------------------------------------------------------------------------
  public:
    glm::vec3 pos;
    glm::vec3 ang;
    float realHeight;
    float hgh;

    bool lock;

    // --------------------------------------------------------------------------------------------
    // Encapsulation
    // --------------------------------------------------------------------------------------------
  private:
    void CheckRange();

  private:
    float elasticAy;

    bool isFreeMode;
    float freeHeight; // the free camera's eye height: the game's (GetHightHeight), lowered by a pinch on Android
    float lastAng;
    glm::vec3 bndPos;
    float kBlendPos;

    bool isInit;
};
