#include <cstdint>
// Exercise GLM's first include through plane.h's inherited pack(1).
#include "math3d/quaternion.h"
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <type_traits>
#include <vector>

static void require(bool condition, const char *reason)
{
    if (!condition) throw std::runtime_error(reason);
}
static void near(double a, double b, const char *reason, double tolerance = 5e-5)
{
    require(std::abs(a - b) <= tolerance * (1 + std::abs(b)), reason);
}
static void vectorNear(const Vector &a, const Vector &b, const char *reason)
{
    near(a.x,b.x,reason); near(a.y,b.y,reason); near(a.z,b.z,reason);
}
static void quatNear(const Quaternion &a, const Quaternion &b, const char *reason)
{
    near(a.x,b.x,reason); near(a.y,b.y,reason); near(a.z,b.z,reason); near(a.w,b.w,reason);
}
#pragma pack(push, 1)
struct OddStorage { char prefix; Vector v; Matrix m; Quaternion q; char suffix; };
struct StridedInput { char prefix; Vector v; char suffix; };
struct StridedOutput { char prefix; Vector4 v; char suffix; };
#pragma pack(pop)

static void contracts()
{
    static_assert(sizeof(Vector)==12 && alignof(Vector)==1);
    static_assert(sizeof(Matrix)==64 && alignof(Matrix)==1);
    static_assert(sizeof(Quaternion)==16 && alignof(Quaternion)==1);
    static_assert(std::is_standard_layout_v<Vector> && std::is_standard_layout_v<Matrix> && std::is_standard_layout_v<Quaternion>);
    static_assert(offsetof(Vector,x)==0 && offsetof(Vector,y)==4 && offsetof(Vector,z)==8);
    static_assert(offsetof(Quaternion,x)==0 && offsetof(Quaternion,y)==4 && offsetof(Quaternion,z)==8 && offsetof(Quaternion,w)==12);
    static_assert(offsetof(Matrix,matrix)==0 && offsetof(Matrix,v.pos)==48);
    static_assert(offsetof(OddStorage,v)==1 && offsetof(OddStorage,m)==13 && offsetof(OddStorage,q)==77);
    require(std::endian::native==std::endian::little,"byte fixtures require little endian");
    alignas(16) OddStorage odd{};
    odd.prefix=17; odd.suffix=19;
    odd.v=Vector(1.f,-2.f,.5f); odd.q.Set(1.f,-2.f,.5f,0.f);
    const unsigned char bytes[]={0,0,128,63,0,0,0,192,0,0,0,63,0,0,0,0};
    require(std::memcmp(&odd.v,bytes,12)==0,"packed vector serialization changed");
    require(std::memcmp(&odd.q,bytes,16)==0,"packed quaternion xyzw serialization changed");
    float matrixBytes[16];
    for(int i=0;i<16;++i) matrixBytes[i]=float(i+1);
    std::memcpy(odd.m.matrix,matrixBytes,64);
    require(odd.m.v.pos.x==13 && odd.m.m[2][1]==10,"packed matrix representation changed");
    odd.m.Transposition(); odd.m.Transposition();
    require(std::memcmp(odd.m.matrix,matrixBytes,64)==0,"packed matrix roundtrip changed bytes");
    odd.v=Vector(3.f,4.f,12.f); near(odd.v.GetLength(),13,"odd address length");
    near(odd.v.GetLength2D(),std::sqrt(153.f),"odd address 2D length");
    near(odd.v.Normalize(),13,"normalization returns original length");
    near(odd.v.GetLength(),1,"odd address normalized length");
    odd.v=Vector(3.f,9.f,4.f); near(odd.v.Normalize2D(),5,"XZ normalization length");
    vectorNear(odd.v,Vector(.6f,0.f,.8f),"XZ normalization components");
    Vector zero(0.f); near(zero.Normalize(),0,"zero normalization"); vectorNear(zero,Vector(0.f),"zero preserved");
    Vector nan(std::numeric_limits<float>::quiet_NaN(),3.f,4.f);
    require(std::isnan(nan.Normalize()) && nan.y==3 && nan.z==4,"NaN vector normalization leaves vector unchanged");
    Quaternion qzero(0.f,0.f,0.f,0.f); qzero.Normalize(); quatNear(qzero,Quaternion(0.f,0.f,0.f,0.f),"zero quaternion normalization");
    Quaternion nonunit(0.f,0.f,0.f,2.f); nonunit.Inverse();
    quatNear(nonunit,Quaternion(),"legacy inverse is normalized conjugate");
    Quaternion identity, zquat(0.f,0.f,1.f,0.f);
    quatNear(identity*zquat,Quaternion(0.f,-2.f,1.f,0.f),"legacy non-Hamilton multiplication y term");
    Quaternion opposite(0.f,0.f,0.f,-1.f), interpolated;
    interpolated.SLerp(identity,opposite,0.f); quatNear(interpolated,opposite,"SLerp flips first endpoint");
    const float angle=std::acos(-1.f)/2;
    Matrix rotation; rotation.BuildRotateZ(angle);
    vectorNear(rotation.MulVertex(Vector(1.f,0.f,0.f)),Vector(0.f,1.f,0.f),"packed Z rotation orientation");
    Quaternion qz(rotation); qz.GetMatrix(odd.m);
    vectorNear(odd.m.MulVertex(Vector(1.f,0.f,0.f)),Vector(0.f,1.f,0.f),"quaternion matrix orientation");
    Matrix scale,translate,product; scale.BuildScale(2.f,3.f,4.f); translate.BuildPosition(10.f,20.f,30.f);
    product.EqMultiply(scale,translate);
    vectorNear(product.MulVertex(Vector(1.f,2.f,3.f)),Vector(12.f,26.f,42.f),"packed reverse GLM composition");
    Matrix projection; projection.BuildProjection(angle,1600,900,.5f,100.f);
    alignas(16) StridedInput input[]={{21,Vector(0.f,0.f,.5f),22},{23,Vector(0.f,.1f,100.f),24}};
    alignas(16) StridedOutput output[]={{31,{},32},{33,{},34}};
    projection.Projection(&output[0].v,&input[0].v,2,800,450,sizeof(StridedInput),sizeof(StridedOutput));
    near(output[0].v.z,0,"packed near depth"); near(output[1].v.z,1,"packed far depth");
    near(output[0].v.x,800,"packed screen center"); near(output[0].v.w,2,"packed reciprocal w");
    require(output[1].v.y<450,"packed screen Y sign");
    require(input[0].prefix==21 && input[1].suffix==24 && output[0].prefix==31 && output[0].suffix==32 && output[1].prefix==33 && output[1].suffix==34,"packed projection stride guards");
    odd.q=Quaternion(.2f,.3f,.4f,.5f); odd.q.Normalize().Conjugate(); odd.q.GetMatrix(odd.m);
    odd.v=odd.m.MulVertex(odd.v); odd.m.Transposition3X3();
    require(odd.prefix==17 && odd.suffix==19,"odd address arithmetic guards");
}

