// Copyright (c) 2021-2025 Advanced Micro Devices, Inc.
// SPDX-License-Identifier: MIT
// C++ adaptation of the MIT amd-adlx 0.1.0 bindings at
// 32b5a740d42295c5dfe9026b9f52683da0f3af91 (gpu/perfmon/system/types/helper.rs).
// Only read-side ABI prefixes are retained. Unused slots are never called.
// See LICENSE in this directory. No ADLX SDK headers or runtime are bundled.
#pragma once
#include <cstddef>
#include <cstdint>

namespace loadbar::adlx {
using Result = int;
using Unused = void(__stdcall *)();
inline constexpr std::uint64_t kVersion = (1ULL << 48) | (5ULL << 32) | 124;
inline constexpr Result kOk = 0, kInitialized = 2, kUnsupported = 12;
template <class V> struct Object { const V *vtable; };
struct Base {
    long(__stdcall *acquire)(void *);
    long(__stdcall *release)(void *);
    Result(__stdcall *query)(void *, const wchar_t *, void **);
};
struct GpuTable {
    Base base;
    Unused unused3[15];
    Result(__stdcall *unique_id)(void *, int *);
    Unused unused19[15];
    Result(__stdcall *luid)(void *, void *);
};
using Gpu = Object<GpuTable>;
struct ListTable {
    Base base;
    unsigned(__stdcall *size)(void *);
    Unused unused4;
    unsigned(__stdcall *begin)(void *);
    unsigned(__stdcall *end)(void *);
    Unused unused7[4];
    Result(__stdcall *at)(void *, unsigned, Gpu **);
};
using List = Object<ListTable>;
struct MetricsTable {
    Base base;
    Result(__stdcall *timestamp)(void *, std::int64_t *);
    Unused unused4[3];
    Result(__stdcall *temperature)(void *, double *);
};
using Metrics = Object<MetricsTable>;
struct SupportTable {
    Base base;
    Unused unused3[3];
    Result(__stdcall *temperature)(void *, unsigned char *);
};
using Support = Object<SupportTable>;
struct PerformanceTable {
    Base base;
    Unused unused3[15];
    Result(__stdcall *current)(void *, Gpu *, Metrics **);
    Unused unused19[2];
    Result(__stdcall *supported)(void *, Gpu *, Support **);
};
using Performance = Object<PerformanceTable>;
struct SystemTable {
    Unused unused0;
    Result(__stdcall *gpus)(void *, List **);
    Unused unused2[7];
    Result(__stdcall *performance)(void *, Performance **);
};
using System = Object<SystemTable>;
using Initialize = Result(__cdecl *)(std::uint64_t, System **);
using Terminate = Result(__cdecl *)();
static_assert(sizeof(void *) == 8);
static_assert(offsetof(GpuTable, luid) == 34 * sizeof(void *));
static_assert(offsetof(ListTable, at) == 11 * sizeof(void *));
static_assert(offsetof(MetricsTable, temperature) == 7 * sizeof(void *));
static_assert(offsetof(SupportTable, temperature) == 6 * sizeof(void *));
static_assert(offsetof(PerformanceTable, current) == 18 * sizeof(void *));
static_assert(offsetof(PerformanceTable, supported) == 21 * sizeof(void *));
static_assert(offsetof(SystemTable, performance) == 9 * sizeof(void *));
} // namespace loadbar::adlx
