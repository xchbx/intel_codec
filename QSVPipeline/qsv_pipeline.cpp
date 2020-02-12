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
// ------------------------------------------------------------------------------------------

#include "rgy_tchar.h"
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <sstream>
#include <map>
#include <unordered_map>
#include <algorithm>
#include <cassert>
#include <climits>
#include <deque>
#include <mutex>
#define TTMATH_NOASM
#include "ttmath/ttmath.h"
#include "rgy_osdep.h"
#include "qsv_pipeline.h"
#include "qsv_query.h"
#include "rgy_input.h"
#include "rgy_output.h"
#include "rgy_input_raw.h"
#include "rgy_bitstream.h"
#include "qsv_hw_device.h"
#include "qsv_allocator.h"
#include "qsv_allocator_sys.h"
#if defined(_WIN32) || defined(_WIN64)
#include "api_hook.h"
#endif

#if D3D_SURFACES_SUPPORT
#include "qsv_hw_d3d9.h"
#include "qsv_hw_d3d11.h"

#include "qsv_allocator_d3d9.h"
#include "qsv_allocator_d3d11.h"
#endif

#ifdef LIBVA_SUPPORT
#include "qsv_hw_va.h"
#include "qsv_allocator_va.h"
#endif

#define RGY_ERR_MES(ret, MES)    {if (RGY_ERR_NONE > (ret)) { PrintMes(RGY_LOG_ERROR, _T("%s : %s\n"), MES, get_err_mes(ret)); return err_to_mfx(ret);}}
#define QSV_ERR_MES(sts, MES)    {if (MFX_ERR_NONE > (sts)) { PrintMes(RGY_LOG_ERROR, _T("%s : %s\n"), MES, get_err_mes((int)sts)); return sts;}}
#define CHECK_RANGE_LIST(value, list, name)    { if (CheckParamList((value), (list), (name)) != MFX_ERR_NONE) { return MFX_ERR_INVALID_VIDEO_PARAM; } }

int CQSVPipeline::clamp_param_int(int value, int low, int high, const TCHAR *param_name) {
    auto value_old = value;
    value = clamp(value, low, high);
    if (value != value_old) {
        PrintMes(RGY_LOG_WARN, _T("%s value changed %d -> %d, must be in range of %d-%d\n"), param_name, value_old, value, low, high);
    }
    return value;
}

bool CQSVPipeline::CompareParam(const mfxParamSet& prmIn, const mfxParamSet& prmOut) {
    bool ret = false;
#define COMPARE_INT(member, ignoreIfInput) { \
    if (prmIn.member != prmOut.member) { \
        ret = true;\
        PrintMes(((int64_t)prmIn.member == (int64_t)ignoreIfInput) ? RGY_LOG_DEBUG : RGY_LOG_WARN, _T("%s value changed %d -> %d by driver\n"), _T(#member), (int)prmIn.member, (int)prmOut.member); \
    }}
#define TRI_STATE(x) ((x == 0) ? _T("auto") : ((x == MFX_CODINGOPTION_ON) ? _T("on") : _T("off")))
#define COMPARE_TRI(member, ignoreIfInput) { \
    if (prmIn.member != prmOut.member) { \
        ret = true;\
        PrintMes((prmIn.member == ignoreIfInput) ? RGY_LOG_DEBUG : RGY_LOG_WARN, _T("%s value changed %s -> %s by driver\n"), _T(#member), TRI_STATE(prmIn.member), TRI_STATE(prmOut.member)); \
    }}
#define COMPARE_HEX(member, ignoreIfInput) { \
    if (prmIn.member != prmOut.member) { \
        ret = true;\
        PrintMes((prmIn.member == ignoreIfInput) ? RGY_LOG_DEBUG : RGY_LOG_WARN, _T("%s value changed 0x%x -> 0x%x by driver\n"), _T(#member), (int)prmIn.member, (int)prmOut.member); \
    }}
#define COMPARE_DBL(member, ignoreIfInput) { \
    if (prmIn.member != prmOut.member) { \
        ret = true;\
        PrintMes((prmIn.member == ignoreIfInput) ? RGY_LOG_DEBUG : RGY_LOG_WARN, _T("%s value changed %lf -> %lf by driver\n"), _T(#member), (double)prmIn.member, (double)prmOut.member); \
    }}
#define COMPARE_STR(member, ignoreIfInput, printMethod) { \
    if (prmIn.member != prmOut.member) { \
        ret = true;\
        PrintMes((prmIn.member == ignoreIfInput) ? RGY_LOG_DEBUG : RGY_LOG_WARN, _T("%s value changed %s -> %s by driver\n"), _T(#member), printMethod(prmIn.member), printMethod(prmOut.member)); \
    }}
#define COMPARE_LST(member, ignoreIfInput, list) { \
    if (prmIn.member != prmOut.member) { \
        ret = true;\
        PrintMes((prmIn.member == ignoreIfInput) ? RGY_LOG_DEBUG : RGY_LOG_WARN, _T("%s value changed %s -> %s by driver\n"), _T(#member), get_chr_from_value(list, prmIn.member), get_chr_from_value(list, prmOut.member)); \
    }}
    COMPARE_INT(vidprm.AsyncDepth,             0);
    COMPARE_HEX(vidprm.IOPattern,              0);
    COMPARE_INT(vidprm.mfx.NumThread,          0);
    COMPARE_INT(vidprm.mfx.BRCParamMultiplier, 0);
    COMPARE_INT(vidprm.mfx.LowPower,           0);
    COMPARE_STR(vidprm.mfx.CodecId,            0, CodecIdToStr);
    COMPARE_LST(vidprm.mfx.CodecProfile,       0, get_profile_list(prmIn.vidprm.mfx.CodecId));
    COMPARE_LST(vidprm.mfx.CodecLevel,         0, get_level_list(prmIn.vidprm.mfx.CodecId));
    COMPARE_INT(vidprm.mfx.NumThread,          0);
    COMPARE_INT(vidprm.mfx.TargetUsage,       -1);
    COMPARE_INT(vidprm.mfx.GopPicSize,         0);
    COMPARE_INT(vidprm.mfx.GopRefDist,         0);
    COMPARE_INT(vidprm.mfx.GopOptFlag,         0);
    COMPARE_INT(vidprm.mfx.IdrInterval,        0);
    COMPARE_STR(vidprm.mfx.RateControlMethod,  0, EncmodeToStr);
    if (prmIn.vidprm.mfx.RateControlMethod == MFX_RATECONTROL_CQP) {
        COMPARE_INT(vidprm.mfx.QPI, -1);
        COMPARE_INT(vidprm.mfx.QPP, -1);
        COMPARE_INT(vidprm.mfx.QPB, -1);
    } else if (rc_is_type_lookahead(m_mfxEncParams.mfx.RateControlMethod)) {
        COMPARE_INT(cop2.LookAheadDepth, -1);
        if (check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_8)) {
            COMPARE_LST(cop2.LookAheadDS, 0, list_lookahead_ds);
        }
        if (check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_11)) {
            COMPARE_INT(cop3.WinBRCSize,       0);
            COMPARE_INT(cop3.WinBRCMaxAvgKbps, 0);
        }
        if (MFX_RATECONTROL_LA_ICQ == m_mfxEncParams.mfx.RateControlMethod) {
            COMPARE_INT(vidprm.mfx.ICQQuality, -1);
        }
    } else if (MFX_RATECONTROL_ICQ == m_mfxEncParams.mfx.RateControlMethod) {
        COMPARE_INT(vidprm.mfx.ICQQuality, -1);
    } else {
        COMPARE_INT(vidprm.mfx.TargetKbps, 0);
        if (m_mfxEncParams.mfx.RateControlMethod == MFX_RATECONTROL_AVBR) {
            COMPARE_INT(vidprm.mfx.TargetKbps, 0);
        } else {
            COMPARE_INT(vidprm.mfx.MaxKbps, 0);
            if (m_mfxEncParams.mfx.RateControlMethod == MFX_RATECONTROL_QVBR) {
                COMPARE_INT(cop3.QVBRQuality, -1);
            }
        }
    }
    COMPARE_INT(vidprm.mfx.NumSlice,             0);
    COMPARE_INT(vidprm.mfx.NumRefFrame,          0);
    COMPARE_INT(vidprm.mfx.EncodedOrder,         0);
    COMPARE_INT(vidprm.mfx.ExtendedPicStruct,    0);
    COMPARE_INT(vidprm.mfx.TimeStampCalc,        0);
    if (check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_6)) {
        COMPARE_INT(vidprm.mfx.SliceGroupsPresent, 0);
    }
    if (check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_15)) {
        COMPARE_TRI(vidprm.mfx.LowPower, 0);
    }
    if (check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_16)) {
        COMPARE_INT(vidprm.mfx.MaxDecFrameBuffering, 0);
    }

    COMPARE_TRI(cop.RateDistortionOpt,    0);
    COMPARE_INT(cop.MECostType,           0);
    COMPARE_INT(cop.MESearchType,         0);
    COMPARE_TRI(cop.EndOfSequence,        0);
    COMPARE_TRI(cop.FramePicture,         0);
    COMPARE_TRI(cop.CAVLC,                0);
    COMPARE_TRI(cop.ViewOutput,           0);
    COMPARE_TRI(cop.VuiVclHrdParameters,  0);
    COMPARE_TRI(cop.RefPicListReordering, 0);
    COMPARE_TRI(cop.ResetRefList,         0);
    COMPARE_INT(cop.MaxDecFrameBuffering, 0);
    COMPARE_TRI(cop.AUDelimiter,          0);
    COMPARE_TRI(cop.EndOfStream,          0);
    COMPARE_TRI(cop.PicTimingSEI,         0);
    if (check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_3)) {
        COMPARE_TRI(cop.RefPicMarkRep,       0);
        COMPARE_TRI(cop.FieldOutput,         0);
        COMPARE_TRI(cop.NalHrdConformance,   0);
        COMPARE_TRI(cop.SingleSeiNalUnit,    0);
        COMPARE_TRI(cop.VuiNalHrdParameters, 0);
    }
    if (check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_6)) {
        COMPARE_TRI(cop.RecoveryPointSEI, 0);

        COMPARE_INT(cop2.MaxFrameSize,    0);
        COMPARE_INT(cop2.MaxSliceSize,    0);
        COMPARE_TRI(cop2.BitrateLimit,    0);
        COMPARE_TRI(cop2.MBBRC,           0);
        COMPARE_TRI(cop2.ExtBRC,          0);
    }

    if (check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_8)) {
        COMPARE_TRI(cop2.RepeatPPS,           0);
        COMPARE_INT(cop2.BRefType,            0);
        COMPARE_TRI(cop2.AdaptiveI,           0);
        COMPARE_TRI(cop2.AdaptiveB,           0);
        COMPARE_INT(cop2.NumMbPerSlice,       0);
    }
    if (check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_9)) {
        COMPARE_INT(cop2.MaxSliceSize,        0);
        COMPARE_INT(cop2.SkipFrame,           0);
        COMPARE_INT(cop2.MinQPI,              0);
        COMPARE_INT(cop2.MaxQPI,              0);
        COMPARE_INT(cop2.MinQPP,              0);
        COMPARE_INT(cop2.MaxQPP,              0);
        COMPARE_INT(cop2.MinQPB,              0);
        COMPARE_INT(cop2.MaxQPB,              0);
        COMPARE_INT(cop2.FixedFrameRate,      0);
        COMPARE_INT(cop2.DisableDeblockingIdc,0);
    }
    if (check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_10)) {
        COMPARE_INT(cop2.DisableVUI,         0);
        COMPARE_INT(cop2.BufferingPeriodSEI, 0);
    }
    if (check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_11)) {
        COMPARE_TRI(cop2.EnableMAD, 0);
    }
    if (check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_13)) {
        COMPARE_TRI(cop2.UseRawRef, 0);
    }

    if (check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_11)) {
        COMPARE_INT(cop3.NumSliceI,                  0);
        COMPARE_INT(cop3.NumSliceP,                  0);
        COMPARE_INT(cop3.NumSliceB,                  0);
        if (rc_is_type_lookahead(m_mfxEncParams.mfx.RateControlMethod)) {
            COMPARE_INT(cop3.WinBRCMaxAvgKbps,       0);
            COMPARE_INT(cop3.WinBRCSize,             0);
        }
    }
    if (check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_13)) {
        COMPARE_TRI(cop3.EnableMBQP,                 0);
        COMPARE_TRI(cop3.DirectBiasAdjustment,       0);
        COMPARE_TRI(cop3.GlobalMotionBiasAdjustment, 0);
        COMPARE_INT(cop3.MVCostScalingFactor,        0);
    }
    if (check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_16)) {
        COMPARE_INT(cop3.IntRefCycleDist,            0);
        COMPARE_TRI(cop3.MBDisableSkipMap,           0);
        COMPARE_INT(cop3.WeightedPred,               0);
        COMPARE_INT(cop3.WeightedBiPred,             0);
        COMPARE_TRI(cop3.AspectRatioInfoPresent,     0);
        COMPARE_TRI(cop3.OverscanInfoPresent,        0);
        COMPARE_TRI(cop3.OverscanAppropriate,        0);
        COMPARE_TRI(cop3.TimingInfoPresent,          0);
        COMPARE_TRI(cop3.BitstreamRestriction,       0);
        COMPARE_INT(cop3.PRefType,                   0);
    }
    if (check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_17)) {
        COMPARE_TRI(cop3.FadeDetection,              0);
    }
    if (check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_19)) {
        COMPARE_TRI(cop3.LowDelayHrd,                0);
        COMPARE_TRI(cop3.MotionVectorsOverPicBoundaries, 0);
        COMPARE_TRI(cop3.MaxFrameSizeI,      0);
        COMPARE_TRI(cop3.MaxFrameSizeP,      0);
        COMPARE_TRI(cop3.EnableQPOffset,     0);
        COMPARE_TRI(cop3.TransformSkip,      0);
        COMPARE_INT(cop3.QPOffset[0],        0);
        COMPARE_INT(cop3.QPOffset[1],        0);
        COMPARE_INT(cop3.QPOffset[2],        0);
        COMPARE_INT(cop3.NumRefActiveP[0],   0);
        COMPARE_INT(cop3.NumRefActiveP[1],   0);
        COMPARE_INT(cop3.NumRefActiveP[2],   0);
        COMPARE_INT(cop3.NumRefActiveBL0[0], 0);
        COMPARE_INT(cop3.NumRefActiveBL0[1], 0);
        COMPARE_INT(cop3.NumRefActiveBL0[2], 0);
        COMPARE_INT(cop3.NumRefActiveBL1[0], 0);
        COMPARE_INT(cop3.NumRefActiveBL1[1], 0);
        COMPARE_INT(cop3.NumRefActiveBL1[2], 0);
    }
    if (check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_26)) {
        COMPARE_TRI(hevc.SampleAdaptiveOffset,  MFX_SAO_UNKNOWN);
        COMPARE_TRI(hevc.LCUSize, 0);
    }
    return ret;
}

//範囲チェック
mfxStatus CQSVPipeline::CheckParamList(int value, const CX_DESC *list, const char *param_name) {
    for (int i = 0; list[i].desc; i++)
        if (list[i].value == value)
            return MFX_ERR_NONE;
    PrintMes(RGY_LOG_ERROR, _T("%s=%d, is not valid param.\n"), param_name, value);
    return MFX_ERR_INVALID_VIDEO_PARAM;
};

mfxStatus CQSVPipeline::AllocAndInitVppDoNotUse() {
    RGY_MEMSET_ZERO(m_VppDoNotUse);
    m_VppDoNotUse.Header.BufferId = MFX_EXTBUFF_VPP_DONOTUSE;
    m_VppDoNotUse.Header.BufferSz = sizeof(mfxExtVPPDoNotUse);
    m_VppDoNotUse.NumAlg = (mfxU32)m_VppDoNotUseList.size();
    m_VppDoNotUse.AlgList = &m_VppDoNotUseList[0];
    return MFX_ERR_NONE;
}

void CQSVPipeline::FreeVppDoNotUse() {
}

mfxStatus CQSVPipeline::InitMfxDecParams(sInputParams *pInParams) {
    return MFX_ERR_NONE;
}

mfxStatus CQSVPipeline::InitMfxEncParams(sInputParams *pInParams) {
    if (pInParams->CodecId == MFX_CODEC_RAW) {
        PrintMes(RGY_LOG_DEBUG, _T("Raw codec is selected, disable encode.\n"));
        return MFX_ERR_NONE;
    }
    const mfxU32 blocksz = (pInParams->CodecId == MFX_CODEC_HEVC) ? 32 : 16;
    auto print_feature_warnings = [this](int log_level, const TCHAR *feature_name) {
        PrintMes(log_level, _T("%s is not supported on current platform, disabled.\n"), feature_name);
    };

    if (pInParams->CodecId == MFX_CODEC_HEVC) {
        if (MFX_ERR_NONE != m_SessionPlugins->LoadPlugin(MFX_PLUGINTYPE_VIDEO_ENCODE, MFX_PLUGINID_HEVCE_HW, 1)) {
            PrintMes(RGY_LOG_ERROR, _T("Failed to load hw hevc encoder.\n"));
            PrintMes(RGY_LOG_ERROR, _T("hevc encoding is not supported on current platform.\n"));
            return MFX_ERR_UNSUPPORTED;
        }
    } else if (pInParams->CodecId == MFX_CODEC_VP8) {
        if (MFX_ERR_NONE != m_SessionPlugins->LoadPlugin(MFX_PLUGINTYPE_VIDEO_ENCODE, MFX_PLUGINID_VP8E_HW, 1)) {
            PrintMes(RGY_LOG_ERROR, _T("Failed to load hw vp8 encoder.\n"));
            PrintMes(RGY_LOG_ERROR, _T("vp8 encoding is not supported on current platform.\n"));
            return MFX_ERR_UNSUPPORTED;
        }
    } else if (pInParams->CodecId == MFX_CODEC_VP9) {
        if (MFX_ERR_NONE != m_SessionPlugins->LoadPlugin(MFX_PLUGINTYPE_VIDEO_ENCODE, MFX_PLUGINID_VP9E_HW, 1)) {
            PrintMes(RGY_LOG_ERROR, _T("Failed to load hw vp9 encoder.\n"));
            PrintMes(RGY_LOG_ERROR, _T("vp9 encoding is not supported on current platform.\n"));
            return MFX_ERR_UNSUPPORTED;
        }
    }
    //エンコードモードのチェック
    auto availableFeaures = CheckEncodeFeature(m_mfxSession, m_mfxVer, pInParams->nEncMode, pInParams->CodecId);
    PrintMes(RGY_LOG_DEBUG, _T("Detected avaliable features for hw API v%d.%d, %s, %s\n%s\n"),
        m_mfxVer.Major, m_mfxVer.Minor,
        CodecIdToStr(pInParams->CodecId), EncmodeToStr(pInParams->nEncMode), MakeFeatureListStr(availableFeaures).c_str());
    if (!(availableFeaures & ENC_FEATURE_CURRENT_RC)) {
        //このコーデックがサポートされているかどうか確認する
        if (   pInParams->nEncMode == MFX_RATECONTROL_CQP
            || pInParams->nEncMode == MFX_RATECONTROL_VBR
            || pInParams->nEncMode == MFX_RATECONTROL_CBR
            || !(CheckEncodeFeature(m_mfxSession, m_mfxVer, MFX_RATECONTROL_CQP, pInParams->CodecId) & ENC_FEATURE_CURRENT_RC)) {
            PrintMes(RGY_LOG_ERROR, _T("%s encoding is not supported on current platform.\n"), CodecIdToStr(pInParams->CodecId));
            return MFX_ERR_INVALID_VIDEO_PARAM;
        }
        const int rc_error_log_level = (pInParams->nFallback) ? RGY_LOG_WARN : RGY_LOG_ERROR;
        PrintMes(rc_error_log_level, _T("%s mode is not supported on current platform.\n"), EncmodeToStr(pInParams->nEncMode));
        if (MFX_RATECONTROL_LA == pInParams->nEncMode) {
            if (!check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_7)) {
                PrintMes(rc_error_log_level, _T("Lookahead mode is only supported by API v1.7 or later.\n"));
            }
        }
        if (   MFX_RATECONTROL_ICQ    == pInParams->nEncMode
            || MFX_RATECONTROL_LA_ICQ == pInParams->nEncMode
            || MFX_RATECONTROL_VCM    == pInParams->nEncMode) {
            if (!check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_8)) {
                PrintMes(rc_error_log_level, _T("%s mode is only supported by API v1.8 or later.\n"), EncmodeToStr(pInParams->nEncMode));
            }
        }
        if (   MFX_RATECONTROL_LA_EXT == pInParams->nEncMode
            || MFX_RATECONTROL_LA_HRD == pInParams->nEncMode
            || MFX_RATECONTROL_QVBR   == pInParams->nEncMode) {
            if (!check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_11)) {
                PrintMes(rc_error_log_level, _T("%s mode is only supported by API v1.11 or later.\n"), EncmodeToStr(pInParams->nEncMode));
            }
        }
        if (!pInParams->nFallback) {
            return MFX_ERR_INVALID_VIDEO_PARAM;
        }
        //fallback
        const int RC_BITRATE[] = { MFX_RATECONTROL_CBR, MFX_RATECONTROL_VBR, MFX_RATECONTROL_AVBR, MFX_RATECONTROL_VCM, MFX_RATECONTROL_LA, MFX_RATECONTROL_LA_HRD, MFX_RATECONTROL_LA_EXT, MFX_RATECONTROL_QVBR };
        //ビットレート指定モードかどうか
        bool bSelectedRCBitrate = std::find(RC_BITRATE, RC_BITRATE + _countof(RC_BITRATE), pInParams->nEncMode) != (RC_BITRATE + _countof(RC_BITRATE));
        //fallbackの候補リスト、優先度の高い順にセットする
        vector<int> check_rc_list;
        //現在のレート制御モードは使用できないので、それ以外を確認する
        auto check_rc_add = [pInParams, &check_rc_list](int rc_mode) {
            if (pInParams->nEncMode != rc_mode) {
                check_rc_list.push_back(rc_mode);
            }
        };

        //品質指定系の場合、若干補正をかけた値を設定する
        int nAdjustedQP[3] = { QSV_DEFAULT_QPI, QSV_DEFAULT_QPP, QSV_DEFAULT_QPB };
        if (bSelectedRCBitrate) {
            //ビットレートモードなら、QVBR->VBRをチェックする
            check_rc_add(MFX_RATECONTROL_QVBR);
            check_rc_add(MFX_RATECONTROL_VBR);
        } else {
            //固定品質モードなら、ICQ->CQPをチェックする
            check_rc_add(MFX_RATECONTROL_ICQ);
            check_rc_add(MFX_RATECONTROL_CQP);
            //品質指定系の場合、若干補正をかけた値を設定する
            if (pInParams->nEncMode == MFX_RATECONTROL_LA_ICQ) {
                nAdjustedQP[0] = pInParams->nICQQuality - 8;
                nAdjustedQP[1] = pInParams->nICQQuality - 6;
                nAdjustedQP[2] = pInParams->nICQQuality - 3;
            } else if (pInParams->nEncMode == MFX_RATECONTROL_ICQ) {
                nAdjustedQP[0] = pInParams->nICQQuality - 1;
                nAdjustedQP[1] = pInParams->nICQQuality + 1;
                nAdjustedQP[2] = pInParams->nICQQuality + 4;
            } else if (pInParams->nEncMode == MFX_RATECONTROL_CQP) {
                nAdjustedQP[0] = pInParams->nQPI;
                nAdjustedQP[1] = pInParams->nQPP;
                nAdjustedQP[2] = pInParams->nQPB;
            }
        }
        //check_rc_listに設定したfallbackの候補リストをチェックする
        bool bFallbackSuccess = false;
        for (uint32_t i = 0; i < (uint32_t)check_rc_list.size(); i++) {
            auto availRCFeatures = CheckEncodeFeature(m_mfxSession, m_mfxVer, (uint16_t)check_rc_list[i], pInParams->CodecId);
            if (availRCFeatures & ENC_FEATURE_CURRENT_RC) {
                pInParams->nEncMode = (uint16_t)check_rc_list[i];
                if (pInParams->nEncMode == MFX_RATECONTROL_LA_ICQ) {
                    pInParams->nICQQuality = (uint16_t)clamp(nAdjustedQP[1] + 6, 1, 51);
                } else if (pInParams->nEncMode == MFX_RATECONTROL_LA_ICQ) {
                    pInParams->nICQQuality = (uint16_t)clamp(nAdjustedQP[1], 1, 51);
                } else if (pInParams->nEncMode == MFX_RATECONTROL_CQP) {
                    pInParams->nQPI = (uint16_t)clamp(nAdjustedQP[0], 0, 51);
                    pInParams->nQPP = (uint16_t)clamp(nAdjustedQP[1], 0, 51);
                    pInParams->nQPB = (uint16_t)clamp(nAdjustedQP[2], 0, 51);
                }
                bFallbackSuccess = true;
                availableFeaures = availRCFeatures;
                PrintMes(rc_error_log_level, _T("Falling back to %s mode.\n"), EncmodeToStr(pInParams->nEncMode));
                break;
            }
        }
        //なんらかの理由でフォールバックできなかったらエラー終了
        if (!bFallbackSuccess) {
            return MFX_ERR_INVALID_VIDEO_PARAM;
        }
    }
    if (pInParams->nBframes == QSV_BFRAMES_AUTO) {
        pInParams->nBframes = (pInParams->CodecId == MFX_CODEC_HEVC) ? QSV_DEFAULT_HEVC_BFRAMES : QSV_DEFAULT_H264_BFRAMES;
    }
    //その他機能のチェック
    if (pInParams->bAdaptiveI && !(availableFeaures & ENC_FEATURE_ADAPTIVE_I)) {
        PrintMes(RGY_LOG_WARN, _T("Adaptve I-frame insert is not supported on current platform, disabled.\n"));
        pInParams->bAdaptiveI = false;
    }
    if (pInParams->bAdaptiveB && !(availableFeaures & ENC_FEATURE_ADAPTIVE_B)) {
        PrintMes(RGY_LOG_WARN, _T("Adaptve B-frame insert is not supported on current platform, disabled.\n"));
        pInParams->bAdaptiveB = false;
    }
    if (pInParams->bBPyramid && !(availableFeaures & ENC_FEATURE_B_PYRAMID)) {
        print_feature_warnings(RGY_LOG_WARN, _T("B pyramid"));
        pInParams->bBPyramid = false;
    }
    if (pInParams->bCAVLC && !(availableFeaures & ENC_FEATURE_CAVLC)) {
        print_feature_warnings(RGY_LOG_WARN, _T("CAVLC"));
        pInParams->bCAVLC = false;
    }
    if (pInParams->extBRC && !(availableFeaures & ENC_FEATURE_EXT_BRC)) {
        print_feature_warnings(RGY_LOG_WARN, _T("ExtBRC"));
        pInParams->extBRC = false;
    }
    if (pInParams->extBrcAdaptiveLTR && !(availableFeaures & ENC_FEATURE_EXT_BRC_ADAPTIVE_LTR)) {
        print_feature_warnings(RGY_LOG_WARN, _T("AdaptiveLTR"));
        pInParams->extBrcAdaptiveLTR = false;
    }
    if (pInParams->bMBBRC && !(availableFeaures & ENC_FEATURE_MBBRC)) {
        print_feature_warnings(RGY_LOG_WARN, _T("MBBRC"));
        pInParams->bMBBRC = false;
    }
    if (   (MFX_RATECONTROL_LA     == pInParams->nEncMode
         || MFX_RATECONTROL_LA_ICQ == pInParams->nEncMode)
        && pInParams->nLookaheadDS != MFX_LOOKAHEAD_DS_UNKNOWN
        && !(availableFeaures & ENC_FEATURE_LA_DS)) {
        print_feature_warnings(RGY_LOG_WARN, _T("Lookahead qaulity setting"));
        pInParams->nLookaheadDS = MFX_LOOKAHEAD_DS_UNKNOWN;
    }
    if (pInParams->nTrellis != MFX_TRELLIS_UNKNOWN && !(availableFeaures & ENC_FEATURE_TRELLIS)) {
        print_feature_warnings(RGY_LOG_WARN, _T("trellis"));
        pInParams->nTrellis = MFX_TRELLIS_UNKNOWN;
    }
    if (pInParams->bRDO && !(availableFeaures & ENC_FEATURE_RDO)) {
        print_feature_warnings(RGY_LOG_WARN, _T("RDO"));
        pInParams->bRDO = false;
    }
    if ((pInParams->nPicStruct & (MFX_PICSTRUCT_FIELD_TFF | MFX_PICSTRUCT_FIELD_BFF))
        && pInParams->vpp.deinterlace == MFX_DEINTERLACE_NONE
        && !(availableFeaures & ENC_FEATURE_INTERLACE)) {
        PrintMes(RGY_LOG_ERROR, _T("Interlaced encoding is not supported on current rate control mode.\n"));
        return MFX_ERR_INVALID_VIDEO_PARAM;
    }
    if (pInParams->CodecId == MFX_CODEC_AVC
        && (pInParams->nPicStruct & (MFX_PICSTRUCT_FIELD_TFF | MFX_PICSTRUCT_FIELD_BFF))
        && pInParams->vpp.deinterlace == MFX_DEINTERLACE_NONE
        && pInParams->nBframes > 0
        && getCPUGen(&m_mfxSession) == CPU_GEN_HASWELL
        && m_memType == D3D11_MEMORY) {
        PrintMes(RGY_LOG_WARN, _T("H.264 interlaced encoding with B frames on d3d11 mode results fuzzy outputs on Haswell CPUs.\n"));
        PrintMes(RGY_LOG_WARN, _T("B frames will be disabled.\n"));
        pInParams->nBframes = 0;
    }
    //最近のドライバでは問題ない模様
    //if (pInParams->nBframes > 2 && pInParams->CodecId == MFX_CODEC_HEVC) {
    //    PrintMes(RGY_LOG_WARN, _T("HEVC encoding + B-frames > 2 might cause artifacts, please check the output.\n"));
    //}
    if (pInParams->bBPyramid && pInParams->nBframes >= 10 && !(availableFeaures & ENC_FEATURE_B_PYRAMID_MANY_BFRAMES)) {
        PrintMes(RGY_LOG_WARN, _T("B pyramid with too many bframes is not supported on current platform, B pyramid disabled.\n"));
        pInParams->bBPyramid = false;
    }
    if (pInParams->bBPyramid && getCPUGen(&m_mfxSession) < CPU_GEN_HASWELL) {
        PrintMes(RGY_LOG_WARN, _T("B pyramid on IvyBridge generation might cause artifacts, please check your encoded video.\n"));
    }
    if (pInParams->bNoDeblock && !(availableFeaures & ENC_FEATURE_NO_DEBLOCK)) {
        print_feature_warnings(RGY_LOG_WARN, _T("No deblock"));
        pInParams->bNoDeblock = false;
    }
    if (pInParams->bIntraRefresh && !(availableFeaures & ENC_FEATURE_INTRA_REFRESH)) {
        print_feature_warnings(RGY_LOG_WARN, _T("Intra Refresh"));
        pInParams->bIntraRefresh = false;
    }
    if (0 != (pInParams->nQPMin[0] | pInParams->nQPMin[1] | pInParams->nQPMin[2]
            | pInParams->nQPMax[0] | pInParams->nQPMax[1] | pInParams->nQPMax[2]) && !(availableFeaures & ENC_FEATURE_QP_MINMAX)) {
        print_feature_warnings(RGY_LOG_WARN, _T("Min/Max QP"));
        memset(pInParams->nQPMin, 0, sizeof(pInParams->nQPMin));
        memset(pInParams->nQPMax, 0, sizeof(pInParams->nQPMax));
    }
    if (0 != pInParams->nWinBRCSize) {
        if (!(availableFeaures & ENC_FEATURE_WINBRC)) {
            print_feature_warnings(RGY_LOG_WARN, _T("WinBRC"));
            pInParams->nWinBRCSize = 0;
        } else if (0 == pInParams->nMaxBitrate) {
            print_feature_warnings(RGY_LOG_WARN, _T("Min/Max QP"));
            PrintMes(RGY_LOG_WARN, _T("WinBRC requires Max bitrate to be set, disabled.\n"));
            pInParams->nWinBRCSize = 0;
        }
    }
    if (pInParams->bDirectBiasAdjust && !(availableFeaures & ENC_FEATURE_DIRECT_BIAS_ADJUST)) {
        print_feature_warnings(RGY_LOG_WARN, _T("Direct Bias Adjust"));
        pInParams->bDirectBiasAdjust = 0;
    }
    if (pInParams->bGlobalMotionAdjust && !(availableFeaures & ENC_FEATURE_GLOBAL_MOTION_ADJUST)) {
        print_feature_warnings(RGY_LOG_WARN, _T("MV Cost Scaling"));
        pInParams->bGlobalMotionAdjust = 0;
        pInParams->nMVCostScaling = 0;
    }
    if (pInParams->bUseFixedFunc && !(availableFeaures & ENC_FEATURE_FIXED_FUNC)) {
        print_feature_warnings(RGY_LOG_WARN, _T("Fixed Func"));
        pInParams->bUseFixedFunc = 0;
    }
    if (pInParams->nWeightP && !(availableFeaures & ENC_FEATURE_WEIGHT_P)) {
        if (pInParams->nWeightP == MFX_CODINGOPTION_ON) {
            print_feature_warnings(RGY_LOG_WARN, _T("WeightP"));
        }
        pInParams->nWeightP = 0;
    }
    if (pInParams->nWeightB && !(availableFeaures & ENC_FEATURE_WEIGHT_B)) {
        if (pInParams->nWeightB == MFX_CODINGOPTION_ON) {
            print_feature_warnings(RGY_LOG_WARN, _T("WeightB"));
        }
        pInParams->nWeightB = 0;
    }
