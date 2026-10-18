#pragma once

#include <cerrno>
#include <ios>

namespace storm::io
{
struct Result
{
    bool ok;
    int error;
    std::ios::iostate state;
};

// Keep the caller's mask and failed state, but never leak an ios_base::failure
// from write/read/flush/close or from restoring a mask on an already bad stream.
template <typename Stream, typename Operation>
Result checked(Stream &stream, Operation operation)
{
    const auto mask = stream.exceptions();
    stream.exceptions(std::ios::goodbit);
    errno = 0;
    int error;
    try
    {
        operation();
        error = errno;
    }
    catch (const std::ios_base::failure &)
    {
        error = errno;
        stream.setstate(std::ios::badbit);
    }
    catch (...)
    {
        try { stream.exceptions(mask); }
        catch (const std::ios_base::failure &) {}
        throw;
    }
    const Result result{!stream.fail(), error, stream.rdstate()};
    try { stream.exceptions(mask); }
    catch (const std::ios_base::failure &) { /* exceptions() sets the mask before throwing. */ }
    return result;
}
} // namespace storm::io
