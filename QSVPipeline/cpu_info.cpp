﻿// -----------------------------------------------------------------------------------------
// QSVEnc by rigaya
// -----------------------------------------------------------------------------------------
// The MIT License
//
// Copyright (c) 2011-2016 rigaya
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
// --------------------------------------------------------------------------------------------

#include <vector>
#include <string>
#include <fstream>
#include <vector>
#include <algorithm>
#include <chrono>
#include <thread>
#include <mutex>
#include <climits>
#include <condition_variable>
#include "rgy_tchar.h"
#ifdef _MSC_VER
#include <intrin.h>
#else
#include <x86intrin.h>
#endif
#include <emmintrin.h>
#include "rgy_osdep.h"
#include "rgy_util.h"
#include "rgy_version.h"
#include "cpu_info.h"

int getCPUName(char *buffer, size_t nSize) {
    int CPUInfo[4] = {-1};
    __cpuid(CPUInfo, 0x80000000);
    unsigned int nExIds = CPUInfo[0];
    if (nSize < 0x40)
        return 1;

    memset(buffer, 0, 0x40);
    for (unsigned int i = 0x80000000; i <= nExIds; i++) {
        __cpuid(CPUInfo, i);
        int offset = 0;
        switch (i) {
            case 0x80000002: offset =  0; break;
            case 0x80000003: offset = 16; break;
            case 0x80000004: offset = 32; break;
            default:
                continue;
        }
        memcpy(buffer + offset, CPUInfo, sizeof(CPUInfo));
    }
    auto remove_string =[](char *target_str, const char *remove_str) {
        char *ptr = strstr(target_str, remove_str);
        if (nullptr != ptr) {
            memmove(ptr, ptr + strlen(remove_str), (strlen(ptr) - strlen(remove_str) + 1) *  sizeof(target_str[0]));
        }
    };
    remove_string(buffer, "(R)");
    remove_string(buffer, "(TM)");
    remove_string(buffer, "CPU");
    //crop space beforce string
    for (int i = 0; buffer[i]; i++) {
        if (buffer[i] != ' ') {
            if (i)
                memmove(buffer, buffer + i, strlen(buffer + i) + 1);
            break;
        }
    }
    //remove space which continues.
    for (int i = 0; buffer[i]; i++) {
        if (buffer[i] == ' ') {
            int space_idx = i;
            while (buffer[i+1] == ' ')
                i++;
            if (i != space_idx)
                memmove(buffer + space_idx + 1, buffer + i + 1, strlen(buffer + i + 1) + 1);
        }
    }
    //delete last blank
    if (0 < strlen(buffer)) {
        char *last_ptr = buffer + strlen(buffer) - 1;
        if (' ' == *last_ptr)
            *last_ptr = '\0';
    }
    return 0;
}

#if _MSC_VER
static int getCPUName(wchar_t *buffer, size_t nSize) {
    int ret = 0;
    char *buf = (char *)calloc(nSize, sizeof(char));
    if (NULL == buf) {
        buffer[0] = L'\0';
        ret = 1;
    } else {
        if (0 == (ret = getCPUName(buf, nSize))) {
            if (MultiByteToWideChar(CP_ACP, 0, buf, -1, buffer, (DWORD)nSize) == 0) {
                buffer[0] = L'\0';
                ret = 1;
            }
        }
        free(buf);
    }
    return ret;
}
#endif //#if _MSC_VER

double getCPUDefaultClockFromCPUName() {
    double defaultClock = 0.0;
    TCHAR buffer[1024] = { 0 };
    getCPUName(buffer, _countof(buffer));
    TCHAR *ptr_mhz = _tcsstr(buffer, _T("MHz"));
    TCHAR *ptr_ghz = _tcsstr(buffer, _T("GHz"));
    TCHAR *ptr = _tcschr(buffer, _T('@'));
    bool clockInfoAvailable = (NULL != ptr_mhz || ptr_ghz != NULL) && NULL != ptr;
    if (clockInfoAvailable && 1 == _stscanf_s(ptr+1, _T("%lf"), &defaultClock)) {
        return defaultClock * ((NULL == ptr_ghz) ? 1000.0 : 1.0);
    }
    return 0.0;
}

