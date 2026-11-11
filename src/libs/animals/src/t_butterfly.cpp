#include "t_butterfly.h"
#include "storm_glm.h"
#include "core.h"
#include "math_inlines.h"
#include "rands.h"


glm::vec3 TButterfly::center;

//--------------------------------------------------------------------
TButterfly::TButterfly() : minY(0.f), maxY(MAX_HEIGHT), firstDraw(true), time(0.f)
{
}

//--------------------------------------------------------------------
TButterfly::~TButterfly()
{
}

//--------------------------------------------------------------------
void TButterfly::Initialize(const glm::vec3 &_center, float _radius, int32_t _bufferIndex, int _tI, int _tJ)
{
    bufferIndex = _bufferIndex;
    centerPosition.x = _center.x + randCentered(_radius);
    centerPosition.y = _center.y + randCentered(_radius);
    centerPosition.z = _center.z + randCentered(_radius);
    centerVelocity = storm::math::normalizeOrZero(glm::vec3(randCentered(1.0f), 0.f /*randCentered(1.0f)*/, randCentered(1.0f)));
    displaceVector = storm::math::normalizeOrZero(glm::vec3(randCentered(1.0f), randCentered(1.0f), randCentered(1.0f)));
    timeToNextDisplace = static_cast<int32_t>(rand(static_cast<float>(MAX_DISPLACE_TIME)));
    tI = SINGLE_SIZE * _tI;
    tJ = SINGLE_SIZE * _tJ;

    if (rand() & 0x1)
    {
        // active, flying
        fullActiveTime = static_cast<int32_t>(randUpper(static_cast<float>(MAX_ACTIVITY_TIME)));
        activeTime = static_cast<int32_t>(rand(static_cast<float>(fullActiveTime)));
        waitTime = 0;
        active = true;
    }
    else
    {
        // inactive, not flying
        waitTime = static_cast<int32_t>(rand(static_cast<float>(MAX_WAIT_TIME)));
        centerPosition.y = minY;
        fullActiveTime = static_cast<int32_t>(randUpper(static_cast<float>(MAX_ACTIVITY_TIME)));
        activeTime = 0;
        active = false;
    }
}

//--------------------------------------------------------------------
void TButterfly::Calculate(int32_t _dTime, COLLIDE *_collide, entity_container_cref its)
{
    if (!active)
    {
        waitTime -= _dTime;

        if (waitTime < 0)
        {
            // start being active
            fullActiveTime = static_cast<int32_t>(randUpper(static_cast<float>(MAX_ACTIVITY_TIME)));
            activeTime = 0;
            waitTime = 0;
            active = true;
            return;
        }
    }

    // active
    activeTime += _dTime;
    if (activeTime > fullActiveTime)
    {
        if (fabsf(centerPosition.y - minY) < MIN_Y_DELTA)
        {
            // near minY
            centerPosition.y = minY;
            waitTime = static_cast<int32_t>(rand(static_cast<float>(MAX_WAIT_TIME)));
            active = false;
            return;
        }
        // far from minY
        if (centerPosition.y < minY)
        {
            activeTime = 0;
            centerPosition.y = minY;
        }
    }

    const auto timeDelta = static_cast<float>(_dTime) / 1000.0f;

    // calculate new velocity
    timeToNextDisplace -= _dTime;
    if (timeToNextDisplace < 0)
    {
        displaceVector = storm::math::normalizeOrZero(glm::vec3(randCentered(1.0f), 0.f, randCentered(1.0f)));
        timeToNextDisplace = static_cast<int32_t>(rand(static_cast<float>(MAX_DISPLACE_TIME)));
    }
    centerVelocity = storm::math::normalizeOrZero(centerVelocity + (rand(DISPLACE_SPEED) * timeDelta) * displaceVector);

    auto activity = 1.0f - activeTime / fullActiveTime; //~!~
    if (activity < MIN_ACTIVITY)
        activity = MIN_ACTIVITY;

    const auto velocityDelta = /*activity * */ VELOCITY * timeDelta;
    time = fmodf(time + activity * timeDelta * WINGS_TIME_K, PI);
    const auto yDeltaAbs = fabsf(velocityDelta * Y_SPEED);

    // trace and change velocity if needed
    if ((activeTime < fullActiveTime) || (fabsf(centerPosition.y - minY) > (yDeltaAbs + MIN_Y_DELTA)))
    {
        const auto ray = _collide->Trace(
            its, centerPosition,
            centerPosition + glm::vec3(velocityDelta * centerVelocity.x, 0.f, velocityDelta * centerVelocity.z), nullptr,
            0);
        if (ray <= 1.0f)
            centerVelocity = -centerVelocity;
    }

    // calculate new position
    centerPosition.x += velocityDelta * centerVelocity.x;
    centerPosition.z += velocityDelta * centerVelocity.z;

    if (activity > 0.5f)
    {
        const auto probable = ((rand() % RISE_IMPROBABILITY) != 1);
        if (probable)
            centerPosition.y += yDeltaAbs;
        else
            centerPosition.y -= yDeltaAbs;
    }
    else
    {
        const auto probable = ((rand() % FALL_IMPROBABILITY) != 1);
        if (probable)
            centerPosition.y -= yDeltaAbs;
        else
            centerPosition.y += yDeltaAbs;
    }

    if (centerPosition.y > maxY)
        activeTime = fullActiveTime;
    if (centerPosition.y < minY)
        activeTime = 0;

    static auto maxRemoteDistance2 = 1.7f * MAX_REMOTE_DISTANCE;
    if ((fabsf(centerPosition.x - center.x) + fabsf(centerPosition.z - center.z)) > maxRemoteDistance2)
    {
        // teleport near center
        centerPosition.x = center.x + randCentered(MAX_REMOTE_DISTANCE);
        centerPosition.y = center.y + randUpper(MAX_REMOTE_DISTANCE / 10.0f);
        centerPosition.z = center.z + randCentered(MAX_REMOTE_DISTANCE);
    }
}

