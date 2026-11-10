// Manual micro-benchmark of hot math operations (not a ctest).
// Everything goes through legacy_api.h, so the same source measures the
// current GLM spelling and (run.py --baseline) an archived class API: mt:: is
// the CVECTOR/CMatrix generation, mt::m3:: the math3d one.
// Usage: bench [repeats [name-substring]] -> "name ns/op checksum" lines.
// The best of `repeats` short batches is reported (thread CPU time where
// available), which stays usable on a busy host. The checksum is a parity aid
// when comparing header revisions.
#include "legacy_api.h"
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>
#if defined(__unix__) || defined(__APPLE__)
#include <time.h>
#define STORM_BENCH_THREAD_CLOCK 1
#endif

namespace
{
constexpr int kCount = 1024;
int gRepeats = 40;
const char *gFilter = nullptr;

double nowNs()
{
#ifdef STORM_BENCH_THREAD_CLOCK
    timespec ts;
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) == 0)
        return double(ts.tv_sec) * 1e9 + double(ts.tv_nsec);
#endif
    return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

#if defined(__GNUC__) || defined(__clang__)
template <typename T> inline void keep(T &value)
{
    asm volatile("" : : "g"(&value) : "memory");
}
#else
template <typename T> inline void keep(T &value)
{
    static volatile const void *sink;
    sink = &value;
}
#endif

template <typename Body> void measure(const char *name, int opsPerPass, Body body)
{
    if (gFilter && !std::strstr(name, gFilter))
        return;
    double best = 1e300;
    double checksum = 0;
    // Calibrate the number of passes to ~2 ms batches.
    int passes = 1;
    for (;;)
    {
        const double start = nowNs();
        for (int pass = 0; pass < passes; ++pass)
            checksum = body();
        if (nowNs() - start > 2e6 || passes > (1 << 24))
            break;
        passes *= 2;
    }
    for (int repeat = 0; repeat < gRepeats; ++repeat)
    {
        const double start = nowNs();
        for (int pass = 0; pass < passes; ++pass)
            checksum = body();
        const double perOp = (nowNs() - start) / (double(passes) * opsPerPass);
        if (perOp < best)
            best = perOp;
    }
    std::printf("%-28s %9.3f ns/op  checksum=%.9g\n", name, best, checksum);
}

double sum(const mt::Mat4 &m)
{
    double total = 0;
    for (int i = 0; i < 16; ++i)
        total += mt::data(m)[i];
    return total;
}
double sum(const mt::Vec3 &v)
{
    return double(v.x) + v.y + v.z;
}
// mt::m3 types are the mt:: ones in the current generation and separate
// classes in a baseline, hence the distinct names.
namespace m3 = mt::m3;
double sum3(const m3::Mat4 &m)
{
    double total = 0;
    for (int i = 0; i < 16; ++i)
        total += m3::data(m)[i];
    return total;
}
double sum3(const m3::Vec3 &v)
{
    return double(v.x) + v.y + v.z;
}
double sum3(const m3::Quat &q)
{
    return double(q.x) + q.y + q.z + q.w;
}
} // namespace