#if !ENABLE_FADE_DETECT
    if (pInParams->nFadeDetect == MFX_CODINGOPTION_ON) {
        PrintMes(RGY_LOG_WARN, _T("fade-detect will be disabled due to instability.\n"));
        pInParams->nFadeDetect = MFX_CODINGOPTION_UNKNOWN;
    }
#endif
    if (pInParams->nFadeDetect != MFX_CODINGOPTION_UNKNOWN && !(availableFeaures & ENC_FEATURE_FADE_DETECT)) {
        if (pInParams->nFadeDetect == MFX_CODINGOPTION_ON) {
            print_feature_warnings(RGY_LOG_WARN, _T("FadeDetect"));
        }
        pInParams->nFadeDetect = MFX_CODINGOPTION_UNKNOWN;
    }
    if (pInParams->CodecId == MFX_CODEC_HEVC) {
        if (pInParams->hevc_ctu > 0 && !(availableFeaures & ENC_FEATURE_HEVC_CTU)) {
            print_feature_warnings(RGY_LOG_WARN, _T("HEVC CTU"));
            pInParams->hevc_ctu = 0;
        }
        if (pInParams->hevc_sao != MFX_SAO_UNKNOWN && !(availableFeaures & ENC_FEATURE_HEVC_SAO)) {
            print_feature_warnings(RGY_LOG_WARN, _T("HEVC SAO"));
            pInParams->hevc_sao = MFX_SAO_UNKNOWN;
        }
        if (pInParams->hevc_tskip != MFX_CODINGOPTION_UNKNOWN && !(availableFeaures & ENC_FEATURE_HEVC_TSKIP)) {
            print_feature_warnings(RGY_LOG_WARN, _T("HEVC tskip"));
            pInParams->hevc_tskip = MFX_CODINGOPTION_UNKNOWN;
        }
    }
    bool bQPOffsetUsed = false;
    std::for_each(pInParams->pQPOffset, pInParams->pQPOffset + _countof(pInParams->pQPOffset), [&bQPOffsetUsed](decltype(pInParams->pQPOffset[0]) v){ bQPOffsetUsed |= (v != 0); });
    if (bQPOffsetUsed && !(availableFeaures & ENC_FEATURE_PYRAMID_QP_OFFSET)) {
        print_feature_warnings(RGY_LOG_WARN, _T("QPOffset"));
        memset(pInParams->pQPOffset, 0, sizeof(pInParams->pQPOffset));
        bQPOffsetUsed = false;
    }

    if (!(availableFeaures & ENC_FEATURE_VUI_INFO)) {
        if (pInParams->bFullrange) {
            print_feature_warnings(RGY_LOG_WARN, _T("fullrange"));
            pInParams->bFullrange = false;
        }
        if (pInParams->Transfer) {
            print_feature_warnings(RGY_LOG_WARN, _T("transfer"));
            pInParams->Transfer = (mfxU16)list_transfer[0].value;
        }
        if (pInParams->VideoFormat) {
            print_feature_warnings(RGY_LOG_WARN, _T("videoformat"));
            pInParams->VideoFormat = (mfxU16)list_videoformat[0].value;
        }
        if (pInParams->ColorMatrix) {
            print_feature_warnings(RGY_LOG_WARN, _T("colormatrix"));
            pInParams->ColorMatrix = (mfxU16)list_colormatrix[0].value;
        }
        if (pInParams->ColorPrim) {
            print_feature_warnings(RGY_LOG_WARN, _T("colorprim"));
            pInParams->ColorPrim = (mfxU16)list_colorprim[0].value;
        }
    }

    //Intra Refereshが指定された場合は、GOP関連の設定を自動的に上書き
    if (pInParams->bIntraRefresh) {
        pInParams->bforceGOPSettings = true;
    }
    //profileを守るための調整
    if (pInParams->CodecProfile == MFX_PROFILE_AVC_BASELINE) {
        pInParams->nBframes = 0;
        pInParams->bCAVLC = true;
    }
    if (pInParams->bCAVLC) {
        pInParams->bRDO = false;
    }

    CHECK_RANGE_LIST(pInParams->CodecId,      list_codec,   "codec");
    CHECK_RANGE_LIST(pInParams->CodecLevel,   get_level_list(pInParams->CodecId),   "level");
    CHECK_RANGE_LIST(pInParams->CodecProfile, get_profile_list(pInParams->CodecId), "profile");
    CHECK_RANGE_LIST(pInParams->nEncMode,     list_rc_mode, "rc mode");

    //設定開始
    m_mfxEncParams.mfx.CodecId                 = pInParams->CodecId;
    m_mfxEncParams.mfx.RateControlMethod       = pInParams->nEncMode;
    if (MFX_RATECONTROL_CQP == m_mfxEncParams.mfx.RateControlMethod) {
        //CQP
        m_mfxEncParams.mfx.QPI             = (mfxU16)clamp_param_int(pInParams->nQPI, 0, 51, _T("qp-i"));
        m_mfxEncParams.mfx.QPP             = (mfxU16)clamp_param_int(pInParams->nQPP, 0, 51, _T("qp-p"));
        m_mfxEncParams.mfx.QPB             = (mfxU16)clamp_param_int(pInParams->nQPB, 0, 51, _T("qp-b"));
    } else if (MFX_RATECONTROL_ICQ    == m_mfxEncParams.mfx.RateControlMethod
            || MFX_RATECONTROL_LA_ICQ == m_mfxEncParams.mfx.RateControlMethod) {
        m_mfxEncParams.mfx.ICQQuality      = (mfxU16)clamp_param_int(pInParams->nICQQuality, 1, 51, _T("icq"));
        m_mfxEncParams.mfx.MaxKbps         = 0;
    } else {
        auto maxBitrate = (std::max)((std::max)(pInParams->nBitRate, pInParams->nMaxBitrate),
            pInParams->VBVBufsize / 8 /*これはbyte単位の指定*/);
        if (maxBitrate > USHRT_MAX) {
            m_mfxEncParams.mfx.BRCParamMultiplier = (mfxU16)(maxBitrate / USHRT_MAX) + 1;
            pInParams->nBitRate    /= m_mfxEncParams.mfx.BRCParamMultiplier;
            pInParams->nMaxBitrate /= m_mfxEncParams.mfx.BRCParamMultiplier;
            pInParams->VBVBufsize  /= m_mfxEncParams.mfx.BRCParamMultiplier;
        }
        m_mfxEncParams.mfx.TargetKbps      = (mfxU16)pInParams->nBitRate; // in kbps
        if (m_mfxEncParams.mfx.RateControlMethod == MFX_RATECONTROL_AVBR) {
            //AVBR
            //m_mfxEncParams.mfx.Accuracy        = pInParams->nAVBRAccuarcy;
            m_mfxEncParams.mfx.Accuracy        = 500;
            m_mfxEncParams.mfx.Convergence     = pInParams->nAVBRConvergence;
        } else {
            //CBR, VBR
            m_mfxEncParams.mfx.MaxKbps         = (mfxU16)pInParams->nMaxBitrate;
            m_mfxEncParams.mfx.BufferSizeInKB  = (mfxU16)pInParams->VBVBufsize / 8; //これはbyte単位の指定
            m_mfxEncParams.mfx.InitialDelayInKB = m_mfxEncParams.mfx.BufferSizeInKB / 2;
        }
    }
    if (check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_15)) {
        m_mfxEncParams.mfx.LowPower = (mfxU16)((pInParams->bUseFixedFunc) ? MFX_CODINGOPTION_ON : MFX_CODINGOPTION_OFF);
    }
    m_mfxEncParams.mfx.TargetUsage             = (mfxU16)clamp_param_int(pInParams->nTargetUsage, MFX_TARGETUSAGE_BEST_QUALITY, MFX_TARGETUSAGE_BEST_SPEED, _T("quality")); // trade-off between quality and speed

    mfxU32 OutputFPSRate = pInParams->nFPSRate;
    mfxU32 OutputFPSScale = pInParams->nFPSScale;
    if ((pInParams->nPicStruct & (MFX_PICSTRUCT_FIELD_TFF | MFX_PICSTRUCT_FIELD_BFF))) {
        switch (pInParams->vpp.deinterlace) {
        case MFX_DEINTERLACE_IT:
        case MFX_DEINTERLACE_IT_MANUAL:
            OutputFPSRate = OutputFPSRate * 4;
            OutputFPSScale = OutputFPSScale * 5;
            break;
        case MFX_DEINTERLACE_BOB:
        case MFX_DEINTERLACE_AUTO_DOUBLE:
            OutputFPSRate = OutputFPSRate * 2;
            break;
        default:
            break;
        }
    } else {
        switch (pInParams->vpp.fpsConversion) {
        case FPS_CONVERT_MUL2:
            OutputFPSRate = OutputFPSRate * 2;
            break;
        case FPS_CONVERT_MUL2_5:
            OutputFPSRate = OutputFPSRate * 5 / 2;
            break;
        default:
            break;
        }
    }
    auto gcd = rgy_gcd(OutputFPSRate, OutputFPSScale);
    OutputFPSRate /= gcd;
    OutputFPSScale /= gcd;
    PrintMes(RGY_LOG_DEBUG, _T("InitMfxEncParams: Output FPS %d/%d\n"), OutputFPSRate, OutputFPSScale);
    if (pInParams->nGOPLength == 0) {
        pInParams->nGOPLength = (mfxU16)((OutputFPSRate + OutputFPSScale - 1) / OutputFPSScale) * 10;
        PrintMes(RGY_LOG_DEBUG, _T("InitMfxEncParams: Auto GOP Length: %d\n"), pInParams->nGOPLength);
    }
    m_mfxEncParams.mfx.FrameInfo.FrameRateExtN = OutputFPSRate;
    m_mfxEncParams.mfx.FrameInfo.FrameRateExtD = OutputFPSScale;
    m_mfxEncParams.mfx.EncodedOrder            = 0;
    m_mfxEncParams.mfx.NumSlice                = pInParams->nSlices;

    m_mfxEncParams.mfx.NumRefFrame             = (mfxU16)clamp_param_int(pInParams->nRef, 0, 16, _T("ref"));
    m_mfxEncParams.mfx.CodecLevel              = pInParams->CodecLevel;
    m_mfxEncParams.mfx.CodecProfile            = pInParams->CodecProfile;
    m_mfxEncParams.mfx.GopOptFlag              = 0;
    m_mfxEncParams.mfx.GopOptFlag             |= (!pInParams->bopenGOP) ? MFX_GOP_CLOSED : 0x00;
    m_mfxEncParams.mfx.IdrInterval             = (!pInParams->bopenGOP) ? 0 : (mfxU16)((OutputFPSRate + OutputFPSScale - 1) / OutputFPSScale) * 20 / pInParams->nGOPLength;
    //MFX_GOP_STRICTにより、インタレ保持時にフレームが壊れる場合があるため、無効とする
    //m_mfxEncParams.mfx.GopOptFlag             |= (pInParams->bforceGOPSettings) ? MFX_GOP_STRICT : NULL;

    m_mfxEncParams.mfx.GopPicSize              = (pInParams->bIntraRefresh) ? 0 : pInParams->nGOPLength;
    m_mfxEncParams.mfx.GopRefDist              = (mfxU16)(clamp_param_int(pInParams->nBframes, -1, 16, _T("bframes")) + 1);

    // specify memory type
    m_mfxEncParams.IOPattern = (mfxU16)((pInParams->memType != SYSTEM_MEMORY) ? MFX_IOPATTERN_IN_VIDEO_MEMORY : MFX_IOPATTERN_IN_SYSTEM_MEMORY);

    // frame info parameters
    m_mfxEncParams.mfx.FrameInfo.ChromaFormat = MFX_CHROMAFORMAT_YUV420;
    m_mfxEncParams.mfx.FrameInfo.PicStruct    = (pInParams->vpp.deinterlace) ? MFX_PICSTRUCT_PROGRESSIVE : pInParams->nPicStruct;

    // set sar info
    mfxI32 m_iSAR[2] = { pInParams->nPAR[0], pInParams->nPAR[1] };
    adjust_sar(&m_iSAR[0], &m_iSAR[1], pInParams->nDstWidth, pInParams->nDstHeight);
    m_mfxEncParams.mfx.FrameInfo.AspectRatioW = (mfxU16)m_iSAR[0];
    m_mfxEncParams.mfx.FrameInfo.AspectRatioH = (mfxU16)m_iSAR[1];

    RGY_MEMSET_ZERO(m_CodingOption);
    m_CodingOption.Header.BufferId = MFX_EXTBUFF_CODING_OPTION;
    m_CodingOption.Header.BufferSz = sizeof(mfxExtCodingOption);
    //if (!pInParams->bUseHWLib) {
    //    //swライブラリ使用時のみ
    //    m_CodingOption.InterPredBlockSize = pInParams->nInterPred;
    //    m_CodingOption.IntraPredBlockSize = pInParams->nIntraPred;
    //    m_CodingOption.MVSearchWindow     = pInParams->MVSearchWindow;
    //    m_CodingOption.MVPrecision        = pInParams->nMVPrecision;
    //}
    //if (!pInParams->bUseHWLib || pInParams->CodecProfile == MFX_PROFILE_AVC_BASELINE) {
    //    //swライブラリ使用時かbaselineを指定した時
    //    m_CodingOption.RateDistortionOpt  = (mfxU16)((pInParams->bRDO) ? MFX_CODINGOPTION_ON : MFX_CODINGOPTION_UNKNOWN);
    //    m_CodingOption.CAVLC              = (mfxU16)((pInParams->bCAVLC) ? MFX_CODINGOPTION_ON : MFX_CODINGOPTION_UNKNOWN);
    //}
    //m_CodingOption.FramePicture = MFX_CODINGOPTION_ON;
    //m_CodingOption.FieldOutput = MFX_CODINGOPTION_ON;
    //m_CodingOption.VuiVclHrdParameters = MFX_CODINGOPTION_ON;
    //m_CodingOption.VuiNalHrdParameters = MFX_CODINGOPTION_ON;
    m_CodingOption.AUDelimiter = (mfxU16)((pInParams->bOutputAud) ? MFX_CODINGOPTION_ON : MFX_CODINGOPTION_OFF);
    m_CodingOption.PicTimingSEI = (mfxU16)((pInParams->bOutputPicStruct) ? MFX_CODINGOPTION_ON : MFX_CODINGOPTION_OFF);
    //m_CodingOption.SingleSeiNalUnit = MFX_CODINGOPTION_OFF;

    //API v1.6の機能
    if (check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_6)) {
        INIT_MFX_EXT_BUFFER(m_CodingOption2, MFX_EXTBUFF_CODING_OPTION2);
        if (check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_8)) {
            m_CodingOption2.AdaptiveI   = (mfxU16)((pInParams->bAdaptiveI) ? MFX_CODINGOPTION_ON : MFX_CODINGOPTION_UNKNOWN);
            m_CodingOption2.AdaptiveB   = (mfxU16)((pInParams->bAdaptiveB) ? MFX_CODINGOPTION_ON : MFX_CODINGOPTION_UNKNOWN);
            m_CodingOption2.BRefType    = (mfxU16)((pInParams->bBPyramid)  ? MFX_B_REF_PYRAMID   : MFX_B_REF_OFF);

            CHECK_RANGE_LIST(pInParams->nLookaheadDS, list_lookahead_ds, "la-quality");
            m_CodingOption2.LookAheadDS = pInParams->nLookaheadDS;
        }
        if (check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_7)) {
            m_CodingOption2.LookAheadDepth = (pInParams->nLookaheadDepth == 0) ? pInParams->nLookaheadDepth : (mfxU16)clamp_param_int(pInParams->nLookaheadDepth, QSV_LOOKAHEAD_DEPTH_MIN, QSV_LOOKAHEAD_DEPTH_MAX, _T("la-depth"));

            CHECK_RANGE_LIST(pInParams->nTrellis, list_avc_trellis_for_options, "trellis");
            m_CodingOption2.Trellis = pInParams->nTrellis;
        }
        if (pInParams->bMBBRC) {
            m_CodingOption2.MBBRC = MFX_CODINGOPTION_ON;
        }

        if (check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_26)
            && pInParams->extBrcAdaptiveLTR) {
            m_CodingOption2.BitrateLimit = MFX_CODINGOPTION_OFF;
        }
        //if (pInParams->bExtBRC) {
        //    m_CodingOption2.ExtBRC = MFX_CODINGOPTION_ON;
        //}
        if (pInParams->bIntraRefresh) {
            m_CodingOption2.IntRefType = 1;
            m_CodingOption2.IntRefCycleSize = (pInParams->nGOPLength >= 2) ? pInParams->nGOPLength : (mfxU16)((OutputFPSRate + OutputFPSScale - 1) / OutputFPSScale) * 10;
        }
        if (pInParams->bNoDeblock) {
            m_CodingOption2.DisableDeblockingIdc = MFX_CODINGOPTION_ON;
        }
        for (int i = 0; i < 3; i++) {
            pInParams->nQPMin[i] = (mfxU8)clamp_param_int(pInParams->nQPMin[i], 0, 51, _T("qp min"));
            pInParams->nQPMax[i] = (mfxU8)clamp_param_int(pInParams->nQPMax[i], 0, 51, _T("qp max"));
            mfxU8 qpMin = (std::min)(pInParams->nQPMin[i], pInParams->nQPMax[i]);
            mfxU8 qpMax = (std::max)(pInParams->nQPMin[i], pInParams->nQPMax[i]);
            pInParams->nQPMin[i] = (0 == pInParams->nQPMin[i]) ? 0 : qpMin;
            pInParams->nQPMax[i] = (0 == pInParams->nQPMax[i]) ? 0 : qpMax;
        }
        m_CodingOption2.MaxQPI = pInParams->nQPMax[0];
        m_CodingOption2.MaxQPP = pInParams->nQPMax[1];
        m_CodingOption2.MaxQPB = pInParams->nQPMax[2];
        m_CodingOption2.MinQPI = pInParams->nQPMin[0];
        m_CodingOption2.MinQPP = pInParams->nQPMin[1];
        m_CodingOption2.MinQPB = pInParams->nQPMin[2];
        m_EncExtParams.push_back((mfxExtBuffer *)&m_CodingOption2);
    }

    if (check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_8)) {
        if (m_mfxEncParams.mfx.CodecId == MFX_CODEC_HEVC) {
            if (pInParams->hevc_tier != 0) {
                m_mfxEncParams.mfx.CodecLevel |= (mfxU16)pInParams->hevc_tier;
            }
        }
    }

    //API v1.11の機能
    if (check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_11)) {
        INIT_MFX_EXT_BUFFER(m_CodingOption3, MFX_EXTBUFF_CODING_OPTION3);
        if (MFX_RATECONTROL_QVBR == m_mfxEncParams.mfx.RateControlMethod) {
            m_CodingOption3.QVBRQuality = (mfxU16)clamp_param_int(pInParams->nQVBRQuality, 1, 51, _T("qvbr-q"));
        }
        if (0 != pInParams->nMaxBitrate) {
            m_CodingOption3.WinBRCSize = (0 != pInParams->nWinBRCSize) ? pInParams->nWinBRCSize : (mfxU16)((OutputFPSRate + OutputFPSScale - 1) / OutputFPSScale);
            m_CodingOption3.WinBRCMaxAvgKbps = (mfxU16)pInParams->nMaxBitrate;
        }

        //API v1.13の機能
        if (check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_13)) {
            m_CodingOption3.DirectBiasAdjustment       = (mfxU16)((pInParams->bDirectBiasAdjust)   ? MFX_CODINGOPTION_ON : MFX_CODINGOPTION_OFF);
            m_CodingOption3.GlobalMotionBiasAdjustment = (mfxU16)((pInParams->bGlobalMotionAdjust) ? MFX_CODINGOPTION_ON : MFX_CODINGOPTION_OFF);
            if (pInParams->bGlobalMotionAdjust) {
                CHECK_RANGE_LIST(pInParams->nMVCostScaling, list_mv_cost_scaling, "mv-scaling");
                m_CodingOption3.MVCostScalingFactor    = pInParams->nMVCostScaling;
            }
        }
        if (check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_16)) {
            m_CodingOption3.WeightedBiPred = pInParams->nWeightB;
            m_CodingOption3.WeightedPred   = pInParams->nWeightP;
        }
        if (check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_17)) {
            m_CodingOption3.FadeDetection = check_coding_option(pInParams->nFadeDetect);
        }
        if (check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_19)) {
            if (bQPOffsetUsed) {
                m_CodingOption3.EnableQPOffset = MFX_CODINGOPTION_ON;
                memcpy(m_CodingOption3.QPOffset, pInParams->pQPOffset, sizeof(pInParams->pQPOffset));
            }
            m_CodingOption3.FadeDetection = check_coding_option(pInParams->nFadeDetect);
        }
        if (check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_23)) {
            m_CodingOption3.RepartitionCheckEnable = pInParams->nRepartitionCheck;
        }
        if (check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_26)) {
            m_CodingOption3.ExtBrcAdaptiveLTR = (mfxU16)(pInParams->extBrcAdaptiveLTR ? MFX_CODINGOPTION_ON : MFX_CODINGOPTION_UNKNOWN);
        }
        m_EncExtParams.push_back((mfxExtBuffer *)&m_CodingOption3);
    }

    //Bluray互換出力
    if (pInParams->nBluray) {
        if (   m_mfxEncParams.mfx.RateControlMethod != MFX_RATECONTROL_CBR
            && m_mfxEncParams.mfx.RateControlMethod != MFX_RATECONTROL_VBR
            && m_mfxEncParams.mfx.RateControlMethod != MFX_RATECONTROL_LA
            && m_mfxEncParams.mfx.RateControlMethod != MFX_RATECONTROL_LA_HRD) {
                if (pInParams->nBluray == 1) {
                    PrintMes(RGY_LOG_ERROR, _T("")
                        _T("Current encode mode (%s) is not preferred for Bluray encoding,\n")
                        _T("since it cannot set Max Bitrate.\n")
                        _T("Please consider using Lookahead/VBR/CBR mode for Bluray encoding.\n"), EncmodeToStr(m_mfxEncParams.mfx.RateControlMethod));
                    return MFX_ERR_INCOMPATIBLE_VIDEO_PARAM;
                } else {
                    //pInParams->nBluray == 2 -> force Bluray
                    PrintMes(RGY_LOG_WARN, _T("")
                        _T("Current encode mode (%s) is not preferred for Bluray encoding,\n")
                        _T("since it cannot set Max Bitrate.\n")
                        _T("This output might not be able to be played on a Bluray Player.\n")
                        _T("Please consider using Lookahead/VBR/CBR mode for Bluray encoding.\n"), EncmodeToStr(m_mfxEncParams.mfx.RateControlMethod));
                }
        }
        if (   m_mfxEncParams.mfx.RateControlMethod == MFX_RATECONTROL_CBR
            || m_mfxEncParams.mfx.RateControlMethod == MFX_RATECONTROL_VBR
            || m_mfxEncParams.mfx.RateControlMethod == MFX_RATECONTROL_LA
            || m_mfxEncParams.mfx.RateControlMethod == MFX_RATECONTROL_LA_HRD) {
                m_mfxEncParams.mfx.MaxKbps    = (std::min)(m_mfxEncParams.mfx.MaxKbps, (uint16_t)40000);
                m_mfxEncParams.mfx.TargetKbps = (std::min)(m_mfxEncParams.mfx.TargetKbps, m_mfxEncParams.mfx.MaxKbps);
                if (m_mfxEncParams.mfx.BufferSizeInKB == 0) {
                    m_mfxEncParams.mfx.BufferSizeInKB = m_mfxEncParams.mfx.MaxKbps / 8;
                }
                if (m_mfxEncParams.mfx.InitialDelayInKB == 0) {
                    m_mfxEncParams.mfx.InitialDelayInKB = m_mfxEncParams.mfx.BufferSizeInKB / 2;
                }
        } else {
            m_mfxEncParams.mfx.BufferSizeInKB = 25000 / 8;
        }
        m_mfxEncParams.mfx.CodecLevel = (m_mfxEncParams.mfx.CodecLevel == 0) ? MFX_LEVEL_AVC_41 : ((std::min)(m_mfxEncParams.mfx.CodecLevel, (uint16_t)MFX_LEVEL_AVC_41));
        m_mfxEncParams.mfx.NumSlice   = (std::max)(m_mfxEncParams.mfx.NumSlice, (uint16_t)4);
        m_mfxEncParams.mfx.GopOptFlag &= (~MFX_GOP_STRICT);
        m_mfxEncParams.mfx.GopRefDist = (std::min)(m_mfxEncParams.mfx.GopRefDist, (uint16_t)(3+1));
        m_mfxEncParams.mfx.GopPicSize = (int)((std::min)(m_mfxEncParams.mfx.GopPicSize, (uint16_t)30) / m_mfxEncParams.mfx.GopRefDist) * m_mfxEncParams.mfx.GopRefDist;
        m_mfxEncParams.mfx.NumRefFrame = (std::min)(m_mfxEncParams.mfx.NumRefFrame, (uint16_t)6);
        m_CodingOption.MaxDecFrameBuffering = m_mfxEncParams.mfx.NumRefFrame;
        m_CodingOption.VuiNalHrdParameters = MFX_CODINGOPTION_ON;
        m_CodingOption.VuiVclHrdParameters = MFX_CODINGOPTION_ON;
        m_CodingOption.AUDelimiter  = MFX_CODINGOPTION_ON;
        m_CodingOption.PicTimingSEI = MFX_CODINGOPTION_ON;
        m_CodingOption.ResetRefList = MFX_CODINGOPTION_ON;
        //m_CodingOption.EndOfSequence = MFX_CODINGOPTION_ON; //hwモードでは効果なし 0x00, 0x00, 0x01, 0x0a
        //m_CodingOption.EndOfStream   = MFX_CODINGOPTION_ON; //hwモードでは効果なし 0x00, 0x00, 0x01, 0x0b
        PrintMes(RGY_LOG_DEBUG, _T("InitMfxEncParams: Adjusted param for Bluray encoding.\n"));
    }

    m_EncExtParams.push_back((mfxExtBuffer *)&m_CodingOption);

    //m_mfxEncParams.mfx.TimeStampCalc = (mfxU16)((pInParams->vpp.nDeinterlace == MFX_DEINTERLACE_IT) ? MFX_TIMESTAMPCALC_TELECINE : MFX_TIMESTAMPCALC_UNKNOWN);
    //m_mfxEncParams.mfx.ExtendedPicStruct = pInParams->nPicStruct;

    if (check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_3) &&
        (pInParams->VideoFormat != list_videoformat[0].value ||
         pInParams->ColorPrim   != list_colorprim[0].value ||
         pInParams->Transfer    != list_transfer[0].value ||
         pInParams->ColorMatrix != list_colormatrix[0].value ||
         pInParams->bFullrange
        ) ) {
#define GET_COLOR_PRM(v, list) (mfxU16)((v == COLOR_VALUE_AUTO) ? ((pInParams->nDstHeight >= HD_HEIGHT_THRESHOLD) ? list[HD_INDEX].value : list[SD_INDEX].value) : v)
            //色設定 (for API v1.3)
            CHECK_RANGE_LIST(pInParams->VideoFormat,    list_videoformat, "videoformat");
            CHECK_RANGE_LIST(pInParams->ColorPrim,      list_colorprim,   "colorprim");
            CHECK_RANGE_LIST(pInParams->Transfer,       list_transfer,    "transfer");
            CHECK_RANGE_LIST(pInParams->ColorMatrix,    list_colormatrix, "colormatrix");

            INIT_MFX_EXT_BUFFER(m_VideoSignalInfo, MFX_EXTBUFF_VIDEO_SIGNAL_INFO);
            m_VideoSignalInfo.ColourDescriptionPresent = 1; //"1"と設定しないと正しく反映されない
            m_VideoSignalInfo.VideoFormat              = pInParams->VideoFormat;
            m_VideoSignalInfo.VideoFullRange           = pInParams->bFullrange != 0;
            m_VideoSignalInfo.ColourPrimaries          = GET_COLOR_PRM(pInParams->ColorPrim,   list_colorprim);
            m_VideoSignalInfo.TransferCharacteristics  = GET_COLOR_PRM(pInParams->Transfer,    list_transfer);
            m_VideoSignalInfo.MatrixCoefficients       = GET_COLOR_PRM(pInParams->ColorMatrix, list_colormatrix);
#undef GET_COLOR_PRM
            m_EncExtParams.push_back((mfxExtBuffer *)&m_VideoSignalInfo);
    }
    if (check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_13)
        && pInParams->chromaloc != 0) {
        INIT_MFX_EXT_BUFFER(m_chromalocInfo, MFX_EXTBUFF_CHROMA_LOC_INFO);
        m_chromalocInfo.ChromaLocInfoPresentFlag = 1;
        m_chromalocInfo.ChromaSampleLocTypeTopField = (mfxU16)pInParams->chromaloc;
        m_chromalocInfo.ChromaSampleLocTypeBottomField = (mfxU16)pInParams->chromaloc;
        m_EncExtParams.push_back((mfxExtBuffer *)&m_chromalocInfo);
    }

    m_mfxEncParams.mfx.FrameInfo.FourCC = MFX_FOURCC_NV12;
    if (m_mfxEncParams.mfx.CodecId == MFX_CODEC_HEVC && m_mfxEncParams.mfx.CodecProfile == MFX_PROFILE_HEVC_MAIN10) {
        m_mfxEncParams.mfx.FrameInfo.FourCC = MFX_FOURCC_P010;
        m_mfxEncParams.mfx.FrameInfo.BitDepthLuma = 10;
        m_mfxEncParams.mfx.FrameInfo.BitDepthChroma = 10;
        m_mfxEncParams.mfx.FrameInfo.Shift = 1;
    }
    m_mfxEncParams.mfx.FrameInfo.Width  = (mfxU16)ALIGN(pInParams->nDstWidth, blocksz);
    m_mfxEncParams.mfx.FrameInfo.Height = (mfxU16)((MFX_PICSTRUCT_PROGRESSIVE == m_mfxEncParams.mfx.FrameInfo.PicStruct)?
        ALIGN(pInParams->nDstHeight, blocksz) : ALIGN(pInParams->nDstHeight, blocksz * 2));

    m_mfxEncParams.mfx.FrameInfo.CropX = 0;
    m_mfxEncParams.mfx.FrameInfo.CropY = 0;
    m_mfxEncParams.mfx.FrameInfo.CropW = pInParams->nDstWidth;
    m_mfxEncParams.mfx.FrameInfo.CropH = pInParams->nDstHeight;

    // In case of HEVC when height and/or width divided with 8 but not divided with 16
    // add extended parameter to increase performance
    if ( ( !((m_mfxEncParams.mfx.FrameInfo.CropW & 15 ) ^ 8 ) ||
           !((m_mfxEncParams.mfx.FrameInfo.CropH & 15 ) ^ 8 ) ) &&
             (m_mfxEncParams.mfx.CodecId == MFX_CODEC_HEVC) ) {
        INIT_MFX_EXT_BUFFER(m_ExtHEVCParam, MFX_EXTBUFF_HEVC_PARAM);
        m_ExtHEVCParam.PicWidthInLumaSamples = m_mfxEncParams.mfx.FrameInfo.CropW;
        m_ExtHEVCParam.PicHeightInLumaSamples = m_mfxEncParams.mfx.FrameInfo.CropH;
        m_EncExtParams.push_back((mfxExtBuffer*)&m_ExtHEVCParam);
    }

    if (m_mfxEncParams.mfx.CodecId == MFX_CODEC_VP8) {
        INIT_MFX_EXT_BUFFER(m_ExtVP8CodingOption, MFX_EXTBUFF_VP8_CODING_OPTION);
        m_ExtVP8CodingOption.SharpnessLevel = (mfxU16)clamp_param_int(pInParams->nVP8Sharpness, 0, 8, _T("sharpness"));
        m_EncExtParams.push_back((mfxExtBuffer*)&m_ExtVP8CodingOption);
    }

    if (MFX_CODEC_JPEG == pInParams->CodecId) {
        m_mfxEncParams.mfx.Interleaved = 1;
        m_mfxEncParams.mfx.Quality = pInParams->nQuality;
        m_mfxEncParams.mfx.RestartInterval = 0;
        RGY_MEMSET_ZERO(m_mfxEncParams.mfx.reserved5);
    }

    if (!m_EncExtParams.empty()) {
        m_mfxEncParams.ExtParam = &m_EncExtParams[0];
        m_mfxEncParams.NumExtParam = (mfxU16)m_EncExtParams.size();
        for (const auto& extParam : m_EncExtParams) {
            PrintMes(RGY_LOG_DEBUG, _T("InitMfxEncParams: set ext param %s.\n"), fourccToStr(extParam->BufferId).c_str());
        }
    }

    PrintMes(RGY_LOG_DEBUG, _T("InitMfxEncParams: enc input frame %dx%d (%d,%d,%d,%d)\n"),
        m_mfxEncParams.mfx.FrameInfo.Width, m_mfxEncParams.mfx.FrameInfo.Height,
        m_mfxEncParams.mfx.FrameInfo.CropX, m_mfxEncParams.mfx.FrameInfo.CropY, m_mfxEncParams.mfx.FrameInfo.CropW, m_mfxEncParams.mfx.FrameInfo.CropH);
    PrintMes(RGY_LOG_DEBUG, _T("InitMfxEncParams: enc input color format %s, chroma %d, bitdepth %d, shift %d\n"),
        ColorFormatToStr(m_mfxEncParams.mfx.FrameInfo.FourCC), m_mfxEncParams.mfx.FrameInfo.ChromaFormat, m_mfxEncParams.mfx.FrameInfo.BitDepthLuma, m_mfxEncParams.mfx.FrameInfo.Shift);
    PrintMes(RGY_LOG_DEBUG, _T("InitMfxEncParams: set all enc params.\n"));
    return MFX_ERR_NONE;
}