//--------------------------------------------------------------------
void TButterfly::Draw(HDC _dc)
{
    /*
    float activity;
    if (!active)
      activity = 0.f;
    else
    {
      activity = 1.0f - activeTime / fullActiveTime;
      if (activity < MIN_ACTIVITY)
        activity = MIN_ACTIVITY;
    }

    static int MAIN_LINE_LENGTH = 16.0;
    static int xOffset = 320;
    static int yOffset = 240;
    unsigned char cDelta = centerPosition.y+200;
    HPEN pen = CreatePen(PS_SOLID, (int) 5.0f*centerPosition.y/MAX_Y, RGB(255.0*activity,0,0));
    SelectObject(_dc, pen);

    float ang = atan2(centerVelocity.z, centerVelocity.x);

    int frontX = xOffset + centerPosition.x + 0.5*cos(ang)*MAIN_LINE_LENGTH;
    int frontY = yOffset + centerPosition.z + 0.5*sin(ang)*MAIN_LINE_LENGTH;

    int rearX = xOffset + centerPosition.x + 0.5*cos(ang + PI)*MAIN_LINE_LENGTH;
    int rearY = yOffset + centerPosition.z + 0.5*sin(ang + PI)*MAIN_LINE_LENGTH;

    int leftX = xOffset + centerPosition.x + 0.1*cos(ang + PId2)*MAIN_LINE_LENGTH;
    int leftY = yOffset + centerPosition.z + 0.1*sin(ang + PId2)*MAIN_LINE_LENGTH;

    int rightX = xOffset + centerPosition.x + 0.1*cos(ang - PId2)*MAIN_LINE_LENGTH;
    int rightY = yOffset + centerPosition.z + 0.1*sin(ang - PId2)*MAIN_LINE_LENGTH;

    MoveToEx(_dc, frontX, frontY, 0);
    LineTo(_dc, rearX, rearY);
    MoveToEx(_dc, frontX, frontY, 0);
    LineTo(_dc, leftX, leftY);
    MoveToEx(_dc, frontX, frontY, 0);
    LineTo(_dc, rightX, rightY);

    DeleteObject(pen);
    */
}

//--------------------------------------------------------------------
void TButterfly::Effect(const glm::vec3 &_position)
{
    if (active)
        return;

    if (fabsf(_position.x - centerPosition.x) + fabsf(_position.z - centerPosition.z) > MAX_EFFECT_RADIUS)
        return;

    fullActiveTime = static_cast<int32_t>(randUpper(static_cast<float>(MAX_ACTIVITY_TIME)));
    activeTime = 0;
    waitTime = 0;
    active = true;
}