#if defined(_WIN32) || defined(_WIN64)

typedef BOOL (WINAPI *LPFN_GLPI)(PSYSTEM_LOGICAL_PROCESSOR_INFORMATION, PDWORD);

static DWORD CountSetBits(ULONG_PTR bitMask) {
    DWORD LSHIFT = sizeof(ULONG_PTR)*8 - 1;
    DWORD bitSetCount = 0;
    for (ULONG_PTR bitTest = (ULONG_PTR)1 << LSHIFT; bitTest; bitTest >>= 1)
        bitSetCount += ((bitMask & bitTest) != 0);

    return bitSetCount;
}

bool get_cpu_info(cpu_info_t *cpu_info) {
    if (nullptr == cpu_info)
        return false;

    memset(cpu_info, 0, sizeof(cpu_info[0]));

    LPFN_GLPI glpi = (LPFN_GLPI)GetProcAddress(GetModuleHandle(_T("kernel32")), "GetLogicalProcessorInformation");
    if (nullptr == glpi)
        return false;

    DWORD returnLength = 0;
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION buffer = nullptr;
    while (FALSE == glpi(buffer, &returnLength)) {
        if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
            if (buffer)
                free(buffer);
            if (NULL == (buffer = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION)malloc(returnLength)))
                return FALSE;
        }
    }

    DWORD processorPackageCount = 0;
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION ptr = buffer;
    for (DWORD byteOffset = 0; byteOffset + sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION) <= returnLength;
        byteOffset += sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION)) {
        switch (ptr->Relationship) {
        case RelationNumaNode:
            // Non-NUMA systems report a single record of this type.
            cpu_info->nodes++;
            break;
        case RelationProcessorCore:
            cpu_info->physical_cores++;
            // A hyperthreaded core supplies more than one logical processor.
            cpu_info->logical_cores += CountSetBits(ptr->ProcessorMask);
            break;

        case RelationCache:
        {
            // Cache data is in ptr->Cache, one CACHE_DESCRIPTOR structure for each cache.
            PCACHE_DESCRIPTOR Cache = &ptr->Cache;
            if (1 <= Cache->Level && Cache->Level <= _countof(cpu_info->caches)) {
                cache_info_t *cache = &cpu_info->caches[Cache->Level-1];
                cache->count++;
                cache->level = Cache->Level;
                cache->linesize = Cache->LineSize;
                cache->size += Cache->Size;
                cache->associativity = Cache->Associativity;
                cpu_info->max_cache_level = (std::max)(cpu_info->max_cache_level, cache->level);
            }
            break;
        }
        case RelationProcessorPackage:
            // Logical processors share a physical package.
            processorPackageCount++;
            break;

        default:
            //Unsupported LOGICAL_PROCESSOR_RELATIONSHIP value.
            break;
        }
        ptr++;
    }
    if (buffer)
        free(buffer);

    return true;
}

#else //#if defined(_WIN32) || defined(_WIN64)
bool get_cpu_info(cpu_info_t *cpu_info) {
    memset(cpu_info, 0, sizeof(cpu_info[0]));
    std::ifstream inputFile("/proc/cpuinfo");
    std::istreambuf_iterator<char> data_begin(inputFile);
    std::istreambuf_iterator<char> data_end;
    std::string script_data = std::string(data_begin, data_end);
    inputFile.close();

    for (auto line : split(script_data, "\n")) {
        auto pos = line.find("processor");
        if (pos != std::string::npos) {
            int i = 0;
            if (1 == sscanf(line.substr(line.find(":") + 1).c_str(), "%d", &i)) {
                cpu_info->logical_cores = (std::max)(cpu_info->logical_cores, i + 1u);
            }
            continue;
        }
        pos = line.find("cpu cores");
        if (pos != std::string::npos) {
            int i = 0;
            if (1 == sscanf(line.substr(line.find(":") + 1).c_str(), "%d", &i)) {
                cpu_info->physical_cores = (std::max)(cpu_info->physical_cores, (uint32_t)i);
            }
            continue;
        }
        pos = line.find("pyhisical id");
        if (pos != std::string::npos) {
            int i = 0;
            if (1 == sscanf(line.substr(line.find(":") + 1).c_str(), "%d", &i)) {
                cpu_info->nodes = (std::max)(cpu_info->nodes, i + 1u);
            }
            continue;
        }
    }
    return true;
}
#endif //#if defined(_WIN32) || defined(_WIN64)