mfxStatus CQSVPipeline::InitMfxVppParams(sInputParams *pInParams) {
    const mfxU32 blocksz = (pInParams->CodecId == MFX_CODEC_HEVC) ? 32 : 16;
    mfxU64 availableFeaures = CheckVppFeatures(m_mfxSession, m_mfxVer);
#if ENABLE_FPS_CONVERSION
    if (FPS_CONVERT_NONE != pInParams->vpp.nFPSConversion && !(availableFeaures & VPP_FEATURE_FPS_CONVERSION_ADV)) {
        PrintMes(RGY_LOG_WARN, _T("FPS Conversion not supported on this platform, disabled.\n"));
        pInParams->vpp.nFPSConversion = FPS_CONVERT_NONE;
    }
#else
    if (pInParams->vpp.rotate) {
        if (!(availableFeaures & VPP_FEATURE_ROTATE)) {
            PrintMes(RGY_LOG_ERROR, _T("vpp-rotate is not supported on this platform.\n"));
            return MFX_ERR_UNSUPPORTED;
        }
        if ((pInParams->nPicStruct & (MFX_PICSTRUCT_FIELD_TFF | MFX_PICSTRUCT_FIELD_BFF))) {
            PrintMes(RGY_LOG_ERROR, _T("vpp-rotate is not supported with interlaced output.\n"));
            return MFX_ERR_INVALID_VIDEO_PARAM;
        }
    }
    //現時点ではうまく動いてなさそうなので無効化
    if (FPS_CONVERT_NONE != pInParams->vpp.fpsConversion) {
        PrintMes(RGY_LOG_WARN, _T("FPS Conversion not supported on this build, disabled.\n"));
        pInParams->vpp.fpsConversion = FPS_CONVERT_NONE;
    }
#endif

    if (pInParams->vpp.imageStabilizer && !(availableFeaures & VPP_FEATURE_IMAGE_STABILIZATION)) {
        PrintMes(RGY_LOG_WARN, _T("Image Stabilizer not supported on this platform, disabled.\n"));
        pInParams->vpp.imageStabilizer = 0;
    }

    if ((pInParams->nPicStruct & (MFX_PICSTRUCT_FIELD_TFF | MFX_PICSTRUCT_FIELD_BFF))) {
        switch (pInParams->vpp.deinterlace) {
        case MFX_DEINTERLACE_IT_MANUAL:
            if (!(availableFeaures & VPP_FEATURE_DEINTERLACE_IT_MANUAL)) {
                PrintMes(RGY_LOG_ERROR, _T("Deinterlace \"it-manual\" is not supported on this platform.\n"));
                return MFX_ERR_INVALID_VIDEO_PARAM;
            }
            break;
        case MFX_DEINTERLACE_AUTO_SINGLE:
        case MFX_DEINTERLACE_AUTO_DOUBLE:
            if (!(availableFeaures & VPP_FEATURE_DEINTERLACE_AUTO)) {
                PrintMes(RGY_LOG_ERROR, _T("Deinterlace \"auto\" is not supported on this platform.\n"));
                return MFX_ERR_INVALID_VIDEO_PARAM;
            }
            break;
        default:
            break;
        }
    }

    if (pInParams->vpp.scalingQuality != MFX_SCALING_MODE_DEFAULT
        && !(availableFeaures & VPP_FEATURE_SCALING_QUALITY)) {
        PrintMes(RGY_LOG_WARN, _T("vpp scaling quality is not supported on this platform, disabled.\n"));
        pInParams->vpp.scalingQuality = MFX_SCALING_MODE_DEFAULT;
    }

    if (pInParams->vpp.mirrorType != MFX_MIRRORING_DISABLED
        && !(availableFeaures & VPP_FEATURE_MIRROR)) {
        PrintMes(RGY_LOG_ERROR, _T("vpp mirroring is not supported on this platform, disabled.\n"));
        return MFX_ERR_UNSUPPORTED;
    }

    if (!pInParams) {
        return MFX_ERR_MEMORY_ALLOC;
    }

    m_mfxVppParams.IOPattern = (pInParams->memType != SYSTEM_MEMORY) ?
        MFX_IOPATTERN_IN_VIDEO_MEMORY | MFX_IOPATTERN_OUT_VIDEO_MEMORY :
        MFX_IOPATTERN_IN_SYSTEM_MEMORY | MFX_IOPATTERN_OUT_SYSTEM_MEMORY;

    m_mfxVppParams.vpp.In.PicStruct = pInParams->nPicStruct;
    m_mfxVppParams.vpp.In.FrameRateExtN = pInParams->nFPSRate;
    m_mfxVppParams.vpp.In.FrameRateExtD = pInParams->nFPSScale;
    m_mfxVppParams.vpp.In.AspectRatioW  = (mfxU16)pInParams->nPAR[0];
    m_mfxVppParams.vpp.In.AspectRatioH  = (mfxU16)pInParams->nPAR[1];

    mfxFrameInfo inputFrameInfo = frameinfo_rgy_to_enc(m_pFileReader->GetInputFrameInfo());
    if (inputFrameInfo.FourCC == 0 || inputFrameInfo.FourCC == MFX_FOURCC_NV12) {
        m_mfxVppParams.vpp.In.FourCC       = MFX_FOURCC_NV12;
        m_mfxVppParams.vpp.In.ChromaFormat = MFX_CHROMAFORMAT_YUV420;
        m_mfxVppParams.vpp.In.Width     = (mfxU16)ALIGN(pInParams->nWidth, blocksz);
        m_mfxVppParams.vpp.In.Height    = (mfxU16)((MFX_PICSTRUCT_PROGRESSIVE == m_mfxVppParams.vpp.In.PicStruct)?
            ALIGN(pInParams->nHeight, blocksz) : ALIGN(pInParams->nHeight, blocksz));
    } else {
        m_mfxVppParams.vpp.In.FourCC         = inputFrameInfo.FourCC;
        m_mfxVppParams.vpp.In.ChromaFormat   = inputFrameInfo.ChromaFormat;
        m_mfxVppParams.vpp.In.BitDepthLuma   = (pInParams->inputBitDepthLuma) ? pInParams->inputBitDepthLuma : inputFrameInfo.BitDepthLuma;
        m_mfxVppParams.vpp.In.BitDepthChroma = (pInParams->inputBitDepthChroma) ? pInParams->inputBitDepthChroma : inputFrameInfo.BitDepthChroma;
        if (m_mfxVppParams.vpp.In.ChromaFormat == MFX_CHROMAFORMAT_YUV422) {
            //10を指定しないとおかしな変換が行われる
            if (m_mfxVppParams.vpp.In.BitDepthLuma > 8) m_mfxVppParams.vpp.In.BitDepthLuma = 10;
            if (m_mfxVppParams.vpp.In.BitDepthChroma > 8) m_mfxVppParams.vpp.In.BitDepthChroma = 10;
            if (m_mfxVppParams.vpp.In.BitDepthLuma > 8) m_mfxVppParams.vpp.In.Shift = 1;
        }
        //QSVデコーダは特別にShiftパラメータを使う可能性がある
        if (inputFrameInfo.Shift) {
            m_mfxVppParams.vpp.In.Shift      = inputFrameInfo.Shift;
        }
        m_mfxVppParams.vpp.In.Width     = (mfxU16)ALIGN(inputFrameInfo.CropW, blocksz);
        m_mfxVppParams.vpp.In.Height    = (mfxU16)((MFX_PICSTRUCT_PROGRESSIVE == m_mfxVppParams.vpp.In.PicStruct) ?
            ALIGN(inputFrameInfo.CropH, blocksz) : ALIGN(inputFrameInfo.CropH, blocksz * 2));
    }

    m_mfxVppParams.vpp.In.CropW = pInParams->nWidth;
    m_mfxVppParams.vpp.In.CropH = pInParams->nHeight;

    //QSVデコードを行う場合、CropはVppで行う
    if (m_pFileReader->getInputCodec() != RGY_CODEC_UNKNOWN) {
        m_mfxVppParams.vpp.In.CropX = (mfxU16)pInParams->sInCrop.e.left;
        m_mfxVppParams.vpp.In.CropY = (mfxU16)pInParams->sInCrop.e.up;
        m_mfxVppParams.vpp.In.CropW -= (mfxU16)(pInParams->sInCrop.e.left   + pInParams->sInCrop.e.right);
        m_mfxVppParams.vpp.In.CropH -= (mfxU16)(pInParams->sInCrop.e.bottom + pInParams->sInCrop.e.up);
        PrintMes(RGY_LOG_DEBUG, _T("InitMfxVppParams: vpp crop enabled.\n"));
    }
    PrintMes(RGY_LOG_DEBUG, _T("InitMfxVppParams: vpp input frame %dx%d (%d,%d,%d,%d)\n"),
        m_mfxVppParams.vpp.In.Width, m_mfxVppParams.vpp.In.Height, m_mfxVppParams.vpp.In.CropX, m_mfxVppParams.vpp.In.CropY, m_mfxVppParams.vpp.In.CropW, m_mfxVppParams.vpp.In.CropH);
    PrintMes(RGY_LOG_DEBUG, _T("InitMfxVppParams: vpp input color format %s, chroma %d, bitdepth %d, shift %d\n"),
        ColorFormatToStr(m_mfxVppParams.vpp.In.FourCC), m_mfxVppParams.vpp.In.ChromaFormat, m_mfxVppParams.vpp.In.BitDepthLuma, m_mfxVppParams.vpp.In.Shift);

    memcpy(&m_mfxVppParams.vpp.Out, &m_mfxVppParams.vpp.In, sizeof(mfxFrameInfo));

    if (m_mfxEncParams.mfx.FrameInfo.FourCC) {
        m_mfxVppParams.vpp.Out.ChromaFormat   = MFX_CHROMAFORMAT_YUV420;
        m_mfxVppParams.vpp.Out.FourCC         = m_mfxEncParams.mfx.FrameInfo.FourCC;
        m_mfxVppParams.vpp.Out.BitDepthLuma   = m_mfxEncParams.mfx.FrameInfo.BitDepthLuma;
        m_mfxVppParams.vpp.Out.BitDepthChroma = m_mfxEncParams.mfx.FrameInfo.BitDepthChroma;
        m_mfxVppParams.vpp.Out.Shift          = m_mfxEncParams.mfx.FrameInfo.Shift;
        m_mfxVppParams.vpp.Out.PicStruct = (pInParams->vpp.deinterlace) ? MFX_PICSTRUCT_PROGRESSIVE : pInParams->nPicStruct;
    }
    if ((pInParams->nPicStruct & (MFX_PICSTRUCT_FIELD_TFF | MFX_PICSTRUCT_FIELD_BFF))) {
        INIT_MFX_EXT_BUFFER(m_ExtDeinterlacing, MFX_EXTBUFF_VPP_DEINTERLACING);
        switch (pInParams->vpp.deinterlace) {
        case MFX_DEINTERLACE_NORMAL:
        case MFX_DEINTERLACE_AUTO_SINGLE:
            m_ExtDeinterlacing.Mode = (uint16_t)((pInParams->vpp.deinterlace == MFX_DEINTERLACE_NORMAL) ? MFX_DEINTERLACING_30FPS_OUT : MFX_DEINTERLACING_AUTO_SINGLE);
            break;
        case MFX_DEINTERLACE_IT:
        case MFX_DEINTERLACE_IT_MANUAL:
            if (pInParams->vpp.deinterlace == MFX_DEINTERLACE_IT_MANUAL) {
                m_ExtDeinterlacing.Mode = MFX_DEINTERLACING_FIXED_TELECINE_PATTERN;
                m_ExtDeinterlacing.TelecinePattern = (mfxU16)pInParams->vpp.telecinePattern;
            } else {
                m_ExtDeinterlacing.Mode = MFX_DEINTERLACING_24FPS_OUT;
            }
            m_mfxVppParams.vpp.Out.FrameRateExtN = (m_mfxVppParams.vpp.Out.FrameRateExtN * 4) / 5;
            break;
        case MFX_DEINTERLACE_BOB:
        case MFX_DEINTERLACE_AUTO_DOUBLE:
            m_ExtDeinterlacing.Mode = (uint16_t)((pInParams->vpp.deinterlace == MFX_DEINTERLACE_BOB) ? MFX_DEINTERLACING_BOB : MFX_DEINTERLACING_AUTO_DOUBLE);
            m_mfxVppParams.vpp.Out.FrameRateExtN = m_mfxVppParams.vpp.Out.FrameRateExtN * 2;
            break;
        case MFX_DEINTERLACE_NONE:
        default:
            break;
        }
        if (pInParams->vpp.deinterlace != MFX_DEINTERLACE_NONE) {
#if ENABLE_ADVANCED_DEINTERLACE
            if (check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_13)) {
                m_VppExtParams.push_back((mfxExtBuffer *)&m_ExtDeinterlacing);
                m_VppDoUseList.push_back(MFX_EXTBUFF_VPP_DEINTERLACING);
            }
#endif
            m_mfxVppParams.vpp.Out.PicStruct = MFX_PICSTRUCT_PROGRESSIVE;
            VppExtMes += _T("Deinterlace (");
            VppExtMes += get_chr_from_value(list_deinterlace, pInParams->vpp.deinterlace);
            if (pInParams->vpp.deinterlace == MFX_DEINTERLACE_IT_MANUAL) {
                VppExtMes += _T(", ");
                VppExtMes += get_chr_from_value(list_telecine_patterns, pInParams->vpp.telecinePattern);
            }
            VppExtMes += _T(")\n");
            PrintMes(RGY_LOG_DEBUG, _T("InitMfxVppParams: vpp deinterlace enabled.\n"));
        }
        pInParams->vpp.fpsConversion = FPS_CONVERT_NONE;
    } else {
        switch (pInParams->vpp.fpsConversion) {
        case FPS_CONVERT_MUL2:
            m_mfxVppParams.vpp.Out.FrameRateExtN = m_mfxVppParams.vpp.Out.FrameRateExtN * 2;
            break;
        case FPS_CONVERT_MUL2_5:
            m_mfxVppParams.vpp.Out.FrameRateExtN = m_mfxVppParams.vpp.Out.FrameRateExtN * 5 / 2;
            break;
        default:
            break;
        }
    }
    m_mfxVppParams.vpp.Out.CropX = 0;
    m_mfxVppParams.vpp.Out.CropY = 0;
    m_mfxVppParams.vpp.Out.CropW = pInParams->nDstWidth;
    m_mfxVppParams.vpp.Out.CropH = pInParams->nDstHeight;
    m_mfxVppParams.vpp.Out.Width = (mfxU16)ALIGN(pInParams->nDstWidth, blocksz);
    m_mfxVppParams.vpp.Out.Height = (mfxU16)((MFX_PICSTRUCT_PROGRESSIVE == m_mfxVppParams.vpp.Out.PicStruct)?
        ALIGN(pInParams->nDstHeight, blocksz) : ALIGN(pInParams->nDstHeight, blocksz));

    if (check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_8)
        && (   MFX_FOURCC_RGB3 == m_mfxVppParams.vpp.In.FourCC
            || MFX_FOURCC_RGB4 == m_mfxVppParams.vpp.In.FourCC)) {

        INIT_MFX_EXT_BUFFER(m_ExtVppVSI, MFX_EXTBUFF_VPP_VIDEO_SIGNAL_INFO);
        m_ExtVppVSI.In.NominalRange    = MFX_NOMINALRANGE_0_255;
        m_ExtVppVSI.In.TransferMatrix  = MFX_TRANSFERMATRIX_UNKNOWN;
        m_ExtVppVSI.Out.NominalRange   = (mfxU16)((pInParams->bFullrange) ? MFX_NOMINALRANGE_0_255 : MFX_NOMINALRANGE_16_235);
        m_ExtVppVSI.Out.TransferMatrix = MFX_TRANSFERMATRIX_UNKNOWN;
        if (pInParams->ColorMatrix == get_cx_index(list_colormatrix, _T("bt709"))) {
            m_ExtVppVSI.Out.TransferMatrix = MFX_TRANSFERMATRIX_BT709;
        } else if (pInParams->ColorMatrix == get_cx_index(list_colormatrix, _T("bt601"))) {
            m_ExtVppVSI.Out.TransferMatrix = MFX_TRANSFERMATRIX_BT601;
        }
        m_VppExtParams.push_back((mfxExtBuffer *)&m_ExtVppVSI);
        m_VppDoUseList.push_back(MFX_EXTBUFF_VPP_VIDEO_SIGNAL_INFO);
        PrintMes(RGY_LOG_DEBUG, _T("InitMfxVppParams: vpp colorspace conversion enabled.\n"));
    }
    PrintMes(RGY_LOG_DEBUG, _T("InitMfxVppParams: vpp output frame %dx%d (%d,%d,%d,%d)\n"),
        m_mfxVppParams.vpp.Out.Width, m_mfxVppParams.vpp.Out.Height, m_mfxVppParams.vpp.Out.CropX, m_mfxVppParams.vpp.Out.CropY, m_mfxVppParams.vpp.Out.CropW, m_mfxVppParams.vpp.Out.CropH);
    PrintMes(RGY_LOG_DEBUG, _T("InitMfxVppParams: vpp output color format %s, chroma %d, bitdepth %d, shift %d\n"),
        ColorFormatToStr(m_mfxVppParams.vpp.Out.FourCC), m_mfxVppParams.vpp.Out.ChromaFormat, m_mfxVppParams.vpp.Out.BitDepthLuma, m_mfxVppParams.vpp.Out.Shift);
    PrintMes(RGY_LOG_DEBUG, _T("InitMfxVppParams: set all vpp params.\n"));
    return MFX_ERR_NONE;
}

mfxStatus CQSVPipeline::CreateVppExtBuffers(sInputParams *pParams) {
    m_VppExtParams.clear();
    m_VppDoUseList.clear();
    m_VppDoNotUseList.clear();
    m_VppDoNotUseList.push_back(MFX_EXTBUFF_VPP_PROCAMP);
    auto vppExtAddMes = [this](tstring str) {
        VppExtMes += str;
        PrintMes(RGY_LOG_DEBUG, _T("CreateVppExtBuffers: %s"), str.c_str());
    };

    if (pParams->vpp.detail.enable) {
        INIT_MFX_EXT_BUFFER(m_ExtDetail, MFX_EXTBUFF_VPP_DETAIL);
        m_ExtDetail.DetailFactor = (mfxU16)clamp_param_int(pParams->vpp.detail.strength, QSV_VPP_DETAIL_ENHANCE_MIN, QSV_VPP_DETAIL_ENHANCE_MAX, _T("vpp-detail-enhance"));
        m_VppExtParams.push_back((mfxExtBuffer*)&m_ExtDetail);

        vppExtAddMes(strsprintf(_T("Detail Enhancer, strength %d\n"), m_ExtDetail.DetailFactor));
        m_VppDoUseList.push_back(MFX_EXTBUFF_VPP_DETAIL);
    } else {
        m_VppDoNotUseList.push_back(MFX_EXTBUFF_VPP_DETAIL);
    }

    switch (pParams->vpp.rotate) {
    case MFX_ANGLE_90:
    case MFX_ANGLE_180:
    case MFX_ANGLE_270:
        INIT_MFX_EXT_BUFFER(m_ExtRotate, MFX_EXTBUFF_VPP_ROTATION);
        m_ExtRotate.Angle = (mfxU16)pParams->vpp.rotate;
        m_VppExtParams.push_back((mfxExtBuffer*)&m_ExtRotate);

        vppExtAddMes(strsprintf(_T("rotate %d\n"), pParams->vpp.rotate));
        m_VppDoUseList.push_back(MFX_EXTBUFF_VPP_ROTATION);
        break;
    default:
        break;
    }

    if (pParams->vpp.mirrorType != MFX_MIRRORING_DISABLED) {
        if (check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_19)) {
            PrintMes(RGY_LOG_ERROR, _T("--vpp-mirror not supported on this platform, disabled.\n"));
            return MFX_ERR_UNSUPPORTED;
        }
        INIT_MFX_EXT_BUFFER(m_ExtMirror, MFX_EXTBUFF_VPP_MIRRORING);
        m_ExtMirror.Type = (mfxU16)pParams->vpp.mirrorType;
        m_VppExtParams.push_back((mfxExtBuffer*)&m_ExtMirror);

        vppExtAddMes(strsprintf(_T("mirroring %s\n"), get_chr_from_value(list_vpp_mirroring, pParams->vpp.mirrorType)));
        m_VppDoUseList.push_back(MFX_EXTBUFF_VPP_MIRRORING);
    }

    if ( (    pParams->nWidth  != pParams->nDstWidth
           || pParams->nHeight != pParams->nDstHeight)
        && pParams->vpp.scalingQuality != MFX_SCALING_MODE_DEFAULT) {
        INIT_MFX_EXT_BUFFER(m_ExtScaling, MFX_EXTBUFF_VPP_SCALING);
        m_ExtScaling.ScalingMode = (mfxU16)pParams->vpp.scalingQuality;
        m_VppExtParams.push_back((mfxExtBuffer*)&m_ExtScaling);

        m_VppDoUseList.push_back(MFX_EXTBUFF_VPP_SCALING);
    }

    if (pParams->vpp.fpsConversion != FPS_CONVERT_NONE) {
        INIT_MFX_EXT_BUFFER(m_ExtFrameRateConv, MFX_EXTBUFF_VPP_FRAME_RATE_CONVERSION);
        m_ExtFrameRateConv.Algorithm = MFX_FRCALGM_FRAME_INTERPOLATION;
        m_VppExtParams.push_back((mfxExtBuffer*)&m_ExtFrameRateConv);

        vppExtAddMes(_T("fps conversion with interpolation\n"));
        m_VppDoUseList.push_back(MFX_EXTBUFF_VPP_FRAME_RATE_CONVERSION);
    }

    if (pParams->vpp.denoise.enable) {
        INIT_MFX_EXT_BUFFER(m_ExtDenoise, MFX_EXTBUFF_VPP_DENOISE);
        m_ExtDenoise.DenoiseFactor = (mfxU16)clamp_param_int(pParams->vpp.denoise.strength, QSV_VPP_DENOISE_MIN, QSV_VPP_DENOISE_MAX, _T("vpp-denoise"));
        m_VppExtParams.push_back((mfxExtBuffer*)&m_ExtDenoise);

        vppExtAddMes(strsprintf(_T("Denoise, strength %d\n"), m_ExtDenoise.DenoiseFactor));
        m_VppDoUseList.push_back(MFX_EXTBUFF_VPP_DENOISE);
    } else {
        m_VppDoNotUseList.push_back(MFX_EXTBUFF_VPP_DENOISE);
    }

    if (check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_26)) {
        if (pParams->vpp.mctf.enable) {
            INIT_MFX_EXT_BUFFER(m_ExtMctf, MFX_EXTBUFF_VPP_MCTF);
            m_ExtMctf.FilterStrength = (mfxU16)clamp_param_int(pParams->vpp.mctf.strength, 0, QSV_VPP_MCTF_MAX, _T("vpp-mctf"));
            m_VppExtParams.push_back((mfxExtBuffer*)&m_ExtMctf);

            if (m_ExtMctf.FilterStrength == 0) {
                vppExtAddMes(_T("mctf, strength auto\n"));
            } else {
                vppExtAddMes(strsprintf(_T("mctf, strength %d\n"), m_ExtMctf.FilterStrength));
            }
            m_VppDoUseList.push_back(MFX_EXTBUFF_VPP_MCTF);
        } else {
            m_VppDoNotUseList.push_back(MFX_EXTBUFF_VPP_MCTF);
        }
    } else {
        if (pParams->vpp.mctf.enable) {
            PrintMes(RGY_LOG_WARN, _T("--vpp-mctf not supported on this platform, disabled.\n"));
            pParams->vpp.mctf.enable = false;
        }
    }

    if (check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_6)) {
        if (pParams->vpp.imageStabilizer) {
            CHECK_RANGE_LIST(pParams->vpp.imageStabilizer, list_vpp_image_stabilizer, "vpp-image-stab");
            INIT_MFX_EXT_BUFFER(m_ExtImageStab, MFX_EXTBUFF_VPP_IMAGE_STABILIZATION);
            m_ExtImageStab.Mode = (mfxU16)pParams->vpp.imageStabilizer;
            m_VppExtParams.push_back((mfxExtBuffer*)&m_ExtImageStab);

            vppExtAddMes(strsprintf(_T("Stabilizer, mode %s\n"), get_vpp_image_stab_mode_str(m_ExtImageStab.Mode)));
            m_VppDoUseList.push_back(MFX_EXTBUFF_VPP_IMAGE_STABILIZATION);
        }
    } else {
        if (pParams->vpp.imageStabilizer) {
            PrintMes(RGY_LOG_WARN, _T("--vpp-image-stab not supported on this platform, disabled.\n"));
            pParams->vpp.imageStabilizer = false;
        }
    }

    m_VppDoNotUseList.push_back(MFX_EXTBUFF_VPP_SCENE_ANALYSIS);

    if (   check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_3)
        && (pParams->nPicStruct & (MFX_PICSTRUCT_FIELD_TFF | MFX_PICSTRUCT_FIELD_BFF))) {
            switch (pParams->vpp.deinterlace) {
            case MFX_DEINTERLACE_IT:
            case MFX_DEINTERLACE_IT_MANUAL:
            case MFX_DEINTERLACE_BOB:
            case MFX_DEINTERLACE_AUTO_DOUBLE:
                INIT_MFX_EXT_BUFFER(m_ExtFrameRateConv, MFX_EXTBUFF_VPP_FRAME_RATE_CONVERSION);
                m_ExtFrameRateConv.Algorithm = MFX_FRCALGM_DISTRIBUTED_TIMESTAMP;

                m_VppDoUseList.push_back(MFX_EXTBUFF_VPP_FRAME_RATE_CONVERSION);
                break;
            default:
                break;
            }
    }

    if (m_VppDoUseList.size()) {
        INIT_MFX_EXT_BUFFER(m_VppDoUse, MFX_EXTBUFF_VPP_DOUSE);
        m_VppDoUse.NumAlg = (mfxU32)m_VppDoUseList.size();
        m_VppDoUse.AlgList = &m_VppDoUseList[0];

        m_VppExtParams.insert(m_VppExtParams.begin(), (mfxExtBuffer *)&m_VppDoUse);
        for (const auto& extParam : m_VppDoUseList) {
            PrintMes(RGY_LOG_DEBUG, _T("CreateVppExtBuffers: set DoUse %s.\n"), fourccToStr(extParam).c_str());
        }
    }

    //Haswell以降では、DONOTUSEをセットするとdetail enhancerの効きが固定になるなど、よくわからない挙動を示す。
    if (m_VppDoNotUseList.size() && getCPUGen(&m_mfxSession) < CPU_GEN_HASWELL) {
        AllocAndInitVppDoNotUse();
        m_VppExtParams.push_back((mfxExtBuffer *)&m_VppDoNotUse);
        for (const auto& extParam : m_VppDoNotUseList) {
            PrintMes(RGY_LOG_DEBUG, _T("CreateVppExtBuffers: set DoNotUse %s.\n"), fourccToStr(extParam).c_str());
        }
    }

    m_mfxVppParams.ExtParam = (m_VppExtParams.size()) ? &m_VppExtParams[0] : nullptr;
    m_mfxVppParams.NumExtParam = (mfxU16)m_VppExtParams.size();

    return MFX_ERR_NONE;
}

#pragma warning (push)
#pragma warning (disable: 4100)
mfxStatus CQSVPipeline::InitVppPrePlugins(sInputParams *pParams) {
    mfxStatus sts = MFX_ERR_NONE;
#if ENABLE_CUSTOM_VPP
    tstring vppPreMes = _T("");
    if (pParams->vpp.delogo.pFilePath) {
        unique_ptr<CVPPPlugin> filter(new CVPPPlugin());
        DelogoParam param(m_pMFXAllocator.get(), m_memType, pParams->vpp.delogo.pFilePath, pParams->vpp.delogo.pSelect, pParams->strSrcFile,
            (short)pParams->vpp.delogo.posOffset.first, (short)pParams->vpp.delogo.posOffset.second, (short)pParams->vpp.delogo.depth,
            (short)pParams->vpp.delogo.YOffset, (short)pParams->vpp.delogo.CbOffset, (short)pParams->vpp.delogo.CrOffset, pParams->vpp.delogo.add);
        sts = filter->Init(m_mfxVer, _T("delogo"), &param, sizeof(param), true, m_memType, m_hwdev, m_pMFXAllocator.get(), 3, m_mfxVppParams.vpp.In, m_mfxVppParams.IOPattern, m_pQSVLog);
        if (sts == MFX_ERR_ABORTED) {
            PrintMes(RGY_LOG_WARN, _T("%s\n"), filter->getMessage().c_str());
            sts = MFX_ERR_NONE;
        } else if (sts != MFX_ERR_NONE) {
            PrintMes(RGY_LOG_ERROR, _T("%s\n"), filter->getMessage().c_str());
            return sts;
        } else {
            sts = MFXJoinSession(m_mfxSession, filter->getSession());
            QSV_ERR_MES(sts, _T("Failed to join vpp pre filter session."));
            tstring mes = filter->getMessage();
            PrintMes(RGY_LOG_DEBUG, _T("InitVppPrePlugins: add filter: %s\n"), mes.c_str());
            vppPreMes += mes;
            m_VppPrePlugins.push_back(std::move(filter));
        }
    }
    if (pParams->vpp.halfTurn) {
        unique_ptr<CVPPPlugin> filter(new CVPPPlugin());
        RotateParam param(180);
        sts = filter->Init(m_mfxVer, _T("rotate"), &param, sizeof(param), true, m_memType, m_hwdev, m_pMFXAllocator.get(), 3, m_mfxVppParams.vpp.In, m_mfxVppParams.IOPattern, m_pQSVLog);
        if (sts != MFX_ERR_NONE) {
            PrintMes(RGY_LOG_ERROR, _T("%s\n"), filter->getMessage().c_str());
            return sts;
        } else {
            sts = MFXJoinSession(m_mfxSession, filter->getSession());
            QSV_ERR_MES(sts, _T("Failed to join vpp pre filter session."));
            tstring mes = filter->getMessage();
            PrintMes(RGY_LOG_DEBUG, _T("InitVppPrePlugins: add filter: %s\n"), mes.c_str());
            vppPreMes += mes;
            m_VppPrePlugins.push_back(std::move(filter));
        }
    }
    VppExtMes = vppPreMes + VppExtMes;
#endif
    return sts;
}

