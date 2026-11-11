"""DX9RENDER buffer handles: -1 ("no buffer") and out-of-range ids never index the handle table.

Compiles the production block from DX9RENDER::LiveBuffer through DX9RENDER::ReleaseIndexBuffer with a
stub renderer whose member in front of the table ends in a float 1.0f, as keptPixels_'s max_load_factor
did with libc++ (Android): ReleaseIndexBuffer(-1) read it as a buffer pointer and crashed at 0x3f800000.
UBSan's bounds check fails the run if any call still indexes the table out of range."""
from pathlib import Path
import subprocess, sys, tempfile

ROOT = Path(__file__).resolve().parents[2]
p = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / 'src/libs/renderer/src/s_device.cpp'
s = p.read_text()
a = s.index('bool DX9RENDER::LiveBuffer(')
b = s.index('\nvoid DX9RENDER::SetTransform(', a)
code = r'''
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#define MAX_BUFFERS 8
#define CHECKD3DERR(x) ((x) != 0)
namespace spdlog { int errors = 0;
template <class... A> void error(const char *, A &&...) { ++errors; } }
struct Buffer {
    int refs = 1, locks = 0; unsigned char data[16]{};
    int Lock(unsigned, int, void **p, unsigned) { ++locks; *p = data; return 0; }
    int Unlock() { --locks; return 0; }
    int Release() { return --refs; }
};
struct VERTEX_BUFFER { uint32_t dwNumLocks; uint32_t dwUsage; int32_t type; int32_t size; Buffer *buff; };
struct INDEX_BUFFER { uint32_t dwNumLocks; uint32_t dwUsage; int32_t size; Buffer *buff; };
struct DX9RENDER {
    // Ends like libc++'s unordered_map: ..., float max_load_factor = 1.0f (+ padding).
    struct { void *buckets = nullptr; size_t count = 0; void *first = nullptr; size_t size = 0; float mlf = 1.0f; } front;
    INDEX_BUFFER IndexBuffers[MAX_BUFFERS]{};
    VERTEX_BUFFER VertexBuffers[MAX_BUFFERS]{};
    uint32_t dwNumLV = 0, dwNumLI = 0;
    bool LiveBuffer(int32_t id, bool index, const char *call) const;
    void *LockVertexBuffer(int32_t id, uint32_t dwFlags = 0);
    void UnLockVertexBuffer(int32_t id);
    int32_t GetVertexBufferSize(int32_t id);
    void *LockIndexBuffer(int32_t id, uint32_t dwFlags = 0);
    void UnLockIndexBuffer(int32_t id);
    void ReleaseVertexBuffer(int32_t id);
    void ReleaseIndexBuffer(int32_t id);
};
''' + s[a:b] + r'''
int main()
{
    DX9RENDER r;
    Buffer vb, ib;
    r.VertexBuffers[2] = {0, 0, 0, 16, &vb};
    r.IndexBuffers[3] = {0, 0, 16, &ib};
    // "No buffer": the owners' initial handle, released before every Create.
    r.ReleaseVertexBuffer(-1);
    r.ReleaseIndexBuffer(-1);
    assert(spdlog::errors == 0 && r.front.mlf == 1.0f);
    // Handles outside the table are reported and never dereferenced.
    for (int id : {-2, MAX_BUFFERS, 1 << 20})
    {
        r.ReleaseVertexBuffer(id);
        r.ReleaseIndexBuffer(id);
        assert(!r.LockVertexBuffer(id) && !r.LockIndexBuffer(id) && r.GetVertexBufferSize(id) == 0);
        r.UnLockVertexBuffer(id);
        r.UnLockIndexBuffer(id);
    }
    assert(spdlog::errors == 3 * 7);
    // A handle without a buffer: Lock is reported, Release of it stays a no-op (double release).
    assert(!r.LockIndexBuffer(5) && spdlog::errors == 3 * 7 + 1);
    r.ReleaseIndexBuffer(5);
    assert(spdlog::errors == 3 * 7 + 1);
    // Live handles work as before.
    assert(r.LockVertexBuffer(2) == vb.data && r.GetVertexBufferSize(2) == 16);
    r.UnLockVertexBuffer(2);
    assert(r.LockIndexBuffer(3) == ib.data);
    r.UnLockIndexBuffer(3);
    r.ReleaseVertexBuffer(2);
    r.ReleaseIndexBuffer(3);
    assert(vb.refs == 0 && ib.refs == 0 && !r.VertexBuffers[2].buff && !r.IndexBuffers[3].buff && vb.locks == 0);
    assert(spdlog::errors == 3 * 7 + 1);
    std::cout << "actual DX9RENDER buffer handles: -1 is no buffer, out-of-range ids reported, live ids unchanged PASS\n";
}
'''
with tempfile.TemporaryDirectory(prefix='buffer-handles-') as d:
    c = Path(d) / 'test.cpp'
    c.write_text(code)
    exe = Path(d) / 'test'
    subprocess.run(['c++', '-std=c++17', '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-g', str(c), '-o',
                    str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
