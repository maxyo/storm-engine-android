#pragma once

#include "../../i_common/mem_file.h"

#include "storm_glm.h"
#include <string>

class DataPosition
{
    std::string Name;

    glm::vec3 Value;

  public:
    // constructor / destructor
    DataPosition();
    virtual ~DataPosition();

    // Get value (Current time, Random factor [0..1])
    const glm::vec3 &GetValue() const;

    // Set value
    void SetValue(const glm::vec3 &val);

    void Load(MemFile *File);
    void Write(MemFile *File) const;

    void SetName(const char *szName);
    const char *GetName() const;
};
