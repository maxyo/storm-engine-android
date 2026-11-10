// Manual micro-benchmark of hot math operations (not a ctest).
// Uses only the historical public API so it builds against any header revision.
// Usage: bench [repeats [name-substring]] -> "name ns/op checksum" lines.
// The best of `repeats` short batches is reported (thread CPU time where
// available), which stays usable on a busy host. The checksum is a parity aid
// when comparing header revisions.
#include "matrix.h"
#include "math3d/quaternion.h"
#include "math3d/color.h"
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

double sum(const CMatrix &m)
{
    double total = 0;
    for (int i = 0; i < 16; ++i)
        total += m.matrix[i];
    return total;
}
double sum(const Matrix &m)
{
    double total = 0;
    for (int i = 0; i < 16; ++i)
        total += m.matrix[i];
    return total;
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

    std::vector<CMatrix> ca(kCount), cb(kCount), cr(kCount);
    std::vector<Matrix> ma(kCount), mb(kCount), mr(kCount);
    std::vector<CVECTOR> cv(kCount), cw(kCount), cres(kCount);
    std::vector<Vector> pv(kCount), pres(kCount);
    std::vector<Vector4> pres4(kCount);
    std::vector<MTX_PRJ_VECTOR> cproj(kCount);
    std::vector<Quaternion> qa(kCount), qb(kCount), qr(kCount);
    std::vector<float> blend(kCount), scalars(kCount);
    for (int i = 0; i < kCount; ++i)
    {
        float in[12];
        for (float &value : in)
            value = values(rng);
        ca[i].BuildMatrix(in[0] * .3f, in[1] * .3f, in[2] * .3f, in[3], in[4], in[5]);
        cb[i].BuildMatrix(in[6] * .3f, in[7] * .3f, in[8] * .3f, in[9], in[10], in[11]);
        ma[i].Build(in[0] * .3f, in[1] * .3f, in[2] * .3f, in[3], in[4], in[5]);
        mb[i].Build(in[6] * .3f, in[7] * .3f, in[8] * .3f, in[9], in[10], in[11]);
        const float x = values(rng), y = values(rng), z = values(rng);
        cv[i] = CVECTOR(x, y, z);
        pv[i] = Vector(x, y, z);
        const float wx = values(rng), wy = values(rng), wz = values(rng);
        cw[i] = CVECTOR(wx, wy, wz);
        qa[i] = Quaternion(ma[i]);
        qb[i] = Quaternion(mb[i]);
        blend[i] = unit(rng) * .5f + .5f;
    }
    std::vector<Color> colors(kCount), colorResults(kCount);
    for (int i = 0; i < kCount; ++i)
    {
        const float r = unit(rng), g = unit(rng), b = unit(rng), a = unit(rng);
        colors[i] = Color(r * 1.5f, g * 1.5f, b * 1.5f, a * 1.5f);
    }
    CMatrix cprojection;
    cprojection.BuildProjectionMatrix(1.2f, 1600.f, 900.f, .5f, 2000.f);
    Matrix mprojection;
    mprojection.BuildProjection(1.2f, 1600.f, 900.f, .5f, 2000.f);

    measure("CMatrix::EqMultiply", kCount, [&] {
        keep(ca); keep(cb);
        for (int i = 0; i < kCount; ++i)
            cr[i].EqMultiply(ca[i], cb[i]);
        keep(cr);
        return sum(cr[kCount - 1]);
    });
    measure("CMatrix operator*", kCount, [&] {
        keep(ca); keep(cb);
        for (int i = 0; i < kCount; ++i)
            cr[i] = ca[i] * cb[i];
        keep(cr);
        return sum(cr[kCount - 1]);
    });
    measure("CMatrix * CVECTOR", kCount, [&] {
        keep(ca); keep(cv);
        for (int i = 0; i < kCount; ++i)
            cres[i] = ca[i] * cv[i];
        keep(cres);
        return double(cres[kCount - 1].x) + cres[kCount - 1].y + cres[kCount - 1].z;
    });
    measure("CMatrix::MulToInv", kCount, [&] {
        keep(ca); keep(cv);
        for (int i = 0; i < kCount; ++i)
            ca[i].MulToInv(cv[i], cres[i]);
        keep(cres);
        return double(cres[kCount - 1].x) + cres[kCount - 1].y + cres[kCount - 1].z;
    });
    measure("CMatrix::Projection[]", kCount, [&] {
        keep(cv);
        cprojection.Projection(cv.data(), cproj.data(), kCount, 800.f, 450.f, sizeof(CVECTOR),
                               sizeof(MTX_PRJ_VECTOR));
        keep(cproj);
        return double(cproj[kCount - 1].x) + cproj[kCount - 1].y + cproj[kCount - 1].z + cproj[kCount - 1].rhw;
    });
    measure("CVECTOR normalize (!)", kCount, [&] {
        keep(cv);
        for (int i = 0; i < kCount; ++i)
            cres[i] = !cv[i];
        keep(cres);
        return double(cres[kCount - 1].x) + cres[kCount - 1].y + cres[kCount - 1].z;
    });
    measure("CVECTOR cross (^)", kCount, [&] {
        keep(cv); keep(cw);
        for (int i = 0; i < kCount; ++i)
            cres[i] = cv[i] ^ cw[i];
        keep(cres);
        return double(cres[kCount - 1].x) + cres[kCount - 1].y + cres[kCount - 1].z;
    });
    measure("CVECTOR dot (|)", kCount, [&] {
        keep(cv); keep(cw);
        for (int i = 0; i < kCount; ++i)
            scalars[i] = cv[i] | cw[i];
        keep(scalars);
        return double(scalars[kCount - 1]);
    });
    measure("Matrix::EqMultiply", kCount, [&] {
        keep(ma); keep(mb);
        for (int i = 0; i < kCount; ++i)
            mr[i].EqMultiply(ma[i], mb[i]);
        keep(mr);
        return sum(mr[kCount - 1]);
    });
    measure("Matrix::EqMultiplyFast", kCount, [&] {
        keep(ma); keep(mb);
        for (int i = 0; i < kCount; ++i)
            mr[i].EqMultiplyFast(ma[i], mb[i]);
        keep(mr);
        return sum(mr[kCount - 1]);
    });
    measure("Matrix::Inverse", kCount, [&] {
        keep(ma);
        for (int i = 0; i < kCount; ++i)
        {
            mr[i] = ma[i];
            mr[i].Inverse();
        }
        keep(mr);
        return sum(mr[kCount - 1]);
    });
    measure("Matrix::InverseWhithScale", kCount, [&] {
        keep(ma);
        for (int i = 0; i < kCount; ++i)
        {
            mr[i] = ma[i];
            mr[i].InverseWhithScale();
        }
        keep(mr);
        return sum(mr[kCount - 1]);
    });
    measure("Matrix::MulVertex", kCount, [&] {
        keep(ma); keep(pv);
        for (int i = 0; i < kCount; ++i)
            pres[i] = ma[i].MulVertex(pv[i]);
        keep(pres);
        return double(pres[kCount - 1].x) + pres[kCount - 1].y + pres[kCount - 1].z;
    });
    measure("Matrix::MulNormal", kCount, [&] {
        keep(ma); keep(pv);
        for (int i = 0; i < kCount; ++i)
            pres[i] = ma[i].MulNormal(pv[i]);
        keep(pres);
        return double(pres[kCount - 1].x) + pres[kCount - 1].y + pres[kCount - 1].z;
    });
    measure("Matrix::MulVertexByInverse", kCount, [&] {
        keep(ma); keep(pv);
        for (int i = 0; i < kCount; ++i)
            pres[i] = ma[i].MulVertexByInverse(pv[i]);
        keep(pres);
        return double(pres[kCount - 1].x) + pres[kCount - 1].y + pres[kCount - 1].z;
    });
    measure("Matrix::Projection[]", kCount, [&] {
        keep(pv);
        mprojection.Projection(pres4.data(), pv.data(), kCount, 800.f, 450.f);
        keep(pres4);
        return double(pres4[kCount - 1].x) + pres4[kCount - 1].y + pres4[kCount - 1].z + pres4[kCount - 1].w;
    });
    measure("Quaternion::SLerp", kCount, [&] {
        keep(qa); keep(qb); keep(blend);
        for (int i = 0; i < kCount; ++i)
            qr[i].SLerp(qa[i], qb[i], blend[i]);
        keep(qr);
        return double(qr[kCount - 1].x) + qr[kCount - 1].y + qr[kCount - 1].z + qr[kCount - 1].w;
    });
    measure("Quaternion::GetMatrix", kCount, [&] {
        keep(qa);
        for (int i = 0; i < kCount; ++i)
            qa[i].GetMatrix(mr[i]);
        keep(mr);
        return sum(mr[kCount - 1]);
    });
    measure("Quaternion SLerp+GetMatrix", kCount, [&] {
        keep(qa); keep(qb); keep(blend);
        for (int i = 0; i < kCount; ++i)
        {
            Quaternion q;
            q.SLerp(qa[i], qb[i], blend[i]);
            q.GetMatrix(mr[i]);
        }
        keep(mr);
        return sum(mr[kCount - 1]);
    });
    measure("Quaternion operator*", kCount, [&] {
        keep(qa); keep(qb);
        for (int i = 0; i < kCount; ++i)
            qr[i] = qa[i] * qb[i];
        keep(qr);
        return double(qr[kCount - 1].x) + qr[kCount - 1].y + qr[kCount - 1].z + qr[kCount - 1].w;
    });
    measure("Quaternion::Set(Matrix)", kCount, [&] {
        keep(ma);
        for (int i = 0; i < kCount; ++i)
            qr[i].Set(ma[i]);
        keep(qr);
        return double(qr[kCount - 1].x) + qr[kCount - 1].y + qr[kCount - 1].z + qr[kCount - 1].w;
    });
    measure("Matrix::Transposition", kCount, [&] {
        keep(ma);
        for (int i = 0; i < kCount; ++i)
        {
            mr[i] = ma[i];
            mr[i].Transposition();
        }
        keep(mr);
        return sum(mr[kCount - 1]);
    });
    measure("Matrix::IsIdentity", kCount, [&] {
        keep(ma);
        int count = 0;
        for (int i = 0; i < kCount; ++i)
            count += ma[i].IsIdentity() ? 1 : 0;
        return double(count);
    });
    measure("Color LerpA+Clamp", kCount, [&] {
        keep(colors); keep(blend);
        for (int i = 0; i < kCount; ++i)
        {
            colorResults[i].LerpA(colors[i], colors[kCount - 1 - i], blend[i]);
            colorResults[i].Clamp();
        }
        keep(colorResults);
        return double(colorResults[kCount - 1].r) + colorResults[kCount - 1].g + colorResults[kCount - 1].b +
               colorResults[kCount - 1].a;
    });
    measure("Vector normalize (!)", kCount, [&] {
        keep(pv);
        for (int i = 0; i < kCount; ++i)
            pres[i] = !pv[i];
        keep(pres);
        return double(pres[kCount - 1].x) + pres[kCount - 1].y + pres[kCount - 1].z;
    });
    return 0;
}
