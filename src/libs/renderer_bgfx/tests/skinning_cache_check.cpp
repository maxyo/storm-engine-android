#include "skinning_stream_cache.hpp"
#include <array>
#include <iostream>
struct Vertex {std::array<std::byte,36> bytes{};};
struct Counters {int created=0,uploads=0,destroyed=0;bool fail=false;std::byte last{};};
struct Stream final:storm::render::DynamicVertexStream {
    Counters& counters;int key;
    explicit Stream(Counters& c):counters(c),key(c.created++) {}
    ~Stream(){++counters.destroyed;}
    void upload(std::span<const std::byte> bytes) override {
        if(counters.fail) throw std::runtime_error("Injected upload failure");
        storm::render::skinnedVertexCount(bytes);counters.last=bytes[0];++counters.uploads;
    }
    storm::render::VertexBufferId id() const override{return {key};}
};
void require(bool value){if(!value) throw std::runtime_error("Skinning cache contract failed");}
int main() {
    using Lifetime=storm::render::VertexSourceLifetime;
    Counters counters;storm::model::SkinningStreamCache<Vertex> cache;int a=0,b=0,skins=0;
    auto sourceA=std::make_shared<Lifetime>(),sourceB=std::make_shared<Lifetime>();
    auto create=[&]{return std::make_unique<Stream>(counters);};
    std::byte currentPose{11};
    auto skin=[&](Vertex* dst){++skins;dst[0].bytes[0]=currentPose;};
    cache.beginPose();
    const auto first=cache.transform(&a,sourceA,3,create,skin);
    require(first&&counters.uploads==1&&skins==1);
    require(cache.transform(&a,sourceA,3,create,skin).value==first.value&&counters.uploads==1);
    const auto second=cache.transform(&b,sourceB,3,create,skin);
    require(second&&second.value!=first.value&&counters.uploads==2);
    // Fixed player positions do not matter to the production policy. Changing
    // head control, blend weights or action at that position must all upload.
    for (auto pose : {22,33,44}) {
        currentPose=std::byte(pose);cache.beginPose();
        cache.transform(&a,sourceA,3,create,skin);
        require(counters.last==currentPose);
        const auto uploads=counters.uploads;
        cache.transform(&a,sourceA,3,create,skin);
        require(counters.uploads==uploads); // same pose's second material draw
    }
    require(counters.created==2&&counters.uploads==5&&skins==5);
    cache.transform(&a,sourceA,4,create,skin);require(counters.uploads==6);
    counters.fail=true;cache.beginPose();bool failed=false;
    try {cache.transform(&a,sourceA,4,create,skin);}catch(const std::runtime_error&){failed=true;}
    require(failed&&counters.uploads==6);counters.fail=false;
    cache.transform(&a,sourceA,4,create,skin);require(counters.uploads==7&&skins==8);

    // Copies of a source descriptor must not delay invalidation. No subsequent
    // draw is needed to release its GPU stream. Idempotent release is safe.
    auto descriptorCopy=sourceA;
    sourceA->invalidate();sourceA->invalidate();require(counters.destroyed==1);
    failed=false;
    try {cache.transform(&a,descriptorCopy,4,create,skin);}catch(const std::invalid_argument&){failed=true;}
    require(failed);
    // Deterministic allocator-address reuse, unchanged size, no intervening draw.
    sourceA=std::make_shared<Lifetime>();currentPose=std::byte(55);
    cache.transform(&a,sourceA,4,create,skin);require(counters.last==currentPose&&counters.created==3);
    // Release/restore cycles must not accumulate GPU allocations, even if the
    // model is not rendered after the release. This is the actual lifetime API
    // used by geometry ReleaseVertexBuffer, including direct NODE replacement.
    for (int i=0;i<32;++i) {
        sourceA->invalidate();require(counters.created-counters.destroyed==1);
        sourceA=std::make_shared<Lifetime>();currentPose=std::byte(i);
        cache.transform(&a,sourceA,4,create,skin);require(counters.last==currentPose);
    }
    // A second model using the same source has its own pose and stream. Source
    // teardown invalidates both, independent of node/model ownership transfers.
    storm::model::SkinningStreamCache<Vertex> other;
    other.transform(&a,sourceA,4,create,skin);require(counters.created-counters.destroyed==3);
    sourceA->invalidate();require(counters.created-counters.destroyed==1);
    sourceB.reset();require(counters.created==counters.destroyed);
    cache.beginPose();other.beginPose();
    // Cache destruction/animation replacement before source destruction leaves
    // only weak observers; later source release cannot call a dead cache owner.
    auto sourceC=std::make_shared<Lifetime>();
    cache.transform(&a,sourceC,3,create,skin);cache.clear();sourceC->invalidate();
    require(counters.created==counters.destroyed);
    std::cout<<"Skinning stationary-pose invalidation/source generations/immediate release/reuse/ownership/failure: PASS\n";
}
