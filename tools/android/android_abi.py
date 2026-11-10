"""Android ABIs the engine builds: arm64-v8a for every release, x86_64 for emulator debug builds.

The arm64 build directories keep their unsuffixed names; other ABIs append suffix() so that
their build trees never share (or invalidate) the warm arm64 ones.
"""
ABIS = {
    # ABI: (toolchain arch: cmake/toolchains/android-<arch>.cmake, NDK triple, ELF e_machine, OpenSSL target)
    'arm64-v8a': ('arm64', 'aarch64-linux-android', 183, 'android-arm64'),
    'x86_64': ('x86_64', 'x86_64-linux-android', 62, 'android-x86_64'),
}
DEFAULT = 'arm64-v8a'


def arch(abi):
    return ABIS[abi][0]


def triple(abi):
    return ABIS[abi][1]


def elf_machine(abi):
    return ABIS[abi][2]


def openssl_target(abi):
    return ABIS[abi][3]


def suffix(abi):
    return '' if abi == DEFAULT else '-' + arch(abi)


def toolchain(root, abi):
    return root / 'cmake/toolchains' / f'android-{arch(abi)}.cmake'


def check_elf(path, abi):
    header = path.read_bytes()[:20]
    if header[:6] != b'\x7fELF\x02\x01' or int.from_bytes(header[18:20], 'little') != elf_machine(abi):
        raise RuntimeError(f'Not an ELF64 little-endian {abi} library: {path}')
