#pragma once

#include <algorithm>
#include <limits>
#include <random>
#include <ranges>
#include <type_traits>
#include <utility>
#include <vector>

namespace storm
{

// A uniform value in [0, max), from one generator per thread.
template <typename Type> struct RandUniform
{
    Type operator()(Type max)
    {
        thread_local std::minstd_rand engine{std::random_device{}()};
        return std::uniform_real_distribution<Type>{Type{0}, max}(engine);
    }
};

template <typename Type> struct EpsilonLessThan
{
    bool operator()(Type lhs, Type rhs)
    {
        return rhs - lhs > std::numeric_limits<Type>::epsilon();
    }
};

/**
 * \brief Probability table quantizing generated random value according to its weight
 * \tparam Type - containing type
 * \tparam Resolution - resolution type
 * \tparam RandProvider - functor returning a value in [0, max)
 */
template <typename Type, typename Resolution = float, typename RandProvider = RandUniform<Resolution>>
    requires std::is_floating_point_v<Resolution>
class ProbabilityTable
{
    using element = std::pair<Resolution, Type>;

  public:
    ProbabilityTable() = default;

    template <class... Args> void emplace(Resolution weight, Args &&...args)
    {
        weight_sum_ += weight;
        table_.emplace_back(std::make_pair(weight_sum_, std::forward<Args>(args)...));
    }

    const Type &pickRandom() const
    {

        const Resolution random_value = provider_(weight_sum_);
        const auto it = std::ranges::upper_bound(table_, random_value, EpsilonLessThan<Resolution>{}, &element::first);
        // A value within epsilon of the sum is in the last entry's range: the comparison's epsilon leaves no entry
        // above it, and uniform_real_distribution may round up to its upper bound.
        return it == table_.end() ? table_.back().second : it->second;
    }

    bool empty() const
    {
        return table_.empty();
    }

  private:
    mutable RandProvider provider_;
    Resolution weight_sum_{0.0f};
    std::vector<element> table_;
};

} // namespace storm