mfxStatus CQSVPipeline::InitVppPostPlugins(sInputParams *pParams) {
    return MFX_ERR_NONE;
}
#pragma warning (pop)

mfxStatus CQSVPipeline::CreateHWDevice() {
    mfxStatus sts = MFX_ERR_NONE;

#if D3D_SURFACES_SUPPORT
    POINT point = {0, 0};
    HWND window = WindowFromPoint(point);
    m_hwdev.reset();

    if (m_memType) {
#if MFX_D3D11_SUPPORT
        if (m_memType == D3D11_MEMORY
            && (m_hwdev = std::make_shared<CQSVD3D11Device>())) {
            m_memType = D3D11_MEMORY;
            PrintMes(RGY_LOG_DEBUG, _T("HWDevice: d3d11 - initializing...\n"));

            sts = m_hwdev->Init(NULL, GetAdapterID(m_mfxSession), m_pQSVLog);
            if (sts != MFX_ERR_NONE) {
                m_hwdev.reset();
                PrintMes(RGY_LOG_DEBUG, _T("HWDevice: d3d11 - initializing failed.\n"));
            }
        }
#endif // #if MFX_D3D11_SUPPORT
        if (!m_hwdev && (m_hwdev = std::make_shared<CQSVD3D9Device>())) {
            //もし、d3d11要求で失敗したら自動的にd3d9に切り替える
            //sessionごと切り替える必要がある
            if (m_memType != D3D9_MEMORY) {
                PrintMes(RGY_LOG_DEBUG, _T("Retry openning device, chaging to d3d9 mode, re-init session.\n"));
                InitSession(true, D3D9_MEMORY);
                m_memType = m_memType;
            }

            PrintMes(RGY_LOG_DEBUG, _T("HWDevice: d3d9 - initializing...\n"));
            sts = m_hwdev->Init(window, GetAdapterID(m_mfxSession), m_pQSVLog);
        }
    }
    QSV_ERR_MES(sts, _T("Failed to initialize HW Device."));
    PrintMes(RGY_LOG_DEBUG, _T("HWDevice: initializing success.\n"));

#elif LIBVA_SUPPORT
    m_hwdev.reset(CreateVAAPIDevice());
    if (!m_hwdev) {
        return MFX_ERR_MEMORY_ALLOC;
    }
    sts = m_hwdev->Init(NULL, GetAdapterID(m_mfxSession), m_pQSVLog);
    QSV_ERR_MES(sts, _T("Failed to initialize HW Device."));
#endif
    return MFX_ERR_NONE;
}

mfxStatus CQSVPipeline::ResetDevice() {
    if (m_memType & (D3D9_MEMORY | D3D11_MEMORY)) {
        PrintMes(RGY_LOG_DEBUG, _T("HWDevice: reset.\n"));
        return m_hwdev->Reset();
    }
    return MFX_ERR_NONE;
}

mfxStatus CQSVPipeline::AllocFrames() {
    mfxStatus sts = MFX_ERR_NONE;
    mfxFrameAllocRequest DecRequest;
    mfxFrameAllocRequest EncRequest;
    mfxFrameAllocRequest VppRequest[2];
    mfxFrameAllocRequest NextRequest; //出力されてくるフレーム情報とフレームタイプを記録する

    uint16_t nEncSurfNum = 0; // enc用のフレーム数
    uint16_t nVppSurfNum = 0; // vpp用のフレーム数

    uint16_t nInputSurfAdd   = 0;
    uint16_t nDecSurfAdd     = 0; // dec用のフレーム数
    uint16_t nVppPreSurfAdd  = 0; // vpp pre用のフレーム数
    uint16_t nVppSurfAdd     = 0;
    uint16_t nVppPostSurfAdd = 0; // vpp post用のフレーム数

    RGY_MEMSET_ZERO(DecRequest);
    RGY_MEMSET_ZERO(EncRequest);
    RGY_MEMSET_ZERO(VppRequest[0]);
    RGY_MEMSET_ZERO(VppRequest[1]);
    for (const auto& filter : m_VppPrePlugins) {
        RGY_MEMSET_ZERO(filter->m_PluginResponse);
    }
    for (const auto& filter : m_VppPostPlugins) {
        RGY_MEMSET_ZERO(filter->m_PluginResponse);
    }
    RGY_MEMSET_ZERO(NextRequest);

    PrintMes(RGY_LOG_DEBUG, _T("AllocFrames: m_nAsyncDepth - %d frames\n"), m_nAsyncDepth);

    //各要素が要求するフレーム数を調べる
    if (m_pmfxENC) {
        sts = m_pmfxENC->QueryIOSurf(&m_mfxEncParams, &EncRequest);
        QSV_ERR_MES(sts, _T("Failed to get required buffer size for encoder."));
        PrintMes(RGY_LOG_DEBUG, _T("AllocFrames: Enc query - %d frames\n"), EncRequest.NumFrameSuggested);
    }

    if (m_pmfxVPP) {
        // VppRequest[0]はvppへの入力, VppRequest[1]はvppからの出力
        sts = m_pmfxVPP->QueryIOSurf(&m_mfxVppParams, VppRequest);
        QSV_ERR_MES(sts, _T("Failed to get required buffer size for vpp."));
        if (m_nAVSyncMode & (RGY_AVSYNC_VFR | RGY_AVSYNC_FORCE_CFR)) {
            //ptsチェック用に使うフレームを追加する
            VppRequest[0].NumFrameMin       += CHECK_PTS_MAX_INSERT_FRAMES;
            VppRequest[0].NumFrameSuggested += CHECK_PTS_MAX_INSERT_FRAMES;
        }
        PrintMes(RGY_LOG_DEBUG, _T("AllocFrames: Vpp query[0] - %d frames\n"), VppRequest[0].NumFrameSuggested);
        PrintMes(RGY_LOG_DEBUG, _T("AllocFrames: Vpp query[1] - %d frames\n"), VppRequest[1].NumFrameSuggested);
    }

    if (m_pmfxDEC) {
        sts = m_pmfxDEC->QueryIOSurf(&m_mfxDecParams, &DecRequest);
        QSV_ERR_MES(sts, _T("Failed to get required buffer size for decoder."));
        if (m_nAVSyncMode & (RGY_AVSYNC_VFR | RGY_AVSYNC_FORCE_CFR)) {
            //ptsチェック用に使うフレームを追加する
            DecRequest.NumFrameMin       += CHECK_PTS_MAX_INSERT_FRAMES;
            DecRequest.NumFrameSuggested += CHECK_PTS_MAX_INSERT_FRAMES;
        }
        PrintMes(RGY_LOG_DEBUG, _T("AllocFrames: Dec query - %d frames\n"), DecRequest.NumFrameSuggested);
    }

    nInputSurfAdd = (std::max<uint16_t>)(m_EncThread.m_nFrameBuffer, 1);

    nDecSurfAdd = DecRequest.NumFrameSuggested;

    //vppの出力用のフレームとencの入力用のフレームは共有される
    nEncSurfNum = EncRequest.NumFrameSuggested + m_nAsyncDepth;

    //m_nAsyncDepthを考慮して、vppの入力用のフレーム数を決める
    nVppSurfNum = VppRequest[0].NumFrameSuggested + m_nAsyncDepth;

    PrintMes(RGY_LOG_DEBUG, _T("AllocFrames: nInputSurfAdd %d frames\n"), nInputSurfAdd);
    PrintMes(RGY_LOG_DEBUG, _T("AllocFrames: nDecSurfAdd   %d frames\n"), nDecSurfAdd);

    if (m_pmfxDEC) {
        NextRequest = DecRequest;
    }

    //VppPrePlugins
    if (m_VppPrePlugins.size()) {
        for (mfxU32 i = 0; i < (mfxU32)m_VppPrePlugins.size(); i++) {
            mfxU16 mem_type = (mfxU16)((HW_MEMORY & m_memType) ? MFX_MEMTYPE_EXTERNAL_FRAME : MFX_MEMTYPE_SYSTEM_MEMORY);
            m_VppPrePlugins[i]->m_nSurfNum += m_nAsyncDepth;
            if (i == 0) {
                mem_type |= (nDecSurfAdd) ? (MFX_MEMTYPE_VIDEO_MEMORY_DECODER_TARGET | MFX_MEMTYPE_FROM_DECODE) : (MFX_MEMTYPE_VIDEO_MEMORY_PROCESSOR_TARGET | MFX_MEMTYPE_FROM_VPPOUT);
                m_VppPrePlugins[i]->m_nSurfNum += (uint16_t)(std::max)(1, (int)nInputSurfAdd + nDecSurfAdd - m_nAsyncDepth + 1);
            } else {
                //surfaceが2つの要素c1とc2に共有されるとき、NumSurf = c1_out + c2_in - AsyncDepth + 1
                mem_type |= MFX_MEMTYPE_VIDEO_MEMORY_PROCESSOR_TARGET | MFX_MEMTYPE_FROM_VPPOUT;
                m_VppPrePlugins[i]->m_nSurfNum += m_VppPrePlugins[i-1]->m_nSurfNum - m_nAsyncDepth + 1;
            }
            m_VppPrePlugins[i]->m_PluginRequest.Type = mem_type;
            m_VppPrePlugins[i]->m_PluginRequest.NumFrameMin = m_VppPrePlugins[i]->m_nSurfNum;
            m_VppPrePlugins[i]->m_PluginRequest.NumFrameSuggested = m_VppPrePlugins[i]->m_nSurfNum;
            memcpy(&m_VppPrePlugins[i]->m_PluginRequest.Info, &(m_VppPrePlugins[i]->m_pluginVideoParams.mfx.FrameInfo), sizeof(mfxFrameInfo));
            if (m_pmfxDEC && nDecSurfAdd) {
                m_VppPrePlugins[i]->m_PluginRequest.Info.Width  = DecRequest.Info.Width;
                m_VppPrePlugins[i]->m_PluginRequest.Info.Height = DecRequest.Info.Height;
                m_VppPrePlugins[i]->m_pluginVideoParams.mfx.FrameInfo.Width  = DecRequest.Info.Width;
                m_VppPrePlugins[i]->m_pluginVideoParams.mfx.FrameInfo.Height = DecRequest.Info.Height;
            }
            NextRequest = m_VppPrePlugins[i]->m_PluginRequest;
            memcpy(&NextRequest.Info, &(m_VppPrePlugins[i]->m_pluginVideoParams.vpp.Out), sizeof(mfxFrameInfo));
            PrintMes(RGY_LOG_DEBUG, _T("AllocFrames: PrePlugins[%d] %s, type: %s, %dx%d [%d,%d,%d,%d], request %d frames\n"),
                i, m_VppPrePlugins[i]->getFilterName().c_str(), qsv_memtype_str(mem_type).c_str(),
                m_VppPrePlugins[i]->m_PluginRequest.Info.Width, m_VppPrePlugins[i]->m_PluginRequest.Info.Height,
                m_VppPrePlugins[i]->m_PluginRequest.Info.CropX, m_VppPrePlugins[i]->m_PluginRequest.Info.CropY,
                m_VppPrePlugins[i]->m_PluginRequest.Info.CropW, m_VppPrePlugins[i]->m_PluginRequest.Info.CropH,
                m_VppPrePlugins[i]->m_PluginRequest.NumFrameSuggested);
        }

        //後始末
        nDecSurfAdd = 0;
        nInputSurfAdd = 0;
        nVppPreSurfAdd = m_VppPrePlugins.back()->m_nSurfNum;
    }

    //Vpp
    if (m_pmfxVPP) {
        nVppSurfNum += (int16_t)(std::max)(1, (int)nInputSurfAdd + nDecSurfAdd + nVppPreSurfAdd - m_nAsyncDepth + 1);

        //VppRequest[0]の準備
        VppRequest[0].NumFrameMin = nVppSurfNum;
        VppRequest[0].NumFrameSuggested = nVppSurfNum;
        memcpy(&VppRequest[0].Info, &(m_mfxVppParams.mfx.FrameInfo), sizeof(mfxFrameInfo));
        if (m_pmfxDEC && nDecSurfAdd) {
            VppRequest[0].Type = DecRequest.Type;
            VppRequest[0].Info.Width  = DecRequest.Info.Width;
            VppRequest[0].Info.Height = DecRequest.Info.Height;
            m_mfxVppParams.mfx.FrameInfo.Width = DecRequest.Info.Width;
            m_mfxVppParams.mfx.FrameInfo.Height = DecRequest.Info.Height;
            //フレームのリクエストを出す時点でCropの値を入れておくと、
            //DecFrameAsyncでMFX_ERR_UNDEFINED_BEHAVIORを出してしまう
            //Cropの値はVppFrameAsyncの直前に渡すようにする
            VppRequest[0].Info.CropX = DecRequest.Info.CropX;
            VppRequest[0].Info.CropY = DecRequest.Info.CropY;
            VppRequest[0].Info.CropW = DecRequest.Info.CropW;
            VppRequest[0].Info.CropH = DecRequest.Info.CropH;
        }

        //後始末
        nInputSurfAdd = 0;
        nDecSurfAdd = 0;
        nVppPreSurfAdd = 0;
        nVppSurfAdd = (std::max<uint16_t>)(VppRequest[1].NumFrameSuggested, 1);
        NextRequest = VppRequest[1];
        memcpy(&NextRequest.Info, &(m_mfxVppParams.vpp.Out), sizeof(mfxFrameInfo));
        PrintMes(RGY_LOG_DEBUG, _T("AllocFrames: Vpp type: %s, %dx%d [%d,%d,%d,%d], request %d frames\n"),
            qsv_memtype_str(VppRequest[0].Type).c_str(),
            VppRequest[0].Info.Width, VppRequest[0].Info.Height,
            VppRequest[0].Info.CropX, VppRequest[0].Info.CropY, VppRequest[0].Info.CropW, VppRequest[0].Info.CropH, VppRequest[0].NumFrameSuggested);
    }

    //VppPostPlugins
    if (m_VppPostPlugins.size()) {
        for (mfxU32 i = 0; i < (mfxU32)m_VppPostPlugins.size(); i++) {
            mfxU16 mem_type = (mfxU16)((HW_MEMORY & m_memType) ? MFX_MEMTYPE_EXTERNAL_FRAME : MFX_MEMTYPE_SYSTEM_MEMORY);
            m_VppPostPlugins[i]->m_nSurfNum += m_nAsyncDepth;
            if (i == 0) {
                mem_type |= (nDecSurfAdd) ? (MFX_MEMTYPE_VIDEO_MEMORY_DECODER_TARGET | MFX_MEMTYPE_FROM_DECODE) : (MFX_MEMTYPE_VIDEO_MEMORY_PROCESSOR_TARGET | MFX_MEMTYPE_FROM_VPPOUT);
                m_VppPostPlugins[i]->m_nSurfNum += (uint16_t)(std::max)(1, (int)nInputSurfAdd + nDecSurfAdd + nVppPreSurfAdd + nVppSurfAdd - m_nAsyncDepth + 1);
            } else {
                //surfaceが2つの要素c1とc2に共有されるとき、NumSurf = c1_out + c2_in - AsyncDepth + 1
                mem_type |= MFX_MEMTYPE_VIDEO_MEMORY_PROCESSOR_TARGET | MFX_MEMTYPE_FROM_VPPOUT;
                m_VppPostPlugins[i]->m_nSurfNum += m_VppPostPlugins[i-1]->m_nSurfNum - m_nAsyncDepth + 1;
            }
            m_VppPostPlugins[i]->m_PluginRequest.Type = mem_type;
            m_VppPostPlugins[i]->m_PluginRequest.NumFrameMin = m_VppPostPlugins[i]->m_nSurfNum;
            m_VppPostPlugins[i]->m_PluginRequest.NumFrameSuggested = m_VppPostPlugins[i]->m_nSurfNum;
            memcpy(&m_VppPostPlugins[i]->m_PluginRequest.Info, &(m_VppPostPlugins[i]->m_pluginVideoParams.mfx.FrameInfo), sizeof(mfxFrameInfo));
            if (m_pmfxDEC && nDecSurfAdd) {
                m_VppPostPlugins[i]->m_PluginRequest.Type = DecRequest.Type;
                m_VppPostPlugins[i]->m_PluginRequest.Info.Width  = DecRequest.Info.Width;
                m_VppPostPlugins[i]->m_PluginRequest.Info.Height = DecRequest.Info.Height;
                m_VppPostPlugins[i]->m_pluginVideoParams.mfx.FrameInfo.Width  = DecRequest.Info.Width;
                m_VppPostPlugins[i]->m_pluginVideoParams.mfx.FrameInfo.Height = DecRequest.Info.Height;
            }
            NextRequest = m_VppPostPlugins[i]->m_PluginRequest;
            memcpy(&NextRequest.Info, &(m_VppPostPlugins[i]->m_pluginVideoParams.vpp.Out), sizeof(mfxFrameInfo));
            PrintMes(RGY_LOG_DEBUG, _T("AllocFrames: PostPlugins[%d] %s, type: %s, %dx%d [%d,%d,%d,%d], request %d frames\n"),
                i, m_VppPostPlugins[i]->getFilterName().c_str(), qsv_memtype_str(mem_type).c_str(),
                m_VppPostPlugins[i]->m_PluginRequest.Info.Width, m_VppPostPlugins[i]->m_PluginRequest.Info.Height,
                m_VppPostPlugins[i]->m_PluginRequest.Info.CropX, m_VppPostPlugins[i]->m_PluginRequest.Info.CropY,
                m_VppPostPlugins[i]->m_PluginRequest.Info.CropW, m_VppPostPlugins[i]->m_PluginRequest.Info.CropH,
                m_VppPostPlugins[i]->m_PluginRequest.NumFrameSuggested);
        }

        //後始末
        nInputSurfAdd = 0;
        nDecSurfAdd = 0;
        nVppPreSurfAdd = 0;
        nVppSurfAdd = 0;
        nVppPostSurfAdd = m_VppPostPlugins.back()->m_nSurfNum;
    }

    //Enc、エンコーダが有効でない場合は出力フレーム
    {
        nEncSurfNum += (uint16_t)(std::max)(1, (int)nInputSurfAdd + nDecSurfAdd + nVppPreSurfAdd + nVppSurfAdd + nVppPostSurfAdd - m_nAsyncDepth + 1);
        if (m_pmfxENC == nullptr) {
            EncRequest = NextRequest;
            nEncSurfNum += (m_nAsyncDepth - 1);
        } else {
            memcpy(&EncRequest.Info, &(m_mfxEncParams.mfx.FrameInfo), sizeof(mfxFrameInfo));
        }
        EncRequest.NumFrameMin = nEncSurfNum;
        EncRequest.NumFrameSuggested = nEncSurfNum;
        if (m_pmfxDEC && nDecSurfAdd) {
            EncRequest.Type |= MFX_MEMTYPE_FROM_DECODE;
            EncRequest.Info.Width = std::max(EncRequest.Info.Width, DecRequest.Info.Width);
            EncRequest.Info.Height = std::max(EncRequest.Info.Height, DecRequest.Info.Height);
        }
        if (nVppPreSurfAdd || nVppSurfAdd || nVppPostSurfAdd) {
            EncRequest.Type |= MFX_MEMTYPE_FROM_VPPOUT;
        }

        //後始末
        nInputSurfAdd = 0;
        nDecSurfAdd = 0;
        nVppPreSurfAdd = 0;
        nVppSurfAdd = 0;
        nVppPostSurfAdd = 0;
        PrintMes(RGY_LOG_DEBUG, _T("AllocFrames: %s type: %s, %dx%d [%d,%d,%d,%d], request %d frames\n"),
            (m_pmfxENC) ? _T("Enc") : _T("Out"),
            qsv_memtype_str(EncRequest.Type).c_str(),
            EncRequest.Info.Width, EncRequest.Info.Height,
            EncRequest.Info.CropX, EncRequest.Info.CropY, EncRequest.Info.CropW, EncRequest.Info.CropH, EncRequest.NumFrameSuggested);
    }

    // エンコーダ用のメモリ確保
    sts = m_pMFXAllocator->Alloc(m_pMFXAllocator->pthis, &EncRequest, &m_EncResponse);
    QSV_ERR_MES(sts, _T("Failed to allocate frames for encoder."));
    PrintMes(RGY_LOG_DEBUG, _T("AllocFrames: Allocated EncRequest %d\n"), m_EncResponse.NumFrameActual);

    // vppを使用するなら、vpp用のメモリを確保する
    if (m_pmfxVPP) {
        sts = m_pMFXAllocator->Alloc(m_pMFXAllocator->pthis, &(VppRequest[0]), &m_VppResponse);
        QSV_ERR_MES(sts, _T("Failed to allocate frames for vpp."));
        PrintMes(RGY_LOG_DEBUG, _T("AllocFrames: Allocated VppRequest %d\n"), m_VppResponse.NumFrameActual);
    }

    //エンコーダ用のmfxFrameSurface1配列を作成する
    m_pEncSurfaces.resize(m_EncResponse.NumFrameActual);

    for (int i = 0; i < m_EncResponse.NumFrameActual; i++) {
        memset(&(m_pEncSurfaces[i]), 0, sizeof(mfxFrameSurface1));
        memcpy(&m_pEncSurfaces[i].Info, &(EncRequest.Info), sizeof(mfxFrameInfo));

        if (m_bExternalAlloc) {
            m_pEncSurfaces[i].Data.MemId = m_EncResponse.mids[i];
        } else {
            sts = m_pMFXAllocator->Lock(m_pMFXAllocator->pthis, m_EncResponse.mids[i], &(m_pEncSurfaces[i].Data));
            QSV_ERR_MES(sts, _T("Failed to allocate surfaces for encoder."));
        }
    }

    //vpp用のmfxFrameSurface1配列を作成する
    if (m_pmfxVPP) {
        m_pVppSurfaces.resize(m_VppResponse.NumFrameActual);

        for (int i = 0; i < m_VppResponse.NumFrameActual; i++) {
            RGY_MEMSET_ZERO(m_pVppSurfaces[i]);
            memcpy(&m_pVppSurfaces[i].Info, &(VppRequest[0].Info), sizeof(mfxFrameInfo));

            if (m_bExternalAlloc) {
                m_pVppSurfaces[i].Data.MemId = m_VppResponse.mids[i];
            } else {
                sts = m_pMFXAllocator->Lock(m_pMFXAllocator->pthis, m_VppResponse.mids[i], &(m_pVppSurfaces[i].Data));
                QSV_ERR_MES(sts, _T("Failed to allocate surfaces for vpp."));
            }
        }
    }

    //vpp pre用のmfxFrameSurface1配列を作成する
    for (const auto& filter : m_VppPrePlugins) {
        if (MFX_ERR_NONE != (sts = filter->AllocSurfaces(m_pMFXAllocator.get(), m_bExternalAlloc))) {
            PrintMes(RGY_LOG_ERROR, _T("AllocFrames: Failed to alloc surface for %s\n"), filter->getFilterName().c_str());
            return sts;
        }
        PrintMes(RGY_LOG_DEBUG, _T("AllocFrames: Allocated surface for %s\n"), filter->getFilterName().c_str());
    }

    //vpp post用のmfxFrameSurface1配列を作成する
    for (const auto& filter : m_VppPostPlugins) {
        if (MFX_ERR_NONE != (sts = filter->AllocSurfaces(m_pMFXAllocator.get(), m_bExternalAlloc))) {
            PrintMes(RGY_LOG_ERROR, _T("AllocFrames: Failed to alloc surface for %s\n"), filter->getFilterName().c_str());
            return sts;
        }
        PrintMes(RGY_LOG_DEBUG, _T("AllocFrames: Allocated surface for %s\n"), filter->getFilterName().c_str());
    }

    return MFX_ERR_NONE;
}

mfxStatus CQSVPipeline::CreateAllocator() {
    mfxStatus sts = MFX_ERR_NONE;
    PrintMes(RGY_LOG_DEBUG, _T("CreateAllocator: MemType: %s\n"), MemTypeToStr(m_memType));

    if (D3D9_MEMORY == m_memType || D3D11_MEMORY == m_memType || VA_MEMORY == m_memType || HW_MEMORY == m_memType) {
#if D3D_SURFACES_SUPPORT
        sts = CreateHWDevice();
        QSV_ERR_MES(sts, _T("Failed to CreateHWDevice."));
        PrintMes(RGY_LOG_DEBUG, _T("CreateAllocator: CreateHWDevice success.\n"));

        mfxHDL hdl = NULL;
#if MFX_D3D11_SUPPORT
        mfxHandleType hdl_t = (D3D11_MEMORY == m_memType) ? MFX_HANDLE_D3D11_DEVICE : MFX_HANDLE_D3D9_DEVICE_MANAGER;
#else
        mfxHandleType hdl_t = MFX_HANDLE_D3D9_DEVICE_MANAGER;
#endif
        sts = m_hwdev->GetHandle(hdl_t, &hdl);
        QSV_ERR_MES(sts, _T("Failed to get HW device handle."));
        PrintMes(RGY_LOG_DEBUG, _T("CreateAllocator: HW device GetHandle success.\n"));

        mfxIMPL impl = 0;
        m_mfxSession.QueryIMPL(&impl);
        if (impl != MFX_IMPL_SOFTWARE) {
            // hwエンコード時のみハンドルを渡す
            sts = m_mfxSession.SetHandle(hdl_t, hdl);
            QSV_ERR_MES(sts, _T("Failed to set HW device handle to encode session."));
            PrintMes(RGY_LOG_DEBUG, _T("CreateAllocator: set HW device handle to encode session.\n"));
        }

        //D3D allocatorを作成
#if MFX_D3D11_SUPPORT
        if (D3D11_MEMORY == m_memType) {
            PrintMes(RGY_LOG_DEBUG, _T("CreateAllocator: Create d3d11 allocator.\n"));
            m_pMFXAllocator.reset(new QSVAllocatorD3D11);
            if (!m_pMFXAllocator) {
                PrintMes(RGY_LOG_ERROR, _T("Failed to allcate memory for D3D11FrameAllocator.\n"));
                return MFX_ERR_MEMORY_ALLOC;
            }

            QSVAllocatorParamsD3D11 *pd3dAllocParams = new QSVAllocatorParamsD3D11;
            if (!pd3dAllocParams) {
                PrintMes(RGY_LOG_ERROR, _T("Failed to allcate memory for D3D11AllocatorParams.\n"));
                return MFX_ERR_MEMORY_ALLOC;
            }
            pd3dAllocParams->pDevice = reinterpret_cast<ID3D11Device *>(hdl);
            PrintMes(RGY_LOG_DEBUG, _T("CreateAllocator: d3d11...\n"));

            m_pmfxAllocatorParams.reset(pd3dAllocParams);
        } else
#endif // #if MFX_D3D11_SUPPORT
        {
            PrintMes(RGY_LOG_DEBUG, _T("CreateAllocator: Create d3d9 allocator.\n"));
            m_pMFXAllocator.reset(new QSVAllocatorD3D9);
            if (!m_pMFXAllocator) {
                PrintMes(RGY_LOG_ERROR, _T("Failed to allcate memory for D3DFrameAllocator.\n"));
                return MFX_ERR_MEMORY_ALLOC;
            }

            QSVAllocatorParamsD3D9 *pd3dAllocParams = new QSVAllocatorParamsD3D9;
            if (!pd3dAllocParams) {
                PrintMes(RGY_LOG_ERROR, _T("Failed to allcate memory for pd3dAllocParams.\n"));
                return MFX_ERR_MEMORY_ALLOC;
            }
            pd3dAllocParams->pManager = reinterpret_cast<IDirect3DDeviceManager9 *>(hdl);
            PrintMes(RGY_LOG_DEBUG, _T("CreateAllocator: d3d9...\n"));

            m_pmfxAllocatorParams.reset(pd3dAllocParams);
        }

        //GPUメモリ使用時には external allocatorを使用する必要がある
        //mfxSessionにallocatorを渡してやる必要がある
        sts = m_mfxSession.SetFrameAllocator(m_pMFXAllocator.get());
        QSV_ERR_MES(sts, _T("Failed to set frame allocator to encode session."));
        PrintMes(RGY_LOG_DEBUG, _T("CreateAllocator: frame allocator set to session.\n"));

        m_bExternalAlloc = true;
#endif
#ifdef LIBVA_SUPPORT
        sts = CreateHWDevice();
        QSV_ERR_MES(sts, _T("Failed to CreateHWDevice."));

        mfxHDL hdl = NULL;
        sts = m_hwdev->GetHandle(MFX_HANDLE_VA_DISPLAY, &hdl);
        QSV_ERR_MES(sts, _T("Failed to get HW device handle."));
        PrintMes(RGY_LOG_DEBUG, _T("CreateAllocator: HW device GetHandle success. : 0x%x\n"), (uint32_t)(size_t)hdl);

        //ハンドルを渡す
        sts = m_mfxSession.SetHandle(MFX_HANDLE_VA_DISPLAY, hdl);
        QSV_ERR_MES(sts, _T("Failed to set HW device handle to encode session."));

        //VAAPI allocatorを作成
        m_pMFXAllocator.reset(new QSVAllocatorVA());
        if (!m_pMFXAllocator) {
            PrintMes(RGY_LOG_ERROR, _T("Failed to allcate memory for vaapiFrameAllocator.\n"));
            return MFX_ERR_MEMORY_ALLOC;
        }

        QSVAllocatorParamsVA *p_vaapiAllocParams = new QSVAllocatorParamsVA;
        if (!p_vaapiAllocParams) {
            PrintMes(RGY_LOG_ERROR, _T("Failed to allcate memory for vaapiAllocatorParams.\n"));
            return MFX_ERR_MEMORY_ALLOC;
        }

        p_vaapiAllocParams->m_dpy = (VADisplay)hdl;
        m_pmfxAllocatorParams.reset(p_vaapiAllocParams);

        //GPUメモリ使用時には external allocatorを使用する必要がある
        //mfxSessionにallocatorを渡してやる必要がある
        sts = m_mfxSession.SetFrameAllocator(m_pMFXAllocator.get());
        QSV_ERR_MES(sts, _T("Failed to set frame allocator to encode session."));
        PrintMes(RGY_LOG_DEBUG, _T("CreateAllocator: frame allocator set to session.\n"));

        m_bExternalAlloc = true;
#endif
    } else {
#ifdef LIBVA_SUPPORT
        //システムメモリ使用でも MFX_HANDLE_VA_DISPLAYをHW libraryに渡してやる必要がある
        mfxIMPL impl;
        m_mfxSession.QueryIMPL(&impl);

        if (MFX_IMPL_HARDWARE == MFX_IMPL_BASETYPE(impl)) {
            sts = CreateHWDevice();
            QSV_ERR_MES(sts, _T("Failed to CreateHWDevice."));

            mfxHDL hdl = NULL;
            sts = m_hwdev->GetHandle(MFX_HANDLE_VA_DISPLAY, &hdl);
            QSV_ERR_MES(sts, _T("Failed to get HW device handle."));
            PrintMes(RGY_LOG_DEBUG, _T("CreateAllocator: HW device GetHandle success. : 0x%x\n"), (uint32_t)(size_t)hdl);

            //ハンドルを渡す
            sts = m_mfxSession.SetHandle(MFX_HANDLE_VA_DISPLAY, hdl);
            QSV_ERR_MES(sts, _T("Failed to set HW device handle to encode session."));
        }
#endif
        //system memory allocatorを作成
        m_pMFXAllocator.reset(new QSVAllocatorSys);
        if (!m_pMFXAllocator) {
            return MFX_ERR_MEMORY_ALLOC;
        }
        PrintMes(RGY_LOG_DEBUG, _T("CreateAllocator: sys mem allocator...\n"));
    }

    //メモリallocatorの初期化
    if (MFX_ERR_NONE > (sts = m_pMFXAllocator->Init(m_pmfxAllocatorParams.get(), m_pQSVLog))) {
        PrintMes(RGY_LOG_ERROR, _T("Failed to initialize %s memory allocator. : %s\n"), MemTypeToStr(m_memType), get_err_mes(sts));
        return sts;
    }
    PrintMes(RGY_LOG_DEBUG, _T("CreateAllocator: frame allocator initialized.\n"));

    return MFX_ERR_NONE;
}

