#pragma once

// Differential samples of a harness, with the source line that produced each
// one. run.py compares the values against another header revision and, with
// --details, attributes every non-bit-exact sample to its line.
#include <cstdint>
#include <fstream>
#include <source_location>
#include <stdexcept>
#include <string>
#include <vector>

struct SampleLog
{
    using Where = std::source_location;
    std::vector<double> values;
    std::vector<uint32_t> lines;

    void scalar(double value, Where at = Where::current())
    {
        values.push_back(value);
        lines.push_back(at.line());
    }
    // Any x/y/z vector, float or double.
    template <typename V> void vec(const V &v, Where at = Where::current())
    {
        scalar(v.x, at);
        scalar(v.y, at);
        scalar(v.z, at);
    }
    void floats(const float *data, int count, Where at = Where::current())
    {
        for (int i = 0; i < count; ++i)
            scalar(data[i], at);
    }
    void write(const char *file) const
    {
        if (!file)
            return;
        std::ofstream out(file, std::ios::binary);
        out.write(reinterpret_cast<const char *>(values.data()),
                  static_cast<std::streamsize>(values.size() * sizeof(double)));
        out.close();
        std::ofstream where(std::string(file) + ".lines", std::ios::binary);
        where.write(reinterpret_cast<const char *>(lines.data()),
                    static_cast<std::streamsize>(lines.size() * sizeof(uint32_t)));
        where.close();
        if (out.fail() || where.fail())
            throw std::runtime_error("write differential samples");
    }
};