cpu_info_t get_cpu_info() {
    cpu_info_t cpu;
    get_cpu_info(&cpu);
    return cpu;
}

const int TEST_COUNT = 5000;

#ifdef __cplusplus
extern "C" {
#endif
#if _M_IX86
    extern int runl_por(uint32_t loop_count);
#else
    extern int64_t runl_por(uint32_t loop_count);
#endif
#ifdef __cplusplus
}
#endif

static double get_tick_per_clock() {
    const int outer_loop_count = 1000;
    const int inner_loop_count = TEST_COUNT;
    auto tick_min = runl_por(inner_loop_count);
    for (int i = 0; i < outer_loop_count; i++) {
        auto ret = runl_por(inner_loop_count);
        tick_min = std::min(tick_min, ret);
    }
    return tick_min / (128.0 * inner_loop_count);
}

static double get_tick_per_sec() {
    const int nMul = 100;
    const int outer_loop_count = TEST_COUNT * nMul;
    runl_por(outer_loop_count);
    auto start = std::chrono::high_resolution_clock::now();
    auto tick = runl_por(outer_loop_count);
    auto fin = std::chrono::high_resolution_clock::now();
    double second = std::chrono::duration_cast<std::chrono::microseconds>(fin - start).count() * 1e-6;
    return tick / second;
}