static void differential(const char *sampleFile)
{
    std::mt19937 random(0x5041434b);
    std::uniform_real_distribution<float> value(-10.f,10.f);
    std::vector<double> samples;
    auto scalar=[&](double v){ samples.push_back(v); };
    auto vec=[&](const Vector &v){scalar(v.x);scalar(v.y);scalar(v.z);};
    auto quat=[&](const Quaternion &q){scalar(q.x);scalar(q.y);scalar(q.z);scalar(q.w);};
    auto mat=[&](const Matrix &m){for(int i=0;i<16;++i) scalar(m.matrix[i]);};
    for(int iteration=0;iteration<3000;++iteration)
    {
        Vector a(value(random),value(random),value(random)),b(value(random),value(random),value(random));
        for(auto result:{a+b,a-b,a*b,a/b,a+3.25f,a-3.25f,a*3.25f,a/3.25f,a+3.251,a-3.251,a*3.251,a/3.251,3.25f-a,3.251-a,-a,!a,a^b}) vec(result);
        scalar(a|b); scalar(~a); scalar(a.GetLength()); scalar(a.GetLength2D()); scalar(a.GetLength2D2());
        Vector normalized(a); scalar(normalized.Normalize());vec(normalized);
        normalized=a;scalar(normalized.Normalize2D());vec(normalized);
        Vector temp(a); temp|=b;vec(temp);temp=a;temp^=b;vec(temp);
        temp=a;temp.Reflection(b);vec(temp);temp.Lerp(a,b,.35f);vec(temp);
        Matrix ma,mb,mc;
        for(int i=0;i<16;++i){ma.matrix[i]=value(random);mb.matrix[i]=value(random);}
        mc.EqMultiplyFast(ma,mb);mat(mc);
        mc=ma;mc.EqMultiply(mc,mb);mat(mc);
        mc=ma;mc.Transposition();mat(mc);mc=ma;mc.Transposition3X3();mat(mc);
        vec(ma.MulVertex(a));vec(ma.MulNormal(a));vec(ma.MulVertexByInverse(a));vec(ma.MulNormalByInverse(a));
        // Keep W safely away from zero while testing non-affine projection.
        ma.m[0][3]=.01f;ma.m[1][3]=.02f;ma.m[2][3]=.03f;ma.m[3][3]=20.f;
        auto screen=ma.Projection(a,800.f,450.f);scalar(screen.x);scalar(screen.y);scalar(screen.z);scalar(screen.w);
        Matrix rigid(value(random),value(random),value(random),value(random),value(random),value(random));
        vectorNear(rigid.MulVertexByInverse(rigid.MulVertex(a)),a,"packed rigid inverse point");
        vectorNear(rigid.MulNormalByInverse(rigid.MulNormal(a)),a,"packed rigid inverse normal");
        Quaternion p(value(random),value(random),value(random),value(random)),q(value(random),value(random),value(random),value(random));
        quat(p+q);quat(p-q);quat(p*q);quat(p*3.25f);quat(p/3.25f);
        Quaternion work(p);work+=3.25f;quat(work);work=p;work-=3.25f;quat(work);work=p;quat(-work);
        work=p;work*=work;quat(work);
        work=p;work.Normalize();quat(work);scalar(p.GetLength());scalar(p.GetLengthSqr());
        work=p;work.Conjugate();quat(work);work=p;work.Inverse();quat(work);
        p.GetMatrix(mc);mat(mc);work.Set(ma);quat(work);
        p.Normalize();q.Normalize();
        for(float blend:{-.25f,0.f,.25f,.5f,1.f,1.25f}){work.SLerp(p,q,blend);quat(work);}
        // Matrix -> quaternion branches/signs remain legacy, including trace<=0.
        work.Set(rigid);quat(work);work.GetMatrix(mc);mat(mc);
    }
    // Deterministic cases straddle the legacy linear/spherical threshold.
    for(float cosine:{-.9001f,-.9f,-.8999f,.8999f,.9f,.9001f,1.f})
    {
        Quaternion first,second(std::sqrt(1.f-cosine*cosine),0.f,0.f,cosine),result;
        for(float t:{0.f,.25f,.5f,1.f}){result.SLerp(first,second,t);quat(result);}
    }
    if(sampleFile)
    {
        std::ofstream out(sampleFile,std::ios::binary);
        out.write(reinterpret_cast<const char *>(samples.data()),samples.size()*sizeof(double));
        out.close();require(!out.fail(),"write packed differential samples");
    }
}
int main(int argc,char **argv)
{
    contracts();differential(argc>1?argv[1]:nullptr);
    std::cout<<"PASS: packed ABI/bytes, odd-address arithmetic, matrix conventions, quaternion compatibility and randomized contracts\n";
}