void CQSVPipeline::DeleteFrames() {
    m_pEncSurfaces.clear();
    m_pVppSurfaces.clear();
    m_pDecSurfaces.clear();

    if (m_pMFXAllocator) {
        m_pMFXAllocator->Free(m_pMFXAllocator->pthis, &m_EncResponse);
        m_pMFXAllocator->Free(m_pMFXAllocator->pthis, &m_VppResponse);
        m_pMFXAllocator->Free(m_pMFXAllocator->pthis, &m_DecResponse);
    }

    RGY_MEMSET_ZERO(m_EncResponse);
    RGY_MEMSET_ZERO(m_VppResponse);
    RGY_MEMSET_ZERO(m_DecResponse);
}

void CQSVPipeline::DeleteHWDevice() {
    m_hwdev.reset();
}

void CQSVPipeline::DeleteAllocator() {
    m_pMFXAllocator.reset();
    m_pmfxAllocatorParams.reset();

    DeleteHWDevice();
}

CQSVPipeline::CQSVPipeline() {
    m_memType = SYSTEM_MEMORY;
    m_bExternalAlloc = false;
    m_nAsyncDepth = 0;
    m_nAVSyncMode = RGY_AVSYNC_ASSUME_CFR;
    m_nProcSpeedLimit = 0;
    m_bTimerPeriodTuning = false;
    m_nMFXThreads = -1;

    m_pAbortByUser = NULL;
    m_heAbort.reset();

    m_pEncSatusInfo.reset();
    m_pFileWriterListAudio.clear();

    m_trimParam.list.clear();
    m_trimParam.offset = 0;

#if ENABLE_MVC_ENCODING
    m_bIsMVC = false;
    m_MVCflags = MVC_DISABLED;
    m_nNumView = 0;
    RGY_MEMSET_ZERO(m_MVCSeqDesc);
    m_MVCSeqDesc.Header.BufferId = MFX_EXTBUFF_MVC_SEQ_DESC;
    m_MVCSeqDesc.Header.BufferSz = sizeof(m_MVCSeqDesc);
#endif
    INIT_MFX_EXT_BUFFER(m_VppDoNotUse,        MFX_EXTBUFF_VPP_DONOTUSE);
    INIT_MFX_EXT_BUFFER(m_VideoSignalInfo,    MFX_EXTBUFF_VIDEO_SIGNAL_INFO);
    INIT_MFX_EXT_BUFFER(m_CodingOption,       MFX_EXTBUFF_CODING_OPTION);
    INIT_MFX_EXT_BUFFER(m_CodingOption2,      MFX_EXTBUFF_CODING_OPTION2);
    INIT_MFX_EXT_BUFFER(m_CodingOption3,      MFX_EXTBUFF_CODING_OPTION3);
    INIT_MFX_EXT_BUFFER(m_ExtVP8CodingOption, MFX_EXTBUFF_VP8_CODING_OPTION);
    INIT_MFX_EXT_BUFFER(m_ExtHEVCParam,       MFX_EXTBUFF_HEVC_PARAM);
    INIT_MFX_EXT_BUFFER(m_ThreadsParam,       MFX_EXTBUFF_THREADS_PARAM);

    m_hwdev.reset();

    RGY_MEMSET_ZERO(m_DecInputBitstream);

    RGY_MEMSET_ZERO(m_InitParam);
    RGY_MEMSET_ZERO(m_mfxDecParams);
    RGY_MEMSET_ZERO(m_mfxEncParams);
    RGY_MEMSET_ZERO(m_mfxVppParams);

    RGY_MEMSET_ZERO(m_DecVidProc);
    RGY_MEMSET_ZERO(m_VppDoNotUse);
    RGY_MEMSET_ZERO(m_VppDoUse);
    RGY_MEMSET_ZERO(m_ExtDenoise);
    RGY_MEMSET_ZERO(m_ExtMctf);
    RGY_MEMSET_ZERO(m_ExtDetail);
    RGY_MEMSET_ZERO(m_ExtDeinterlacing);
    RGY_MEMSET_ZERO(m_ExtFrameRateConv);
    RGY_MEMSET_ZERO(m_ExtRotate);
    RGY_MEMSET_ZERO(m_ExtVppVSI);
    RGY_MEMSET_ZERO(m_ExtImageStab);
    RGY_MEMSET_ZERO(m_ExtMirror);
    RGY_MEMSET_ZERO(m_ExtScaling);

    RGY_MEMSET_ZERO(m_EncResponse);
    RGY_MEMSET_ZERO(m_VppResponse);
    RGY_MEMSET_ZERO(m_DecResponse);
}

CQSVPipeline::~CQSVPipeline() {
    Close();
}

void CQSVPipeline::SetAbortFlagPointer(bool *abortFlag) {
    m_pAbortByUser = abortFlag;
}

mfxStatus CQSVPipeline::readChapterFile(tstring chapfile) {
    PrintMes(RGY_LOG_ERROR, _T("chater reading unsupportted in this build"));
    return MFX_ERR_UNKNOWN;
}

RGY_CSP CQSVPipeline::EncoderCsp(const sInputParams *pParams, int *pShift) {
    if (pParams->CodecId == MFX_CODEC_HEVC && pParams->CodecProfile == MFX_PROFILE_HEVC_MAIN10) {
        if (pShift) {
            *pShift = 6;
        }
        return RGY_CSP_P010;
    }
    if (pShift) {
        *pShift = 0;
    }
    return RGY_CSP_NV12;
}

mfxStatus CQSVPipeline::InitOutput(sInputParams *pParams) {
    RGY_ERR ret = RGY_ERR_NONE;
    bool stdoutUsed = false;
    const auto outputVideoInfo = (pParams->CodecId != MFX_CODEC_RAW) ? videooutputinfo(m_mfxEncParams.mfx, m_VideoSignalInfo, m_chromalocInfo) : videooutputinfo(m_mfxVppParams.vpp.Out);
    HEVCHDRSei hedrsei;
    if (hedrsei.parse(std::string(pParams->sMaxCll ? pParams->sMaxCll : ""), std::string(pParams->sMasterDisplay ? pParams->sMasterDisplay : ""))) {
        PrintMes(RGY_LOG_ERROR, _T("Failed to parse HEVC HDR10 metadata.\n"));
        return MFX_ERR_UNSUPPORTED;
    }
	
    if (pParams->CodecId == MFX_CODEC_RAW) {
        m_pFileWriter.reset(new CQSVOutFrame());
        YUVWriterParam param;
        param.bY4m = true;
        param.memType = m_memType;
        ret = m_pFileWriter->Init(pParams->strDstFile, &outputVideoInfo, &param, m_pQSVLog, m_pEncSatusInfo);
        if (ret != RGY_ERR_NONE) {
            PrintMes(RGY_LOG_ERROR, m_pFileWriter->GetOutputMessage());
            return err_to_mfx(ret);
        }
        stdoutUsed = m_pFileWriter->outputStdout();
        PrintMes(RGY_LOG_DEBUG, _T("Output: Initialized yuv frame writer%s.\n"), (stdoutUsed) ? _T("using stdout") : _T(""));
    } else {
        m_pFileWriter = std::make_shared<RGYOutputRaw>();
        RGYOutputRawPrm rawPrm = { 0 };
        rawPrm.bBenchmark = pParams->bBenchmark != 0;
        rawPrm.nBufSizeMB = pParams->nOutputBufSizeMB;
        rawPrm.codecId = codec_enc_to_rgy(pParams->CodecId);
        rawPrm.seiNal = hedrsei.gen_nal();
        ret = m_pFileWriter->Init(pParams->strDstFile, &outputVideoInfo, &rawPrm, m_pQSVLog, m_pEncSatusInfo);
        if (ret != RGY_ERR_NONE) {
            PrintMes(RGY_LOG_ERROR, m_pFileWriter->GetOutputMessage());
            return err_to_mfx(ret);
        }
        stdoutUsed = m_pFileWriter->outputStdout();
        PrintMes(RGY_LOG_DEBUG, _T("Output: Initialized bitstream writer%s.\n"), (stdoutUsed) ? _T("using stdout") : _T(""));
    }

    return MFX_ERR_NONE;
}

mfxStatus CQSVPipeline::InitInput(sInputParams *pParams) {
    RGY_ERR ret = RGY_ERR_NONE;

    int sourceAudioTrackIdStart = 1;    //トラック番号は1スタート
    int sourceSubtitleTrackIdStart = 1; //トラック番号は1スタート
    int sourceDataTrackIdStart = 1; //トラック番号は1スタート
    if (!m_pEncSatusInfo) {
        m_pEncSatusInfo = std::make_shared<EncodeStatus>();
    }
    VideoInfo inputVideo;
    memset(&inputVideo, 0, sizeof(inputVideo));
    inputVideo.type = (RGY_INPUT_FMT)pParams->nInputFmt;
    inputVideo.srcWidth = pParams->nWidth;
    inputVideo.srcHeight = pParams->nHeight;
    inputVideo.dstWidth = pParams->nDstWidth;
    inputVideo.dstHeight = pParams->nDstHeight;
    inputVideo.csp = EncoderCsp(pParams, &inputVideo.shift);
    inputVideo.sar[0] = pParams->nPAR[0];
    inputVideo.sar[1] = pParams->nPAR[1];
    inputVideo.fpsN = pParams->nFPSRate;
    inputVideo.fpsD = pParams->nFPSScale;
    inputVideo.crop = pParams->sInCrop;
    inputVideo.picstruct = picstruct_enc_to_rgy(pParams->nPicStruct);

    if (inputVideo.type == RGY_INPUT_FMT_AUTO) {
        if (check_ext(pParams->strSrcFile, { ".y4m" }))
            inputVideo.type = RGY_INPUT_FMT_Y4M;
        else if (check_ext(pParams->strSrcFile, { ".yuv" }))
            inputVideo.type = RGY_INPUT_FMT_RAW;
    }

    RGYInputPrm inputPrm;
    inputPrm.threadCsp = pParams->threadCsp;
    inputPrm.simdCsp = pParams->simdCsp;
    RGYInputPrm *pInputPrm = &inputPrm;

    //まずavs or vpy readerをためす
    m_pFileReader = nullptr;
    if (   inputVideo.type == RGY_INPUT_FMT_VPY
        || inputVideo.type == RGY_INPUT_FMT_VPY_MT
        || inputVideo.type == RGY_INPUT_FMT_AVS) {
        if (NULL == m_pFileReader) {
            //aviリーダーに切り替え再試行する
            inputVideo.type = RGY_INPUT_FMT_AVI;
        } else {
            ret = m_pFileReader->Init(pParams->strSrcFile, &inputVideo, pInputPrm, m_pQSVLog, m_pEncSatusInfo);
            if (ret == RGY_ERR_INVALID_COLOR_FORMAT) {
                //入力色空間の制限で使用できない場合はaviリーダーに切り替え再試行する
                PrintMes(RGY_LOG_WARN, m_pFileReader->GetInputMessage());
                m_pFileReader.reset();
                ret = RGY_ERR_NONE;
                PrintMes(RGY_LOG_WARN, _T("Input: switching to avi reader.\n"));
                inputVideo.type = RGY_INPUT_FMT_AVI;
            }
            if (ret != RGY_ERR_NONE) {
                PrintMes(RGY_LOG_ERROR, m_pFileReader->GetInputMessage());
                return err_to_mfx(ret);
            }
        }
    }

    auto subBurnTrack = std::unique_ptr<SubtitleSelect>(new SubtitleSelect());
    subBurnTrack->trackID = pParams->vpp.subburn.nTrack;
    SubtitleSelect *subBurnTrackPtr = subBurnTrack.get();
    if (m_pFileReader == nullptr) {
        switch (inputVideo.type) {
#if ENABLE_RAW_READER
            case RGY_INPUT_FMT_Y4M:
            case RGY_INPUT_FMT_RAW:
                m_pFileReader = std::make_shared<RGYInputRaw>();
                PrintMes(RGY_LOG_DEBUG, _T("Input: yuv reader selected (%s).\n"), (inputVideo.type == RGY_INPUT_FMT_Y4M) ? _T("y4m") : _T("raw"));
                break;
#endif
            default:
                PrintMes(RGY_LOG_DEBUG, _T("Failed to select reader.\n"));
                return MFX_ERR_NOT_FOUND;
        }
        ret = m_pFileReader->Init(pParams->strSrcFile, &inputVideo, pInputPrm, m_pQSVLog, m_pEncSatusInfo);
    }
    if (ret != RGY_ERR_NONE) {
        PrintMes(RGY_LOG_ERROR, m_pFileReader->GetInputMessage());
        return err_to_mfx(ret);
    }
    m_pEncSatusInfo->m_sData.frameTotal = inputVideo.frames;
    PrintMes(RGY_LOG_DEBUG, _T("Input: reader initialization successful.\n"));
    sourceAudioTrackIdStart    += m_pFileReader->GetAudioTrackCount();
    sourceSubtitleTrackIdStart += m_pFileReader->GetSubtitleTrackCount();
    sourceDataTrackIdStart += m_pFileReader->GetDataTrackCount();

    if (
        pParams->pTrimList && pParams->nTrimCount > 0) {
        //avhw/avswリーダー以外は、trimは自分ではセットされないので、ここでセットする
        sTrimParam trimParam;
        trimParam.list = make_vector(pParams->pTrimList, pParams->nTrimCount);
        trimParam.offset = 0;
        m_pFileReader->SetTrimParam(trimParam);
    }
    //trim情報をリーダーから取得する
    m_trimParam = *m_pFileReader->GetTrimParam();
    if (m_trimParam.list.size()) {
        PrintMes(RGY_LOG_DEBUG, _T("Input: trim options\n"));
        for (int i = 0; i < (int)m_trimParam.list.size(); i++) {
            PrintMes(RGY_LOG_DEBUG, _T("%d-%d "), m_trimParam.list[i].start, m_trimParam.list[i].fin);
        }
        PrintMes(RGY_LOG_DEBUG, _T(" (offset: %d)\n"), m_trimParam.offset);
    }
    return MFX_ERR_NONE;
}

mfxStatus CQSVPipeline::CheckParam(sInputParams *pParams) {
    auto inputFrameInfo = m_pFileReader->GetInputFrameInfo();
    auto cropInfo = m_pFileReader->GetInputCropInfo();

    if ((pParams->memType & HW_MEMORY)
        && (inputFrameInfo.csp == RGY_CSP_NV16 || inputFrameInfo.csp == RGY_CSP_P210)) {
        PrintMes(RGY_LOG_WARN, _T("Currently yuv422 surfaces are not supported by d3d9/d3d11 memory.\n"));
        PrintMes(RGY_LOG_WARN, _T("Switching to system memory.\n"));
        pParams->memType = SYSTEM_MEMORY;
    }

    //読み込み時に取得されていれば、それを使用する
    if (inputFrameInfo.srcWidth) {
        pParams->nWidth = (mfxU16)inputFrameInfo.srcWidth;
    }

    if (inputFrameInfo.srcHeight) {
        pParams->nHeight = (mfxU16)inputFrameInfo.srcHeight;
    }

    if (pParams->nPicStruct == RGY_PICSTRUCT_UNKNOWN) {
        pParams->nPicStruct = (mfxU16)picstruct_rgy_to_enc(inputFrameInfo.picstruct);
    }

    if ((!pParams->nFPSRate || !pParams->nFPSScale) && inputFrameInfo.fpsN && inputFrameInfo.fpsD) {
        pParams->nFPSRate = inputFrameInfo.fpsN;
        pParams->nFPSScale = inputFrameInfo.fpsD;
    }

    if (RGY_CSP_BIT_DEPTH[inputFrameInfo.csp] > 8) {
        pParams->inputBitDepthLuma = (mfxU16)(RGY_CSP_BIT_DEPTH[inputFrameInfo.csp] - inputFrameInfo.shift);
        pParams->inputBitDepthChroma = (mfxU16)(RGY_CSP_BIT_DEPTH[inputFrameInfo.csp] - inputFrameInfo.shift);
    }

    //picstructが設定されていない場合、プログレッシブとして扱う
    if (pParams->nPicStruct == MFX_PICSTRUCT_UNKNOWN) {
        pParams->nPicStruct = MFX_PICSTRUCT_PROGRESSIVE;
    }

    int h_mul = 2;
    bool output_interlaced = ((pParams->nPicStruct & (MFX_PICSTRUCT_FIELD_TFF | MFX_PICSTRUCT_FIELD_BFF)) != 0 && !pParams->vpp.deinterlace);
    if (output_interlaced) {
        h_mul *= 2;
    }
    //crop設定の確認
    if (pParams->sInCrop.e.left % 2 != 0 || pParams->sInCrop.e.right % 2 != 0) {
        PrintMes(RGY_LOG_ERROR, _T("crop width should be a multiple of 2.\n"));
        return MFX_ERR_INVALID_VIDEO_PARAM;
    }
    if (pParams->sInCrop.e.bottom % h_mul != 0 || pParams->sInCrop.e.up % h_mul != 0) {
        PrintMes(RGY_LOG_ERROR, _T("crop height should be a multiple of %d.\n"));
        return MFX_ERR_INVALID_VIDEO_PARAM;
    }
    if (0 == pParams->nWidth || 0 == pParams->nHeight) {
        PrintMes(RGY_LOG_ERROR, _T("--input-res must be specified with raw input.\n"));
        return MFX_ERR_INVALID_VIDEO_PARAM;
    }
    if (pParams->nFPSRate == 0 || pParams->nFPSScale == 0) {
        PrintMes(RGY_LOG_ERROR, _T("--fps must be specified with raw input.\n"));
        return MFX_ERR_INVALID_VIDEO_PARAM;
    }
    if (   pParams->nWidth < (pParams->sInCrop.e.left + pParams->sInCrop.e.right)
        || pParams->nHeight < (pParams->sInCrop.e.bottom + pParams->sInCrop.e.up)) {
            PrintMes(RGY_LOG_ERROR, _T("crop size is too big.\n"));
            return MFX_ERR_INVALID_VIDEO_PARAM;
    }

    //出力解像度が設定されていない場合は、入力解像度と同じにする
    if (pParams->nDstWidth == 0) {
        pParams->nDstWidth = pParams->nWidth -  (mfxU16)(pParams->sInCrop.e.left + pParams->sInCrop.e.right);
    }

    if (pParams->nDstHeight == 0) {
        pParams->nDstHeight = pParams->nHeight - (mfxU16)(pParams->sInCrop.e.bottom + pParams->sInCrop.e.up);
    }

    if (m_pFileReader->getInputCodec() == RGY_CODEC_UNKNOWN) {
        //QSVデコードを使わない場合には、入力段階でCropが行われる
        pParams->nWidth -= (mfxU16)(pParams->sInCrop.e.left + pParams->sInCrop.e.right);
        pParams->nHeight -= (mfxU16)(pParams->sInCrop.e.bottom + pParams->sInCrop.e.up);
    }

    //入力解像度と出力解像度が一致しないときはリサイズが必要なので、vppを有効にする
    if (pParams->nDstHeight != pParams->nHeight || pParams->nDstWidth != pParams->nWidth) {
        pParams->vpp.bEnable = true;
        pParams->vpp.bUseResize = true;
    } else {
        pParams->vpp.bUseResize = false;
    }

    //必要ならばSAR比の指定を行う
    if ((!pParams->nPAR[0] || !pParams->nPAR[1]) //SAR比の指定がない
        && inputFrameInfo.sar[0] && inputFrameInfo.sar[1] //入力側からSAR比を取得ずみ
        && !pParams->vpp.bUseResize) { //リサイズは行われない
        pParams->nPAR[0] = inputFrameInfo.sar[0];
        pParams->nPAR[1] = inputFrameInfo.sar[1];
    }

    if (pParams->nDstWidth % 2 != 0) {
        PrintMes(RGY_LOG_ERROR, _T("output width should be a multiple of 2."));
        return MFX_ERR_INVALID_VIDEO_PARAM;
    }

    if (pParams->nDstHeight % h_mul != 0) {
        PrintMes(RGY_LOG_ERROR, _T("output height should be a multiple of %d."), h_mul);
        return MFX_ERR_INVALID_VIDEO_PARAM;
    }
    if (pParams->vpp.rotate) {
#if defined(_WIN32) || defined(_WIN64)
        switch (pParams->vpp.rotate) {
        case MFX_ANGLE_0:
        case MFX_ANGLE_180:
            break;
        case MFX_ANGLE_90:
        case MFX_ANGLE_270:
            //縦横の解像度を入れ替える
            std::swap(pParams->nDstWidth, pParams->nDstHeight);
            break;
        default:
            PrintMes(RGY_LOG_ERROR, _T("vpp-rotate of %d degree is not supported.\n"), (int)pParams->vpp.rotate);
            return MFX_ERR_UNSUPPORTED;
        }
        //vpp-rotateにはd3d11メモリが必要
        if (!(pParams->memType & D3D11_MEMORY) || (pParams->memType & D3D9_MEMORY)) {
            PrintMes(RGY_LOG_WARN, _T("vpp-rotate requires d3d11 surface, forcing d3d11 surface.\n"));
        }
        pParams->memType = D3D11_MEMORY;
#else
        PrintMes(RGY_LOG_ERROR, _T("vpp-rotate is not supported on this platform.\n"));
        return MFX_ERR_UNSUPPORTED;
#endif
    }
    if (pParams->vpp.subburn.nTrack || pParams->vpp.subburn.pFilePath) {
#if defined(_WIN32) || defined(_WIN64)
#if MFX_D3D11_SUPPORT
        uint8_t memType = pParams->memType;
        //d3d11モードはWin8以降
        if (!check_OS_Win8orLater()) {
            memType &= (~D3D11_MEMORY);
        }
        //d3d11モードが必要なら、vpp-subは実行できない
        if (HW_MEMORY == (memType & HW_MEMORY) && check_if_d3d11_necessary()) {
            memType &= (~D3D11_MEMORY);
            PrintMes(RGY_LOG_DEBUG, _T("d3d11 mode required on this system, but vpp-sub does not support d3d11 mode.\n"));
            return MFX_ERR_UNSUPPORTED;
        }
        //d3d11を要求するvpp-rotateとd3d11では実行できないvpp-subは競合する
        if (pParams->vpp.rotate) {
            PrintMes(RGY_LOG_ERROR, _T("vpp-sub could not be used with vpp-rotate.\n"));
            PrintMes(RGY_LOG_ERROR, _T("vpp-rotate requires d3d11 mode, but vpp-sub does not support d3d11 mode.\n"));
            return MFX_ERR_UNSUPPORTED;
        }
#endif //#if MFX_D3D11_SUPPORT
#else
        //Linuxでのカスタムvppには systemメモリが必要
        if (pParams->memType & (D3D9_MEMORY | D3D11_MEMORY)) {
            PrintMes(RGY_LOG_WARN, _T("vpp-sub requires system surface, forcing system surface.\n"));
            pParams->memType = SYSTEM_MEMORY;
        }
#endif
    }

#if !(defined(_WIN32) || defined(_WIN64))
    //Linuxでのカスタムvppには systemメモリが必要
    if (pParams->vpp.delogo.pFilePath) {
        if (pParams->memType & (D3D9_MEMORY | D3D11_MEMORY)) {
            PrintMes(RGY_LOG_WARN, _T("vpp-delogo requires system surface, forcing system surface.\n"));
            pParams->memType = SYSTEM_MEMORY;
        }
    }
#endif

    //フレームレートのチェック
    if (pParams->nFPSRate == 0 || pParams->nFPSScale == 0) {
        PrintMes(RGY_LOG_ERROR, _T("unable to parse fps data.\n"));
        return MFX_ERR_INVALID_VIDEO_PARAM;
    }
    mfxU32 OutputFPSRate = pParams->nFPSRate;
    mfxU32 OutputFPSScale = pParams->nFPSScale;
    mfxU32 outputFrames = inputFrameInfo.frames;

    if ((pParams->nPicStruct & (MFX_PICSTRUCT_FIELD_TFF | MFX_PICSTRUCT_FIELD_BFF))) {
        CHECK_RANGE_LIST(pParams->vpp.deinterlace, list_deinterlace, "vpp-deinterlace");
        if (pParams->nAVSyncMode == RGY_AVSYNC_FORCE_CFR
            && (pParams->vpp.deinterlace == MFX_DEINTERLACE_IT
             || pParams->vpp.deinterlace == MFX_DEINTERLACE_IT_MANUAL
             || pParams->vpp.deinterlace == MFX_DEINTERLACE_BOB
             || pParams->vpp.deinterlace == MFX_DEINTERLACE_AUTO_DOUBLE)) {
            PrintMes(RGY_LOG_ERROR, _T("--avsync forcecfr cannnot be used with deinterlace %s.\n"), get_chr_from_value(list_deinterlace, pParams->vpp.deinterlace));
            return MFX_ERR_INVALID_VIDEO_PARAM;
        }

        switch (pParams->vpp.deinterlace) {
        case MFX_DEINTERLACE_IT:
        case MFX_DEINTERLACE_IT_MANUAL:
            OutputFPSRate = OutputFPSRate * 4;
            OutputFPSScale = OutputFPSScale * 5;
            outputFrames = (outputFrames * 4) / 5;
            break;
        case MFX_DEINTERLACE_BOB:
        case MFX_DEINTERLACE_AUTO_DOUBLE:
            OutputFPSRate = OutputFPSRate * 2;
            outputFrames *= 2;
            break;
        default:
            break;
        }
    }
    switch (pParams->vpp.fpsConversion) {
    case FPS_CONVERT_MUL2:
        OutputFPSRate = OutputFPSRate * 2;
        outputFrames *= 2;
        break;
    case FPS_CONVERT_MUL2_5:
        OutputFPSRate = OutputFPSRate * 5 / 2;
        outputFrames = outputFrames * 5 / 2;
        break;
    default:
        break;
    }
    auto gcd = rgy_gcd(OutputFPSRate, OutputFPSScale);
    OutputFPSRate /= gcd;
    OutputFPSScale /= gcd;
    double inputFileDuration = 0.0;
    m_pEncSatusInfo->Init(OutputFPSRate, OutputFPSScale, outputFrames, inputFileDuration, m_trimParam, m_pQSVLog, m_pPerfMonitor);
    PrintMes(RGY_LOG_DEBUG, _T("CheckParam: %dx%d%s, %d:%d, %d/%d, %d frames\n"),
        pParams->nDstWidth, pParams->nDstHeight, (output_interlaced) ? _T("i") : _T("p"),
        pParams->nPAR[0], pParams->nPAR[1], OutputFPSRate, OutputFPSScale, outputFrames);

    if (pParams->nPerfMonitorSelect || pParams->nPerfMonitorSelectMatplot) {
        m_pPerfMonitor->SetEncStatus(m_pEncSatusInfo);
    }

    //デコードを行う場合は、入力バッファサイズを常に1に設定する (そうしないと正常に動かない)
    //また、バッファサイズを拡大しても特に高速化しない
    if (m_pFileReader->getInputCodec() != RGY_CODEC_UNKNOWN) {
        pParams->nInputBufSize = 1;
        //Haswell以前はHEVCデコーダを使用する場合はD3D11メモリを使用しないと正常に稼働しない (4080ドライバ)
        if (getCPUGen(&m_mfxSession) <= CPU_GEN_HASWELL && m_pFileReader->getInputCodec() == RGY_CODEC_HEVC) {
            if (pParams->memType & D3D9_MEMORY) {
                pParams->memType &= ~D3D9_MEMORY;
                pParams->memType |= D3D11_MEMORY;
            }
            PrintMes(RGY_LOG_DEBUG, _T("Switched to d3d11 mode for HEVC decoding on Haswell.\n"));
        }
    }

    //入力バッファサイズの範囲チェック
    pParams->nInputBufSize = (mfxU16)clamp_param_int(pParams->nInputBufSize, QSV_INPUT_BUF_MIN, QSV_INPUT_BUF_MAX, _T("input-buf"));

    if (pParams->nAVSyncMode) {
        if (true) {
            PrintMes(RGY_LOG_WARN, _T("avsync is supportted only with avsw/avhw reader, disabled.\n"));
            pParams->nAVSyncMode = RGY_AVSYNC_ASSUME_CFR;
        }
    }

    return MFX_ERR_NONE;
}

mfxStatus CQSVPipeline::InitSessionInitParam(mfxU16 threads, mfxU16 priority) {
    INIT_MFX_EXT_BUFFER(m_ThreadsParam, MFX_EXTBUFF_THREADS_PARAM);
    m_ThreadsParam.NumThread = (mfxU16)clamp_param_int(threads, 0, QSV_SESSION_THREAD_MAX, _T("session-threads"));
    m_ThreadsParam.Priority = (mfxU16)clamp_param_int(priority, MFX_PRIORITY_LOW, MFX_PRIORITY_HIGH, _T("priority"));
    m_pInitParamExtBuf[0] = &m_ThreadsParam.Header;

    RGY_MEMSET_ZERO(m_InitParam);
    m_InitParam.ExtParam = m_pInitParamExtBuf;
    m_InitParam.NumExtParam = 1;
    return MFX_ERR_NONE;
}

#if defined(_WIN32) || defined(_WIN64)
typedef decltype(GetSystemInfo)* funcGetSystemInfo;
static int nGetSystemInfoHookThreads = -1;
static std::mutex mtxGetSystemInfoHook;
static funcGetSystemInfo origGetSystemInfoFunc = nullptr;
void __stdcall GetSystemInfoHook(LPSYSTEM_INFO lpSystemInfo) {
    origGetSystemInfoFunc(lpSystemInfo);
    if (lpSystemInfo && nGetSystemInfoHookThreads > 0) {
        decltype(lpSystemInfo->dwActiveProcessorMask) mask = 0;
        const int nThreads = std::max(1, std::min(nGetSystemInfoHookThreads, (int)sizeof(lpSystemInfo->dwActiveProcessorMask) * 8));
        for (int i = 0; i < nThreads; i++) {
            mask |= ((size_t)1<<i);
        }
        lpSystemInfo->dwActiveProcessorMask = mask;
        lpSystemInfo->dwNumberOfProcessors = nThreads;
    }
}
#endif