int main(int argc, char **argv)
{
    if (argc > 1)
        gRepeats = std::atoi(argv[1]);
    if (argc > 2)
        gFilter = argv[2];
    std::mt19937 rng(0x42454e43u);
    std::uniform_real_distribution<float> values(-10.f, 10.f), angles(-3.f, 3.f), unit(-1.f, 1.f);

    std::vector<mt::Mat4> ca(kCount), cb(kCount), cr(kCount);
    std::vector<m3::Mat4> ma(kCount, m3::identity()), mb(kCount, m3::identity()), mr(kCount, m3::identity());
    std::vector<mt::Vec3> cv(kCount), cw(kCount), cres(kCount);
    const m3::Vec3 zero3 = m3::vec(0.f, 0.f, 0.f);
    std::vector<m3::Vec3> pv(kCount, zero3), pw(kCount, zero3), pres(kCount, zero3);
    std::vector<mt::ProjPoint> cproj(kCount);
    const m3::Quat identityQuat = m3::quat(0.f, 0.f, 0.f, 1.f);
    std::vector<m3::Quat> qa(kCount, identityQuat), qb(kCount, identityQuat), qr(kCount, identityQuat);
    std::vector<int> hits(kCount);
    std::vector<float> blend(kCount), scalars(kCount);
    for (int i = 0; i < kCount; ++i)
    {
        float in[12];
        for (float &value : in)
            value = values(rng);
        ca[i] = mt::eulerMatrix(in[0] * .3f, in[1] * .3f, in[2] * .3f, in[3], in[4], in[5]);
        cb[i] = mt::eulerMatrix(in[6] * .3f, in[7] * .3f, in[8] * .3f, in[9], in[10], in[11]);
        ma[i] = m3::eulerMatrix(m3::vec(in[0] * .3f, in[1] * .3f, in[2] * .3f), m3::vec(in[3], in[4], in[5]));
        mb[i] = m3::eulerMatrix(m3::vec(in[6] * .3f, in[7] * .3f, in[8] * .3f), m3::vec(in[9], in[10], in[11]));
        const float x = values(rng), y = values(rng), z = values(rng);
        cv[i] = mt::Vec3(x, y, z);
        pv[i] = m3::vec(x, y, z);
        const float wx = values(rng), wy = values(rng), wz = values(rng);
        cw[i] = mt::Vec3(wx, wy, wz);
        pw[i] = m3::vec(wx, wy, wz);
        qa[i] = m3::quatFromMatrix(ma[i]);
        qb[i] = m3::quatFromMatrix(mb[i]);
        blend[i] = unit(rng) * .5f + .5f;
    }
    std::vector<m3::Color> colors(kCount, m3::color(0.f, 0.f, 0.f, 0.f));
    std::vector<uint32_t> packed(kCount);
    for (int i = 0; i < kCount; ++i)
    {
        const float r = unit(rng), g = unit(rng), b = unit(rng), a = unit(rng);
        colors[i] = m3::color(r * 1.5f, g * 1.5f, b * 1.5f, a * 1.5f);
    }
    const mt::Mat4 cprojection = mt::projectionMatrix(1.2f, 1600.f, 900.f, .5f, 2000.f);

    // --- vector/matrix API through legacy_api.h ---
    measure("mat4 mulRowInto", kCount, [&] {
        keep(ca); keep(cb);
        for (int i = 0; i < kCount; ++i)
            mt::mulRowInto(cr[i], ca[i], cb[i]);
        keep(cr);
        return sum(cr[kCount - 1]);
    });
    measure("mat4 mulRow", kCount, [&] {
        keep(ca); keep(cb);
        for (int i = 0; i < kCount; ++i)
            cr[i] = mt::mulRow(ca[i], cb[i]);
        keep(cr);
        return sum(cr[kCount - 1]);
    });
    measure("mat4 transformPoint", kCount, [&] {
        keep(ca); keep(cv);
        for (int i = 0; i < kCount; ++i)
            cres[i] = mt::transformPoint(ca[i], cv[i]);
        keep(cres);
        return sum(cres[kCount - 1]);
    });
    measure("mat4 inverseTransformPoint", kCount, [&] {
        keep(ca); keep(cv);
        for (int i = 0; i < kCount; ++i)
            cres[i] = mt::inverseTransformPoint(ca[i], cv[i]);
        keep(cres);
        return sum(cres[kCount - 1]);
    });
    measure("mat4 inverseTransformNormal", kCount, [&] {
        keep(ca); keep(cv);
        for (int i = 0; i < kCount; ++i)
            cres[i] = mt::inverseTransformNormal(ca[i], cv[i]);
        keep(cres);
        return sum(cres[kCount - 1]);
    });
    measure("mat4 invertRigid", kCount, [&] {
        keep(ca);
        for (int i = 0; i < kCount; ++i)
        {
            cr[i] = ca[i];
            mt::invertRigid(cr[i]);
        }
        keep(cr);
        return sum(cr[kCount - 1]);
    });
    measure("mat4 transpose3x3", kCount, [&] {
        keep(ca);
        for (int i = 0; i < kCount; ++i)
        {
            cr[i] = ca[i];
            mt::transpose3x3(cr[i]);
        }
        keep(cr);
        return sum(cr[kCount - 1]);
    });
    measure("mat4 eulerMatrix", kCount, [&] {
        keep(cv); keep(cw);
        for (int i = 0; i < kCount; ++i)
            cr[i] = mt::eulerMatrix(cv[i].x, cv[i].y, cv[i].z, cw[i].x, cw[i].y, cw[i].z);
        keep(cr);
        return sum(cr[kCount - 1]);
    });
    measure("mat4 viewMatrix", kCount, [&] {
        keep(cv); keep(cw);
        const mt::Vec3 up(0.f, 1.f, 0.f);
        for (int i = 0; i < kCount; ++i)
            mt::viewMatrix(cr[i], cv[i], cw[i], up);
        keep(cr);
        return sum(cr[kCount - 1]);
    });
    measure("mat4 projectPoints", kCount, [&] {
        keep(cv);
        mt::projectPoints(cprojection, cv.data(), cproj.data(), kCount, 800.f, 450.f, sizeof(mt::Vec3),
                          sizeof(mt::ProjPoint));
        keep(cproj);
        return double(cproj[kCount - 1].x) + cproj[kCount - 1].y + cproj[kCount - 1].z + cproj[kCount - 1].rhw;
    });
    measure("vec3 normalizeOrZero", kCount, [&] {
        keep(cv);
        for (int i = 0; i < kCount; ++i)
            cres[i] = mt::normalizeOrZero(cv[i]);
        keep(cres);
        return sum(cres[kCount - 1]);
    });
    measure("vec3 length", kCount, [&] {
        keep(cv);
        for (int i = 0; i < kCount; ++i)
            scalars[i] = mt::length(cv[i]);
        keep(scalars);
        return double(scalars[kCount - 1]);
    });
    measure("vec3 lengthXZ", kCount, [&] {
        keep(cv);
        for (int i = 0; i < kCount; ++i)
            scalars[i] = mt::lengthXZ(cv[i]);
        keep(scalars);
        return double(scalars[kCount - 1]);
    });
    measure("vec3 divScalar", kCount, [&] {
        keep(cv); keep(blend);
        for (int i = 0; i < kCount; ++i)
            cres[i] = mt::divScalar(cv[i], blend[i] + 1.f);
        keep(cres);
        return sum(cres[kCount - 1]);
    });
    measure("vec3 cross", kCount, [&] {
        keep(cv); keep(cw);
        for (int i = 0; i < kCount; ++i)
            cres[i] = mt::cross(cv[i], cw[i]);
        keep(cres);
        return sum(cres[kCount - 1]);
    });
    measure("vec3 dot", kCount, [&] {
        keep(cv); keep(cw);
        for (int i = 0; i < kCount; ++i)
            scalars[i] = mt::dot(cv[i], cw[i]);
        keep(scalars);
        return double(scalars[kCount - 1]);
    });
    // --- what replaced the math3d classes (mt::m3) ---
    measure("m3 transformPoint", kCount, [&] {
        keep(ma); keep(pv);
        for (int i = 0; i < kCount; ++i)
            pres[i] = m3::transformPoint(ma[i], pv[i]);
        keep(pres);
        return sum3(pres[kCount - 1]);
    });
    measure("m3 rotateNormal", kCount, [&] {
        keep(ma); keep(pv);
        for (int i = 0; i < kCount; ++i)
            pres[i] = m3::rotateNormal(ma[i], pv[i]);
        keep(pres);
        return sum3(pres[kCount - 1]);
    });
    measure("m3 invertRigid", kCount, [&] {
        keep(ma);
        for (int i = 0; i < kCount; ++i)
        {
            mr[i] = ma[i];
            m3::invertRigid(mr[i]);
        }
        keep(mr);
        return sum3(mr[kCount - 1]);
    });
    measure("m3 transpose", kCount, [&] {
        keep(ma);
        for (int i = 0; i < kCount; ++i)
        {
            mr[i] = ma[i];
            m3::transpose(mr[i]);
        }
        keep(mr);
        return sum3(mr[kCount - 1]);
    });
    measure("m3 eulerMatrix", kCount, [&] {
        keep(pv); keep(pw);
        for (int i = 0; i < kCount; ++i)
            mr[i] = m3::eulerMatrix(pv[i], pw[i]);
        keep(mr);
        return sum3(mr[kCount - 1]);
    });
    measure("m3 eulerAngles", kCount, [&] {
        keep(ma);
        for (int i = 0; i < kCount; ++i)
            pres[i] = m3::eulerAngles(ma[i]);
        keep(pres);
        return sum3(pres[kCount - 1]);
    });
    measure("quat slerp", kCount, [&] {
        keep(qa); keep(qb); keep(blend);
        for (int i = 0; i < kCount; ++i)
            qr[i] = m3::slerp(qa[i], qb[i], blend[i]);
        keep(qr);
        return sum3(qr[kCount - 1]);
    });
    measure("quat to matrix", kCount, [&] {
        keep(qa);
        for (int i = 0; i < kCount; ++i)
            mr[i] = m3::quatMatrix(qa[i]);
        keep(mr);
        return sum3(mr[kCount - 1]);
    });
    measure("quat slerp + to matrix", kCount, [&] {
        keep(qa); keep(qb); keep(blend);
        for (int i = 0; i < kCount; ++i)
            mr[i] = m3::quatMatrix(m3::slerp(qa[i], qb[i], blend[i]));
        keep(mr);
        return sum3(mr[kCount - 1]);
    });
    measure("quat from matrix", kCount, [&] {
        keep(ma);
        for (int i = 0; i < kCount; ++i)
            qr[i] = m3::quatFromMatrix(ma[i]);
        keep(qr);
        return sum3(qr[kCount - 1]);
    });
    measure("vec3 normalizeInPlace", kCount, [&] {
        keep(pv);
        for (int i = 0; i < kCount; ++i)
        {
            pres[i] = pv[i];
            scalars[i] = m3::normalizeInPlace(pres[i]);
        }
        keep(pres); keep(scalars);
        return sum3(pres[kCount - 1]) + scalars[kCount - 1];
    });
    measure("vec3 negated", kCount, [&] {
        keep(pv);
        for (int i = 0; i < kCount; ++i)
            pres[i] = m3::negated(pv[i]);
        keep(pres);
        return sum3(pres[kCount - 1]);
    });
    measure("vec3 lerp", kCount, [&] {
        keep(pv); keep(pw); keep(blend);
        for (int i = 0; i < kCount; ++i)
            pres[i] = m3::lerp(pv[i], pw[i], blend[i]);
        keep(pres);
        return sum3(pres[kCount - 1]);
    });
    measure("vec3 angleY", kCount, [&] {
        keep(pv);
        for (int i = 0; i < kCount; ++i)
            scalars[i] = m3::angleY(pv[i], 0.f);
        keep(scalars);
        return double(scalars[kCount - 1]);
    });
    measure("color lerp + toARGB", kCount, [&] {
        keep(colors); keep(blend);
        for (int i = 0; i < kCount; ++i)
            packed[i] = m3::colorToARGB(m3::colorLerp(colors[i], colors[kCount - 1 - i], blend[i], colors[i].a));
        keep(packed);
        return double(packed[kCount - 1]);
    });
    measure("color fromARGB", kCount, [&] {
        keep(packed);
        for (int i = 0; i < kCount; ++i)
            colors[i] = m3::colorFromARGB(packed[i]);
        keep(colors);
        return double(colors[kCount - 1].r) + colors[kCount - 1].g + colors[kCount - 1].b + colors[kCount - 1].a;
    });
    measure("segment/plane", kCount, [&] {
        keep(pv); keep(pw);
        const m3::Plane plane = m3::planeFromPointNormal(m3::vec(.6f, .8f, 0.f), m3::vec(.5f, .25f, -1.f));
        for (int i = 0; i < kCount; ++i)
            hits[i] = m3::intersectSegmentPlane(plane, pv[i], pw[i], pres[i]);
        keep(pres); keep(hits);
        int count = 0;
        for (int i = 0; i < kCount; ++i)
            count += hits[i];
        return sum3(pres[kCount - 1]) + count;
    });
    measure("segment/sphere", kCount, [&] {
        keep(pv); keep(pw);
        const m3::Vec3 center = m3::vec(.5f, .25f, -1.f);
        for (int i = 0; i < kCount; ++i)
            hits[i] = m3::intersectSegmentSphere(pv[i], pw[i], center, 6.f);
        keep(hits);
        int count = 0;
        for (int i = 0; i < kCount; ++i)
            count += hits[i];
        return double(count);
    });
    return 0;
}