//rdtscpを使うと0xc0000096例外 (一般ソフトウェア例外)を発する場合があるらしい
//そこでそれを検出する
bool check_rdtscp_available() {
#if defined(_WIN32) || defined(_WIN64)
    __try {
        UINT dummy;
        __rdtscp(&dummy);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
#endif //defined(_WIN32) || defined(_WIN64)
    return true;
}

//__rdtscが定格クロックに基づいた値を返すのを利用して、実際の動作周波数を得る
//やや時間がかかるので注意
double getCPUMaxTurboClock() {
    double defaultClock = getCPUDefaultClock();
    if (0.0 >= defaultClock) {
        return 0.0;
    }

    //http://instlatx64.atw.hu/
    //によれば、Sandy/Ivy/Haswell/Silvermont
    //いずれでもサポートされているのでノーチェックでも良い気がするが...
    //固定クロックのタイマーを持つかチェック (Fn:8000_0007:EDX8)
    int CPUInfo[4] ={ -1 };
    __cpuid(CPUInfo, 0x80000007);
    if (0 == (CPUInfo[3] & (1<<8))) {
        return defaultClock;
    }
    //rdtscp命令のチェック (Fn:8000_0001:EDX27)
    __cpuid(CPUInfo, 0x80000001);
    if (0 == (CPUInfo[3] & (1<<27))) {
        return defaultClock;
    }
    //例外が発生するなら処理を中断する
    if (!check_rdtscp_available()) {
        return defaultClock;
    }

    const double tick_per_clock = get_tick_per_clock();
    const double tick_per_sec = get_tick_per_sec();
    return (tick_per_sec / tick_per_clock) * 1e-9;
}

#if ENABLE_OPENCL
#include "cl_func.h"
#endif

#pragma warning (push)
#pragma warning (disable: 4100)
double getCPUDefaultClockOpenCL() {
#if !ENABLE_OPENCL
    return 0.0;
#else
    int frequency = 0;
    cl_func_t cl = { 0 };
    cl_data_t data = { 0 };
    if (CL_SUCCESS == cl_get_func(&cl)
        && CL_SUCCESS == cl_get_platform_and_device(nullptr, CL_DEVICE_TYPE_CPU, &data, &cl)) {
        frequency = cl_get_device_max_clock_frequency_mhz(&data, &cl);
    }
    cl_release(&data, &cl);
    return frequency / 1000.0;
#endif // !ENABLE_OPENCL
}
#pragma warning (pop)

double getCPUDefaultClock() {
    double defautlClock = getCPUDefaultClockFromCPUName();
    if (0 >= defautlClock)
        defautlClock = getCPUDefaultClockOpenCL();
    return defautlClock;
}

#if ENCODER_QSV
#include "qsv_query.h"
#endif

int getCPUInfo(TCHAR *buffer, size_t nSize
#if ENCODER_QSV
    , MFXVideoSession *pSession
#endif
) {
    int ret = 0;
    buffer[0] = _T('\0');
    cpu_info_t cpu_info;
    if (getCPUName(buffer, nSize) || !get_cpu_info(&cpu_info)) {
        buffer[0] = _T('\0');
        ret = 1;
    } else {
        double defaultClock = getCPUDefaultClockFromCPUName();
        bool noDefaultClockInCPUName = (0.0 >= defaultClock);
        if (noDefaultClockInCPUName)
            defaultClock = getCPUDefaultClockOpenCL();
        if (defaultClock > 0.0) {
            if (noDefaultClockInCPUName) {
                _stprintf_s(buffer + _tcslen(buffer), nSize - _tcslen(buffer), _T(" @ %.2fGHz"), defaultClock);
            }
            double maxFrequency = getCPUMaxTurboClock();
            //大きな違いがなければ、TurboBoostはないものとして表示しない
            if (maxFrequency / defaultClock > 1.01) {
                _stprintf_s(buffer + _tcslen(buffer), nSize - _tcslen(buffer), _T(" [TB: %.2fGHz]"), maxFrequency);
            }
        }
        _stprintf_s(buffer + _tcslen(buffer), nSize - _tcslen(buffer), _T(" (%dC/%dT)"), cpu_info.physical_cores, cpu_info.logical_cores);
#if ENCODER_QSV
        int cpuGen = getCPUGen(pSession);
        if (cpuGen != CPU_GEN_UNKNOWN) {
            _stprintf_s(buffer + _tcslen(buffer), nSize - _tcslen(buffer), _T(" <%s>"), CPU_GEN_STR[cpuGen]);
        }
#endif
    }
    return ret;
}

BOOL GetProcessTime(HANDLE hProcess, PROCESS_TIME *time) {
#if defined(_WIN32) || defined(_WIN64)
    SYSTEMTIME systime;
    GetSystemTime(&systime);
    return (NULL != hProcess
        && GetProcessTimes(hProcess, (FILETIME *)&time->creation, (FILETIME *)&time->exit, (FILETIME *)&time->kernel, (FILETIME *)&time->user)
        && (WAIT_OBJECT_0 == WaitForSingleObject(hProcess, 0) || SystemTimeToFileTime(&systime, (FILETIME *)&time->exit)));
#else //#if defined(_WIN32) || defined(_WIN64)
    struct tms tm;
    times(&tm);
    time->exit = time->creation;
    time->creation = clock();
    time->kernel = tm.tms_stime;
    time->user = tm.tms_utime;
    return 0;
#endif //#if defined(_WIN32) || defined(_WIN64)
}

BOOL GetProcessTime(PROCESS_TIME *time) {
#if defined(_WIN32) || defined(_WIN64)
    return GetProcessTime(GetCurrentProcess(), time);
#else
    return GetProcessTime(NULL, time);
#endif
}

double GetProcessAvgCPUUsage(HANDLE hProcess, PROCESS_TIME *start) {
    PROCESS_TIME current = { 0 };
    cpu_info_t cpu_info;
    double result = 0;
    if (NULL != hProcess
        && get_cpu_info(&cpu_info)
        && GetProcessTime(hProcess, &current)) {
        uint64_t current_total_time = current.kernel + current.user;
        uint64_t start_total_time = (nullptr == start) ? 0 : start->kernel + start->user;
        result = (current_total_time - start_total_time) * 100.0 / (double)(cpu_info.logical_cores * (current.exit - ((nullptr == start) ? current.creation : start->exit)));
    }
    return result;
}

double GetProcessAvgCPUUsage(PROCESS_TIME *start) {
#if defined(_WIN32) || defined(_WIN64)
    return GetProcessAvgCPUUsage(GetCurrentProcess(), start);
#else
    return GetProcessAvgCPUUsage(NULL, start);
#endif
}

