// Pre-change BlendFrame; retain operation order for the differential oracle.
void Track::baseline(int32_t frame, float kBlend, Quaternion &res)
{
    if (numFrames <= 0)
        return;
    const auto f0 = frame;
    const auto f1 = frame + 1;
    if (f0 >= numFrames || f1 >= numFrames)
    {
        GetFrame(numFrames - 1, res);
        return;
    }
    Quaternion q0, q1;
    GetFrame(f0, q0);
    GetFrame(f1, q1);
    if (kBlend < 0.0f)
        kBlend = 0.0f;
    if (kBlend > 1.0f)
        kBlend = 1.0f;
    res.SLerp(q0, q1, kBlend);
}
