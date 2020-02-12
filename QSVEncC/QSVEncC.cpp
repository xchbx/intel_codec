// -----------------------------------------------------------------------------------------
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

#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <cassert>
#include <fstream>
#include <iomanip>
#include <set>
#include <vector>
#include <numeric>
#include <algorithm>
#include <ctime>
#include "rgy_osdep.h"
#if defined(_WIN32) || defined(_WIN64)
#include <shellapi.h>
#endif

#include "qsv_pipeline.h"
#include "qsv_cmd.h"
#include "qsv_prm.h"
#include "qsv_query.h"
#include "rgy_version.h"

#if ENABLE_CPP_REGEX
#include <regex>
#endif //#if ENABLE_CPP_REGEX
#if ENABLE_DTL
#include <dtl/dtl.hpp>
#endif //#if ENABLE_DTL

#if defined(_WIN32) || defined(_WIN64)
static bool check_locale_is_ja() {
    const WORD LangID_ja_JP = MAKELANGID(LANG_JAPANESE, SUBLANG_JAPANESE_JAPAN);
    return GetUserDefaultLangID() == LangID_ja_JP;
}
#endif //#if defined(_WIN32) || defined(_WIN64)

static void show_version() {
    _ftprintf(stdout, _T("%s"), GetQSVEncVersion().c_str());
}

static tstring PrintListOptions(const TCHAR *option_name, const CX_DESC *list, int default_index) {
    const TCHAR *indent_space = _T("                                ");
    const int indent_len = (int)_tcslen(indent_space);
    const int max_len = 77;
    tstring str = strsprintf(_T("   %s "), option_name);
    while ((int)str.length() < indent_len)
        str += _T(" ");
    int line_len = (int)str.length();
    for (int i = 0; list[i].desc; i++) {
        if (line_len + _tcslen(list[i].desc) + _tcslen(_T(", ")) >= max_len) {
            str += strsprintf(_T("\n%s"), indent_space);
            line_len = indent_len;
        } else {
            if (i) {
                str += strsprintf(_T(", "));
                line_len += 2;
            }
        }
        str += strsprintf(_T("%s"), list[i].desc);
        line_len += (int)_tcslen(list[i].desc);
    }
    str += strsprintf(_T("\n%s default: %s\n"), indent_space, list[default_index].desc);
    return str;
}

class CombinationGenerator {
public:
    CombinationGenerator(int i) : m_nCombination(i) {

    }
    void create(vector<int> used) {
        if ((int)used.size() == m_nCombination) {
            m_nCombinationList.push_back(used);
        }
        for (int i = 0; i < m_nCombination; i++) {
            if (std::find(used.begin(), used.end(), i) == used.end()) {
                vector<int> u = used;
                u.push_back(i);
                create(u);
            }
        }
    }
    vector<vector<int>> generate() {
        vector<int> used;
        create(used);
        return m_nCombinationList;
    };
    int m_nCombination;
    vector<vector<int>> m_nCombinationList;
};

typedef struct ListData {
    const TCHAR *name;
    const CX_DESC *list;
    int default_index;
} ListData;

static tstring PrintMultipleListOptions(const TCHAR *option_name, const TCHAR *option_desc, const vector<ListData>& listDatas) {
    tstring str;
    const TCHAR *indent_space = _T("                                ");
    const int indent_len = (int)_tcslen(indent_space);
    const int max_len = 79;
    str += strsprintf(_T("   %s "), option_name);
    while ((int)str.length() < indent_len) {
        str += _T(" ");
    }
    str += strsprintf(_T("%s\n"), option_desc);
    const auto data_name_max_len = indent_len + 4 + std::accumulate(listDatas.begin(), listDatas.end(), 0,
        [](const int max_len, const ListData data) { return (std::max)(max_len, (int)_tcslen(data.name)); });

    for (const auto& data : listDatas) {
        tstring line = strsprintf(_T("%s- %s: "), indent_space, data.name);
        while ((int)line.length() < data_name_max_len) {
            line += strsprintf(_T(" "));
        }
        for (int i = 0; data.list[i].desc; i++) {
            const int desc_len = (int)(_tcslen(data.list[i].desc) + _tcslen(_T(", ")) + ((i == data.default_index) ? _tcslen(_T("(default)")) : 0));
            if (line.length() + desc_len >= max_len) {
                str += line + _T("\n");
                line = indent_space;
                while ((int)line.length() < data_name_max_len) {
                    line += strsprintf(_T(" "));
                }
            } else {
                if (i) {
                    line += strsprintf(_T(", "));
                }
            }
            line += strsprintf(_T("%s%s"), data.list[i].desc, (i == data.default_index) ? _T("(default)") : _T(""));
        }
        str += line + _T("\n");
    }
    return str;
}