mfxStatus CQSVPipeline::InitSession(bool useHWLib, mfxU16 memType) {
    mfxStatus sts = MFX_ERR_NONE;
    m_SessionPlugins.reset();
    m_mfxSession.Close();
    PrintMes(RGY_LOG_DEBUG, _T("InitSession: Start initilaizing... memType: %s\n"), MemTypeToStr(memType));
#if defined(_WIN32) || defined(_WIN64)
    //コードの簡略化のため、静的フィールドを使うので、念のためロックをかける
    {
        std::lock_guard<std::mutex> lock(mtxGetSystemInfoHook);
        {
            nGetSystemInfoHookThreads = m_nMFXThreads;
            apihook api_hook;
            api_hook.hook(_T("kernel32.dll"), "GetSystemInfo", GetSystemInfoHook, (void **)&origGetSystemInfoFunc);
#endif

            auto InitSessionEx = [&](mfxIMPL impl, mfxVersion *verRequired) {
#if ENABLE_SESSION_THREAD_CONFIG
                if (m_ThreadsParam.NumThread != 0 || m_ThreadsParam.Priority != get_value_from_chr(list_priority, _T("normal"))) {
                    m_InitParam.Implementation = impl;
                    m_InitParam.Version = MFX_LIB_VERSION_1_15;
                    if (useHWLib) {
                        m_InitParam.GPUCopy = MFX_GPUCOPY_ON;
                    }
                    if (MFX_ERR_NONE == m_mfxSession.InitEx(m_InitParam)) {
                        return MFX_ERR_NONE;
                    } else {
                        m_ThreadsParam.NumThread = 0;
                        m_ThreadsParam.Priority = get_value_from_chr(list_priority, _T("normal"));
                    }
                }
#endif
                return m_mfxSession.Init(impl, verRequired);
            };

            if (useHWLib) {
                //とりあえず、MFX_IMPL_HARDWARE_ANYでの初期化を試みる
                mfxIMPL impl = MFX_IMPL_HARDWARE_ANY;
                m_memType = (memType) ? D3D9_MEMORY : SYSTEM_MEMORY;
#if MFX_D3D11_SUPPORT
                //Win7でD3D11のチェックをやると、
                //デスクトップコンポジションが切られてしまう問題が発生すると報告を頂いたので、
                //D3D11をWin8以降に限定
                if (!check_OS_Win8orLater()) {
                    memType &= (~D3D11_MEMORY);
                    PrintMes(RGY_LOG_DEBUG, _T("InitSession: OS is Win7, do not check for d3d11 mode.\n"));
                }

                //D3D11モードは基本的には遅い模様なので、自動モードなら切る
                if (HW_MEMORY == (memType & HW_MEMORY) && false == check_if_d3d11_necessary()) {
                    memType &= (~D3D11_MEMORY);
                    PrintMes(RGY_LOG_DEBUG, _T("InitSession: d3d11 memory mode not required, switching to d3d9 memory mode.\n"));
                }
#endif //#if MFX_D3D11_SUPPORT
                //まずd3d11モードを試すよう設定されていれば、ますd3d11を試して、失敗したらd3d9での初期化を試みる
                for (int i_try_d3d11 = 0; i_try_d3d11 < 1 + (HW_MEMORY == (memType & HW_MEMORY)); i_try_d3d11++) {
#if MFX_D3D11_SUPPORT
                    if (D3D11_MEMORY & memType) {
                        if (0 == i_try_d3d11) {
                            impl |= MFX_IMPL_VIA_D3D11; //d3d11モードも試す場合は、まずd3d11モードをチェック
                            m_memType = D3D11_MEMORY;
                            PrintMes(RGY_LOG_DEBUG, _T("InitSession: trying to init session for d3d11 mode.\n"));
                        } else {
                            impl &= ~MFX_IMPL_VIA_D3D11; //d3d11をオフにして再度テストする
                            m_memType = D3D9_MEMORY;
                            PrintMes(RGY_LOG_DEBUG, _T("InitSession: trying to init session for d3d9 mode.\n"));
                        }
                    }
#endif
                    mfxVersion verRequired = MFX_LIB_VERSION_1_1;

                    sts = InitSessionEx(impl, &verRequired);

                    //MFX_IMPL_HARDWARE_ANYがサポートされない場合もあり得るので、失敗したらこれをオフにしてもう一回試す
                    if (MFX_ERR_NONE != sts) {
                        PrintMes(RGY_LOG_DEBUG, _T("InitSession: failed to init session for multi GPU mode, retry by single GPU mode.\n"));
                        sts = m_mfxSession.Init((impl & (~MFX_IMPL_HARDWARE_ANY)) | MFX_IMPL_HARDWARE, &verRequired);
                    }

                    //成功したらループを出る
                    if (MFX_ERR_NONE == sts) {
                        break;
                    }
                }
                PrintMes(RGY_LOG_DEBUG, _T("InitSession: initialized using %s memory.\n"), MemTypeToStr(m_memType));
            } else {
                mfxIMPL impl = MFX_IMPL_SOFTWARE;
                mfxVersion verRequired = MFX_LIB_VERSION_1_1;
                sts = InitSessionEx(impl, &verRequired);
                m_memType = SYSTEM_MEMORY;
                PrintMes(RGY_LOG_DEBUG, _T("InitSession: initialized with system memory.\n"));
            }
#if defined(_WIN32) || defined(_WIN64)
        }
    }
#endif

    //使用できる最大のversionをチェック
    m_mfxSession.QueryVersion(&m_mfxVer);
    PrintMes(RGY_LOG_DEBUG, _T("InitSession: mfx lib version: %d.%d\n"), m_mfxVer.Major, m_mfxVer.Minor);
    return sts;
}

mfxStatus CQSVPipeline::InitLog(sInputParams *pParams) {
    //ログの初期化
    m_pQSVLog.reset(new RGYLog(pParams->pStrLogFile, pParams->nLogLevel));
    if (pParams->pStrLogFile) {
        m_pQSVLog->writeFileHeader(pParams->strDstFile);
    }
    return MFX_ERR_NONE;
}

mfxStatus CQSVPipeline::Init(sInputParams *pParams) {
    if (pParams == nullptr) {
        return MFX_ERR_NULL_PTR;
    }

    InitLog(pParams);

    mfxStatus sts = MFX_ERR_NONE;

    if (pParams->bBenchmark) {
        pParams->nAVMux = RGY_MUX_NONE;
        if (pParams->nAudioSelectCount) {
            for (int i = 0; i < pParams->nAudioSelectCount; i++) {
                rgy_free(pParams->ppAudioSelectList[i]);
            }
            rgy_free(pParams->ppAudioSelectList);
            pParams->nAudioSelectCount = 0;
            PrintMes(RGY_LOG_WARN, _T("audio copy or audio encoding disabled on benchmark mode.\n"));
        }
        if (pParams->nSubtitleSelectCount) {
            pParams->nSubtitleSelectCount = 0;
            PrintMes(RGY_LOG_WARN, _T("subtitle copy disabled on benchmark mode.\n"));
        }
        if (pParams->nPerfMonitorSelect || pParams->nPerfMonitorSelectMatplot) {
            pParams->nPerfMonitorSelect = 0;
            pParams->nPerfMonitorSelectMatplot = 0;
            PrintMes(RGY_LOG_WARN, _T("performance monitor disabled on benchmark mode.\n"));
        }
        static const TCHAR *RAW_FORMAT = _T("raw");
        static const size_t RAW_FORMAT_LEN = _tcslen(RAW_FORMAT) + 1;
        pParams->pAVMuxOutputFormat = (TCHAR *)realloc(pParams->pAVMuxOutputFormat, RAW_FORMAT_LEN * sizeof(RAW_FORMAT[0]));
        _tcscpy_s(pParams->pAVMuxOutputFormat, RAW_FORMAT_LEN, RAW_FORMAT);
        PrintMes(RGY_LOG_DEBUG, _T("Param adjusted for benchmark mode.\n"));
    }

    //メモリの指定が自動の場合、出力コーデックがrawなら、systemメモリを自動的に使用する
    if (HW_MEMORY == (pParams->memType & HW_MEMORY) && pParams->CodecId == MFX_CODEC_RAW) {
        PrintMes(RGY_LOG_DEBUG, _T("Automatically selecting system memory for output raw frames.\n"));
        pParams->memType = SYSTEM_MEMORY;
    }
    if (true) {
        m_pPerfMonitor = std::unique_ptr<CPerfMonitor>(new CPerfMonitor());
        const bool bLogOutput = pParams->nPerfMonitorSelect || pParams->nPerfMonitorSelectMatplot;
        tstring perfMonLog;
        if (bLogOutput) {
            perfMonLog = tstring(pParams->strDstFile) + _T("_perf.csv");
        }
        if (m_pPerfMonitor->init(perfMonLog.c_str(), pParams->pPythonPath, (bLogOutput) ? pParams->nPerfMonitorInterval : 1000,
            (int)pParams->nPerfMonitorSelect, (int)pParams->nPerfMonitorSelectMatplot,
#if defined(_WIN32) || defined(_WIN64)
            std::unique_ptr<void, handle_deleter>(OpenThread(SYNCHRONIZE | THREAD_QUERY_INFORMATION, false, GetCurrentThreadId()), handle_deleter()),
#else
            nullptr,
#endif
            m_pQSVLog, nullptr)) {
            PrintMes(RGY_LOG_WARN, _T("Failed to initialize performance monitor, disabled.\n"));
            m_pPerfMonitor.reset();
        }
    }

    m_nMFXThreads = pParams->nSessionThreads;
    m_nAVSyncMode = pParams->nAVSyncMode;

    sts = InitSessionInitParam(pParams->nSessionThreads, pParams->nSessionThreadPriority);
    if (sts < MFX_ERR_NONE) return sts;

    sts = InitInput(pParams);
    if (sts < MFX_ERR_NONE) return sts;

    sts = CheckParam(pParams);
    if (sts != MFX_ERR_NONE) return sts;

    sts = m_EncThread.Init(pParams->nInputBufSize);
    QSV_ERR_MES(sts, _T("Failed to allocate memory for thread control."));

    sts = InitSession(true, pParams->memType);
    QSV_ERR_MES(sts, _T("Failed to initialize encode session."));

    m_SessionPlugins = std::unique_ptr<CSessionPlugins>(new CSessionPlugins(m_mfxSession));

    sts = CreateAllocator();
    if (sts < MFX_ERR_NONE) return sts;

    sts = InitMfxDecParams(pParams);
    if (sts < MFX_ERR_NONE) return sts;

    sts = InitMfxEncParams(pParams);
    if (sts < MFX_ERR_NONE) return sts;

    sts = InitMfxVppParams(pParams);
    if (sts < MFX_ERR_NONE) return sts;

    sts = CreateVppExtBuffers(pParams);
    if (sts < MFX_ERR_NONE) return sts;

    sts = InitVppPrePlugins(pParams);
    if (sts < MFX_ERR_NONE) return sts;

    sts = InitVppPostPlugins(pParams);
    if (sts < MFX_ERR_NONE) return sts;

    sts = InitOutput(pParams);
    if (sts < MFX_ERR_NONE) return sts;

    // Create enc (Not necessary because it is not encoded for raw output)
    if (pParams->CodecId != MFX_CODEC_RAW) {
        m_pmfxENC.reset(new MFXVideoENCODE(m_mfxSession));
        if (!m_pmfxENC) {
            return MFX_ERR_MEMORY_ALLOC;
        }
    }


    //Create a vpp if necessary
    if (   pParams->nWidth  != pParams->nDstWidth
        || pParams->nHeight != pParams->nDstHeight
        || m_mfxVppParams.vpp.In.FourCC         != m_mfxVppParams.vpp.Out.FourCC
        || m_mfxVppParams.vpp.In.BitDepthLuma   != m_mfxVppParams.vpp.Out.BitDepthLuma
        || m_mfxVppParams.vpp.In.BitDepthChroma != m_mfxVppParams.vpp.Out.BitDepthChroma
        || m_mfxVppParams.NumExtParam > 1
        || pParams->vpp.deinterlace) {
        PrintMes(RGY_LOG_DEBUG, _T("Vpp Enabled...\n"));
        m_pmfxVPP.reset(new MFXVideoVPP(m_mfxSession));
        if (!m_pmfxVPP) {
            return MFX_ERR_MEMORY_ALLOC;
        }
    }
    if (m_mfxVppParams.vpp.In.FourCC != m_mfxVppParams.vpp.Out.FourCC) {
        tstring mes = strsprintf(_T("ColorFmtConvertion: %s -> %s\n"), ColorFormatToStr(m_mfxVppParams.vpp.In.FourCC), ColorFormatToStr(m_mfxVppParams.vpp.Out.FourCC));
        PrintMes(RGY_LOG_DEBUG, _T("Vpp Enabled: %s\n"), mes.c_str());
        VppExtMes += mes;
    }
    if (pParams->nWidth  != pParams->nDstWidth ||
        pParams->nHeight != pParams->nDstHeight) {
        tstring mes = strsprintf(_T("Resizer, %dx%d -> %dx%d"), pParams->nWidth, pParams->nHeight, pParams->nDstWidth, pParams->nDstHeight);
        if (pParams->vpp.scalingQuality != MFX_SCALING_MODE_DEFAULT) {
            mes += tstring(_T(" (")) + get_chr_from_value(list_vpp_scaling_quality, pParams->vpp.scalingQuality) + _T(")");
        }
        PrintMes(RGY_LOG_DEBUG, _T("Vpp Enabled: %s\n"), mes.c_str());
        VppExtMes += mes;
    }

    const int nPipelineElements = !!m_pmfxDEC + !!m_pmfxVPP + !!m_pmfxENC + (int)m_VppPrePlugins.size() + (int)m_VppPostPlugins.size();
    if (nPipelineElements == 0) {
        PrintMes(RGY_LOG_ERROR, _T("None of the pipeline element (DEC,VPP,ENC) are activated!\n"));
        return MFX_ERR_INVALID_VIDEO_PARAM;
    }
    PrintMes(RGY_LOG_DEBUG, _T("pipeline element count: %d\n"), nPipelineElements);

    m_nProcSpeedLimit = pParams->nProcSpeedLimit;
    m_nAsyncDepth = (mfxU16)clamp_param_int(pParams->nAsyncDepth, 0, QSV_ASYNC_DEPTH_MAX, _T("async-depth"));
    if (m_nAsyncDepth == 0) {
        m_nAsyncDepth = (mfxU16)(std::min)(QSV_DEFAULT_ASYNC_DEPTH + (nPipelineElements - 1), 8);
        PrintMes(RGY_LOG_DEBUG, _T("async depth automatically set to %d\n"), m_nAsyncDepth);
    }

#if defined(_WIN32) || defined(_WIN64)
    if (!pParams->bDisableTimerPeriodTuning) {
        m_bTimerPeriodTuning = true;
        timeBeginPeriod(1);
        PrintMes(RGY_LOG_DEBUG, _T("timeBeginPeriod(1)\n"));
    }
#endif //#if defined(_WIN32) || defined(_WIN64)

    sts = ResetMFXComponents(pParams);
    if (sts < MFX_ERR_NONE) return sts;

    return MFX_ERR_NONE;
}

void CQSVPipeline::Close() {
    PrintMes(RGY_LOG_DEBUG, _T("Closing pipeline...\n"));
    //PrintMes(RGY_LOG_INFO, _T("Frame number: %hd\r"), m_pFileWriter.m_nProcessedFramesNum);

    PrintMes(RGY_LOG_DEBUG, _T("Closing enc status...\n"));
    m_pEncSatusInfo.reset();

    PrintMes(RGY_LOG_DEBUG, _T("Closing m_EncThread...\n"));
    m_EncThread.Close();

    PrintMes(RGY_LOG_DEBUG, _T("Closing Plugins...\n"));
    m_SessionPlugins.reset();

    m_trimParam.list.clear();
    m_trimParam.offset = 0;

    PrintMes(RGY_LOG_DEBUG, _T("Closing m_pmfxDEC/ENC/VPP...\n"));
    m_pmfxDEC.reset();
    m_pmfxENC.reset();
    m_pmfxVPP.reset();
    m_VppPrePlugins.clear();
    m_VppPostPlugins.clear();

#if ENABLE_MVC_ENCODING
    FreeMVCSeqDesc();
#endif
    FreeVppDoNotUse();

    m_VppExtParams.clear();
    m_EncExtParams.clear();
    m_VppDoNotUseList.clear();
    m_VppDoUseList.clear();
    m_VppExtParams.clear();
    VppExtMes.clear();

    m_DecInputBitstream.clear();

    PrintMes(RGY_LOG_DEBUG, _T("Closing TaskPool...\n"));
    m_TaskPool.Close();

    PrintMes(RGY_LOG_DEBUG, _T("Closing mfxSession...\n"));
    m_mfxSession.Close();

    PrintMes(RGY_LOG_DEBUG, _T("DeleteFrames...\n"));
    DeleteFrames();

    PrintMes(RGY_LOG_DEBUG, _T("DeleteAllocator...\n"));
    // allocator if used as external for MediaSDK must be deleted after SDK components
    DeleteAllocator();

    PrintMes(RGY_LOG_DEBUG, _T("Closing audio readers (if used)...\n"));
    m_AudioReaders.clear();

    for (auto pWriter : m_pFileWriterListAudio) {
        if (pWriter) {
            if (pWriter != m_pFileWriter) {
                pWriter->Close();
                pWriter.reset();
            }
        }
    }
    m_pFileWriterListAudio.clear();

    PrintMes(RGY_LOG_DEBUG, _T("Closing writer...\n"));
    if (m_pFileWriter) {
        m_pFileWriter->Close();
        m_pFileWriter.reset();
    }

    PrintMes(RGY_LOG_DEBUG, _T("Closing reader...\n"));
    if (m_pFileReader) {
        m_pFileReader->Close();
        m_pFileReader.reset();
    }
#if defined(_WIN32) || defined(_WIN64)
    if (m_bTimerPeriodTuning) {
        timeEndPeriod(1);
        m_bTimerPeriodTuning = false;
        PrintMes(RGY_LOG_DEBUG, _T("timeEndPeriod(1)\n"));
    }
#endif //#if defined(_WIN32) || defined(_WIN64)

    PrintMes(RGY_LOG_DEBUG, _T("Closing perf monitor...\n"));
    m_pPerfMonitor.reset();

    m_nMFXThreads = -1;
    m_pAbortByUser = NULL;
    m_nAVSyncMode = RGY_AVSYNC_ASSUME_CFR;
    m_nProcSpeedLimit = 0;
    PrintMes(RGY_LOG_DEBUG, _T("Closed pipeline.\n"));
    if (m_pQSVLog.get() != nullptr) {
        m_pQSVLog->writeFileFooter();
        m_pQSVLog.reset();
    }
}

mfxStatus CQSVPipeline::ResetMFXComponents(sInputParams* pParams) {
    if (!pParams) {
        return MFX_ERR_NULL_PTR;
    }

    mfxStatus sts = MFX_ERR_NONE;
    PrintMes(RGY_LOG_DEBUG, _T("ResetMFXComponents: Start...\n"));

    if (m_pmfxENC) {
        sts = m_pmfxENC->Close();
        QSV_IGNORE_STS(sts, MFX_ERR_NOT_INITIALIZED);
        QSV_ERR_MES(sts, _T("Failed to reset encoder (fail on closing)."));
        PrintMes(RGY_LOG_DEBUG, _T("ResetMFXComponents: Enc closed.\n"));
    }

    if (m_pmfxVPP) {
        sts = m_pmfxVPP->Close();
        QSV_IGNORE_STS(sts, MFX_ERR_NOT_INITIALIZED);
        QSV_ERR_MES(sts, _T("Failed to reset vpp (fail on closing)."));
        PrintMes(RGY_LOG_DEBUG, _T("ResetMFXComponents: Vpp closed.\n"));
    }

    if (m_pmfxDEC) {
        sts = m_pmfxDEC->Close();
        QSV_IGNORE_STS(sts, MFX_ERR_NOT_INITIALIZED);
        QSV_ERR_MES(sts, _T("Failed to reset decoder (fail on closing)."));
        PrintMes(RGY_LOG_DEBUG, _T("ResetMFXComponents: Dec closed.\n"));
    }

    // free allocated frames
    DeleteFrames();
    PrintMes(RGY_LOG_DEBUG, _T("ResetMFXComponents: Frames deleted.\n"));

    m_TaskPool.Close();

    sts = AllocFrames();
    if (sts < MFX_ERR_NONE) return sts;
    PrintMes(RGY_LOG_DEBUG, _T("ResetMFXComponents: Frames allocated.\n"));

    //MediaSDK内のエラーをRGY_LOG_DEBUG以下の時以外には一時的に無視するようにする。
    //RGY_LOG_DEBUG以下の時にも、「無視できるエラーが発生するかもしれない」ことをログに残す。
    auto logIgnoreMFXLibraryInternalErrors = [this]() {
        const auto log_level = m_pQSVLog->getLogLevel();
        if (log_level >= RGY_LOG_MORE) {
            m_pQSVLog->setLogLevel(RGY_LOG_QUIET); //一時的にエラーを無視
        } else {
            PrintMes(RGY_LOG_DEBUG, _T("ResetMFXComponents: there might be error below, but it might be internal error which could be ignored.\n"));
        }
        return log_level;
    };

    if (m_pmfxENC) {
        const auto log_level = logIgnoreMFXLibraryInternalErrors();
        m_prmSetIn.vidprm = m_mfxEncParams;
        m_prmSetIn.cop    = m_CodingOption;
        m_prmSetIn.cop2   = m_CodingOption2;
        m_prmSetIn.cop3   = m_CodingOption3;
        m_prmSetIn.hevc   = m_ExtHEVCParam;
        sts = m_pmfxENC->Init(&m_mfxEncParams);
        m_pQSVLog->setLogLevel(log_level);
        if (MFX_WRN_PARTIAL_ACCELERATION == sts) {
            PrintMes(RGY_LOG_WARN, _T("ResetMFXComponents: partial acceleration on Encoding.\n"));
            QSV_IGNORE_STS(sts, MFX_WRN_PARTIAL_ACCELERATION);
        }
        QSV_ERR_MES(sts, _T("Failed to initialize encoder."));
        PrintMes(RGY_LOG_DEBUG, _T("ResetMFXComponents: Enc initialized.\n"));
    }

    if (m_pmfxVPP) {
        //ここでの内部エラーは最終的にはmfxライブラリ内部で解決される場合もあり、これをログ上は無視するようにする。
        //具体的にはSandybridgeでd3dメモリでVPPを使用する際、m_pmfxVPP->Init()実行時に
        //"QSVAllocator: Failed CheckRequestType: undeveloped feature"と表示されるが、
        //m_pmfxVPP->Initの戻り値自体はMFX_ERR_NONEであるので、内部で解決されたものと思われる。
        //もちろん、m_pmfxVPP->Init自体がエラーを返した時にはきちんとログに残す。
        const auto log_level = logIgnoreMFXLibraryInternalErrors();
        sts = m_pmfxVPP->Init(&m_mfxVppParams);
        m_pQSVLog->setLogLevel(log_level);
        if (MFX_WRN_PARTIAL_ACCELERATION == sts) {
            PrintMes(RGY_LOG_WARN, _T("ResetMFXComponents: partial acceleration on vpp.\n"));
            QSV_IGNORE_STS(sts, MFX_WRN_PARTIAL_ACCELERATION);
        }
        QSV_ERR_MES(sts, _T("Failed to initialize vpp."));
        PrintMes(RGY_LOG_DEBUG, _T("ResetMFXComponents: Vpp initialized.\n"));
    }

    if (m_pmfxDEC) {
        const auto log_level = logIgnoreMFXLibraryInternalErrors();
        sts = m_pmfxDEC->Init(&m_mfxDecParams);
        m_pQSVLog->setLogLevel(log_level);
        if (MFX_WRN_PARTIAL_ACCELERATION == sts) {
            PrintMes(RGY_LOG_WARN, _T("ResetMFXComponents: partial acceleration on decoding.\n"));
            QSV_IGNORE_STS(sts, MFX_WRN_PARTIAL_ACCELERATION);
        }
        QSV_ERR_MES(sts, _T("Failed to initialize decoder.\n"));
        PrintMes(RGY_LOG_DEBUG, _T("ResetMFXComponents: Dec initialized.\n"));
    }

    mfxU32 nEncodedDataBufferSize = m_mfxEncParams.mfx.FrameInfo.Width * m_mfxEncParams.mfx.FrameInfo.Height * 4;
    PrintMes(RGY_LOG_DEBUG, _T("ResetMFXComponents: Creating task pool, poolSize %d, bufsize %d KB.\n"), m_nAsyncDepth, nEncodedDataBufferSize >> 10);
    sts = m_TaskPool.Init(&m_mfxSession, m_pMFXAllocator.get(), m_pFileWriter, m_nAsyncDepth, nEncodedDataBufferSize);
    QSV_ERR_MES(sts, _T("Failed to initialize task pool for encoding."));
    PrintMes(RGY_LOG_DEBUG, _T("ResetMFXComponents: Created task pool.\n"));

    return MFX_ERR_NONE;
}

mfxStatus CQSVPipeline::AllocateSufficientBuffer(mfxBitstream *pBS) {
    if (!pBS) {
        return MFX_ERR_NULL_PTR;
    }

    mfxVideoParam par = { 0 };
    mfxStatus sts = m_pmfxENC->GetVideoParam(&par);
    QSV_ERR_MES(sts, _T("Failed to get required output buffer size from encoder."));

    sts = mfxBitstreamExtend(pBS, par.mfx.BufferSizeInKB * 1000 * (std::max)(1, (int)par.mfx.BRCParamMultiplier));
    if (sts != MFX_ERR_NONE) {
        PrintMes(RGY_LOG_ERROR, _T("Failed to allocate memory for output bufffer: %s\n"), get_err_mes(sts));
        mfxBitstreamClear(pBS);
        return sts;
    }

    return MFX_ERR_NONE;
}

//この関数がMFX_ERR_NONE以外を返すことでRunEncodeは終了処理に入る
mfxStatus CQSVPipeline::GetNextFrame(mfxFrameSurface1 **pSurface) {
    const int inputBufIdx = m_EncThread.m_nFrameGet % m_EncThread.m_nFrameBuffer;
    sInputBufSys *pInputBuf = &m_EncThread.m_InputBuf[inputBufIdx];

    //_ftprintf(stderr, "GetNextFrame: wait for %d\n", m_EncThread.m_nFrameGet);
    //_ftprintf(stderr, "wait for heInputDone, %d\n", m_EncThread.m_nFrameGet);
    PrintMes(RGY_LOG_TRACE, _T("Enc Thread: Wait Done %d.\n"), m_EncThread.m_nFrameGet);
    //HWデコードの場合、本来ここで待機する必要はなく、またここでRGY_ERR_MORE_DATAを返す必要もない
    while (WaitForSingleObject(pInputBuf->heInputDone, (m_pFileReader->getInputCodec() == RGY_CODEC_UNKNOWN) ? INFINITE : 1) == WAIT_TIMEOUT) {
        //ここに入るのはHWデコードの場合のみ
        //HWデコードの時は、本来このロックはフレーム読み込みには使用しておらず、
        //CQSVPipeline::Run()内のm_pFileReader->LoadNextFrame()による進捗管理のために行っているに過ぎない
        //そのためCQSVPipeline::Run()が終了している、
        //すなわちm_EncThread.m_stsThreadがMFX_ERR_MORE_DATAであれば、
        //特に待機せずMFX_ERR_NONEを返して終了する
        if (m_EncThread.m_stsThread == MFX_ERR_MORE_DATA) {
            return MFX_ERR_NONE;
        }
    }
    //エラー?中断要求などでの終了
    if (m_EncThread.m_bthForceAbort) {
        PrintMes(RGY_LOG_DEBUG, _T("GetNextFrame: Encode Aborted...\n"));
        return m_EncThread.m_stsThread;
    }
    //読み込み完了による終了
    if (m_EncThread.m_stsThread == MFX_ERR_MORE_DATA && m_EncThread.m_nFrameGet == m_pEncSatusInfo->m_sData.frameIn) {
        PrintMes(RGY_LOG_DEBUG, _T("GetNextFrame: Frame read finished.\n"));
        return m_EncThread.m_stsThread;
    }
    //フレーム読み込みでない場合は、フレーム関連の処理は行わない
    if (m_pFileReader->getInputCodec() == RGY_CODEC_UNKNOWN) {
        *pSurface = (mfxFrameSurface1 *)pInputBuf->pFrameSurface;
        (*pSurface)->Data.TimeStamp = inputBufIdx;
        (*pSurface)->Data.Locked = FALSE;
        m_EncThread.m_nFrameGet++;
    }
    return MFX_ERR_NONE;
}

mfxStatus CQSVPipeline::SetNextSurface(mfxFrameSurface1 *pSurface) {
    const int inputBufIdx = m_EncThread.m_nFrameSet % m_EncThread.m_nFrameBuffer;
    sInputBufSys *pInputBuf = &m_EncThread.m_InputBuf[inputBufIdx];
    //フレーム読み込みでない場合は、フレーム関連の処理は行わない
    if (m_pFileReader->getInputCodec() == RGY_CODEC_UNKNOWN) {
        //_ftprintf(stderr, "Set heInputStart: %d\n", m_EncThread.m_nFrameSet);
        pSurface->Data.Locked = TRUE;
        //_ftprintf(stderr, "set surface %d, set event heInputStart %d\n", pSurface, m_EncThread.m_nFrameSet);
        pInputBuf->pFrameSurface = (RGYFrame *)pSurface;
    }
    SetEvent(pInputBuf->heInputStart);
    PrintMes(RGY_LOG_TRACE, _T("Enc Thread: Set Start %d.\n"), m_EncThread.m_nFrameSet);
    m_EncThread.m_nFrameSet++;
    return MFX_ERR_NONE;
}

mfxStatus CQSVPipeline::GetFreeTask(QSVTask **ppTask) {
    mfxStatus sts = MFX_ERR_NONE;

    sts = m_TaskPool.GetFreeTask(ppTask);
    if (MFX_ERR_NOT_FOUND == sts) {
        sts = SynchronizeFirstTask();
        QSV_ERR_MES(sts, _T("Failed to SynchronizeFirstTask"));

        // try again
        sts = m_TaskPool.GetFreeTask(ppTask);
    }

    return sts;
}

mfxStatus CQSVPipeline::SynchronizeFirstTask() {
    mfxStatus sts = m_TaskPool.SynchronizeFirstTask();

    return sts;
}

void CQSVPipeline::RunEncThreadLauncher(void *pParam) {
    reinterpret_cast<CQSVPipeline*>(pParam)->RunEncode();
}

mfxStatus CQSVPipeline::Run() {
    return Run(0);
}

