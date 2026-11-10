// Exercise the real production JSON builders. Only the calling thread is faulted;
// libtorrent network/disk worker allocation failures are outside this regression.
#include "../../android/torrent/torrent_session.hpp"
#include <cstdio>
#include <cstdlib>
#include <new>
#include <stdexcept>
#include <string>

thread_local long fail_after = -1;
thread_local bool allocation_failed = false;
void* operator new(std::size_t size) {
    if (fail_after >= 0 && fail_after-- == 0) {
        allocation_failed = true;
        throw std::bad_alloc();
    }
    if (void* memory = std::malloc(size ? size : 1)) return memory;
    throw std::bad_alloc();
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }

// Fails the n-th allocation for n = 0, 1, ... until an operation completes untouched.
template<class Operation, class Check>
void sweep(char const* label, Operation operation, Check check) {
    int failures = 0;
    for (long position = 0; position < 20000; ++position) {
        allocation_failed = false;
        fail_after = position;
        std::string result;
        bool returned = false;
        try { result = operation(); returned = true; }
        catch (std::exception const&) { }
        fail_after = -1; // Checks and reporting never run under injection.
        if (returned) check(result);
        if (!allocation_failed) {
            if (!returned || failures < 5) throw std::runtime_error("Fault sweep did not exercise production allocations");
            std::fprintf(stderr, "PASS %s: %d allocation positions\n", label, failures);
            return;
        }
        ++failures;
    }
    throw std::runtime_error("Allocation sweep exceeded its bound");
}

int main(int argc, char** argv) {
    try {
        if (argc != 3) throw std::runtime_error("Expected fixture and private staging arguments");
        std::string fixture = argv[1];
        auto expected = storm::torrent::inspect_file(fixture);
        // The Python runner parses every printed poll and compares its cached metadata with this manifest.
        std::puts(expected.c_str());
        sweep("inspect/quote/integer arrays", [&] { return storm::torrent::inspect_file(fixture); },
            [&](std::string const& actual) {
                if (actual != expected) throw std::runtime_error("Inspection returned partial JSON after allocation failure");
            });
        storm::torrent::Session session(fixture, argv[2], true);
        std::puts(session.poll().c_str()); // builds the immutable metadata cache
        sweep("poll with cached metadata", [&] { return session.poll(); },
            [&](std::string const& actual) { std::puts(actual.c_str()); });
        return 0;
    } catch (std::exception const& error) {
        fail_after = -1;
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