static tstring help() {
    tstring str;
    str += strsprintf(_T("Usage: QSVEncC [Options] -i <filename> -o <filename>\n"));
    str += strsprintf(_T("\n")
        _T("%s input can be %s%s%sraw YUV or YUV4MPEG2(y4m) format.\n")
        _T("when raw(default), fps, input-res are also necessary.\n")
        _T("\n")
        _T("output format will be automatically set by the output extension.\n")
        _T("when output filename is set to \"-\", H.264/AVC ES output is thrown to stdout.\n")
        _T("\n")
        _T("Example:\n")
        _T("  QSVEncC -i \"<avsfilename>\" -o \"<outfilename>\"\n")
        _T("  avs2pipemod -y4mp \"<avsfile>\" | QSVEncC --y4m -i - -o \"<outfilename>\"\n")
        _T("\n")
        _T("Example for Benchmark:\n")
        _T("  QSVEncC -i \"<avsfilename>\" --benchmark \"<benchmark_result.txt>\"\n")
        );
    str += strsprintf(_T("\n")
        _T("Information Options: \n")
        _T("-h,-? --help                    show help\n")
        _T("-v,--version                    show version info\n")
        _T("   --check-hw                   check if QuickSyncVideo is available\n")
        _T("   --check-lib                  check lib API version installed\n")
        _T("   --check-features [<string>]  check encode/vpp features\n")
        _T("                                 with no option value, result will on stdout,\n")
        _T("                                 otherwise, it is written to file path set\n")
        _T("                                 and opened by default application.\n")
        _T("                                 when writing to file, txt/html/csv format\n")
        _T("                                 is available, chosen by the extension\n")
        _T("                                 of the output file.\n")
        _T("   --check-features-html [<string>]\n")
        _T("                                check encode/vpp features and write html report to\n")
        _T("                                 specified path. With no value, \"qsv_check.html\"\n")
        _T("                                 will be created to current directory.\n")
        _T("   --check-environment          check environment info\n")
        _T("\n"));

    str += strsprintf(_T("\n")
        _T("Basic Encoding Options: \n")
        _T("-c,--codec <string>             set encode codec\n")
        _T("                                 - h264(default), hevc, mpeg2, raw\n")
        _T("-i,--input-file <filename>      set input file name\n")
        _T("-o,--output-file <filename>     set ouput file name\n")
        _T("\n")
        _T(" Input formats (will be estimated from extension if not set.)\n")
        _T("   --raw                        set input as raw format\n")
        _T("   --y4m                        set input as y4m format\n")
    );
    str += strsprintf(_T("\n")
        _T("   --nv12                       set raw input as NV12 color format,\n")
        _T("                                if not specified YV12 is expected\n")
        _T("   --tff                        set as interlaced, top field first\n")
        _T("   --bff                        set as interlaced, bottom field first\n")
        _T("   --fps <int>/<int> or <float> video frame rate (frames per second)\n")
        _T("\n")
        _T("   --input-res <int>x<int>      input resolution\n")
        _T("   --output-res <int>x<int>     output resolution\n")
        _T("                                if different from input, uses vpp resizing\n")
        _T("                                if not set, output resolution will be same\n")
        _T("                                as input (no resize will be done).\n")
        _T("   --fixed-func                 use fixed func instead of GPU EU (default:off)\n")
        _T("\n"));
    str += strsprintf(_T("Frame buffer Options:\n")
        _T(" frame buffers are selected automatically by default.\n")
#ifdef D3D_SURFACES_SUPPORT
        _T(" d3d9 memory is faster than d3d11, so d3d9 frames are used whenever possible,\n")
        _T(" except decode/vpp only mode (= no encoding mode, system frames are used).\n")
        _T(" On particular cases, such as runnning on a system with dGPU, or running\n")
        _T(" vpp-rotate, will require the uses of d3d11 surface.\n")
        _T(" Options below will change this default behavior.\n")
        _T("\n")
        _T("   --disable-d3d                disable using d3d surfaces\n")
#if MFX_D3D11_SUPPORT
        _T("   --d3d                        use d3d9/d3d11 surfaces\n")
        _T("   --d3d9                       use d3d9 surfaces\n")
        _T("   --d3d11                      use d3d11 surfaces\n")
#else
        str += strsprintf(_T("")
            _T("   --d3d                        use d3d9 surfaces\n")
#endif //MFX_D3D11_SUPPORT
#endif //D3D_SURFACES_SUPPORT
#ifdef LIBVA_SUPPORT
            _T("   --disable-va                 disable using va surfaces\n")
            _T("   --va                         use va surfaces\n")
#endif //#ifdef LIBVA_SUPPORT
            _T("\n"));
    str += strsprintf(_T("Encode Mode Options:\n")
        _T(" EncMode default: --cqp\n")
        _T("   --cqp <int> or               encode in Constant QP, default %d:%d:%d\n")
        _T("         <int>:<int>:<int>      set qp value for i:p:b frame\n")
        _T("   --la <int>                   set bitrate in Lookahead mode (kbps)\n")
        _T("   --la-hrd <int>               set bitrate in HRD-Lookahead mode (kbps)\n")
        _T("   --icq <int>                  encode in Intelligent Const. Quality mode\n")
        _T("                                  default value: %d\n")
        _T("   --la-icq <int>               encode in ICQ mode with Lookahead\n")
        _T("                                  default value: %d\n")
        _T("   --cbr <int>                  set bitrate in CBR mode (kbps)\n")
        _T("   --vbr <int>                  set bitrate in VBR mode (kbps)\n")
        _T("   --avbr <int>                 set bitrate in AVBR mode (kbps)\n")
        _T("                                 AVBR mode is only supported with API v1.3\n")
        _T("   --avbr-unitsize <int>        avbr calculation period in x100 frames\n")
        _T("                                 default %d (= unit size %d00 frames)\n")
        //_T("   --avbr-range <float>           avbr accuracy range from bitrate set\n)"
        //_T("                                   in percentage, defalut %.1f(%%)\n)"
        _T("   --qvbr <int>                 set bitrate in Quality VBR mode.\n")
        _T("                                 requires --qvbr-q option to be set as well\n")
        _T("   --qvbr-q <int>  or           set quality used in qvbr mode. default: %d\n")
        _T("   --qvbr-quality <int>          QVBR mode is only supported with API v1.11\n")
        _T("   --vcm <int>                  set bitrate in VCM mode (kbps)\n")
        _T("\n"),
        QSV_DEFAULT_QPI, QSV_DEFAULT_QPP, QSV_DEFAULT_QPB,
        QSV_DEFAULT_QPI, QSV_DEFAULT_QPP, QSV_DEFAULT_QPB,
        QSV_DEFAULT_ICQ, QSV_DEFAULT_ICQ,
        QSV_DEFAULT_CONVERGENCE, QSV_DEFAULT_CONVERGENCE,
        QSV_DEFAULT_QVBR);
    str += strsprintf(_T("Other Encode Options:\n")
        _T("   --fallback-rc                enable fallback of ratecontrol mode, when\n")
        _T("                                 platform does not support new ratecontrol modes.\n")
        _T("-a,--async-depth                set async depth for QSV pipeline. (0-%d)\n")
        _T("                                 default: 0 (=auto, 4+2*(extra pipeline step))\n")
        _T("   --max-bitrate <int>          set max bitrate(kbps)\n")
        _T("   --qp-min <int> or            set min QP, default 0 (= unset)\n")
        _T("           <int>:<int>:<int>\n")
        _T("   --qp-max <int> or            set max QP, default 0 (= unset)\n")
        _T("           <int>:<int>:<int>\n")
        _T("   --qp-offset <int>[:<int>][:<int>]...\n")
        _T("                                set qp offset of each pyramid reference layers.\n")
        _T("                                 default 0 (= unset).\n")
        _T("-u,--quality <string>           encode quality\n")
        _T("                                  - best, higher, high, balanced(default)\n")
        _T("                                    fast, faster, fastest\n")
        _T("   --la-depth <int>             set Lookahead Depth, %d-%d\n")
        _T("   --la-window-size <int>       enables Lookahead Windowed Rate Control mode,\n")
        _T("                                  and set the window size in frames.\n")
        _T("   --la-quality <string>        set lookahead quality.\n")
        _T("                                 - auto(default), fast, medium, slow\n")
        _T("   --(no-)mbbrc                 enables per macro block rate control\n")
        _T("                                 default: auto\n")
        _T("   --ref <int>                  reference frames\n")
        _T("                                  default %d (auto)\n")
        _T("-b,--bframes <int>              number of sequential b frames\n")
        _T("                                  default %d(HEVC) / %d(others)\n")
        _T("   --(no-)b-pyramid             enables B-frame pyramid reference (default:off)\n")
        _T("   --(no-)direct-bias-adjust    lower usage of B frame Direct/Skip type\n")
        _T("   --gop-len <int>              (max) gop length, default %d (auto)\n")
        _T("                                  when auto, fps x 10 will be set.\n")
        _T("   --(no-)open-gop              enables open gop (default:off)\n")
        _T("   --strict-gop                 force gop structure\n")
        _T("   --(no-)i-adapt               enables adaptive I frame insert (default:off)\n")
        _T("   --(no-)b-adapt               enables adaptive B frame insert (default:off)\n")
        _T("   --(no-)weightp               enable weighted prediction for P frame\n")
        _T("   --(no-)weightb               enable weighted prediction for B frame\n")
        _T("   --(no-)adapt-ltr             enable adaptive LTR frames\n")
        _T("   --(no-)repartition-check     [H.264] enable prediction from small partitions\n")
#if ENABLE_FADE_DETECT
        _T("   --(no-)fade-detect           enable fade detection\n")
#endif //#if ENABLE_FADE_DETECT
        _T("   --trellis <string>           set trellis mode used in encoding\n")
        _T("                                 - auto(default), off, i, ip, all\n")
        _T("   --mv-scaling                 set mv cost scaling\n")
        _T("                                 - 0  set MV cost to be 0\n")
        _T("                                 - 1  set MV cost 1/2 of default\n")
        _T("                                 - 2  set MV cost 1/4 of default\n")
        _T("                                 - 3  set MV cost 1/8 of default\n")
        _T("   --slices <int>               number of slices, default 0 (auto)\n")
        _T("   --vbv-bufsize <int>          set vbv buffer size (kbit) / default: auto\n")
        _T("   --no-deblock                 [h264] disables H.264 deblock feature\n")
        _T("   --tskip                      [hevc] enable transform skip\n")
        _T("   --sao <string>               [hevc]\n")
        _T("                                 - auto    default\n")
        _T("                                 - none    disable sao\n")
        _T("                                 - luma    enable sao for luma\n")
        _T("                                 - chroma  enable sao for chroma\n")
        _T("                                 - all     enable sao for luma & chroma\n")
        _T("   --ctu <int>                  [hevc] max ctu size\n")
        _T("                                 - auto(default), 16, 32, 64\n")
        _T("   --sharpness <int>            [vp8] set sharpness level for vp8 enc\n")
        _T("\n"),
        QSV_ASYNC_DEPTH_MAX,
        QSV_LOOKAHEAD_DEPTH_MIN, QSV_LOOKAHEAD_DEPTH_MAX,
        QSV_DEFAULT_REF,
        QSV_DEFAULT_HEVC_BFRAMES, QSV_DEFAULT_H264_BFRAMES,
        QSV_DEFAULT_GOP_LEN);
    str += PrintMultipleListOptions(_T("--level <string>"), _T("set codec level"),
        { { _T("H.264"), list_avc_level,   0 },
          { _T("HEVC"),  list_hevc_level,  0 },
          { _T("MPEG2"), list_mpeg2_level, 0 }
        });
    str += PrintMultipleListOptions(_T("--profile <string>"), _T("set codec profile"),
        { { _T("H.264"), list_avc_profile,   0 },
          { _T("HEVC"),  list_hevc_profile,  0 },
          { _T("MPEG2"), list_mpeg2_profile, 0 }
        });
    str += PrintMultipleListOptions(_T("--tier <string>"), _T("set codec tier"),
        { { _T("HEVC"),  list_hevc_tier,  0 },
        });

    str += strsprintf(_T("\n")
        _T("   --sar <int>:<int>            set Sample Aspect Ratio\n")
        _T("   --dar <int>:<int>            set Display Aspect Ratio\n")
        _T("   --bluray                     for H.264 bluray encoding\n")
        _T("\n")
        _T("   --crop <int>,<int>,<int>,<int>\n")
        _T("                                set crop pixels of left, up, right, bottom.\n")
        _T("\n"));
    str += PrintListOptions(_T("--videoformat <string>"), list_videoformat, 0);
    str += PrintListOptions(_T("--colormatrix <string>"), list_colormatrix, 0);
    str += PrintListOptions(_T("--colorprim <string>"),   list_colorprim,   0);
    str += PrintListOptions(_T("--transfer <string>"),    list_transfer,    0);
    str += strsprintf(_T("")
        _T("   --aud                        insert aud nal unit to ouput stream.\n")
        _T("   --pic-struct                 insert pic-timing SEI with pic_struct.\n")
        _T("   --chromaloc <int>            set chroma location flag [ 0 ... 5 ]\n")
        _T("                                  default: 0 = unspecified\n")
        _T("   --fullrange                  set stream as fullrange yuv\n")
        _T("   --max-cll <int>,<int>        set MaxCLL and MaxFall in nits. e.g. \"1000,300\"\n")
        _T("   --master-display <string>    set Mastering display data.\n")
        _T("      e.g. \"G(13250,34500)B(7500,3000)R(34000,16000)WP(15635,16450)L(10000000,1)\"\n"));
    str += strsprintf(_T("\n")
        //_T("   --sw                         use software encoding, instead of QSV (hw)\n")
        _T("   --input-buf <int>            buffer size for input in frames (%d-%d)\n")
        _T("                                 default   hw: %d,  sw: %d\n")
        _T("                                 cannot be used with avqsv reader.\n"),
        QSV_INPUT_BUF_MIN, QSV_INPUT_BUF_MAX,
        QSV_DEFAULT_INPUT_BUF_HW, QSV_DEFAULT_INPUT_BUF_SW
        );
    str += strsprintf(_T("")
        _T("   --output-buf <int>           buffer size for output in MByte\n")
        _T("                                 default %d MB (0-%d)\n"),
        QSV_DEFAULT_OUTPUT_BUF_MB, RGY_OUTPUT_BUF_MB_MAX
        );
    str += strsprintf(_T("")
#if defined(_WIN32) || defined(_WIN64)
        _T("   --mfx-thread <int>          set mfx thread num (-1 (auto), 2, 3, ...)\n")
        _T("                                 note that mfx thread cannot be less than 2.\n")
#endif
        _T("   --input-thread <int>        set input thread num\n")
        _T("                                  0: disable (slow, but less cpu usage)\n")
        _T("                                  1: use one thread\n")
#if ENABLE_AVCODEC_OUT_THREAD
        _T("   --output-thread <int>        set output thread num\n")
        _T("                                 -1: auto (= default)\n")
        _T("                                  0: disable (slow, but less memory usage)\n")
        _T("                                  1: use one thread\n")
#if 0
        _T("   --audio-thread <int>         set audio thread num, available only with output thread\n")
        _T("                                 -1: auto (= default)\n")
        _T("                                  0: disable (slow, but less memory usage)\n")
        _T("                                  1: use one thread\n")
        _T("                                  2: use two thread\n")
#endif //#if ENABLE_AVCODEC_AUDPROCESS_THREAD
#endif //#if ENABLE_AVCODEC_OUT_THREAD
        _T("   --min-memory                 minimize memory usage of QSVEncC.\n")
        _T("                                 same as --output-thread 0 --audio-thread 0\n")
        _T("                                   --mfx-thread 2 -a 1 --input-buf 1 --output-buf 0\n")
        _T("                                 this will cause lower performance!\n")
        _T("   --max-procfps <int>         limit processing speed to lower resource usage.\n")
        _T("                                 default:0 (no limit)\n")
        );
    str += strsprintf(
        _T("   --log <string>               output log to file (txt or html).\n")
        _T("   --log-level <string>         set output log level\n")
        _T("                                 info(default), warn, error, debug\n")
        _T("   --log-framelist <string>     output frame info for avqsv reader (for debug)\n")
#if _DEBUG
        _T("   --log-mus-ts <string>         (for debug)\n")
        _T("   --log-copy-framedata <string> (for debug)\n")
#endif
        );
#if ENABLE_SESSION_THREAD_CONFIG
    str += strsprintf(_T("")
        _T("   --session-threads            set num of threads for QSV session. (0-%d)\n")
        _T("                                 default: 0 (=auto)\n")
        _T("   --session-thread-priority    set thread priority for QSV session.\n")
        _T("                                  - low, normal(default), high\n"),
        QSV_SESSION_THREAD_MAX);
#endif
    str += strsprintf(_T("\n")
        _T("   --benchmark <string>         run in benchmark mode\n")
        _T("                                 and write result in txt file\n")
        _T("   --bench-quality \"all\" or <string>[,<string>][,<string>]...\n")
        _T("                                 default: \"best,balanced,fastest\"\n")
        _T("                                list of target quality to check on benchmark\n")
        _T("   --perf-monitor [<string>][,<string>]...\n")
        _T("       check performance info of QSVEncC and output to log file\n")
        _T("       select counter from below, default = all\n")
        _T("   --perf-monitor-plot [<string>][,<string>]...\n")
        _T("       plot perf monitor realtime (required python, pyqtgraph)\n")
        _T("       select counter from below, default = cpu,bitrate\n")
        _T("                                 \n")
        _T("     counters for perf-monitor, perf-monitor-plot\n")
        _T("                                 all          ... monitor all info\n")
        _T("                                 cpu_total    ... cpu total usage (%%)\n")
        _T("                                 cpu_kernel   ... cpu kernel usage (%%)\n")
#if defined(_WIN32) || defined(_WIN64)
        _T("                                 cpu_main     ... cpu main thread usage (%%)\n")
        _T("                                 cpu_enc      ... cpu encode thread usage (%%)\n")
        _T("                                 cpu_in       ... cpu input thread usage (%%)\n")
        _T("                                 cpu_out      ... cpu output thread usage (%%)\n")
        _T("                                 cpu_aud_proc ... cpu aud proc thread usage (%%)\n")
        _T("                                 cpu_aud_enc  ... cpu aud enc thread usage (%%)\n")
#endif //#if defined(_WIN32) || defined(_WIN64)
        _T("                                 cpu          ... monitor all cpu info\n")
#if defined(_WIN32) || defined(_WIN64)
        _T("                                 gpu_load    ... gpu usage (%%)\n")
        _T("                                 gpu_clock   ... gpu avg clock (%%)\n")
        _T("                                 gpu         ... monitor all gpu info\n")
#endif //#if defined(_WIN32) || defined(_WIN64)
        _T("                                 queue       ... queue usage\n")
        _T("                                 mem_private ... private memory (MB)\n")
        _T("                                 mem_virtual ... virtual memory (MB)\n")
        _T("                                 mem         ... monitor all memory info\n")
        _T("                                 io_read     ... io read  (MB/s)\n")
        _T("                                 io_write    ... io write (MB/s)\n")
        _T("                                 io          ... monitor all io info\n")
        _T("                                 fps         ... encode speed (fps)\n")
        _T("                                 fps_avg     ... encode avg. speed (fps)\n")
        _T("                                 bitrate     ... encode bitrate (kbps)\n")
        _T("                                 bitrate_avg ... encode avg. bitrate (kbps)\n")
        _T("                                 frame_out   ... written_frames\n")
        _T("                                 \n")
        _T("   --python <string>            set python path for --perf-monitor-plot\n")
        _T("                                 default: python\n")
        _T("   --perf-monitor-interval <int> set perf monitor check interval (millisec)\n")
        _T("                                 default 250, must be 50 or more\n")
#if defined(_WIN32) || defined(_WIN64)
        _T("   --(no-)timer-period-tuning   enable(disable) timer period tuning\n")
        _T("                                  default: enabled\n")
#endif //#if defined(_WIN32) || defined(_WIN64)
        );
#if 0
    str += strsprintf(_T("\n")
        _T(" Settings below are available only for software ecoding.\n")
        _T("   --cavlc                      use cavlc instead of cabac\n")
        _T("   --rdo                        use rate distortion optmization\n")
        _T("   --inter-pred <int>           set minimum block size used for\n")
        _T("   --intra-pred <int>           inter/intra prediction\n")
        _T("                                  0: auto(default)   1: 16x16\n")
        _T("                                  2: 8x8             3: 4x4\n")
        _T("   --mv-search <int>            set window size for mv search\n")
        _T("                                  default: 0 (auto)\n")
        _T("   --mv-precision <int>         set precision of mv search\n")
        _T("                                  0: auto(default)   1: full-pell\n")
        _T("                                  2: half-pell       3: quater-pell\n")
        );
#endif
    str += strsprintf(_T("\nVPP Options:\n")
        _T("   --vpp-denoise <int>          use vpp denoise, set strength (%d-%d)\n")
        _T("   --vpp-mctf [\"auto\" or <int>] use vpp motion compensated temporal filter(mctf)\n")
        _T("                                  set strength (%d-%d), default: %d (auto)\n")
        _T("   --vpp-detail-enhance <int>   use vpp detail enahancer, set strength (%d-%d)\n")
        _T("   --vpp-deinterlace <string>   set vpp deinterlace mode\n")
        _T("                                 - none     disable deinterlace\n")
        _T("                                 - normal   normal deinterlace\n")
        _T("                                 - it       inverse telecine\n")
#if ENABLE_ADVANCED_DEINTERLACE
        _T("                                 - it-manual <string>\n")
        _T("                                     \"32\", \"2332\", \"repeat\", \"41\"\n")
#endif
        _T("                                 - bob      double framerate\n")
#if ENABLE_ADVANCED_DEINTERLACE
        _T("                                 - auto     auto deinterlace\n")
        _T("                                 - auto-bob auto bob deinterlace\n")
#endif
#if ENABLE_FPS_CONVERSION
        _T("   --vpp-fps-conv <string>      set fps conversion mode\n")
        _T("                                enabled only when input is progressive\n")
        _T("                                 - none, x2, x2.5\n")
#endif
        _T("   --vpp-image-stab <string>    set image stabilizer mode\n")
        _T("                                 - none, upscale, box\n")
        _T("   --vpp-rotate <int>           rotate image\n")
        _T("                                 90, 180, 270.\n")
        _T("   --vpp-mirror <string>        mirror image\n")
        _T("                                 - h   mirror in horizontal direction\n")
        _T("                                 - v   mirror in vertical   direction\n")
        _T("   --vpp-half-turn              half turn video image\n")
        _T("                                 unoptimized and very slow.\n")
        _T("   --vpp-resize <string>        set scaling quality\n")
        _T("                                 - auto(default)\n")
        _T("                                 - simple   use simple scaling\n")
        _T("                                 - fine     use high quality scaling\n")
#if ENABLE_CUSTOM_VPP
        _T("   --vpp-delogo <string>        set delogo file path\n")
        _T("   --vpp-delogo-select <string> set target logo name or auto select file\n")
        _T("                                 or logo index starting from 1.\n")
        _T("   --vpp-delogo-pos <int>:<int> set delogo pos offset\n")
        _T("   --vpp-delogo-depth <int>     set delogo depth [default:%d]\n")
        _T("   --vpp-delogo-y  <int>        set delogo y  param\n")
        _T("   --vpp-delogo-cb <int>        set delogo cb param\n")
        _T("   --vpp-delogo-cr <int>        set delogo cr param\n")
        _T("   --vpp-delogo-add             add logo mode\n"),
#endif //#if ENABLE_CUSTOM_VPP,
        QSV_VPP_DENOISE_MIN, QSV_VPP_DENOISE_MAX,
        QSV_VPP_MCTF_MIN, QSV_VPP_MCTF_MAX, QSV_VPP_MCTF_AUTO,
        QSV_VPP_DETAIL_ENHANCE_MIN, QSV_VPP_DETAIL_ENHANCE_MAX,
        QSV_DEFAULT_VPP_DELOGO_DEPTH
        );
    return str;
}

static void show_help() {
    _ftprintf(stdout, _T("%s\n"), help().c_str());
}

static int writeFeatureList(tstring filename, bool for_auo, FeatureListStrType type = FEATURE_LIST_STR_TYPE_UNKNOWN) {
    uint32_t codepage = CP_THREAD_ACP;
    if (type == FEATURE_LIST_STR_TYPE_UNKNOWN) {
            type = FEATURE_LIST_STR_TYPE_TXT;
    }

    FILE *fp = stdout;
    if (filename.length()) {
        if (_tfopen_s(&fp, filename.c_str(), _T("w"))) {
            return 1;
        }
    }

    auto print_tstring = [&](tstring str) {
        if (type == FEATURE_LIST_STR_TYPE_TXT) {
            _ftprintf(fp, _T("%s"), str.c_str());
        }
    };

    fprintf(stderr, "[DEBUG]--->Checking for QSV...\n");
    print_tstring(GetQSVEncVersion());

    tstring environmentInfo = getEnviromentInfo(false);
    print_tstring(environmentInfo + _T("\n"));
    bool bOSSupportsQSV = true;
#if defined(_WIN32) || defined(_WIN64)
    OSVERSIONINFOEXW osver;
    tstring osversion = getOSVersion(&osver);
    bOSSupportsQSV &= osver.dwPlatformId == VER_PLATFORM_WIN32_NT;
    bOSSupportsQSV &= (osver.dwMajorVersion >= 7 || osver.dwMajorVersion == 6 && osver.dwMinorVersion >= 1);
    bOSSupportsQSV &= osver.wProductType == VER_NT_WORKSTATION;
#else
    tstring osversion = getOSVersion();
#endif

    mfxVersion test = { 0, 1 };
    for (int impl_type = 0; impl_type < 1; impl_type++) {
        mfxVersion lib = (impl_type) ? get_mfx_libsw_version() : get_mfx_libhw_version();
        const TCHAR *impl_str = (impl_type) ?  _T("Software") : _T("Hardware");
        if (!check_lib_version(lib, test)) {
            if (impl_type == 0) 
			{
                print_tstring(_T("QSV unavailable.\n"));
            } else {
                print_tstring(strsprintf(_T("Media SDK %s unavailable.\n"), impl_str));
            }
        } else {
            const auto codec_feature_list = (for_auo) ? MakeFeatureListStr(type, make_vector(CODEC_LIST_AUO)) : MakeFeatureListStr(type);
            if (codec_feature_list.size() != 0){
                print_tstring(strsprintf(_T("Media SDK Version: ")));
                print_tstring(strsprintf(_T("%s API v%d.%d\n\n"), impl_str, lib.Major, lib.Minor));
                auto codecHeader = _T("[DEBUG]--->Supported Enc features:\n");
                print_tstring(codecHeader);
				print_tstring(strsprintf(_T("[DEBUG]--->Supported Enc features size = (%d):\n"),codec_feature_list.size()));
				
                uint32_t i = 0;
                for (; i < codec_feature_list.size(); i++)
				{
                    auto codec_feature = codec_feature_list[i].second;
                    print_tstring(strsprintf(_T("%s\n\n"), codec_feature.c_str()));
                }
                if (!for_auo) {
                    const auto vppHeader = tstring(_T("Supported Vpp features:\n"));
                    const auto vppFeatures = MakeVppFeatureStr(type);
                    print_tstring(vppHeader + _T("\n"));
                    print_tstring(strsprintf(_T("%s\n\n"), vppFeatures.c_str()));

                    const auto decHeader = tstring(_T("Supported Decode features:\n"));
                    const auto decFeatures = MakeDecFeatureStr(type);
                    print_tstring(decHeader + _T("\n"));
                    print_tstring(strsprintf(_T("%s\n\n"), decFeatures.c_str()));
                }
            }
        }
    }
    if (filename.length() && fp) {
        fclose(fp);
#if defined(_WIN32) || defined(_WIN64)
        TCHAR exePath[1024] = { 0 };
        if (32 <= (size_t)FindExecutable(filename.c_str(), nullptr, exePath) \
			&& _tcslen(exePath) \
			&& PathFileExists(exePath)) 
		{
            ShellExecute(NULL, _T("open"), filename.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
#endif //#if defined(_WIN32) || defined(_WIN64)
    }
    return 0;
}


int parse_print_options(const TCHAR *option_name, const TCHAR *arg1) {

    // process multi-character options
    if (0 == _tcscmp(option_name, _T("help"))) {
        show_version();
        show_help();
        return 1;
    }
    if (0 == _tcscmp(option_name, _T("version"))) {
        show_version();
        return 1;
    }
    if (0 == _tcscmp(option_name, _T("check-environment"))) {
        show_version();
        _ftprintf(stdout, _T("%s"), getEnviromentInfo(true).c_str());
        return 1;
    }
    if (0 == _tcscmp(option_name, _T("check-environment-auo"))) {
        show_version();
        _ftprintf(stdout, _T("%s"), getEnviromentInfo(false).c_str());
        mfxVersion lib = get_mfx_libhw_version();
        mfxVersion test = { 0, 1 };
        if (check_lib_version(lib, test)) {
            _ftprintf(stdout, _T("Media SDK Version: Hardware API v%d.%d\n\n"), lib.Major, lib.Minor);
        }
        return 1;
    }
    if (0 == _tcscmp(option_name, _T("check-features"))) {
        tstring output = (arg1[0] != _T('-')) ? arg1 : _T("");
        writeFeatureList(output, false);
        return 1;
    }
    if (0 == _tcscmp(option_name, _T("check-features-auo"))) {
        writeFeatureList(_T(""), true);
        return 1;
    }
    if (0 == _tcscmp(option_name, _T("check-features-html"))) {
        tstring output = (arg1[0] != _T('-')) ? arg1 : _T("");
        writeFeatureList(output, false, FEATURE_LIST_STR_TYPE_HTML);
        return 1;
    }
    if (0 == _tcscmp(option_name, _T("check-hw"))
        || 0 == _tcscmp(option_name, _T("hw-check")))
    {
        mfxVersion ver = { 0, 1 };
        if (check_lib_version(get_mfx_libhw_version(), ver) != 0) {
            _ftprintf(stdout, _T("Success: QuickSyncVideo (hw encoding) available\n"));
            exit(0);
        } else {
            _ftprintf(stdout, _T("Error: QuickSyncVideo (hw encoding) unavailable\n"));
            exit(1);
        }
    }
    if (0 == _tcscmp(option_name, _T("lib-check"))
        || 0 == _tcscmp(option_name, _T("check-lib"))) {
        mfxVersion test = { 0, 1 };
        mfxVersion hwlib = get_mfx_libhw_version();
        mfxVersion swlib = get_mfx_libsw_version();
        show_version();
#ifdef _M_IX86
        const TCHAR *dll_platform = _T("32");
#else
        const TCHAR *dll_platform = _T("64");
#endif
        if (check_lib_version(hwlib, test))
            _ftprintf(stdout, _T("libmfxhw%s.dll : v%d.%d\n"), dll_platform, hwlib.Major, hwlib.Minor);
        else
            _ftprintf(stdout, _T("libmfxhw%s.dll : ----\n"), dll_platform);
        if (check_lib_version(swlib, test))
            _ftprintf(stdout, _T("libmfxsw%s.dll : v%d.%d\n"), dll_platform, swlib.Major, swlib.Minor);
        else
            _ftprintf(stdout, _T("libmfxsw%s.dll : ----\n"), dll_platform);
        return 1;
    }
    return 0;
}

//Ctrl + C handle function
static bool g_signal_abort = false;
#pragma warning(push)
#pragma warning(disable:4100)
static void sigcatch(int sig) {
    g_signal_abort = true;
}
#pragma warning(pop)
static int set_signal_handler() {
    int ret = 0;
    if (SIG_ERR == signal(SIGINT, sigcatch)) {
        _ftprintf(stderr, _T("failed to set signal handler.\n"));
    }
    return ret;
}

int run_encode(sInputParams *params) {
    mfxStatus sts = MFX_ERR_NONE; // return value check

    unique_ptr<CQSVPipeline> pPipeline(new CQSVPipeline);
    if (!pPipeline) {
        return MFX_ERR_MEMORY_ALLOC;
    }

    sts = pPipeline->Init(params);
    if (sts < MFX_ERR_NONE) return sts;

    if (params->pStrLogFile) {
        free(params->pStrLogFile);
        params->pStrLogFile = NULL;
    }

    pPipeline->SetAbortFlagPointer(&g_signal_abort);
    set_signal_handler();

    if (MFX_ERR_NONE != (sts = pPipeline->CheckCurrentVideoParam())) {
        return sts;
    }

    for (;;) {
        sts = pPipeline->Run();

        if (MFX_ERR_DEVICE_LOST == sts || MFX_ERR_DEVICE_FAILED == sts) {
            _ftprintf(stderr, _T("\nERROR: Hardware device was lost or returned an unexpected error. Recovering...\n"));
            sts = pPipeline->ResetDevice();
            if (sts < MFX_ERR_NONE) return sts;

            sts = pPipeline->ResetMFXComponents(params);
            if (sts < MFX_ERR_NONE) return sts;
            continue;
        } else {
            if (sts < MFX_ERR_NONE) return sts;
            break;
        }
    }

    pPipeline->Close();

    return sts;
}

mfxStatus run_benchmark(sInputParams *params) {
    using namespace std;
    mfxStatus sts = MFX_ERR_NONE;
    basic_string<TCHAR> benchmarkLogFile = params->strDstFile;

    //测试分辨率
    const vector<pair<int, int>> test_resolution = { { 1920, 1080 }, { 1280, 720 } };

    //第一组输出
    {
        params->nDstWidth = test_resolution[0].first;
        params->nDstHeight = test_resolution[0].second;
        params->nTargetUsage = MFX_TARGETUSAGE_BEST_SPEED;

        unique_ptr<CQSVPipeline> pPipeline(new CQSVPipeline);
        if (!pPipeline) {
            return MFX_ERR_MEMORY_ALLOC;
        }

        sts = pPipeline->Init(params);
        if (sts < MFX_ERR_NONE) return sts;

        pPipeline->SetAbortFlagPointer(&g_signal_abort);
        set_signal_handler();
        time_t current_time = time(NULL);
        struct tm *local_time = localtime(&current_time);

        TCHAR encode_info[4096] = { 0 };
        if (MFX_ERR_NONE != (sts = pPipeline->CheckCurrentVideoParam(encode_info, _countof(encode_info)))) {
            return sts;
        }

        bool hardware;
        mfxVersion ver;
        pPipeline->GetEncodeLibInfo(&ver, &hardware);

        auto enviroment_info = getEnviromentInfo();

        MemType memtype = pPipeline->GetMemType();

        basic_stringstream<TCHAR> ss;
        FILE *fp_bench = NULL;
        if (_tfopen_s(&fp_bench, benchmarkLogFile.c_str(), _T("a")) || NULL == fp_bench) {
            pPipeline->PrintMes(RGY_LOG_ERROR, _T("\nERROR: failed opening benchmark result file.\n"));
            return MFX_ERR_INVALID_HANDLE;
        } else {
            fprintf(fp_bench, "Started benchmark on %d.%02d.%02d %2d:%02d:%02d\n",
                1900 + local_time->tm_year, local_time->tm_mon + 1, local_time->tm_mday, local_time->tm_hour, local_time->tm_min, local_time->tm_sec);
            fprintf(fp_bench, "Input File: %s\n", tchar_to_string(params->strSrcFile).c_str());
            fprintf(fp_bench, "Basic parameters of the benchmark\n"
                              " (Target Usage and output resolution will be changed)\n");
            fprintf(fp_bench, "%s\n\n", tchar_to_string(encode_info).c_str());
            fprintf(fp_bench, "%s", tchar_to_string(enviroment_info).c_str());
            fprintf(fp_bench, "QSV: QSVEncC %s (%s) / API[%s]: v%d.%d / %s\n",
                VER_STR_FILEVERSION, tchar_to_string(BUILD_ARCH_STR).c_str(), (hardware) ? "hw" : "sw", ver.Major, ver.Minor, tchar_to_string(MemTypeToStr(memtype)).c_str());
            fprintf(fp_bench, "\n");
            fclose(fp_bench);
        }
        basic_ofstream<TCHAR> benchmark_log_test_open(benchmarkLogFile, ios::out | ios::app);
        if (!benchmark_log_test_open.good()) {
            pPipeline->PrintMes(RGY_LOG_ERROR, _T("\nERROR: failed opening benchmark result file.\n"));
            return MFX_ERR_INVALID_HANDLE;
        }
        benchmark_log_test_open << ss.str();
        benchmark_log_test_open.close();

        for (;;) {
            sts = pPipeline->Run();

            if (MFX_ERR_DEVICE_LOST == sts || MFX_ERR_DEVICE_FAILED == sts) {
                pPipeline->PrintMes(RGY_LOG_ERROR, _T("\nERROR: Hardware device was lost or returned an unexpected error. Recovering...\n"));
                if (   MFX_ERR_NONE != (sts = pPipeline->ResetDevice())
                    || MFX_ERR_NONE != (sts = pPipeline->ResetMFXComponents(params)))
                    break;
            } else {
                break;
            }
        }

        EncodeStatusData data = { 0 };
        sts = pPipeline->GetEncodeStatusData(&data);

        pPipeline->Close();
    }

    //基准数据
    typedef struct benchmark_t {
        pair<int, int> resolution;
        int targetUsage;
        double fps;
        double bitrate;
        double cpuUsagePercent;
    } benchmark_t;

    //目标质量
    vector<CX_DESC> list_target_quality;
    for (uint32_t i = 0; i < _countof(list_quality); i++) {
        if (list_quality[i].desc) {
            int test = 1 << list_quality[i].value;
            if (params->nBenchQuality & test) {
                list_target_quality.push_back(list_quality[i]);
            }
        }
    }

    //更新分辨率并进行测试
    vector<vector<benchmark_t>> benchmark_result;
    benchmark_result.reserve(test_resolution.size() * list_target_quality.size());

    for (uint32_t i = 0; MFX_ERR_NONE == sts && !g_signal_abort && i < list_target_quality.size(); i++) {
        params->nTargetUsage = list_target_quality[i].value;
        vector<benchmark_t> benchmark_per_target_usage;
        for (const auto& resolution : test_resolution) {
            params->nDstWidth = resolution.first;
            params->nDstHeight = resolution.second;

            unique_ptr<CQSVPipeline> pPipeline(new CQSVPipeline);
            if (!pPipeline) {
                return MFX_ERR_MEMORY_ALLOC;
            }

            if (MFX_ERR_NONE != (sts = pPipeline->Init(params))) {
                break;
            }

            pPipeline->SetAbortFlagPointer(&g_signal_abort);
            set_signal_handler();
            if (MFX_ERR_NONE != (sts = pPipeline->CheckCurrentVideoParam())) {
                return sts;
            }

            for (;;) {
                sts = pPipeline->Run();

                if (MFX_ERR_DEVICE_LOST == sts || MFX_ERR_DEVICE_FAILED == sts) {
                    pPipeline->PrintMes(RGY_LOG_ERROR, _T("\nERROR: Hardware device was lost or returned an unexpected error. Recovering...\n"));
                    if (   MFX_ERR_NONE != (sts = pPipeline->ResetDevice())
                        || MFX_ERR_NONE != (sts = pPipeline->ResetMFXComponents(params)))
                        break;
                } else {
                    break;
                }
            }

            EncodeStatusData data = { 0 };
            sts = pPipeline->GetEncodeStatusData(&data);

            pPipeline->Close();

            benchmark_t result;
            result.resolution      = resolution;
            result.targetUsage     = list_target_quality[i].value;
            result.fps             = data.encodeFps;
            result.bitrate         = data.bitrateKbps;
            result.cpuUsagePercent = data.CPUUsagePercent;
            benchmark_per_target_usage.push_back(result);

            _ftprintf(stderr, _T("\n"));

            if (MFX_ERR_NONE != sts || g_signal_abort)
                break;
        }

        benchmark_result.push_back(benchmark_per_target_usage);
    }

    //结果输出
    if (MFX_ERR_NONE == sts && benchmark_result.size()) {
        basic_stringstream<TCHAR> ss;

        uint32_t maxLengthOfTargetUsageDesc = 0;
        for (uint32_t i = 0; i < list_target_quality.size(); i++) {
            maxLengthOfTargetUsageDesc = max(maxLengthOfTargetUsageDesc, (uint32_t)_tcslen(list_target_quality[i].desc));
        }

        FILE *fp_bench = NULL;
        if (_tfopen_s(&fp_bench, benchmarkLogFile.c_str(), _T("a")) || NULL == fp_bench) {
            _ftprintf(stderr, _T("\nERROR: failed opening benchmark result file.\n"));
            return MFX_ERR_INVALID_HANDLE;
        } else {
            fprintf(fp_bench, "TargetUsage ([TU-1]:Best Quality) ～ ([TU-7]:Fastest Speed)\n\n");

            fprintf(fp_bench, "Encode Speed (fps)\n");
            fprintf(fp_bench, "TargetUsage");
            for (const auto& resolution : test_resolution) {
                fprintf(fp_bench, ",   %dx%d", resolution.first, resolution.second);
            }
            fprintf(fp_bench, "\n");

            for (const auto &benchmark_per_target_usage : benchmark_result) {
                fprintf(fp_bench, " 　　TU-%d", benchmark_per_target_usage[0].targetUsage);
                for (const auto &result : benchmark_per_target_usage) {
                    fprintf(fp_bench, ",　　　%6.2f", result.fps);
                }
                fprintf(fp_bench, "\n");
            }
            fprintf(fp_bench, "\n");

            fprintf(fp_bench, "Bitrate (kbps)\n");
            fprintf(fp_bench, "TargetUsage");
            for (const auto& resolution : test_resolution) {
                fprintf(fp_bench, ",   %dx%d", resolution.first, resolution.second);
            }
            fprintf(fp_bench, "\n");
            for (const auto &benchmark_per_target_usage : benchmark_result) {
                fprintf(fp_bench, " 　　TU-%d", benchmark_per_target_usage[0].targetUsage);
                for (const auto &result : benchmark_per_target_usage) {
                    fprintf(fp_bench, ",　　　%6d", (int)(result.bitrate + 0.5));
                }
                fprintf(fp_bench, "\n");
            }
            fprintf(fp_bench, "\n");

            fprintf(fp_bench, "CPU Usage (%%)\n");
            fprintf(fp_bench, "TargetUsage");
            for (const auto& resolution : test_resolution) {
                fprintf(fp_bench, ",   %dx%d", resolution.first, resolution.second);
            }
            fprintf(fp_bench, "\n");
            for (const auto &benchmark_per_target_usage : benchmark_result) {
                fprintf(fp_bench, " 　　TU-%d", benchmark_per_target_usage[0].targetUsage);
                for (const auto &result : benchmark_per_target_usage) {
                    fprintf(fp_bench, ",　　　%6.2f", result.cpuUsagePercent);
                }
                fprintf(fp_bench, "\n");
            }
            fprintf(fp_bench, "\n");
            fclose(fp_bench);
            _ftprintf(stderr, _T("\nFinished benchmark.\n"));
        }
    } else {
        rgy_print_stderr(RGY_LOG_ERROR, _T("\nError occurred during benchmark.\n"));
    }

    return sts;
}

int run(int argc, TCHAR *argv[]) {
	printf("-----------------------------------------> run start\r\n");

    if (argc == 1) {
        show_version();
        show_help();
        return 1;
    }

    for (int iarg = 1; iarg < argc; iarg++) {
        const TCHAR *option_name = nullptr;
        if (argv[iarg][0] == _T('-')) {
            if (argv[iarg][1] == _T('\0')) {
                continue;
            } else if (argv[iarg][1] == _T('-')) {
                option_name = &argv[iarg][2];
            } else if (argv[iarg][2] == _T('\0')) {
                if (nullptr == (option_name = cmd_short_opt_to_long(argv[iarg][1]))) {
                    continue;
                }
            }
        }
        if (option_name != nullptr) {
            int ret = parse_print_options(option_name, (iarg+1 < argc) ? argv[iarg+1] : _T(""));
            if (ret != 0) {
                return ret == 1 ? 0 : 1;
            }
        }
    }

    sInputParams Params = { 0 };
    init_qsvp_prm(&Params);

    vector<const TCHAR *> argvCopy(argv, argv + argc);
    argvCopy.push_back(_T(""));

	printf("----------------------------------------->parse_cmd start.\r\n");
    ParseCmdError err;
    int ret = parse_cmd(&Params, argvCopy.data(), (mfxU8)argc, err);
    if (ret >= 1) {
        show_help();
        return 0;
    }
	printf("\r\n----------------------------------------->parse_cmd end.\r\n");

    if (Params.bBenchmark) {
        return run_benchmark(&Params);
    }
    unique_ptr<CQSVPipeline> pPipeline(new CQSVPipeline);
    if (!pPipeline) {
        return MFX_ERR_MEMORY_ALLOC;
    }

    auto sts = pPipeline->Init(&Params);
    if (sts < MFX_ERR_NONE) return 1;

    pPipeline->SetAbortFlagPointer(&g_signal_abort);
    set_signal_handler();

    if (MFX_ERR_NONE != (sts = pPipeline->CheckCurrentVideoParam())) {
        return sts;
    }
	printf("----------------------------------------->loop start.\r\n");
    for (;;) {
        sts = pPipeline->Run();

        if (MFX_ERR_DEVICE_LOST == sts || MFX_ERR_DEVICE_FAILED == sts) {
            pPipeline->PrintMes(RGY_LOG_ERROR, _T("\nERROR: Hardware device was lost or returned an unexpected error. Recovering...\n"));
            sts = pPipeline->ResetDevice();
            if (sts < MFX_ERR_NONE) return sts;

            sts = pPipeline->ResetMFXComponents(&Params);
            if (sts < MFX_ERR_NONE) return sts;
            continue;
        } else {
            if (sts < MFX_ERR_NONE) return 1;
            break;
        }
    }

    pPipeline->Close();
	printf("----------------------------------------->loop end.\r\n");
    pPipeline->PrintMes(RGY_LOG_INFO, _T("\nProcessing finished\n"));
    return sts;
}

int _tmain(int argc, TCHAR *argv[]) {
    int ret = 0;
	printf("----------------------------------------->_tmain start.\r\n");

    if (0 != (ret = run(argc, argv))) {
        rgy_print_stderr(RGY_LOG_ERROR, _T("QSVEncC.exe finished with error!\n"));
    }
    return ret;
}