mfxStatus CQSVPipeline::Run(size_t SubThreadAffinityMask) {
    mfxStatus sts = MFX_ERR_NONE;

#if defined(_WIN32) || defined(_WIN64)
    TCHAR handleEvent[256];
    _stprintf_s(handleEvent, QSVENCC_ABORT_EVENT, GetCurrentProcessId());
    m_heAbort = unique_ptr<std::remove_pointer<HANDLE>::type, handle_deleter>((HANDLE)CreateEvent(nullptr, TRUE, FALSE, handleEvent));
#endif

    PrintMes(RGY_LOG_DEBUG, _T("Main Thread: Lauching encode thread...\n"));
    sts = m_EncThread.RunEncFuncbyThread(&RunEncThreadLauncher, this, SubThreadAffinityMask);
    QSV_ERR_MES(sts, _T("Failed to start encode thread."));
    PrintMes(RGY_LOG_DEBUG, _T("Main Thread: Starting Encode...\n"));

    const int bufferSize = m_EncThread.m_nFrameBuffer;
    sInputBufSys *pArrayInputBuf = m_EncThread.m_InputBuf;
    sInputBufSys *pInputBuf;
    //入力ループ
    if (m_pFileReader->getInputCodec() == RGY_CODEC_UNKNOWN) {
        for (int i = 0; sts == MFX_ERR_NONE; i++) {
            pInputBuf = &pArrayInputBuf[i % bufferSize];

            //空いているフレームがセットされるのを待機
            PrintMes(RGY_LOG_TRACE, _T("Main Thread: Wait Start %d.\n"), i);
            while (WAIT_TIMEOUT == WaitForSingleObject(pInputBuf->heInputStart, 5000)) {
                //エンコードスレッドが異常終了していたら、それを検知してこちらも終了
                if (!CheckThreadAlive(m_EncThread.GetHandleEncThread())) {
                    PrintMes(RGY_LOG_ERROR, _T("error at encode thread.\n"));
                    sts = MFX_ERR_INVALID_HANDLE;
                }
            }

            //フレームを読み込み
            PrintMes(RGY_LOG_TRACE, _T("Main Thread: LoadNextFrame %d.\n"), i);
            if (sts == MFX_ERR_NONE) {
                sts = err_to_mfx(m_pFileReader->LoadNextFrame(pInputBuf->pFrameSurface));
            }
            if (m_pAbortByUser != nullptr && *m_pAbortByUser) {
                PrintMes(RGY_LOG_INFO, _T("                                                                              \r"));
                sts = MFX_ERR_ABORTED;
            } else if (sts == MFX_ERR_MORE_DATA) {
                m_EncThread.m_stsThread = sts;
            }

            //フレームの読み込み終了を通知
            SetEvent(pInputBuf->heInputDone);
            PrintMes(RGY_LOG_TRACE, _T("Main Thread: Set Done %d.\n"), i);
        }
    } else {
        while (sts == MFX_ERR_NONE) {
            std::this_thread::sleep_for(std::chrono::milliseconds(800));
            if (!CheckThreadAlive(m_EncThread.GetHandleEncThread())) {
                //読み込みが完了しているか、確認
                if (MFX_ERR_MORE_DATA == err_to_mfx(m_pFileReader->LoadNextFrame(nullptr))) {
                    //正常に読み込みを終了
                    sts = MFX_ERR_NONE;
                } else {
                    PrintMes(RGY_LOG_ERROR, _T("error at encode thread.\n"));
                    sts = MFX_ERR_UNKNOWN;
                }
                break;
            }
            //進捗表示 & 読み込み状況確認
            sts = err_to_mfx(m_pFileReader->LoadNextFrame(nullptr));
            if (m_pAbortByUser != nullptr && *m_pAbortByUser) {
                PrintMes(RGY_LOG_INFO, _T("                                                                              \r"));
                sts = MFX_ERR_ABORTED;
            } else if (sts == MFX_ERR_MORE_DATA) {
                m_EncThread.m_stsThread = sts;
            }
        }
    }
    m_EncThread.WaitToFinish(sts, m_pQSVLog);
    PrintMes(RGY_LOG_DEBUG, _T("Main Thread: Finished Main Loop...\n"));

    sts = (std::min)(sts, m_EncThread.m_stsThread);
    QSV_IGNORE_STS(sts, MFX_ERR_MORE_DATA);

    m_EncThread.Close();

    //ここでファイル出力の完了を確認してから、結果表示(m_pEncSatusInfo->WriteResults)を行う
    m_pFileWriter->WaitFin();
    m_pEncSatusInfo->WriteResults();

    PrintMes(RGY_LOG_DEBUG, _T("Main Thread: finished.\n"));
    return sts;
}

mfxStatus CQSVPipeline::RunEncode() {
    PrintMes(RGY_LOG_DEBUG, _T("Encode Thread: Starting Encode...\n"));

    mfxStatus sts = MFX_ERR_NONE;

    mfxFrameSurface1 *pSurfInputBuf = nullptr;
    mfxFrameSurface1 *pSurfEncIn = nullptr;
    mfxFrameSurface1 *pSurfVppIn = nullptr;
    mfxFrameSurface1 *pSurfCheckPts = nullptr; //checkptsから出てきて、他の要素に投入するフレーム / 投入後、ロックを解除する必要がある
    vector<mfxFrameSurface1 *>pSurfVppPreFilter(m_VppPrePlugins.size() + 1, nullptr);
    vector<mfxFrameSurface1 *>pSurfVppPostFilter(m_VppPostPlugins.size() + 1, nullptr);
    mfxFrameSurface1 *pNextFrame = nullptr;
    mfxSyncPoint lastSyncP = nullptr;
    bool bVppRequireMoreFrame = false;
    int nFramePutToEncoder = 0; //エンコーダに投入したフレーム数 (TimeStamp計算用)

    QSVTask *pCurrentTask = nullptr; //現在のタスクへのポインタ
    int nEncSurfIdx = -1; //使用するフレームのインデックス encoder input (vpp output)
    int nVppSurfIdx = -1; //使用するフレームのインデックス vpp input

    bool bVppMultipleOutput = false;  //bob化などの際にvppが余分にフレームを出力するフラグ
    bool bCheckPtsMultipleOutput = false; //dorcecfrなどにともなって、checkptsが余分にフレームを出力するフラグ

    int nLastCheckPtsFrame = -1;
    int nInputFrameCount = -1; //入力されたフレームの数 (最初のフレームが0になるよう、-1で初期化する)  Trimの反映に使用する

    struct frameData {
        mfxSyncPoint syncp;
        mfxFrameSurface1 *pSurface;
        int64_t timestamp;
    };
    std::deque<frameData> qDecodeFrames; //デコードされて出てきたsyncpとframe, timestamp

    const auto inputFrameInfo = m_pFileReader->GetInputFrameInfo();
    //QSVEncでは、常にTimestampは90kHzベースとする(m_outputTimebaseに設定済み)
    int64_t nOutFirstPts = -1;  //入力のptsに対する補正 (スケール: calcTimebase)
    int64_t nOutEstimatedPts = 0; //固定fpsを仮定した時のfps (スケール: calcTimebase)
    const auto hw_timebase = rgy_rational<int>(1, HW_TIMEBASE);
    const auto inputFpsTimebase = rgy_rational<int>((int)inputFrameInfo.fpsD, (int)inputFrameInfo.fpsN);
    const auto calcTimebase = rgy_rational<int>(1, 4) * inputFpsTimebase;
    m_nAVSyncMode = RGY_AVSYNC_ASSUME_CFR;

    const auto nOutFrameDuration = std::max<int64_t>(1, rational_rescalex(1, inputFpsTimebase, calcTimebase)); //固定fpsを仮定した時の1フレームのduration (スケール: calcTimebase)

    CProcSpeedControl speedCtrl(m_nProcSpeedLimit);

    m_pEncSatusInfo->SetStart();

    sts = MFX_ERR_NONE;

    auto get_all_free_surface =[&](mfxFrameSurface1 *pSurfEncInput) {
        //パイプラインの後ろからたどっていく
        pSurfInputBuf = pSurfEncInput; //pSurfEncInにはパイプラインを後ろからたどった順にフレームポインタを更新していく
        pSurfVppPostFilter[m_VppPostPlugins.size()] = pSurfInputBuf; //pSurfVppPreFilterの最後はその直前のステップのフレームに出力される
        for (int i_filter = (int)m_VppPostPlugins.size()-1; i_filter >= 0; i_filter--) {
            int freeSurfIdx = GetFreeSurface(m_VppPostPlugins[i_filter]->m_pPluginSurfaces.get(), m_VppPostPlugins[i_filter]->m_PluginResponse.NumFrameActual);
            if (freeSurfIdx == MSDK_INVALID_SURF_IDX) {
                PrintMes(RGY_LOG_ERROR, _T("Failed to get free surface for vpp post.\n"));
                return MFX_ERR_MEMORY_ALLOC;
            }
            pSurfVppPostFilter[i_filter] = &m_VppPostPlugins[i_filter]->m_pPluginSurfaces[freeSurfIdx];
            pSurfInputBuf = pSurfVppPostFilter[i_filter];
        }
        //vppが有効ならvpp用のフレームも用意する
        if (m_pmfxVPP) {
            //空いているフレームバッファを取得、空いていない場合は待機して、空くまで待ってから取得
            nVppSurfIdx = GetFreeSurface(m_pVppSurfaces.data(), m_VppResponse.NumFrameActual);
            if (nVppSurfIdx == MSDK_INVALID_SURF_IDX) {
                PrintMes(RGY_LOG_ERROR, _T("Failed to get free surface for vpp.\n"));
                return MFX_ERR_MEMORY_ALLOC;
            }
            pSurfVppIn = &m_pVppSurfaces[nVppSurfIdx];
            pSurfInputBuf = pSurfVppIn;
        }
        pSurfVppPreFilter[m_VppPrePlugins.size()] = pSurfInputBuf; //pSurfVppPreFilterの最後はその直前のステップのフレームに出力される
        for (int i_filter = (int)m_VppPrePlugins.size()-1; i_filter >= 0; i_filter--) {
            int freeSurfIdx = GetFreeSurface(m_VppPrePlugins[i_filter]->m_pPluginSurfaces.get(), m_VppPrePlugins[i_filter]->m_PluginResponse.NumFrameActual);
            if (freeSurfIdx == MSDK_INVALID_SURF_IDX) {
                PrintMes(RGY_LOG_ERROR, _T("Failed to get free surface for vpp pre.\n"));
                return MFX_ERR_MEMORY_ALLOC;
            }
            pSurfVppPreFilter[i_filter] = &m_VppPrePlugins[i_filter]->m_pPluginSurfaces[freeSurfIdx];
            pSurfInputBuf = pSurfVppPreFilter[i_filter];
        }
        //最終的にpSurfInputBufには一番最初のステップのフレームポインタが入る
        return MFX_ERR_NONE;
    };

    auto set_surface_to_input_buffer = [&]() {
        mfxStatus sts_set_buffer = MFX_ERR_NONE;
        for (int i = 0; i < m_EncThread.m_nFrameBuffer; i++) {
            get_all_free_surface(&m_pEncSurfaces[GetFreeSurface(m_pEncSurfaces.data(), m_EncResponse.NumFrameActual)]);

            //フレーム読み込みでない場合には、ここでロックする必要はない
            if (m_bExternalAlloc && m_pFileReader->getInputCodec() == RGY_CODEC_UNKNOWN) {
                if (MFX_ERR_NONE != (sts_set_buffer = m_pMFXAllocator->Lock(m_pMFXAllocator->pthis, pSurfInputBuf->Data.MemId, &(pSurfInputBuf->Data))))
                    break;
        }
        //空いているフレームを読み込み側に渡し、該当フレームの読み込み開始イベントをSetする(pInputBuf->heInputStart)
        SetNextSurface(pSurfInputBuf);
    }
        return sts_set_buffer;
    };

    //先読みバッファ用フレームを読み込み側に提供する
    set_surface_to_input_buffer();
    PrintMes(RGY_LOG_DEBUG, _T("Encode Thread: Set surface to input buffer...\n"));

    auto copy_crop_info = [](mfxFrameSurface1 *dst, const mfxFrameInfo *src) {
        if (NULL != dst) {
            dst->Info.CropX = src->CropX;
            dst->Info.CropY = src->CropY;
            dst->Info.CropW = src->CropW;
            dst->Info.CropH = src->CropH;
        }
    };

    auto extract_audio = [&]() {
        RGY_ERR ret = RGY_ERR_NONE;
        return ret;
    };

    auto decode_one_frame = [&](bool getNextBitstream) {
        mfxStatus dec_sts = MFX_ERR_NONE;
        if (m_pmfxDEC) {
            if (getNextBitstream
                //m_DecInputBitstream.size() > 0のときにbitstreamを連結してしまうと
                //環境によっては正常にフレームが取り出せなくなることがある
                //これを避けるため、m_DecInputBitstream.size() == 0のときのみbitstreamを取得する
                //これにより GetNextFrame / SetNextFrame の回数が異常となり、
                //GetNextFrameのロックが抜けれらなくなる場合がある。
                //HWデコード時、本来GetNextFrameのロックは必要ないので、
                //これを無視する実装も併せて行った。
                && (m_DecInputBitstream.size() <= 1)) {
                //この関数がMFX_ERR_NONE以外を返せば、入力ビットストリームは終了
                auto ret = m_pFileReader->GetNextBitstream(&m_DecInputBitstream);
                if (ret == RGY_ERR_MORE_BITSTREAM) {
                    return err_to_mfx(ret); //入力ビットストリームは終了
                }
                RGY_ERR_MES(ret, _T("Error on getting video bitstream."));
            }

            getNextBitstream |= m_DecInputBitstream.size() > 0;

            //デコードも行う場合は、デコード用のフレームをpSurfVppInかpSurfEncInから受け取る
            mfxFrameSurface1 *pSurfDecWork = pNextFrame;
            mfxFrameSurface1 *pSurfDecOut = NULL;
            mfxBitstream *pInputBitstream = (getNextBitstream) ? &m_DecInputBitstream.bitstream() : nullptr;

            if (!m_mfxDecParams.mfx.FrameInfo.FourCC) {
                //デコード前には、デコード用のパラメータでFrameInfoを更新
                copy_crop_info(pSurfDecWork, &m_mfxDecParams.mfx.FrameInfo);
            }
            if (pInputBitstream != nullptr) {
                if (pInputBitstream->TimeStamp == (mfxU64)AV_NOPTS_VALUE) {
                    pInputBitstream->TimeStamp = (mfxU64)MFX_TIMESTAMP_UNKNOWN;
                }
                pInputBitstream->DecodeTimeStamp = MFX_TIMESTAMP_UNKNOWN;
            }
            pSurfDecWork->Data.TimeStamp = (mfxU64)MFX_TIMESTAMP_UNKNOWN;
            pSurfDecWork->Data.DataFlag |= MFX_FRAMEDATA_ORIGINAL_TIMESTAMP;

            for (int i = 0; ; i++) {
                const auto inputDataLen = (pInputBitstream) ? pInputBitstream->DataLength : 0;
                mfxSyncPoint DecSyncPoint = NULL;
                dec_sts = m_pmfxDEC->DecodeFrameAsync(pInputBitstream, pSurfDecWork, &pSurfDecOut, &DecSyncPoint);
                lastSyncP = DecSyncPoint;

                if (MFX_ERR_NONE < dec_sts && !DecSyncPoint) {
                    if (MFX_WRN_DEVICE_BUSY == dec_sts)
                        sleep_hybrid(i);
                    if (i > 1024 * 1024 * 30) {
                        PrintMes(RGY_LOG_ERROR, _T("device kept on busy for 30s, unknown error occurred.\n"));
                        return MFX_ERR_UNKNOWN;
                    }
                } else if (MFX_ERR_NONE < dec_sts && DecSyncPoint) {
                    dec_sts = MFX_ERR_NONE; //出力があれば、警告は無視する
                    break;
                } else if (dec_sts < MFX_ERR_NONE && (dec_sts != MFX_ERR_MORE_DATA && dec_sts != MFX_ERR_MORE_SURFACE)) {
                    PrintMes(RGY_LOG_ERROR, _T("DecodeFrameAsync error: %s.\n"), get_err_mes(dec_sts));
                    break;
                } else {
                    //pInputBitstreamの長さがDecodeFrameAsyncを経ても全く変わっていない場合は、そのデータは捨てる
                    //これを行わないとデコードが止まってしまう
                    if (dec_sts == MFX_ERR_MORE_DATA && pInputBitstream && pInputBitstream->DataLength == inputDataLen) {
                        PrintMes((inputDataLen >= 10) ? RGY_LOG_WARN : RGY_LOG_DEBUG,
                            _T("DecodeFrameAsync: removing %d bytes from input bitstream not read by decoder.\n"), inputDataLen);
                        pInputBitstream->DataLength = 0;
                        pInputBitstream->DataOffset = 0;
                    }
                    break;
                }
            }

            //次のステップのフレームをデコードの出力に設定
            pNextFrame = pSurfDecOut;
            nInputFrameCount += (pSurfDecOut != nullptr && lastSyncP != nullptr);
        } else {
            //デコードがオンでなくても、フレームは入力してるはずなので加算する
            //Trimの反映に使用する
            nInputFrameCount++;
        }
        return dec_sts;
    };

    int64_t prevPts = 0; //(calcTimebase基準)
    auto check_pts = [&]() {
        int64_t outDuration = nOutFrameDuration; //入力fpsに従ったduration (calcTimebase基準)
        int64_t outPts = nOutEstimatedPts; //(calcTimebase基準)
        nOutEstimatedPts += outDuration;
        prevPts = outPts;
        pNextFrame->Data.TimeStamp = rational_rescalex(outPts, calcTimebase, hw_timebase);
        pNextFrame->Data.DataFlag &= (~MFX_FRAMEDATA_ORIGINAL_TIMESTAMP);
        m_outputTimestamp.add(pNextFrame->Data.TimeStamp, rational_rescalex(outDuration, calcTimebase, hw_timebase));
        return MFX_ERR_NONE;
    };

    auto filter_one_frame = [&](const unique_ptr<CVPPPlugin>& filter, mfxFrameSurface1 **ppSurfIn, mfxFrameSurface1 **ppSurfOut) {
        mfxStatus filter_sts = MFX_ERR_NONE;
        mfxSyncPoint filterSyncPoint = NULL;

        for (int i = 0; ; i++) {
            mfxHDL *h1 = (mfxHDL *)ppSurfIn;
            mfxHDL *h2 = (mfxHDL *)ppSurfOut;

            filter_sts = MFXVideoUSER_ProcessFrameAsync(filter->getSession(), h1, 1, h2, 1, &filterSyncPoint);

            if (MFX_WRN_DEVICE_BUSY == filter_sts) {
                sleep_hybrid(i);
                if (i > 1024 * 1024 * 30) {
                    PrintMes(RGY_LOG_ERROR, _T("device kept on busy for 30s, unknown error occurred.\n"));
                    return MFX_ERR_UNKNOWN;
                }
            } else {
                break;
            }
        }
        // save the id of preceding vpp task which will produce input data for the encode task
        if (filterSyncPoint) {
            lastSyncP = filterSyncPoint;
            //pCurrentTask->vppSyncPoint.push_back(filterSyncPoint);
            filterSyncPoint = NULL;
        }
        return filter_sts;
    };

    auto vpp_one_frame =[&](mfxFrameSurface1* pSurfVppIn, mfxFrameSurface1* pSurfVppOut) {
        mfxStatus vpp_sts = MFX_ERR_NONE;
        if (m_pmfxVPP) {
            mfxSyncPoint VppSyncPoint = nullptr;
            bVppMultipleOutput = false;
            bVppRequireMoreFrame = false;

            //vpp前に、vpp用のパラメータでFrameInfoを更新
            copy_crop_info(pSurfVppIn, &m_mfxVppParams.mfx.FrameInfo);

            for (int i = 0; ; i++) {
                //bob化の際、pSurfVppInに連続で同じフレーム(同じtimestamp)を投入すると、
                //最初のフレームには設定したtimestamp、次のフレームにはMFX_TIMESTAMP_UNKNOWNが設定されて出てくる
                //特別pSurfVppOut側のTimestampを設定する必要はなさそう
                vpp_sts = m_pmfxVPP->RunFrameVPPAsync(pSurfVppIn, pSurfVppOut, NULL, &VppSyncPoint);
                lastSyncP = VppSyncPoint;

                if (MFX_ERR_NONE < vpp_sts && !VppSyncPoint) {
                    if (MFX_WRN_DEVICE_BUSY == vpp_sts)
                        sleep_hybrid(i);
                    if (i > 1024 * 1024 * 30) {
                        PrintMes(RGY_LOG_ERROR, _T("device kept on busy for 30s, unknown error occurred.\n"));
                        return MFX_ERR_UNKNOWN;
                    }
                } else if (MFX_ERR_NONE < vpp_sts && VppSyncPoint) {
                    vpp_sts = MFX_ERR_NONE;
                    break;
                } else {
                    break;
                }
            }

            if (MFX_ERR_MORE_DATA == vpp_sts) {
                bVppRequireMoreFrame = true;
            } else if (MFX_ERR_MORE_SURFACE == vpp_sts) {
                bVppMultipleOutput = true;
                vpp_sts = MFX_ERR_NONE;
            }

            if (VppSyncPoint) {
                pCurrentTask->vppSyncPoint.push_back(VppSyncPoint);
                VppSyncPoint = NULL;
                pNextFrame = pSurfVppOut;
            }
        }
        return vpp_sts;
    };

    auto encode_one_frame =[&](mfxFrameSurface1* pSurfEncIn) {
        if (m_pmfxENC == nullptr) {
            //エンコードが有効でない場合、このフレームデータを出力する
            //パイプラインの最後のSyncPointをセットする
            pCurrentTask->encSyncPoint = lastSyncP;
            //フレームデータが出力されるまで空きフレームとして使われないようLockを加算しておく
            //TaskのWriteBitstreamで減算され、解放される
            pSurfEncIn->Data.Locked++;
            //フレームのポインタを出力用にセット
            pCurrentTask->mfxSurf = pSurfEncIn;
            return MFX_ERR_NONE;
        }

        mfxStatus enc_sts = MFX_ERR_NONE;

        //以下の処理は
        if (pSurfEncIn) {
            nFramePutToEncoder++;
            //TimeStampをMFX_TIMESTAMP_UNKNOWNにしておくと、きちんと設定される
            pCurrentTask->mfxBS.TimeStamp = (uint64_t)MFX_TIMESTAMP_UNKNOWN;
            pCurrentTask->mfxBS.DecodeTimeStamp = (uint64_t)MFX_TIMESTAMP_UNKNOWN;
            //bob化の際に増えたフレームのTimeStampには、MFX_TIMESTAMP_UNKNOWNが設定されているのでこれを補間して修正する
            pSurfEncIn->Data.TimeStamp = (uint64_t)m_outputTimestamp.check(pSurfEncIn->Data.TimeStamp);
        }

        bool bDeviceBusy = false;
        for (int i = 0; ; i++) {
            enc_sts = m_pmfxENC->EncodeFrameAsync(nullptr, pSurfEncIn, &pCurrentTask->mfxBS, &pCurrentTask->encSyncPoint);
            bDeviceBusy = false;

            if (MFX_ERR_NONE < enc_sts && !pCurrentTask->encSyncPoint) {
                bDeviceBusy = true;
                if (MFX_WRN_DEVICE_BUSY == enc_sts)
                sleep_hybrid(i);
                if (i > 65536 * 1024 * 30) {
                    PrintMes(RGY_LOG_ERROR, _T("device kept on busy for 30s, unknown error occurred.\n"));
                    return MFX_ERR_UNKNOWN;
                }
            } else if (MFX_ERR_NONE < enc_sts && pCurrentTask->encSyncPoint) {
                enc_sts = MFX_ERR_NONE;
                break;
            } else if (MFX_ERR_NOT_ENOUGH_BUFFER == enc_sts) {
                enc_sts = AllocateSufficientBuffer(&pCurrentTask->mfxBS);
                if (enc_sts < MFX_ERR_NONE) return enc_sts;
            } else if (enc_sts < MFX_ERR_NONE && (enc_sts != MFX_ERR_MORE_DATA && enc_sts != MFX_ERR_MORE_SURFACE)) {
                PrintMes(RGY_LOG_ERROR, _T("EncodeFrameAsync error: %s.\n"), get_err_mes(enc_sts));
                break;
            } else {
                QSV_IGNORE_STS(enc_sts, MFX_ERR_MORE_BITSTREAM);
                break;
            }
        }
        return enc_sts;
    };

    //メインループ
    while (MFX_ERR_NONE <= sts || MFX_ERR_MORE_DATA == sts || MFX_ERR_MORE_SURFACE == sts) {
        if (pSurfCheckPts) {
            //pSurfCheckPtsはcheckptsから出てきて、他の要素に投入するフレーム
            //投入後、ロックを解除する必要がある
            pSurfCheckPts->Data.Locked--;
            pSurfCheckPts = nullptr;
        }
        speedCtrl.wait(m_pEncSatusInfo->m_sData.frameIn);
#if defined(_WIN32) || defined(_WIN64)
        //中断オブジェクトのチェック
        if (WaitForSingleObject(m_heAbort.get(), 0) == WAIT_OBJECT_0) {
            m_EncThread.m_bthForceAbort = true;
        }
#endif

        //空いているフレームバッファを取得、空いていない場合は待機して、出力ストリームの書き出しを待ってから取得
        if (MFX_ERR_NONE != (sts = GetFreeTask(&pCurrentTask)))
            break;

        //空いているフレームバッファを取得、空いていない場合は待機して、空くまで待ってから取得
        nEncSurfIdx = GetFreeSurface(m_pEncSurfaces.data(), m_EncResponse.NumFrameActual);
        if (nEncSurfIdx == MSDK_INVALID_SURF_IDX) {
            PrintMes(RGY_LOG_ERROR, _T("Failed to get free surface for enc.\n"));
            return MFX_ERR_MEMORY_ALLOC;
        }

        // point pSurf to encoder surface
        pSurfEncIn = &m_pEncSurfaces[nEncSurfIdx];

        if (!bVppMultipleOutput) {
            if (MFX_ERR_NONE != (sts = get_all_free_surface(pSurfEncIn))) {
                break;
            }
            if (!bCheckPtsMultipleOutput) {
                //if (m_VppPrePlugins.size()) {
                //    pSurfInputBuf = pSurfVppPreFilter[0];
                //    //ppNextFrame = &;
                //} else if (m_pmfxVPP) {
                //    pSurfInputBuf = &m_pVppSurfaces[nVppSurfIdx];
                //    //ppNextFrame = &pSurfVppIn;
                //} else if (m_VppPostPlugins.size()) {
                //    pSurfInputBuf = &pSurfVppPostFilter[0];
                //    //ppNextFrame = &;
                //} else {
                //    pSurfInputBuf = pSurfEncIn;
                //    //ppNextFrame = &pSurfEncIn;
                //}

                if (m_pFileReader->getInputCodec() == RGY_CODEC_UNKNOWN) {
                    //読み込み側の該当フレームの読み込み終了を待機(pInputBuf->heInputDone)して、読み込んだフレームを取得
                    //この関数がRGY_ERR_NONE以外を返すことでRunEncodeは終了処理に入る
                    sts = GetNextFrame(&pNextFrame);
                    if (sts != MFX_ERR_NONE) {
                        break;
                    }
                    pNextFrame->Data.TimeStamp = (mfxU64)MFX_TIMESTAMP_UNKNOWN;
                    //フレーム読み込みの場合には、必要ならここでロックする
                    if (m_bExternalAlloc) {
                        if (MFX_ERR_NONE != (sts = m_pMFXAllocator->Unlock(m_pMFXAllocator->pthis, (pNextFrame)->Data.MemId, &((pNextFrame)->Data))))
                            break;

                        if (MFX_ERR_NONE != (sts = m_pMFXAllocator->Lock(m_pMFXAllocator->pthis, pSurfInputBuf->Data.MemId, &(pSurfInputBuf->Data))))
                            break;
                    }

                    //空いているフレームを読み込み側に渡す
                    SetNextSurface(pSurfInputBuf);
                } else {
                    //フレーム読み込みでない場合には、フレームバッファをm_pFileReaderを通さずに直接渡す
                    pNextFrame = pSurfInputBuf;
                    if (m_EncThread.m_bthForceAbort) {
                        sts = m_EncThread.m_stsThread;
                        break;
                    }
                }

                auto ret = extract_audio();
                if (ret != RGY_ERR_NONE) {
                    sts = err_to_mfx(ret);
                    break;
                }

                //この関数がMFX_ERR_MORE_BITSTREAMを返せば、入力は終了
                sts = decode_one_frame(true);
                if (sts == MFX_ERR_MORE_DATA || sts == MFX_ERR_MORE_SURFACE)
                    continue;
                if (sts != MFX_ERR_NONE)
                    break;
            }

            if (!frame_inside_range(nInputFrameCount, m_trimParam.list).first)
                continue;

            sts = check_pts();
            if (sts == MFX_ERR_MORE_SURFACE)
                continue;
            if (sts != MFX_ERR_NONE)
                break;

            for (int i_filter = 0; i_filter < (int)m_VppPrePlugins.size(); i_filter++) {
                mfxFrameSurface1 *pSurfFilterOut = pSurfVppPreFilter[i_filter + 1];
                if (MFX_ERR_NONE != (sts = filter_one_frame(m_VppPrePlugins[i_filter], &pNextFrame, &pSurfFilterOut)))
                    break;
                pNextFrame = pSurfFilterOut;
            }
            if (sts != MFX_ERR_NONE)
                break;

            pSurfVppIn = pNextFrame;
        }

        sts = vpp_one_frame(pSurfVppIn, (m_VppPostPlugins.size()) ? pSurfVppPostFilter[0] : pSurfEncIn);
        if (bVppRequireMoreFrame)
            continue;
        if (sts != MFX_ERR_NONE)
            break;

        for (int i_filter = 0; i_filter < (int)m_VppPostPlugins.size(); i_filter++) {
            mfxFrameSurface1 *pSurfFilterOut = pSurfVppPostFilter[i_filter + 1];
            if (MFX_ERR_NONE != (sts = filter_one_frame(m_VppPostPlugins[i_filter], &pNextFrame, &pSurfFilterOut)))
                break;
            pNextFrame = pSurfFilterOut;
        }
        if (sts != MFX_ERR_NONE)
            break;

        sts = encode_one_frame(pNextFrame);
    }

    //MFX_ERR_MORE_DATA/MFX_ERR_MORE_BITSTREAMは入力が終了したことを示す
    QSV_IGNORE_STS(sts, (m_pFileReader->getInputCodec() != RGY_CODEC_UNKNOWN) ? MFX_ERR_MORE_BITSTREAM : MFX_ERR_MORE_DATA);
    //エラーチェック
    m_EncThread.m_stsThread = sts;
    QSV_ERR_MES(sts, _T("Error in encoding pipeline."));
    PrintMes(RGY_LOG_DEBUG, _T("Encode Thread: finished main loop.\n"));

    if (m_pmfxDEC) {
        auto ret = extract_audio();
        RGY_ERR_MES(ret, _T("Error on extracting audio."));

        pNextFrame = NULL;

        while (MFX_ERR_NONE <= sts || sts == MFX_ERR_MORE_SURFACE) {
            if (pSurfCheckPts) {
                //pSurfCheckPtsはcheckptsから出てきて、他の要素に投入するフレーム
                //投入後、ロックを解除する必要がある
                pSurfCheckPts->Data.Locked--;
                pSurfCheckPts = nullptr;
            }

            //空いているフレームバッファを取得、空いていない場合は待機して、出力ストリームの書き出しを待ってから取得
            if (MFX_ERR_NONE != (sts = GetFreeTask(&pCurrentTask)))
                break;

            //空いているフレームバッファを取得、空いていない場合は待機して、空くまで待ってから取得
            nEncSurfIdx = GetFreeSurface(m_pEncSurfaces.data(), m_EncResponse.NumFrameActual);
            if (nEncSurfIdx == MSDK_INVALID_SURF_IDX) {
                PrintMes(RGY_LOG_ERROR, _T("Failed to get free surface for enc.\n"));
                return MFX_ERR_MEMORY_ALLOC;
            }

            pSurfEncIn = &m_pEncSurfaces[nEncSurfIdx];

            if (!bVppMultipleOutput) {
                get_all_free_surface(pSurfEncIn);
                pNextFrame = pSurfInputBuf;

                if (!bCheckPtsMultipleOutput) {
                    sts = decode_one_frame(false);
                    if (sts == MFX_ERR_MORE_SURFACE)
                        continue;
                    if (sts != MFX_ERR_NONE)
                        break;
                }

                sts = check_pts();
                if (sts == MFX_ERR_MORE_SURFACE)
                    continue;
                if (sts != MFX_ERR_NONE)
                    break;

                for (int i_filter = 0; i_filter < (int)m_VppPrePlugins.size(); i_filter++) {
                    mfxFrameSurface1 *pSurfFilterOut = pSurfVppPreFilter[i_filter + 1];
                    if (MFX_ERR_NONE != (sts = filter_one_frame(m_VppPrePlugins[i_filter], &pNextFrame, &pSurfFilterOut)))
                        break;
                    pNextFrame = pSurfFilterOut;
                }
                if (sts != MFX_ERR_NONE)
                    break;

                pSurfVppIn = pNextFrame;
            }

            if (!frame_inside_range(nInputFrameCount, m_trimParam.list).first)
                continue;

            sts = vpp_one_frame(pSurfVppIn, (m_VppPostPlugins.size()) ? pSurfVppPostFilter[0] : pSurfEncIn);
            if (bVppRequireMoreFrame)
                continue;
            if (sts != MFX_ERR_NONE)
                break;

            for (int i_filter = 0; i_filter < (int)m_VppPostPlugins.size(); i_filter++) {
                mfxFrameSurface1 *pSurfFilterOut = pSurfVppPostFilter[i_filter + 1];
                if (MFX_ERR_NONE != (sts = filter_one_frame(m_VppPostPlugins[i_filter], &pNextFrame, &pSurfFilterOut)))
                    break;
                pNextFrame = pSurfFilterOut;
            }
            if (sts != MFX_ERR_NONE)
                break;

            sts = encode_one_frame(pNextFrame);
        }

        //MFX_ERR_MORE_DATAはデコーダにもうflushするべきフレームがないことを示す
        QSV_IGNORE_STS(sts, MFX_ERR_MORE_DATA);
        //エラーチェック
        m_EncThread.m_stsThread = sts;
        QSV_ERR_MES(sts, _T("Error in getting buffered frames from decoder."));
        PrintMes(RGY_LOG_DEBUG, _T("Encode Thread: finished getting buffered frames from decoder.\n"));
    }

    if (m_pmfxVPP) {
        //vppのフレームをflush
        while (MFX_ERR_NONE <= sts || MFX_ERR_MORE_DATA == sts || MFX_ERR_MORE_SURFACE == sts) {
            // MFX_ERR_MORE_SURFACE can be returned only by RunFrameVPPAsync
            // MFX_ERR_MORE_DATA is accepted only from EncodeFrameAsync
            if (pSurfCheckPts) {
                //pSurfCheckPtsはcheckptsから出てきて、他の要素に投入するフレーム
                //投入後、ロックを解除する必要がある
                pSurfCheckPts->Data.Locked--;
                pSurfCheckPts = nullptr;
            }

            pNextFrame = nullptr;

            nEncSurfIdx = GetFreeSurface(m_pEncSurfaces.data(), m_EncResponse.NumFrameActual);
            if (nEncSurfIdx == MSDK_INVALID_SURF_IDX) {
                PrintMes(RGY_LOG_ERROR, _T("Failed to get free surface for enc.\n"));
                return MFX_ERR_MEMORY_ALLOC;
            }

            pSurfEncIn = &m_pEncSurfaces[nEncSurfIdx];

            if (MFX_ERR_NONE != (sts = GetFreeTask(&pCurrentTask)))
                break;

            get_all_free_surface(pSurfEncIn);

            //for (int i_filter = 0; i_filter < (int)m_VppPrePlugins.size(); i_filter++) {
            //    bVppAllFiltersFlushed &= m_VppPrePlugins[i_filter]->m_bPluginFlushed;
            //    if (!m_VppPrePlugins[i_filter]->m_bPluginFlushed) {
            //        mfxFrameSurface1 *pSurfFilterOut = pSurfVppPreFilter[i_filter + 1];
            //        sts = filter_one_frame(m_VppPrePlugins[i_filter], &pNextFrame, &pSurfFilterOut);
            //        if (sts == MFX_ERR_MORE_DATA) {
            //            m_VppPrePlugins[i_filter]->m_bPluginFlushed = true;
            //            sts = MFX_ERR_NONE;
            //        }
            //        MSDK_BREAK_ON_ERROR(sts);
            //        pNextFrame = pSurfFilterOut;
            //    }
            //}
            //MSDK_BREAK_ON_ERROR(sts);

            sts = vpp_one_frame(pNextFrame, (m_VppPostPlugins.size()) ? pSurfVppPostFilter[0] : pSurfEncIn);
            if (bVppRequireMoreFrame) {
                break;
            }
            if (sts != MFX_ERR_NONE)
                break;

            for (int i_filter = 0; i_filter < (int)m_VppPostPlugins.size(); i_filter++) {
                mfxFrameSurface1 *pSurfFilterOut = pSurfVppPostFilter[i_filter + 1];
                if (MFX_ERR_NONE != (sts = filter_one_frame(m_VppPostPlugins[i_filter], &pNextFrame, &pSurfFilterOut)))
                    break;
                pNextFrame = pSurfFilterOut;
            }
            if (sts != MFX_ERR_NONE)
                break;

            sts = encode_one_frame(pNextFrame);
        }

        //MFX_ERR_MORE_DATAはvppにもうflushするべきフレームがないことを示す
        QSV_IGNORE_STS(sts, MFX_ERR_MORE_DATA);
        //エラーチェック
        m_EncThread.m_stsThread = sts;
        QSV_ERR_MES(sts, _T("Error in getting buffered frames from vpp."));
        PrintMes(RGY_LOG_DEBUG, _T("Encode Thread: finished getting buffered frames from vpp.\n"));
    }

    //encのフレームをflush
    while (MFX_ERR_NONE <= sts && m_pmfxENC) {
        if (pSurfCheckPts) {
            pSurfCheckPts->Data.Locked--;
            pSurfCheckPts = nullptr;
        }

        if (MFX_ERR_NONE != (sts = GetFreeTask(&pCurrentTask)))
            break;

        sts = encode_one_frame(NULL);
    }
    PrintMes(RGY_LOG_DEBUG, _T("Encode Thread: finished getting buffered frames from encoder.\n"));

    //MFX_ERR_MORE_DATAはencにもうflushするべきフレームがないことを示す
    QSV_IGNORE_STS(sts, MFX_ERR_MORE_DATA);
    //エラーチェック
    m_EncThread.m_stsThread = sts;
    QSV_ERR_MES(sts, _T("Error in getting buffered frames from encoder."));

    //タスクプールのすべてのタスクの終了を確認
    while (MFX_ERR_NONE == sts) {
        sts = m_TaskPool.SynchronizeFirstTask();
    }

    // MFX_ERR_NOT_FOUNDは、正しい終了ステータス
    QSV_IGNORE_STS(sts, MFX_ERR_NOT_FOUND);
    //エラーチェック
    m_EncThread.m_stsThread = sts;
    QSV_ERR_MES(sts, _T("Error in encoding pipeline, synchronizing pipeline."));

    PrintMes(RGY_LOG_DEBUG, _T("Encode Thread: finished.\n"));
    return sts;
}