//--------------------------------------------------------------------
void TButterfly::Draw(IVBufferManager *_ivManager)
{
    uint16_t *iPointer;
    tButterflyVertex *vPointer;
    int32_t vOffset;
    short shortVOffset;

    _ivManager->GetPointers(bufferIndex, &iPointer, (void **)&vPointer, &vOffset);

    if (firstDraw)
    {
        firstDraw = false;
        shortVOffset = static_cast<short>(vOffset);

        iPointer[0] = 0 + shortVOffset;
        iPointer[1] = 1 + shortVOffset;
        iPointer[2] = 3 + shortVOffset;

        iPointer[3] = 0 + shortVOffset;
        iPointer[4] = 3 + shortVOffset;
        iPointer[5] = 2 + shortVOffset;

        iPointer[6] = 2 + shortVOffset;
        iPointer[7] = 3 + shortVOffset;
        iPointer[8] = 5 + shortVOffset;

        iPointer[9] = 2 + shortVOffset;
        iPointer[10] = 5 + shortVOffset;
        iPointer[11] = 4 + shortVOffset;

        vPointer[0].tu = tI;
        vPointer[0].tv = tJ + SINGLE_SIZE;

        vPointer[1].tu = tI;
        vPointer[1].tv = tJ;

        vPointer[2].tu = tI + SINGLE_SIZE;
        vPointer[2].tv = tJ + SINGLE_SIZE;

        vPointer[3].tu = tI + SINGLE_SIZE;
        vPointer[3].tv = tJ;

        vPointer[4].tu = tI;
        vPointer[4].tv = tJ + SINGLE_SIZE;

        vPointer[5].tu = tI;
        vPointer[5].tv = tJ;
    }

    // position = center + glm::vec3(0.2f, 1.f, 0.2f);
    auto alpha = atan2f(centerVelocity.z, centerVelocity.x);
    static glm::vec3 v0(-MODEL_SIDE, 0, -MODEL_SIDE);
    static glm::vec3 v1(-MODEL_SIDE, 0, MODEL_SIDE);
    static glm::vec3 v2(0, 0, -MODEL_SIDE);
    static glm::vec3 v3(0, 0, MODEL_SIDE);
    static glm::vec3 v4(MODEL_SIDE, 0, -MODEL_SIDE);
    static glm::vec3 v5(MODEL_SIDE, 0, MODEL_SIDE);
    // static glm::vec3 v[6] = {v0, v1, v2, v3, v4, v5};

    glm::mat4 moveToPos, rightWingRotate, leftWingRotate, rotateYm;
    moveToPos = storm::math::translation(centerPosition.x, centerPosition.y, centerPosition.z);
    rightWingRotate = storm::math::rotationZ(time);
    leftWingRotate = storm::math::rotationZ(-time);
    // glm::mat4 rotateYm/*.BuildRotateY*/(0.f, PId4/*atan2f(centerVelocity.z, centerVelocity.x)*/, 0.f);
    // glm::mat4 rotateYm(0.f, -PId2+atan2f(centerPosition.z-oldPos.z, centerPosition.x-oldPos.x), 0.f);
    storm::math::vx(rotateYm) = glm::cross(centerVelocity, glm::vec3(0.0f, 1.0f, 0.0f));
    storm::math::vy(rotateYm) = glm::vec3(0.0f, 1.0f, 0.0f);
    storm::math::vz(rotateYm) = centerVelocity;
    // mtx.Pos() = centerPosition;
    // oldPos = centerPosition;
    /*
      for (int i=0; i<6; i++)
      {
        vPointer[i].pos.x = v[i].x*cosf(alpha) + v[i].z*sinf(alpha) + centerPosition.x;
        vPointer[i].pos.z = v[i].z*cosf(alpha) - v[i].x*sinf(alpha) + centerPosition.z;
        vPointer[i].pos.y = centerPosition.y;
      }
    */

    vPointer[0].pos = storm::math::transformPoint(moveToPos * (rotateYm * leftWingRotate), v0);
    vPointer[1].pos = storm::math::transformPoint(moveToPos * (rotateYm * leftWingRotate), v1);
    vPointer[2].pos = storm::math::transformPoint(moveToPos * rotateYm, v2);
    vPointer[3].pos = storm::math::transformPoint(moveToPos * rotateYm, v3);
    vPointer[4].pos = storm::math::transformPoint(moveToPos * (rotateYm * rightWingRotate), v4);
    vPointer[5].pos = storm::math::transformPoint(moveToPos * (rotateYm * rightWingRotate), v5);
}

//--------------------------------------------------------------------
void TButterfly::Draw(VRenderer *_renderer, MODEL *_model)
{
    glm::mat4 moveToPos, rightWingRotate, leftWingRotate;
    moveToPos = storm::math::translation(centerPosition.x, centerPosition.y, centerPosition.z);
    // rightWingRotate.BuildRotateZ(time);
    // leftWingRotate.BuildRotateZ(-time);
    storm::math::vx(_model->mtx) = glm::cross(centerVelocity, glm::vec3(0.0f, 1.0f, 0.0f));
    storm::math::vy(_model->mtx) = glm::vec3(0.0f, 1.0f, 0.0f);
    storm::math::vz(_model->mtx) = centerVelocity;
    storm::math::pos(_model->mtx) = centerPosition;
}

//--------------------------------------------------------------------
//--------------------------------------------------------------------
//--------------------------------------------------------------------
