# zlib-ng includes MSVC's arm64_neon.h whenever _MSC_VER is defined, but clang-cl implements
# only a subset of MSVC's neon_* intrinsics (e.g. neon_umlal2_16 is missing) and ships its
# own complete arm_neon.h. Restrict the MSVC header to the MSVC compiler proper.
file(READ arch/arm/neon_intrins.h _content)
string(REPLACE
    "#if defined(_MSC_VER) && (defined(_M_ARM64) || defined(_M_ARM64EC))"
    "#if defined(_MSC_VER) && !defined(__clang__) && (defined(_M_ARM64) || defined(_M_ARM64EC))"
    _content "${_content}")
file(WRITE arch/arm/neon_intrins.h "${_content}")