void CQSVPipeline::PrintMes(int log_level, const TCHAR *format, ...) {
    if (m_pQSVLog.get() == nullptr) {
        if (log_level <= RGY_LOG_INFO) {
            return;
        }
    } else if (log_level < m_pQSVLog->getLogLevel()) {
        return;
    }

    va_list args;
    va_start(args, format);

    int len = _vsctprintf(format, args) + 1; // _vscprintf doesn't count terminating '\0'
    vector<TCHAR> buffer(len, 0);
    _vstprintf_s(buffer.data(), len, format, args);
    va_end(args);

    if (m_pQSVLog.get() != nullptr) {
        m_pQSVLog->write(log_level, buffer.data());
    } else {
        _ftprintf(stderr, _T("%s"), buffer.data());
    }
}

void CQSVPipeline::GetEncodeLibInfo(mfxVersion *ver, bool *hardware) {
    if (NULL != ver && NULL != hardware) {
        mfxIMPL impl;
        m_mfxSession.QueryIMPL(&impl);
        *hardware = !!Check_HWUsed(impl);
        *ver = m_mfxVer;
    }

}

MemType CQSVPipeline::GetMemType() {
    return m_memType;
}

mfxStatus CQSVPipeline::GetEncodeStatusData(EncodeStatusData *data) {
    if (NULL == data)
        return MFX_ERR_NULL_PTR;

    if (NULL == m_pEncSatusInfo)
        return MFX_ERR_NOT_INITIALIZED;

    *data = m_pEncSatusInfo->GetEncodeData();
    return MFX_ERR_NONE;
}

const TCHAR *CQSVPipeline::GetInputMessage() {
    return m_pFileReader->GetInputMessage();
}

mfxStatus CQSVPipeline::CheckCurrentVideoParam(TCHAR *str, mfxU32 bufSize) {
    mfxIMPL impl;
    m_mfxSession.QueryIMPL(&impl);

    mfxFrameInfo SrcPicInfo = m_mfxVppParams.vpp.In;
    mfxFrameInfo DstPicInfo = m_mfxEncParams.mfx.FrameInfo;

    mfxU8 spsbuf[256] = { 0 };
    mfxU8 ppsbuf[256] = { 0 };
    mfxExtCodingOptionSPSPPS spspps;
    INIT_MFX_EXT_BUFFER(spspps, MFX_EXTBUFF_CODING_OPTION_SPSPPS);
    spspps.SPSBuffer = spsbuf;
    spspps.SPSBufSize = sizeof(spsbuf);
    spspps.PPSBuffer = ppsbuf;
    spspps.PPSBufSize = sizeof(ppsbuf);

    mfxExtCodingOption cop;
    mfxExtCodingOption2 cop2;
    mfxExtCodingOption3 cop3;
    mfxExtVP8CodingOption copVp8;
    mfxExtHEVCParam hevcPrm;
    INIT_MFX_EXT_BUFFER(cop, MFX_EXTBUFF_CODING_OPTION);
    INIT_MFX_EXT_BUFFER(cop2, MFX_EXTBUFF_CODING_OPTION2);
    INIT_MFX_EXT_BUFFER(cop3, MFX_EXTBUFF_CODING_OPTION3);
    INIT_MFX_EXT_BUFFER(copVp8, MFX_EXTBUFF_VP8_CODING_OPTION);
    INIT_MFX_EXT_BUFFER(hevcPrm, MFX_EXTBUFF_HEVC_PARAM);

    std::vector<mfxExtBuffer *> buf;
    buf.push_back((mfxExtBuffer *)&cop);
    buf.push_back((mfxExtBuffer *)&spspps);
    if (check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_6)) {
        buf.push_back((mfxExtBuffer *)&cop2);
    }
    if (check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_11)) {
        buf.push_back((mfxExtBuffer *)&cop3);
    }
    if (m_mfxEncParams.mfx.CodecId == MFX_CODEC_VP8) {
        buf.push_back((mfxExtBuffer *)&copVp8);
    }
    if (m_mfxEncParams.mfx.CodecId == MFX_CODEC_HEVC && check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_26)) {
        buf.push_back((mfxExtBuffer *)&hevcPrm);
    }

    mfxVideoParam videoPrm;
    RGY_MEMSET_ZERO(videoPrm);
    videoPrm.NumExtParam = (mfxU16)buf.size();
    videoPrm.ExtParam = &buf[0];

    mfxStatus sts = MFX_ERR_NONE;
    if (m_pmfxENC) {
        sts = m_pmfxENC->GetVideoParam(&videoPrm);
        QSV_ERR_MES(sts, _T("Failed to get video param from encoder."));
    } else if (m_pmfxVPP) {
        mfxVideoParam videoPrmVpp;
        RGY_MEMSET_ZERO(videoPrmVpp);
        sts = m_pmfxVPP->GetVideoParam(&videoPrmVpp);
        QSV_ERR_MES(sts, _T("Failed to get video param from vpp."));
        videoPrm.mfx.FrameInfo = videoPrmVpp.vpp.Out;
        DstPicInfo = videoPrmVpp.vpp.Out;
    } else if (m_pmfxDEC) {
        sts = m_pmfxDEC->GetVideoParam(&videoPrm);
        QSV_ERR_MES(sts, _T("Failed to get video param from decoder."));
        DstPicInfo = videoPrm.mfx.FrameInfo;
    }

    if (m_pmfxENC) {
        mfxParamSet prmSetOut;
        prmSetOut.vidprm = videoPrm;
        prmSetOut.cop    = cop;
        prmSetOut.cop2   = cop2;
        prmSetOut.cop3   = cop3;
        prmSetOut.hevc   = hevcPrm;

        CompareParam(m_prmSetIn, prmSetOut);
    }

    TCHAR cpuInfo[256] = { 0 };
    getCPUInfo(cpuInfo, _countof(cpuInfo), &m_mfxSession);

    TCHAR gpu_info[1024] = { 0 };
    if (Check_HWUsed(impl)) {
        getGPUInfo("Intel", gpu_info, _countof(gpu_info));
    }
    TCHAR info[4096] = { 0 };
    mfxU32 info_len = 0;

#define PRINT_INFO(fmt, ...) { info_len += _stprintf_s(info + info_len, _countof(info) - info_len, fmt, __VA_ARGS__); }
#define PRINT_INT_AUTO(fmt, i) { if (i) { info_len += _stprintf_s(info + info_len, _countof(info) - info_len, fmt, i); } else { info_len += _stprintf_s(info + info_len, _countof(info) - info_len, (fmt[_tcslen(fmt)-1]=='\n') ? _T("Auto\n") : _T("Auto")); } }
    PRINT_INFO(    _T("%s\n"), get_encoder_version());
#if defined(_WIN32) || defined(_WIN64)
    OSVERSIONINFOEXW osversioninfo = { 0 };
    tstring osversionstr = getOSVersion(&osversioninfo);
    PRINT_INFO(    _T("OS             %s %s (%d)\n"), osversionstr.c_str(), rgy_is_64bit_os() ? _T("x64") : _T("x86"), osversioninfo.dwBuildNumber);
#else
    PRINT_INFO(    _T("OS             %s %s\n"), getOSVersion().c_str(), rgy_is_64bit_os() ? _T("x64") : _T("x86"));
#endif
    PRINT_INFO(    _T("CPU Info       %s\n"), cpuInfo);
    if (Check_HWUsed(impl)) {
        PRINT_INFO(_T("GPU Info       %s\n"), gpu_info);
    }
    if (Check_HWUsed(impl)) {
        static const TCHAR * const NUM_APPENDIX[] = { _T("st"), _T("nd"), _T("rd"), _T("th")};
        mfxU32 iGPUID = GetAdapterID(m_mfxSession);
        PRINT_INFO(    _T("Media SDK      QuickSyncVideo (hardware encoder)%s, %d%s GPU, API v%d.%d\n"),
            get_low_power_str(videoPrm.mfx.LowPower), iGPUID + 1, NUM_APPENDIX[clamp(iGPUID, 0, _countof(NUM_APPENDIX) - 1)], m_mfxVer.Major, m_mfxVer.Minor);
    } else {
        PRINT_INFO(    _T("Media SDK      software encoder, API v%d.%d\n"), m_mfxVer.Major, m_mfxVer.Minor);
    }
    PRINT_INFO(    _T("Async Depth    %d frames\n"), m_nAsyncDepth);
    PRINT_INFO(    _T("Buffer Memory  %s, %d input buffer, %d work buffer\n"), MemTypeToStr(m_memType), m_EncThread.m_nFrameBuffer, m_EncResponse.NumFrameActual + m_VppResponse.NumFrameActual + m_DecResponse.NumFrameActual);
    //PRINT_INFO(    _T("Input Frame Format   %s\n"), ColorFormatToStr(m_pFileReader->m_ColorFormat));
    //PRINT_INFO(    _T("Input Frame Type     %s\n"), list_interlaced_mfx[get_cx_index(list_interlaced_mfx, SrcPicInfo.PicStruct)].desc);
    tstring inputMes = m_pFileReader->GetInputMessage();
    for (const auto& reader : m_AudioReaders) {
        inputMes += _T("\n") + tstring(reader->GetInputMessage());
    }
    auto inputMesSplitted = split(inputMes, _T("\n"));
    for (mfxU32 i = 0; i < inputMesSplitted.size(); i++) {
        PRINT_INFO(_T("%s%s\n"), (i == 0) ? _T("Input Info     ") : _T("               "), inputMesSplitted[i].c_str());
    }

    sInputCrop inputCrop = m_pFileReader->GetInputCropInfo();
    if (0 != (inputCrop.e.bottom | inputCrop.e.left | inputCrop.e.right | inputCrop.e.up))
        PRINT_INFO(_T("Crop           %d,%d,%d,%d (%dx%d -> %dx%d)\n"),
            inputCrop.e.left, inputCrop.e.up, inputCrop.e.right, inputCrop.e.bottom,
            SrcPicInfo.CropW + inputCrop.e.left + inputCrop.e.right,
            SrcPicInfo.CropH + inputCrop.e.up + inputCrop.e.bottom,
            SrcPicInfo.CropW, SrcPicInfo.CropH);

    if (VppExtMes.size()) {
        const TCHAR *m = _T("VPP Enabled    ");
        size_t len = VppExtMes.length() + 1;
        TCHAR *vpp_mes = (TCHAR*)malloc(len * sizeof(vpp_mes[0]));
        memcpy(vpp_mes, VppExtMes.c_str(), len * sizeof(vpp_mes[0]));
        for (TCHAR *p = vpp_mes, *q; (p = _tcstok_s(p, _T("\n"), &q)) != NULL; ) {
            PRINT_INFO(_T("%s%s\n"), m, p);
            m    = _T("               ");
            p = NULL;
        }
        free(vpp_mes);
        VppExtMes.clear();
    }
    if (m_trimParam.list.size()
        && !(m_trimParam.list[0].start == 0 && m_trimParam.list[0].fin == TRIM_MAX)) {
        PRINT_INFO(_T("%s"), _T("Trim           "));
        for (auto trim : m_trimParam.list) {
            if (trim.fin == TRIM_MAX) {
                PRINT_INFO(_T("%d-fin "), trim.start + m_trimParam.offset);
            } else {
                PRINT_INFO(_T("%d-%d "), trim.start + m_trimParam.offset, trim.fin + m_trimParam.offset);
            }
        }
        PRINT_INFO(_T("[offset: %d]\n"), m_trimParam.offset);
    }
    PRINT_INFO(_T("AVSync         %s\n"), get_chr_from_value(list_avsync, m_nAVSyncMode));
    if (m_pmfxENC) {
        PRINT_INFO(_T("Output         %s %s @ Level %s%s\n"), CodecIdToStr(videoPrm.mfx.CodecId),
            get_profile_list(videoPrm.mfx.CodecId)[get_cx_index(get_profile_list(videoPrm.mfx.CodecId), videoPrm.mfx.CodecProfile)].desc,
            get_level_list(videoPrm.mfx.CodecId)[get_cx_index(get_level_list(videoPrm.mfx.CodecId), videoPrm.mfx.CodecLevel & 0xff)].desc,
            (videoPrm.mfx.CodecId == MFX_CODEC_HEVC && (videoPrm.mfx.CodecLevel & MFX_TIER_HEVC_HIGH)) ? _T(" (high tier)") : _T(""));
    }
    PRINT_INFO(_T("%s         %dx%d%s %d:%d %0.3ffps (%d/%dfps)%s%s\n"),
        (m_pmfxENC) ? _T("      ") : _T("Output"),
        DstPicInfo.CropW, DstPicInfo.CropH, (DstPicInfo.PicStruct & MFX_PICSTRUCT_PROGRESSIVE) ? _T("p") : _T("i"),
        videoPrm.mfx.FrameInfo.AspectRatioW, videoPrm.mfx.FrameInfo.AspectRatioH,
        DstPicInfo.FrameRateExtN / (double)DstPicInfo.FrameRateExtD, DstPicInfo.FrameRateExtN, DstPicInfo.FrameRateExtD,
        (DstPicInfo.PicStruct & MFX_PICSTRUCT_PROGRESSIVE) ? _T("") : _T(", "),
        (DstPicInfo.PicStruct & MFX_PICSTRUCT_PROGRESSIVE) ? _T("") : list_interlaced_mfx[get_cx_index(list_interlaced_mfx, DstPicInfo.PicStruct)].desc);
    if (m_pFileWriter) {
        inputMesSplitted = split(m_pFileWriter->GetOutputMessage(), _T("\n"));
        for (auto mes : inputMesSplitted) {
            if (mes.length()) {
                PRINT_INFO(_T("%s%s\n"), _T("               "), mes.c_str());
            }
        }
    }
    for (auto pWriter : m_pFileWriterListAudio) {
        if (pWriter && pWriter != m_pFileWriter) {
            inputMesSplitted = split(pWriter->GetOutputMessage(), _T("\n"));
            for (auto mes : inputMesSplitted) {
                if (mes.length()) {
                    PRINT_INFO(_T("%s%s\n"), _T("               "), mes.c_str());
                }
            }
        }
    }

    if (m_pmfxENC) {
        PRINT_INFO(_T("Target usage   %s\n"), TargetUsageToStr(videoPrm.mfx.TargetUsage));
        PRINT_INFO(_T("Encode Mode    %s\n"), EncmodeToStr(videoPrm.mfx.RateControlMethod));
        if (m_mfxEncParams.mfx.RateControlMethod == MFX_RATECONTROL_CQP) {
            PRINT_INFO(_T("CQP Value      I:%d  P:%d  B:%d\n"), videoPrm.mfx.QPI, videoPrm.mfx.QPP, videoPrm.mfx.QPB);
        } else if (rc_is_type_lookahead(m_mfxEncParams.mfx.RateControlMethod)) {
            if (m_mfxEncParams.mfx.RateControlMethod != MFX_RATECONTROL_LA_ICQ) {
                PRINT_INFO(_T("Bitrate        %d kbps\n"), videoPrm.mfx.TargetKbps * (std::max<int>)(m_mfxEncParams.mfx.BRCParamMultiplier, 1));
                PRINT_INFO(_T("%s"), _T("Max Bitrate    "));
                PRINT_INT_AUTO(_T("%d kbps\n"), videoPrm.mfx.MaxKbps * (std::max<int>)(m_mfxEncParams.mfx.BRCParamMultiplier, 1));
            }
            PRINT_INFO(_T("Lookahead      depth %d frames"), cop2.LookAheadDepth);
            if (check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_8)) {
                PRINT_INFO(_T(", quality %s"), list_lookahead_ds[get_cx_index(list_lookahead_ds, cop2.LookAheadDS)].desc);
            }
            PRINT_INFO(_T("%s"), _T("\n"));
            if (check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_11)) {
                if (cop3.WinBRCSize) {
                    PRINT_INFO(_T("Windowed RC    %d frames, Max %d kbps\n"), cop3.WinBRCSize, cop3.WinBRCMaxAvgKbps);
                } else {
                    PRINT_INFO(_T("%s"), _T("Windowed RC    off\n"));
                }
            }
            if (m_mfxEncParams.mfx.RateControlMethod == MFX_RATECONTROL_LA_ICQ) {
                PRINT_INFO(_T("ICQ Quality    %d\n"), videoPrm.mfx.ICQQuality);
            }
        } else if (m_mfxEncParams.mfx.RateControlMethod == MFX_RATECONTROL_ICQ) {
            PRINT_INFO(_T("ICQ Quality    %d\n"), videoPrm.mfx.ICQQuality);
        } else {
            PRINT_INFO(_T("Bitrate        %d kbps\n"), videoPrm.mfx.TargetKbps * (std::max<int>)(m_mfxEncParams.mfx.BRCParamMultiplier, 1));
            if (m_mfxEncParams.mfx.RateControlMethod == MFX_RATECONTROL_AVBR) {
                //PRINT_INFO(_T("AVBR Accuracy range\t%.01lf%%"), m_mfxEncParams.mfx.Accuracy / 10.0);
                PRINT_INFO(_T("AVBR Converge  %d frames unit\n"), videoPrm.mfx.Convergence * 100);
            } else {
                PRINT_INFO(_T("%s"), _T("Max Bitrate    "));
                PRINT_INT_AUTO(_T("%d kbps\n"), videoPrm.mfx.MaxKbps * (std::max<int>)(m_mfxEncParams.mfx.BRCParamMultiplier, 1));
                if (m_mfxEncParams.mfx.RateControlMethod == MFX_RATECONTROL_QVBR) {
                    PRINT_INFO(_T("QVBR Quality   %d\n"), cop3.QVBRQuality);
                }
            }
            if (videoPrm.mfx.BufferSizeInKB > 0) {
                PRINT_INFO(_T("VBV Bufsize    %d kbps\n"), videoPrm.mfx.BufferSizeInKB * 8 * (std::max<int>)(m_mfxEncParams.mfx.BRCParamMultiplier, 1));
            }
        }
        if (check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_9)) {
            auto qp_limit_str = [](mfxU8 limitI, mfxU8 limitP, mfxU8 limitB) {
                mfxU8 limit[3] = { limitI, limitP, limitB };
                if (0 == (limit[0] | limit[1] | limit[2])) {
                    return tstring(_T("none"));
                }
                if (limit[0] == limit[1] && limit[0] == limit[2]) {
                    return strsprintf(_T("%d"), limit[0]);
                }

                tstring buf;
                for (int i = 0; i < 3; i++) {
                    buf += ((i) ? _T(":") : _T(""));
                    if (limit[i]) {
                        buf += strsprintf(_T("%d"), limit[i]);
                    } else {
                        buf += _T("-");
                    }
                }
                return buf;
            };
            PRINT_INFO(_T("QP Limit       min: %s, max: %s\n"),
                qp_limit_str(cop2.MinQPI, cop2.MinQPP, cop2.MinQPB).c_str(),
                qp_limit_str(cop2.MaxQPI, cop2.MaxQPP, cop2.MaxQPB).c_str());
        }
        if (check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_7)) {
            PRINT_INFO(_T("Trellis        %s\n"), list_avc_trellis[get_cx_index(list_avc_trellis_for_options, cop2.Trellis)].desc);
        }

        if (videoPrm.mfx.CodecId == MFX_CODEC_AVC && !Check_HWUsed(impl)) {
            PRINT_INFO(_T("CABAC          %s\n"), (cop.CAVLC == MFX_CODINGOPTION_ON) ? _T("off") : _T("on"));
            PRINT_INFO(_T("RDO            %s\n"), (cop.RateDistortionOpt == MFX_CODINGOPTION_ON) ? _T("on") : _T("off"));
            if ((cop.MVSearchWindow.x | cop.MVSearchWindow.y) == 0) {
                PRINT_INFO(_T("mv search      precision: %s\n"), list_mv_presicion[get_cx_index(list_mv_presicion, cop.MVPrecision)].desc);
            } else {
                PRINT_INFO(_T("mv search      precision: %s, window size:%dx%d\n"), list_mv_presicion[get_cx_index(list_mv_presicion, cop.MVPrecision)].desc, cop.MVSearchWindow.x, cop.MVSearchWindow.y);
            }
            PRINT_INFO(_T("min pred size  inter: %s   intra: %s\n"), list_pred_block_size[get_cx_index(list_pred_block_size, cop.InterPredBlockSize)].desc, list_pred_block_size[get_cx_index(list_pred_block_size, cop.IntraPredBlockSize)].desc);
        }
        PRINT_INFO(_T("%s"), _T("Ref frames     "));
        PRINT_INT_AUTO(_T("%d frames\n"), videoPrm.mfx.NumRefFrame);

        PRINT_INFO(_T("%s"), _T("Bframes        "));
        switch (videoPrm.mfx.GopRefDist) {
        case 0:  PRINT_INFO(_T("%s"), _T("Auto\n")); break;
        case 1:  PRINT_INFO(_T("%s"), _T("none\n")); break;
        default: PRINT_INFO(_T("%d frame%s%s%s\n"),
            videoPrm.mfx.GopRefDist - 1, (videoPrm.mfx.GopRefDist > 2) ? _T("s") : _T(""),
            check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_8) ? _T(", B-pyramid: ") : _T(""),
            (check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_8) ? ((MFX_B_REF_PYRAMID == cop2.BRefType) ? _T("on") : _T("off")) : _T(""))); break;
        }

        //PRINT_INFO(    _T("Idr Interval    %d\n"), videoPrm.mfx.IdrInterval);
        PRINT_INFO(_T("%s"), _T("Max GOP Length "));
        PRINT_INT_AUTO(_T("%d frames\n"), videoPrm.mfx.GopPicSize);
        if (check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_8)) {
            //PRINT_INFO(    _T("GOP Structure           "));
            //bool adaptiveIOn = (MFX_CODINGOPTION_ON == cop2.AdaptiveI);
            //bool adaptiveBOn = (MFX_CODINGOPTION_ON == cop2.AdaptiveB);
            //if (!adaptiveIOn && !adaptiveBOn) {
            //    PRINT_INFO(_T("fixed\n"))
            //} else {
            //    PRINT_INFO(_T("Adaptive %s%s%s insert\n"),
            //        (adaptiveIOn) ? _T("I") : _T(""),
            //        (adaptiveIOn && adaptiveBOn) ? _T(",") : _T(""),
            //        (adaptiveBOn) ? _T("B") : _T(""));
            //}
        }
        if (videoPrm.mfx.NumSlice >= 2) {
            PRINT_INFO(_T("Slices         %d\n"), videoPrm.mfx.NumSlice);
        }

        if (videoPrm.mfx.CodecId == MFX_CODEC_VP8) {
            PRINT_INFO(_T("Sharpness      %d\n"), copVp8.SharpnessLevel);
        }

        //last line
        tstring extFeatures;
        if (check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_6)) {
            if (cop2.MBBRC  == MFX_CODINGOPTION_ON) {
                extFeatures += _T("PerMBRC ");
            }
            if (cop2.ExtBRC == MFX_CODINGOPTION_ON) {
                extFeatures += _T("ExtBRC ");
            }
        }
        if (check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_9)) {
            if (cop2.DisableDeblockingIdc) {
                extFeatures += _T("No-Deblock ");
            }
            if (cop2.IntRefType) {
                extFeatures += _T("Intra-Refresh ");
            }
        }
        if (check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_13)) {
            if (cop3.DirectBiasAdjustment == MFX_CODINGOPTION_ON) {
                extFeatures += _T("DirectBiasAdjust ");
            }
            if (cop3.GlobalMotionBiasAdjustment == MFX_CODINGOPTION_ON) {
                extFeatures += strsprintf(_T("MVCostScaling=%d "), cop3.MVCostScalingFactor);
            }
        }
        if (check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_16)) {
            if (cop3.WeightedPred != MFX_WEIGHTED_PRED_UNKNOWN) {
                extFeatures += _T("WeightP ");
            }
            if (cop3.WeightedBiPred != MFX_WEIGHTED_PRED_UNKNOWN) {
                extFeatures += _T("WeightB ");
            }
        }
        if (check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_17)) {
            if (cop3.FadeDetection == MFX_CODINGOPTION_ON) {
                extFeatures += _T("FadeDetect ");
            }
        }
        if (check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_19)) {
            if (cop3.EnableQPOffset == MFX_CODINGOPTION_ON) {
                extFeatures += _T("QPOffset ");
            }
        }
        if (check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_26)) {
            if (cop3.ExtBrcAdaptiveLTR == MFX_CODINGOPTION_ON) {
                extFeatures += _T("AdaptiveLTR ");
            }
        }
        //if (cop.AUDelimiter == MFX_CODINGOPTION_ON) {
        //    extFeatures += _T("aud ");
        //}
        //if (cop.PicTimingSEI == MFX_CODINGOPTION_ON) {
        //    extFeatures += _T("pic_struct ");
        //}
        //if (cop.SingleSeiNalUnit == MFX_CODINGOPTION_ON) {
        //    extFeatures += _T("SingleSEI ");
        //}
        if (check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_23)) {
            if (cop3.RepartitionCheckEnable == MFX_CODINGOPTION_ON) {
                extFeatures += _T("RepartitionCheck ");
            }
        }
        if (m_mfxEncParams.mfx.CodecId == MFX_CODEC_HEVC) {
            if (check_lib_version(m_mfxVer, MFX_LIB_VERSION_1_26)) {
                if (cop3.TransformSkip == MFX_CODINGOPTION_ON) {
                    extFeatures += _T("tskip ");
                }
                if (hevcPrm.LCUSize != 0) {
                    extFeatures += strsprintf(_T("ctu:%d "), hevcPrm.LCUSize);
                }
                if (hevcPrm.SampleAdaptiveOffset != 0) {
                    extFeatures += strsprintf(_T("sao:%s "), get_chr_from_value(list_hevc_sao, hevcPrm.SampleAdaptiveOffset));
                }
            }
        }
        if (extFeatures.length() > 0) {
            PRINT_INFO(_T("Ext. Features  %s\n"), extFeatures.c_str());
        }
    }

    PrintMes(RGY_LOG_INFO, info);
    if (str && bufSize > 0) {
        _tcscpy_s(str, bufSize, info);
    }

    return MFX_ERR_NONE;
#undef PRINT_INFO
#undef PRINT_INT_AUTO
}

#include "ttmath/ttmath.h"

int64_t CQSVPipeline::rational_rescalex(int64_t v, rgy_rational<int> from, rgy_rational<int> to) {
    auto mul = rgy_rational<int64_t>((int64_t)from.n() * (int64_t)to.d(), (int64_t)from.d() * (int64_t)to.n());

#if _M_IX86
#define RESCALE_INT_SIZE 4
#else
#define RESCALE_INT_SIZE 2
#endif
    ttmath::Int<RESCALE_INT_SIZE> tmp1 = v;
    tmp1 *= mul.n();
    ttmath::Int<RESCALE_INT_SIZE> tmp2 = mul.d();

    tmp1 = (tmp1 + tmp2 - 1) / tmp2;
    int64_t ret;
    tmp1.ToInt(ret);
    return ret;
}

