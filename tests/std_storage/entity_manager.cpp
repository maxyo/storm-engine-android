#include <algorithm>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

#include "entity_manager.h"
#include "vma.hpp"

namespace
{
void require(bool value, const char *message)
{
    if (!value)
        throw std::runtime_error(message);
}

struct Probe final : Entity
{
    static inline EntityManager *manager;
    static inline bool initResult = true;
    static inline entid_t lastInitialized;
    static inline entid_t eraseDuringDestruction;
    static inline entid_t destroyTrigger;
    static inline std::vector<entid_t> destroyed;

    bool Init() override
    {
        lastInitialized = GetId();
        return initResult;
    }
    void ProcessStage(Stage, uint32_t) override {}
    ~Probe() override
    {
        destroyed.push_back(GetId());
        if (GetId() == destroyTrigger)
            manager->EraseEntity(eraseDuringDestruction);
    }
};
CREATE_CLASS(Probe)

uint32_t index(entid_t id) { return static_cast<uint32_t>(id); }
uint32_t generation(entid_t id) { return static_cast<uint32_t>(id >> 32); }

void lifecycle()
{
    EntityManager manager;
    Probe::manager = &manager;
    Probe::destroyed.clear();
    const auto a = manager.CreateEntity("Probe");
    const auto b = manager.CreateEntity("pRoBe");
    const auto c = manager.CreateEntity("Probe");
    require(index(a) == 0 && index(b) == 1 && index(c) == 2, "initial indices");
    require(generation(a) == 1 && generation(c) == 3, "initial generations");
    require(manager.GetClassCode(a) == MakeHashValue("Probe"), "class code");
    require(manager.GetEntityIds("Probe") == std::vector<entid_t>({a, b, c}), "cache initial fill");
    manager.SetLayerType(0, layer_type_t::execute);
    manager.AddToLayer(0, c, 20);
    manager.AddToLayer(0, a, 10);
    manager.AddToLayer(0, b, 10);
    require(manager.GetEntityIds(layer_index_t{0}) == std::vector<entid_t>({a, b, c}), "layer priorities");
    auto *oldB = manager.GetEntityPointer(b);
    manager.EraseEntity(b);
    manager.EraseEntity(b); // repeated erase must not queue a second destructor
    manager.EraseEntity(a);
    require(!manager.IsEntityValid(b) && manager.GetEntityPointer(b) == oldB, "pending deletion visibility");
    require(Probe::destroyed.empty(), "destruction must be deferred");
    require(manager.GetEntityIds("Probe") == std::vector<entid_t>({c}), "cache invalidation before destruction");
    std::vector<entid_t> visited;
    manager.ForEachEntity([&](Entity *entity) { visited.push_back(entity->GetId()); });
    require(visited == std::vector<entid_t>({c}), "ForEach skips pending deletions");
    const auto d = manager.CreateEntity("Probe");
    require(index(d) == 3, "pending slots must not be reused");
    manager.NewLifecycle();
    require(Probe::destroyed == std::vector<entid_t>({a, b}), "deferred destruction LIFO");
    require(manager.GetEntityPointer(b) == nullptr, "destroyed pointer");
    require(manager.GetEntityIds(layer_index_t{0}) == std::vector<entid_t>({c}), "layer cleanup");
    const auto e = manager.CreateEntity("Probe");
    const auto f = manager.CreateEntity("Probe");
    require(index(e) == index(b) && index(f) == index(a), "double LIFO reuse order");
    require(generation(e) == 5 && generation(f) == 6, "generation survives slot reuse");
    require(!manager.IsEntityValid(a) && !manager.IsEntityValid(b), "stale generations rejected");
    require(manager.GetEntityPointer(a) == nullptr && manager.GetClassCode(b) == 0, "stale lookups rejected");
    manager.EraseEntity(b);
    require(manager.IsEntityValid(e), "stale erase must not erase replacement");
    require(manager.GetEntityIds("Probe") == std::vector<entid_t>({c, d, e, f}), "cache order after slot reuse");
    manager.NewLifecycle();
    require(Probe::destroyed.size() == 2, "empty lifecycle");
    manager.EraseAll();
    require(Probe::destroyed == std::vector<entid_t>({a, b, f, e, c, d}), "EraseAll visits occupied slots once");
    require(manager.GetEntityIds("Probe").empty() && manager.GetEntityIds(layer_index_t{0}).empty(), "EraseAll clears cache and layers");
    const auto reset = manager.CreateEntity("Probe");
    require(reset == a, "EraseAll resets indices and generation as before");
    manager.EraseAll();
}

void failedInitializationAndReentrantDelete()
{
    EntityManager manager;
    Probe::manager = &manager;
    Probe::destroyed.clear();
    Probe::initResult = false;
    require(manager.CreateEntity("Probe") == invalid_entity, "failed Init return value");
    const auto failed = Probe::lastInitialized;
    require(!manager.IsEntityValid(failed) && manager.GetEntityPointer(failed), "failed Init awaits lifecycle");
    require(manager.GetEntityIds("Probe").empty(), "failed Init removed from cache");
    Probe::initResult = true;
    const auto survivor = manager.CreateEntity("Probe");
    require(index(survivor) == 1, "failed Init slot not reused early");
    manager.NewLifecycle();
    require(Probe::destroyed == std::vector<entid_t>({failed}), "failed Init destroyed once");
    const auto trigger = manager.CreateEntity("Probe");
    require(index(trigger) == index(failed) && generation(trigger) == 3, "failed Init slot reuse");
    Probe::destroyTrigger = trigger;
    Probe::eraseDuringDestruction = survivor;
    manager.EraseEntity(trigger);
    manager.NewLifecycle();
    require(Probe::destroyed == std::vector<entid_t>({failed, trigger, survivor}), "destructor-enqueued deletion drained in same lifecycle");
    const auto next = manager.CreateEntity("Probe");
    require(index(next) == index(survivor), "reentrant delete free order");
    Probe::destroyTrigger = invalid_entity;
    manager.EraseAll();
}

void growth()
{
    EntityManager manager;
    Probe::manager = &manager;
    Probe::destroyed.clear();
    constexpr size_t count = 10000;
    std::vector<entid_t> ids;
    std::vector<entid_t> erased;
    for (size_t i = 0; i < count; ++i)
        ids.push_back(manager.CreateEntity("Probe"));
    // Fill the real deletion and free stacks across many allocation boundaries.
    for (size_t parity = 0; parity < 2; ++parity)
        for (size_t i = parity; i < count; i += 2)
        {
            manager.EraseEntity(ids[i]);
            erased.push_back(ids[i]);
        }
    manager.NewLifecycle();
    require(std::equal(erased.rbegin(), erased.rend(), Probe::destroyed.begin(), Probe::destroyed.end()), "growth destruction order");
    for (size_t i = 0; i < count; ++i)
    {
        const auto replacement = manager.CreateEntity("Probe");
        require(index(replacement) == index(erased[i]), "growth reuse order");
        require(generation(replacement) == count + i + 1, "growth generation");
        require(!manager.IsEntityValid(erased[i]), "growth stale ID");
    }
    manager.EraseAll();
    require(Probe::destroyed.size() == count * 2, "growth no leaks or duplicate deletion");
    // EraseAll also clears queued deletions and existing free indices.
    const auto first = manager.CreateEntity("Probe");
    const auto second = manager.CreateEntity("Probe");
    manager.EraseEntity(first);
    manager.NewLifecycle();
    manager.EraseEntity(second);
    manager.EraseAll();
    manager.NewLifecycle();
    require(manager.CreateEntity("Probe") == ids[0], "reset after mixed pending/free slots");
    manager.EraseAll();
}
}

int main()
{
    Probevmaci.SetHash(MakeHashValue("Probe"));
    lifecycle();
    failedInitializationAndReentrantDelete();
    growth();
    std::cout << "PASS: real EntityManager lifecycle, cache/layers, generations, reuse and growth\n";
}
