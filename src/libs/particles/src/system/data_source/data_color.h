#pragma once

#include "../../i_common/color_vertex.h"
#include "../../i_common/mem_file.h"
#include <string>
#include <vector>

class DataColor
{
    std::string Name;

    const glm::vec4 ZeroColor;

    std::vector<ColorVertex> ColorGraph;

  public:
    // constructor / destructor
    DataColor();
    virtual ~DataColor();

    // Get the value (Current time, Total lifetime, Random factor [0..1])
    glm::vec4 GetValue(float Time, float LifeTime, float K_rand);

    // Sets the "default"
    // two indices, Min = Max = Value
    void SetDefaultValue(const glm::vec4 &Value);

    // Set values
    void SetValues(const ColorVertex *Values, uint32_t Count);

    // Get count of values
    uint32_t GetValuesCount() const;

    // Get min. value (by index)
    const glm::vec4 &GetMinValue(uint32_t Index);

    // Get max. value (by index)
    const glm::vec4 &GetMaxValue(uint32_t Index);

    void Load(MemFile *File);
    void Write(MemFile *File);

    void SetName(const char *szName);
    const char *GetName() const;

    const ColorVertex &GetByIndex(uint32_t Index);
};
