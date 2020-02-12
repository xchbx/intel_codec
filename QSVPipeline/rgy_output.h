﻿// -----------------------------------------------------------------------------------------
// QSVEnc/NVEnc by rigaya
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
// ------------------------------------------------------------------------------------------

#pragma once
#ifndef __RGY_OUTPUT_H__
#define __RGY_OUTPUT_H__

#include <memory>
#include <vector>
#include <unordered_map>
#include <mutex>
#include "rgy_osdep.h"
#include "rgy_tchar.h"
#include "rgy_log.h"
#include "rgy_status.h"
#include "qsv_util.h"

using std::unique_ptr;
using std::shared_ptr;

enum OutputType {
    OUT_TYPE_NONE = 0,
    OUT_TYPE_BITSTREAM,
    OUT_TYPE_SURFACE
};

class RGYTimestamp {
private:
    std::unordered_map<int64_t, int64_t> m_duration;
    std::mutex mtx;
    int64_t last_check_pts;
    int64_t offset;
public:
    RGYTimestamp() : m_duration(), mtx(), last_check_pts(-1), offset(0) {};
    ~RGYTimestamp() {};
    void add(int64_t pts, int64_t duration) {
        std::lock_guard<std::mutex> lock(mtx);
        m_duration[pts] = duration;
    }
    int64_t check(int64_t pts) {
        if (last_check_pts < 0 && pts > 0) {
            offset = -pts;
        }
        std::lock_guard<std::mutex> lock(mtx);
        pts += offset;
        auto pos = m_duration.find(pts);
        if (pos == m_duration.end()) {
            auto last_check_pos = m_duration.find(last_check_pts);
            pts = last_check_pos->first + last_check_pos->second / 2;
            auto next_pts = last_check_pos->first + last_check_pos->second;
            last_check_pos->second = pts - last_check_pos->first;
            m_duration[pts] = next_pts - pts;
        }
        last_check_pts = pts;
        return pts;
    }
    int64_t get_and_pop(int64_t pts) {
        std::lock_guard<std::mutex> lock(mtx);
        auto pos = m_duration.find(pts);
        if (pos == m_duration.end()) {
            return -1;
        }
        auto duration = pos->second;
        m_duration.erase(pos);
        return duration;
    }
};

class RGYOutput {
public:
    RGYOutput();
    virtual ~RGYOutput();

    RGY_ERR Init(const TCHAR *strFileName, const VideoInfo *pVideoOutputInfo, const void *prm, shared_ptr<RGYLog> pLog, shared_ptr<EncodeStatus> pEncSatusInfo) {
        Close();
        m_pPrintMes = pLog;
        m_pEncSatusInfo = pEncSatusInfo;
        if (pVideoOutputInfo) {
            memcpy(&m_VideoOutputInfo, pVideoOutputInfo, sizeof(m_VideoOutputInfo));
        }
        return Init(strFileName, pVideoOutputInfo, prm);
    }

    virtual RGY_ERR WriteNextFrame(RGYBitstream *pBitstream) = 0;
    virtual RGY_ERR WriteNextFrame(RGYFrame *pSurface) = 0;
    virtual void Close();

    virtual bool outputStdout() {
        return m_bOutputIsStdout;
    }

    virtual OutputType getOutType() {
        return m_OutType;
    }
    virtual void WaitFin() {
        return;
    }

    const TCHAR *GetOutputMessage() {
        const TCHAR *mes = m_strOutputInfo.c_str();
        return (mes) ? mes : _T("");
    }
    void AddMessage(int log_level, const tstring& str) {
        if (m_pPrintMes == nullptr || log_level < m_pPrintMes->getLogLevel()) {
            return;
        }
        auto lines = split(str, _T("\n"));
        for (const auto& line : lines) {
            if (line[0] != _T('\0')) {
                m_pPrintMes->write(log_level, (m_strWriterName + _T(": ") + line + _T("\n")).c_str());
            }
        }
    }
    void AddMessage(int log_level, const TCHAR *format, ... ) {
        if (m_pPrintMes == nullptr || log_level < m_pPrintMes->getLogLevel()) {
            return;
        }

        va_list args;
        va_start(args, format);
        int len = _vsctprintf(format, args) + 1; // _vscprintf doesn't count terminating '\0'
        tstring buffer;
        buffer.resize(len, _T('\0'));
        _vstprintf_s(&buffer[0], len, format, args);
        va_end(args);
        AddMessage(log_level, buffer);
    }
protected:
    virtual RGY_ERR Init(const TCHAR *strFileName, const VideoInfo *pOutputInfo, const void *prm) = 0;

    shared_ptr<EncodeStatus> m_pEncSatusInfo;
    unique_ptr<FILE, fp_deleter>  m_fDest;
    bool        m_bOutputIsStdout;
    bool        m_bInited;
    bool        m_bNoOutput;
    OutputType  m_OutType;
    bool        m_bSourceHWMem;
    bool        m_bY4mHeaderWritten;
    tstring     m_strWriterName;
    tstring     m_strOutputInfo;
    VideoInfo   m_VideoOutputInfo;
    shared_ptr<RGYLog> m_pPrintMes;  //ログ出力
    unique_ptr<char, malloc_deleter>            m_pOutputBuffer;
    unique_ptr<uint8_t, aligned_malloc_deleter> m_pReadBuffer;
    unique_ptr<uint8_t, aligned_malloc_deleter> m_pUVBuffer;
};

struct RGYOutputRawPrm {
    bool bBenchmark;
    int nBufSizeMB;
    RGY_CODEC codecId;
    vector<uint8_t> seiNal;
};

class RGYOutputRaw : public RGYOutput {
public:

    RGYOutputRaw();
    virtual ~RGYOutputRaw();

    virtual RGY_ERR WriteNextFrame(RGYBitstream *pBitstream) override;
    virtual RGY_ERR WriteNextFrame(RGYFrame *pSurface) override;
protected:
    virtual RGY_ERR Init(const TCHAR *strFileName, const VideoInfo *pOutputInfo, const void *prm) override;

    vector<uint8_t> m_seiNal;
};


struct YUVWriterParam {
    bool bY4m;
    MemType memType;
};

class CQSVOutFrame : public RGYOutput {
public:

    CQSVOutFrame();
    virtual ~CQSVOutFrame();

    virtual RGY_ERR WriteNextFrame(RGYBitstream *pBitstream) override;
    virtual RGY_ERR WriteNextFrame(RGYFrame *pSurface) override;
protected:
    virtual RGY_ERR Init(const TCHAR *strFileName, const VideoInfo *pOutputInfo, const void *prm) override;

    bool m_bY4m;
};

#endif //__RGY_OUTPUT_H__
