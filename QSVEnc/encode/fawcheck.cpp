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

#include <Windows.h>
#include <limits.h>
#include <Math.h>
#include <vector>

#include "fawcheck.h"
#include "auo_util.h"

const int    FAW_ZERO_FULL              = 0;
const int    FAW_ZERO_HALF              = SHRT_MIN;
const double ZERO_BLOCK_THRESHOLD_RATIO = 0.005;
const double FAW_ERROR_TOO_SHORT_RATIO  = 0.2; //音声サンプリングレートに対する割合
const int    ZERO_BLOCK_COUNT_THRESHOLD = 16; //ゼロブロックが最低秒間いくつあるか
const double ZERO_SUM_RATIO_MIN[3]      = { 768.0 / 1536.0, 256.0 / 1536.0, 256.0 / 1536.0 }; //全体に対するゼロの数下限(フルサイズ, ハーフサイズ)
const double ZERO_SUM_RATIO_MAX         = 0.99479; //全体に対するゼロの数上限
const double ZERO_SD_RATIO              = 0.25; //ゼロブロック内のゼロの平均数に対する標準偏差

//int        audio_rate;        //    音声サンプリングレート
//int        audio_ch;        //    音声チャンネル数
//int        audio_n;        //    音声サンプリング数
//int        audio_size;        //    音声１サンプルのバイト数

//音声データは16bitのみということで
int FAWCheck(short *audio_dat, int audio_n, int audio_rate, int audio_size) {
    std::vector<int> zero_blocks[3];
    int current_zero_blocks[3] = { 0, 0, 0 };

    short *data = NULL;
    const int step = audio_size / sizeof(short);
    const short * const fin = audio_dat + audio_n * step;

    const int zero_block_threshold = (int)(audio_rate * ZERO_BLOCK_THRESHOLD_RATIO);

    //十分な音声があるかチェック
    if (audio_n < audio_rate * FAW_ERROR_TOO_SHORT_RATIO)
        return FAWCHECK_ERROR_TOO_SHORT;

    //ゼロブロックを数える
    for (data = audio_dat; data < fin; data += step) {
        short check[3] = { *data, (BYTE)((*data >> 8) + 128), (BYTE)((*data & 0xff) + 128) };

        for (int i = 0; i < 3; i++) {
            if (check[i] == 0) {
                current_zero_blocks[i]++;
            } else {
                if (current_zero_blocks[i] >= zero_block_threshold)
                    zero_blocks[i].push_back(current_zero_blocks[i]);
                current_zero_blocks[i] = 0;
            }
        }
    }

    //ゼロブロックをチェック
    BOOL check_result[3] = { FALSE, FALSE, FALSE };
    int i;
    for (i = 0; i < 3; i++) {
        if (zero_blocks[i].size() < (size_t)(audio_n * ZERO_BLOCK_COUNT_THRESHOLD / audio_rate))
            continue;
        int zero_sum = 0;
        for (auto zero_len : zero_blocks[i])
            zero_sum += zero_len;
        if (zero_sum < audio_n * ZERO_SUM_RATIO_MIN[i] || zero_sum > audio_n * ZERO_SUM_RATIO_MAX)
            continue;
        double zero_avg = zero_sum / (double)(zero_blocks[i].size());
        double zero_sd = 0;
        for (auto zero_len : zero_blocks[i])
            zero_sd += pow2(zero_len - zero_avg);
        zero_sd = sqrt(zero_sd / (zero_blocks[i].size() - 1));
        if (zero_sd > zero_avg * ZERO_SD_RATIO)
            continue;
        //ここまで来たらFAW
        check_result[i] = TRUE;
    }
    check_result[2] &= check_result[1];
    for (i = 2; i >= 0; i--)
        if (check_result[i])
            break;
    return i + FAW_FULL;
}
