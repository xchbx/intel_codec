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
#include <Math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <locale.h>
#include <shlwapi.h>
#pragma comment(lib, "shlwapi.lib")

#include "auo_util.h"
#include "auo_settings.h"
#include "auo_version.h"

static const int INI_SECTION_BUFSIZE = 32768;
static const int INI_KEY_MAX_LEN = 256;

static const int INI_VER = 2;

static const char * const INI_APPENDIX  = ".ini";
static const char * const CONF_APPENDIX = ".conf";

static const char * const STG_DEFAULT_DIRECTORY_APPENDIX = "_stg";

//----    セクション名    ---------------------------------------------------

static const char * const INI_SECTION_MAIN         = "QSVENC";
static const char * const INI_SECTION_APPENDIX     = "APPENDIX";
static const char * const INI_SECTION_VID          = "VIDEO";
static const char * const INI_SECTION_AUD          = "AUDIO";
static const char * const INI_SECTION_MUX          = "MUXER";
static const char * const INI_SECTION_FN           = "FILENAME_REPLACE";
static const char * const INI_SECTION_PREFIX       = "SETTING_";
static const char * const INI_SECTION_MODE         = "MODE_";
static const char * const INI_SECTION_FBC          = "BITRATE_CALC";


static inline double GetPrivateProfileDouble(const char *section, const char *keyname, double defaultValue, const char *ini_file) {
    char buf[INI_KEY_MAX_LEN], str_default[64], *eptr;
    double d;
    sprintf_s(str_default, _countof(str_default), "%f", defaultValue);
    GetPrivateProfileString(section, keyname, str_default, buf, _countof(buf), ini_file);
    d = strtod(buf, &eptr);
    if (*eptr == '\0') return d;
    return defaultValue;
}

static inline void GetFontInfo(const char *section, const char *keyname_base, AUO_FONT_INFO *font_info, const char *ini_file) {
    const size_t keyname_base_len = strlen(keyname_base);
    if (keyname_base_len >= INI_KEY_MAX_LEN)
        return;
    char key[INI_KEY_MAX_LEN];
    memcpy(key, keyname_base, sizeof(key[0]) * (keyname_base_len + 1));
    strcpy_s(key + keyname_base_len, _countof(key) - keyname_base_len, "_name");
    GetPrivateProfileString(section, key, "", font_info->name, sizeof(font_info->name), ini_file);
    strcpy_s(key + keyname_base_len, _countof(key) - keyname_base_len, "_size");
    font_info->size = GetPrivateProfileDouble(section, key, 0.0, ini_file);
    strcpy_s(key + keyname_base_len, _countof(key) - keyname_base_len, "_style");
    font_info->style = GetPrivateProfileInt(section, key, 0, ini_file);
}

static inline void GetColorInfo(const char *section, const char *keyname, int *color_rgb, const int *default_color_rgb, const char *ini_file) {
    char buf[INI_KEY_MAX_LEN], str_default[64];
    sprintf_s(str_default, _countof(str_default), "%d,%d,%d", default_color_rgb[0], default_color_rgb[1], default_color_rgb[2]);
    GetPrivateProfileString(section, keyname, str_default, buf, _countof(buf), ini_file);
    if (3 != sscanf_s(buf, "%d,%d,%d", &color_rgb[0], &color_rgb[1], &color_rgb[2]))
        memcpy(color_rgb, default_color_rgb, sizeof(color_rgb[0]) * 3);
    for (int i = 0; i < 3; i++)
        color_rgb[i] = clamp(color_rgb[i], 0, 255);
}

static inline void WritePrivateProfileInt(const char *section, const char *keyname, int value, const char *ini_file) {
    char tmp[22];
    sprintf_s(tmp, _countof(tmp), "%d", value);
    WritePrivateProfileString(section, keyname, tmp, ini_file);
}

static inline void WritePrivateProfileIntWithDefault(const char *section, const char *keyname, int value, int _default, const char *ini_file) {
    if (value != (int)GetPrivateProfileInt(section, keyname, _default, ini_file))
        WritePrivateProfileInt(section, keyname, value, ini_file);
}

static inline void WritePrivateProfileDouble(const char *section, const char *keyname, double value, const char *ini_file) {
    char tmp[32];
    sprintf_s(tmp, _countof(tmp), "%lf", value);
    WritePrivateProfileString(section, keyname, tmp, ini_file);
}

static inline void WritePrivateProfileDoubleWithDefault(const char *section, const char *keyname, double value, double _default, const char *ini_file) {
    if (abs(value - GetPrivateProfileDouble(section, keyname, _default, ini_file)) > 1.0e-6)
        WritePrivateProfileDouble(section, keyname, value, ini_file);
}

static inline void WriteFontInfo(const char *section, const char *keyname_base, AUO_FONT_INFO *font_info, const char *ini_file) {
    const size_t keyname_base_len = strlen(keyname_base);
    if (keyname_base_len >= INI_KEY_MAX_LEN)
        return;

    AUO_FONT_INFO current_info = { 0 };
    GetFontInfo(section, keyname_base, &current_info, ini_file);

    char key[INI_KEY_MAX_LEN];
    memcpy(key, keyname_base, sizeof(key[0]) * (keyname_base_len + 1));
    if (str_has_char(font_info->name)) {
        strcpy_s(key + keyname_base_len, _countof(key) - keyname_base_len, "_name");
        WritePrivateProfileString(section, key, font_info->name, ini_file);
    }
    if (font_info->size > 0.0 || font_info->size != current_info.size) {
        strcpy_s(key + keyname_base_len, _countof(key) - keyname_base_len, "_size");
        WritePrivateProfileDouble(section, key, font_info->size, ini_file);
    }
    if (font_info->style != 0 || font_info->style != current_info.style) {
        strcpy_s(key + keyname_base_len, _countof(key) - keyname_base_len, "_style");
        WritePrivateProfileInt(section, key, font_info->style, ini_file);
    }
}

static inline void WriteColorInfo(const char *section, const char *keyname, int *color_rgb, const int *default_color_rgb, const char *ini_file) {
    int current_color[3] = { 0 };
    GetColorInfo(section, keyname, current_color, default_color_rgb, ini_file);
    if (0 != memcmp(color_rgb, current_color, sizeof(color_rgb[0]) * 3)) {
        char buf[256];
        sprintf_s(buf, _countof(buf), "%d,%d,%d", color_rgb[0], color_rgb[1], color_rgb[2]);
        WritePrivateProfileString(section, keyname, buf, ini_file);
    }
}


BOOL    guiEx_settings::init = FALSE;
char    guiEx_settings::ini_section_main[256] = { 0 };
char    guiEx_settings::auo_path[MAX_PATH_LEN] = { 0 };
char    guiEx_settings::ini_fileName[MAX_PATH_LEN] = { 0 };
char    guiEx_settings::conf_fileName[MAX_PATH_LEN] = { 0 };
DWORD   guiEx_settings::ini_filesize = 0;

char  guiEx_settings::blog_url[MAX_PATH_LEN] = { 0 };

guiEx_settings::guiEx_settings() {
    initialize(false);
}

guiEx_settings::guiEx_settings(BOOL disable_loading) {
    initialize(disable_loading);
}

guiEx_settings::guiEx_settings(BOOL disable_loading, const char *_auo_path, const char *main_section) {
    initialize(disable_loading, _auo_path, main_section);
}

void guiEx_settings::initialize(BOOL disable_loading) {
    initialize(disable_loading, NULL, NULL);
}

void guiEx_settings::initialize(BOOL disable_loading, const char *_auo_path, const char *main_section) {
    s_aud_count = 0;
    s_mux_count = 0;
    s_aud = NULL;
    s_mux = NULL;
    ZeroMemory(&s_local, sizeof(s_local));
    ZeroMemory(&s_log, sizeof(s_log));
    ZeroMemory(&s_append, sizeof(s_append));
    s_aud_faw_index = FAW_INDEX_ERROR;
    if (!init) {
        if (_auo_path == NULL)
            get_auo_path(auo_path, _countof(auo_path));
        else
            strcpy_s(auo_path, _countof(auo_path), _auo_path);
        strcpy_s(ini_section_main, _countof(ini_section_main), (main_section == NULL) ? INI_SECTION_MAIN : main_section);
        apply_appendix(ini_fileName,  _countof(ini_fileName),  auo_path, INI_APPENDIX);
        apply_appendix(conf_fileName, _countof(conf_fileName), auo_path, CONF_APPENDIX);
        init = check_inifile() && !disable_loading;
        GetPrivateProfileString(ini_section_main, "blog_url", "", blog_url, _countof(blog_url), ini_fileName);
        if (init) {
            load_encode_stg();
            load_fn_replace();
            load_log_win();
            load_append();
        }
    }
}

guiEx_settings::~guiEx_settings() {
    clear_vid();
    clear_aud();
    clear_mux();
    clear_local();
    clear_fn_replace();
    clear_log_win();
    clear_append();
    clear_fbc();
}

BOOL guiEx_settings::check_inifile() {
    BOOL ret = (INI_VER == GetPrivateProfileInt(ini_section_main, "ini_ver", 0, ini_fileName));
    if (ret && !GetFileSizeDWORD(ini_fileName, &ini_filesize))
        ret = FALSE;
    return ret;
}

BOOL guiEx_settings::get_init_success() {
    return get_init_success(FALSE);
}

BOOL guiEx_settings::get_init_success(BOOL no_message) {
    if (!init && !no_message) {
        char mes[1024];
        char title[256];
        strcpy_s(mes, _countof(mes), AUO_NAME);
        sprintf_s(PathFindExtension(mes), _countof(mes) - strlen(mes), 
            ".iniが存在しないか、iniファイルが古いです。\n%s を開始できません。\n"
            "iniファイルを更新してみてください。",
            AUO_FULL_NAME);
        sprintf_s(title, _countof(title), "%s - エラー", AUO_FULL_NAME);
        MessageBox(NULL, mes, title, MB_ICONERROR);
    }
    return init;
}

int guiEx_settings::get_faw_index() {
    for (int i = 0; i < s_aud_count; i++)
        if (stristr(s_aud[i].filename, "faw"))
            return i;
    return FAW_INDEX_ERROR;
}

void guiEx_settings::load_encode_stg() {
    load_vid();
    load_aud();
    load_mux();
    load_local(); //fullpathの情報がきちんと格納されるよう、最後に呼ぶ
}

void guiEx_settings::load_vid() {
    char key[INI_KEY_MAX_LEN];

    clear_vid();

    s_vid_mc.init(ini_filesize);

    s_vid.filename     = s_vid_mc.SetPrivateProfileString(INI_SECTION_VID, "filename", "qsvencc", ini_fileName);
    s_vid.default_cmd  = s_vid_mc.SetPrivateProfileString(INI_SECTION_VID, "cmd_default", "", ini_fileName);
    s_vid.help_cmd     = s_vid_mc.SetPrivateProfileString(INI_SECTION_VID, "cmd_help", "", ini_fileName);

    s_vid_refresh = TRUE;
}

void guiEx_settings::load_aud() {
    int i, j, k;
    char encoder_section[INI_KEY_MAX_LEN];
    char key[INI_KEY_MAX_LEN];

    clear_aud();

    s_aud_count = GetPrivateProfileInt(INI_SECTION_AUD, "count", 0, ini_fileName);

    s_aud_mc.init(ini_filesize + s_aud_count * (sizeof(AUDIO_SETTINGS) + 1024));
    s_aud = (AUDIO_SETTINGS *)s_aud_mc.CutMem(s_aud_count * sizeof(AUDIO_SETTINGS));
    for (i = 0; i < s_aud_count; i++) {
        sprintf_s(key, _countof(key), "audio_encoder_%d", i+1);
        s_aud[i].keyName = s_aud_mc.SetPrivateProfileString(INI_SECTION_AUD, key, "key", ini_fileName);
        sprintf_s(encoder_section, _countof(encoder_section), "%s%s", INI_SECTION_PREFIX, s_aud[i].keyName);
        s_aud[i].dispname     = s_aud_mc.SetPrivateProfileString(encoder_section, "dispname",     "", ini_fileName);
        s_aud[i].filename     = s_aud_mc.SetPrivateProfileString(encoder_section, "filename",     "", ini_fileName);
        s_aud[i].aud_appendix = s_aud_mc.SetPrivateProfileString(encoder_section, "aud_appendix", "", ini_fileName);
        s_aud[i].raw_appendix = s_aud_mc.SetPrivateProfileString(encoder_section, "raw_appendix", "", ini_fileName);
        s_aud[i].cmd_base     = s_aud_mc.SetPrivateProfileString(encoder_section, "base_cmd",     "", ini_fileName);
        s_aud[i].cmd_2pass    = s_aud_mc.SetPrivateProfileString(encoder_section, "2pass_cmd",    "", ini_fileName);
        s_aud[i].cmd_help     = s_aud_mc.SetPrivateProfileString(encoder_section, "help_cmd",     "", ini_fileName);
        s_aud[i].cmd_ver      = s_aud_mc.SetPrivateProfileString(encoder_section, "ver_cmd",      "", ini_fileName);
        s_aud[i].cmd_raw      = s_aud_mc.SetPrivateProfileString(encoder_section, "raw_cmd",      "", ini_fileName);
        s_aud[i].pipe_input   = GetPrivateProfileInt(            encoder_section, "pipe_input",    0, ini_fileName);
        s_aud[i].disable_log  = GetPrivateProfileInt(            encoder_section, "disable_log",   0, ini_fileName);

        sprintf_s(encoder_section, sizeof(encoder_section), "%s%s", INI_SECTION_MODE, s_aud[i].keyName);
        int tmp_count = GetPrivateProfileInt(encoder_section, "count", 0, ini_fileName);
        //置き換えリストの影響で、この段階ではAUDIO_ENC_MODEが最終的に幾つになるのかわからない
        //とりあえず、一時的に読み込んでみる
        s_aud[i].mode_count = tmp_count;
        AUDIO_ENC_MODE *tmp_mode = (AUDIO_ENC_MODE *)s_aud_mc.CutMem(tmp_count * sizeof(AUDIO_ENC_MODE));
        for (j = 0; j < tmp_count; j++) {
            sprintf_s(key, _countof(key), "mode_%d", j+1);
            tmp_mode[j].name = s_aud_mc.SetPrivateProfileString(encoder_section, key, "", ini_fileName);
            const size_t keybase_len = strlen(key);
            strcpy_s(key + keybase_len, _countof(key) - keybase_len, "_cmd");
            tmp_mode[j].cmd = s_aud_mc.SetPrivateProfileString(encoder_section, key, "", ini_fileName);
            strcpy_s(key + keybase_len, _countof(key) - keybase_len, "_2pass");
            tmp_mode[j].enc_2pass = GetPrivateProfileInt(encoder_section, key, 0, ini_fileName);
            strcpy_s(key + keybase_len, _countof(key) - keybase_len, "_convert8bit");
            tmp_mode[j].use_8bit = GetPrivateProfileInt(encoder_section, key, 0, ini_fileName);
            strcpy_s(key + keybase_len, _countof(key) - keybase_len, "_use_remuxer");
            tmp_mode[j].use_remuxer = GetPrivateProfileInt(encoder_section, key, 0, ini_fileName);
            strcpy_s(key + keybase_len, _countof(key) - keybase_len, "_delay");
            tmp_mode[j].delay = GetPrivateProfileInt(encoder_section, key, 0, ini_fileName);
            strcpy_s(key + keybase_len, _countof(key) - keybase_len, "_bitrate");
            tmp_mode[j].bitrate = GetPrivateProfileInt(encoder_section, key, 0, ini_fileName);
            if (tmp_mode[j].bitrate) {
                strcpy_s(key + keybase_len, _countof(key) - keybase_len, "_bitrate_min");
                tmp_mode[j].bitrate_min = GetPrivateProfileInt(encoder_section, key, 0, ini_fileName);
                strcpy_s(key + keybase_len, _countof(key) - keybase_len, "_bitrate_max");
                tmp_mode[j].bitrate_max = GetPrivateProfileInt(encoder_section, key, 0, ini_fileName);
                strcpy_s(key + keybase_len, _countof(key) - keybase_len, "_bitrate_step");
                tmp_mode[j].bitrate_step = GetPrivateProfileInt(encoder_section, key, 0, ini_fileName);
                strcpy_s(key + keybase_len, _countof(key) - keybase_len, "_bitrate_default");
                tmp_mode[j].bitrate_default = GetPrivateProfileInt(encoder_section, key, 0, ini_fileName);
            } else {
                strcpy_s(key + keybase_len, _countof(key) - keybase_len, "_dispList");
                tmp_mode[j].disp_list = s_aud_mc.SetPrivateProfileString(encoder_section, key, "", ini_fileName);
                s_aud_mc.CutMem(sizeof(key[0]));
                strcpy_s(key + keybase_len, _countof(key) - keybase_len, "_cmdList");
                tmp_mode[j].cmd_list = s_aud_mc.SetPrivateProfileString(encoder_section, key, "", ini_fileName);
                s_aud_mc.CutMem(sizeof(key[0]));
                //リストのcmd置き換えリストの","の数分AUDIO_ENC_MODEは増える
                if (!tmp_mode[j].bitrate)
                    s_aud[i].mode_count += countchr(tmp_mode[j].cmd_list, ',');
            }
        }
        s_aud[i].mode = (AUDIO_ENC_MODE *)s_aud_mc.CutMem(s_aud[i].mode_count * sizeof(AUDIO_ENC_MODE));
        j = 0;
        for (int tmp_index = 0; tmp_index < tmp_count; tmp_index++) {
            if (tmp_mode[tmp_index].bitrate) {
                memcpy(&s_aud[i].mode[j], &tmp_mode[tmp_index], sizeof(AUDIO_ENC_MODE));
                j++;
            } else {
                //置き換えリストを分解する
                char *p, *q;
                int list_count = countchr(tmp_mode[tmp_index].cmd_list, ',') + 1;
                //分解した先頭へのポインタへのポインタ用領域を確保
                char **cmd_list  = (char**)s_aud_mc.CutMem(sizeof(char*) * list_count);
                char **disp_list = (char**)s_aud_mc.CutMem(sizeof(char*) * list_count);
                //cmdの置き換えリストを","により分解
                cmd_list[0] = tmp_mode[tmp_index].cmd_list;
                for (k = 0, p = cmd_list[0];  (cmd_list[k] = strtok_s(p, ",", &q))  != NULL; k++)
                    p = NULL;
                //同様に表示用リストを分解
                disp_list[0] = tmp_mode[tmp_index].disp_list;
                for (k = 0, p = disp_list[0]; (disp_list[k] = strtok_s(p, ",", &q)) != NULL; k++)
                    p = NULL;
                //リストの個数分、置き換えを行ったAUDIO_ENC_MODEを作成する
                for (k = 0; k < list_count; j++, k++) {
                    memcpy(&s_aud[i].mode[j], &tmp_mode[tmp_index], sizeof(AUDIO_ENC_MODE));

                    if (cmd_list[k]) {
                        strcpy_s((char *)s_aud_mc.GetPtr(), s_aud_mc.GetRemain() / sizeof(s_aud[i].mode[j].cmd[0]), s_aud[i].mode[j].cmd);
                        replace((char *)s_aud_mc.GetPtr(), s_aud_mc.GetRemain() / sizeof(s_aud[i].mode[j].cmd[0]), "%{cmdList}", cmd_list[k]);
                        s_aud[i].mode[j].cmd = (char *)s_aud_mc.GetPtr();
                        s_aud_mc.CutString(sizeof(s_aud[i].mode[j].cmd[0]));
                    }

                    if (disp_list[k]) {
                        strcpy_s((char *)s_aud_mc.GetPtr(), s_aud_mc.GetRemain() / sizeof(s_aud[i].mode[j].name[0]), s_aud[i].mode[j].name);
                        replace((char *)s_aud_mc.GetPtr(), s_aud_mc.GetRemain() / sizeof(s_aud[i].mode[j].name[0]), "%{dispList}", disp_list[k]);
                        s_aud[i].mode[j].name = (char *)s_aud_mc.GetPtr();
                        s_aud_mc.CutString(sizeof(s_aud[i].mode[j].name[0]));
                    }
                }
            }
        }
    }
    s_aud_faw_index = get_faw_index();
}

void guiEx_settings::load_mux() {
    int i, j;
    size_t len, keybase_len;
    char muxer_section[INI_KEY_MAX_LEN];
    char key[INI_KEY_MAX_LEN];

    static const int MUX_COUNT = 5;
    static const char * MUXER_TYPE[MUX_COUNT]    = { "MUXER_MP4", "MUXER_MKV", "MUXER_TC2MP4", "MUXER_MPG", "MUXER_MP4_RAW" };
    static const char * MUXER_OUT_EXT[MUX_COUNT] = {      ".mp4",      ".mkv",         ".mp4",      ".mpg",          ".mp4" }; 

    clear_mux();


    s_mux_count = MUX_COUNT;
    s_mux_mc.init(ini_filesize + s_mux_count * sizeof(MUXER_SETTINGS));
    s_mux = (MUXER_SETTINGS *)s_mux_mc.CutMem(s_mux_count * sizeof(MUXER_SETTINGS));
    for (i = 0; i < s_mux_count; i++) {
        sprintf_s(muxer_section, _countof(muxer_section), "%s%s", INI_SECTION_PREFIX, MUXER_TYPE[i]);
        len = strlen(MUXER_TYPE[i]);
        s_mux[i].keyName   = (char *)s_mux_mc.CutMem((len + 1) * sizeof(s_mux[i].keyName[0]));
        memcpy(s_mux[i].keyName, MUXER_TYPE[i], (len + 1) * sizeof(s_mux[i].keyName[0]));
        s_mux[i].dispname  = s_mux_mc.SetPrivateProfileString(muxer_section, "dispname",  "", ini_fileName);
        s_mux[i].filename  = s_mux_mc.SetPrivateProfileString(muxer_section, "filename",  "", ini_fileName);
        s_mux[i].base_cmd  = s_mux_mc.SetPrivateProfileString(muxer_section, "base_cmd",  "", ini_fileName);
        s_mux[i].out_ext   = (char *)s_mux_mc.GetPtr();
        strcpy_s(s_mux[i].out_ext, s_mux_mc.GetRemain(), MUXER_OUT_EXT[i]);
        s_mux_mc.CutString(sizeof(s_mux[i].out_ext[0]));
        s_mux[i].vid_cmd   = s_mux_mc.SetPrivateProfileString(muxer_section, "vd_cmd",    "", ini_fileName);
        s_mux[i].aud_cmd   = s_mux_mc.SetPrivateProfileString(muxer_section, "au_cmd",    "", ini_fileName);
        s_mux[i].tc_cmd    = s_mux_mc.SetPrivateProfileString(muxer_section, "tc_cmd",    "", ini_fileName);
        s_mux[i].delay_cmd = s_mux_mc.SetPrivateProfileString(muxer_section, "delay_cmd", "", ini_fileName);
        s_mux[i].tmp_cmd   = s_mux_mc.SetPrivateProfileString(muxer_section, "tmp_cmd",   "", ini_fileName);
        s_mux[i].help_cmd  = s_mux_mc.SetPrivateProfileString(muxer_section, "help_cmd",  "", ini_fileName);
        s_mux[i].ver_cmd   = s_mux_mc.SetPrivateProfileString(muxer_section, "ver_cmd",   "", ini_fileName);
        s_mux[i].post_mux  = GetPrivateProfileInt(muxer_section, "post_mux", MUXER_DISABLED,  ini_fileName);

        sprintf_s(muxer_section, _countof(muxer_section), "%s%s", INI_SECTION_MODE, s_mux[i].keyName);
        s_mux[i].ex_count = GetPrivateProfileInt(muxer_section, "count", 0, ini_fileName);
        s_mux[i].ex_cmd = (MUXER_CMD_EX *)s_mux_mc.CutMem(s_mux[i].ex_count * sizeof(MUXER_CMD_EX));
        for (j = 0; j < s_mux[i].ex_count; j++) {
            sprintf_s(key, _countof(key), "ex_cmd_%d", j+1);
            s_mux[i].ex_cmd[j].cmd  = s_mux_mc.SetPrivateProfileString(muxer_section, key, "", ini_fileName);
            keybase_len = strlen(key);
            strcpy_s(key + keybase_len, _countof(key) - keybase_len, "_name");
            s_mux[i].ex_cmd[j].name = s_mux_mc.SetPrivateProfileString(muxer_section, key, "", ini_fileName);
            strcpy_s(key + keybase_len, _countof(key) - keybase_len, "_apple");
            s_mux[i].ex_cmd[j].cmd_apple = s_mux_mc.SetPrivateProfileString(muxer_section, key, "", ini_fileName);
            strcpy_s(key + keybase_len, _countof(key) - keybase_len, "_chap");
            s_mux[i].ex_cmd[j].chap_file = s_mux_mc.SetPrivateProfileString(muxer_section, key, "", ini_fileName);
        }
    }
}

void guiEx_settings::load_fn_replace() {
    clear_fn_replace();

    fn_rep_mc.init(ini_filesize);

    char *ptr = (char *)fn_rep_mc.GetPtr();
    size_t len = GetPrivateProfileSection(INI_SECTION_FN, ptr, (DWORD)fn_rep_mc.GetRemain() / sizeof(ptr[0]), ini_fileName);
    fn_rep_mc.CutMem((len + 1) * sizeof(ptr[0]));
    for (; *ptr != NULL; ptr += strlen(ptr) + 1) {
        FILENAME_REPLACE rep = { 0 };
        char *p = strchr(ptr, '=');
        rep.from = (p) ? p + 1 : ptr - 1;
        p = strchr(ptr, ':');
        if (p) *p = '\0';
        rep.to   = (p) ? p + 1 : ptr - 1;
        fn_rep.push_back(rep);
    }
}

void guiEx_settings::make_default_stg_dir(char *default_stg_dir, DWORD nSize) {
    //絶対パスで作成
    //strcpy_s(default_stg_dir, nSize, auo_path);
    //相対パスで作成
    GetRelativePathTo(default_stg_dir, nSize, auo_path, NULL);

    char *filename_ptr = PathFindExtension(default_stg_dir);
    strcpy_s(filename_ptr, nSize - (filename_ptr - default_stg_dir), STG_DEFAULT_DIRECTORY_APPENDIX);
}

void guiEx_settings::load_local() {
    char default_stg_dir[MAX_PATH_LEN];
    make_default_stg_dir(default_stg_dir, _countof(default_stg_dir));

    clear_local();

    s_local.large_cmdbox              = GetPrivateProfileInt(   ini_section_main, "large_cmdbox",             DEFAULT_LARGE_CMD_BOX,         conf_fileName);
    s_local.auto_afs_disable          = GetPrivateProfileInt(   ini_section_main, "auto_afs_disable",         DEFAULT_AUTO_AFS_DISABLE,      conf_fileName);
    s_local.default_output_ext        = GetPrivateProfileInt(   ini_section_main, "default_output_ext",       DEFAULT_OUTPUT_EXT,            conf_fileName);
    s_local.auto_del_chap             = GetPrivateProfileInt(   ini_section_main, "auto_del_chap",            DEFAULT_AUTO_DEL_CHAP,         conf_fileName);
    s_local.keep_qp_file              = GetPrivateProfileInt(   ini_section_main, "keep_qp_file",             DEFAULT_KEEP_QP_FILE,          conf_fileName);
    s_local.disable_tooltip_help      = GetPrivateProfileInt(   ini_section_main, "disable_tooltip_help",     DEFAULT_DISABLE_TOOLTIP_HELP,  conf_fileName);
    s_local.disable_visual_styles     = GetPrivateProfileInt(   ini_section_main, "disable_visual_styles",    DEFAULT_DISABLE_VISUAL_STYLES, conf_fileName);
    s_local.enable_stg_esc_key        = GetPrivateProfileInt(   ini_section_main, "enable_stg_esc_key",       DEFAULT_ENABLE_STG_ESC_KEY,    conf_fileName);
    s_local.chap_nero_convert_to_utf8 = GetPrivateProfileInt(   ini_section_main, "chap_nero_convert_to_utf8",DEFAULT_CHAP_NERO_TO_UTF8,     conf_fileName);
    s_local.get_relative_path         = GetPrivateProfileInt(   ini_section_main, "get_relative_path",        DEFAULT_SAVE_RELATIVE_PATH,    conf_fileName);
    s_local.thread_tuning             = GetPrivateProfileInt(   ini_section_main, "thread_tuning",            DEFAULT_THREAD_TUNING,         conf_fileName);
    s_local.timer_period_tuning       = GetPrivateProfileInt(   ini_section_main, "timer_period_tuning",      DEFAULT_TIMER_PERIOD_TUNING,   conf_fileName);
    s_local.run_bat_minimized         = GetPrivateProfileInt(   ini_section_main, "run_bat_minimized",        DEFAULT_RUN_BAT_MINIMIZED,     conf_fileName);
    s_local.force_bluray              = GetPrivateProfileInt(   ini_section_main, "force_bluray",             DEFAULT_FORCE_BLURAY,          conf_fileName);
    s_local.default_audio_encoder     = GetPrivateProfileInt(   ini_section_main, "default_audio_encoder",    DEFAULT_AUDIO_ENCODER,         conf_fileName);
    s_local.perf_monitor              = GetPrivateProfileInt(   ini_section_main, "perf_monitor",             DEFAULT_PERF_MONITOR,          conf_fileName);
    s_local.perf_monitor_plot         = GetPrivateProfileInt(   ini_section_main, "perf_monitor_plot",        DEFAULT_PERF_MONITOR_PLOT,     conf_fileName);

    
    GetFontInfo(ini_section_main, "conf_font", &s_local.conf_font, conf_fileName);

    GetPrivateProfileString(ini_section_main, "custom_tmp_dir",        "", s_local.custom_tmp_dir,        _countof(s_local.custom_tmp_dir),        conf_fileName);
    GetPrivateProfileString(ini_section_main, "custom_audio_tmp_dir",  "", s_local.custom_audio_tmp_dir,  _countof(s_local.custom_audio_tmp_dir),  conf_fileName);
    GetPrivateProfileString(ini_section_main, "custom_mp4box_tmp_dir", "", s_local.custom_mp4box_tmp_dir, _countof(s_local.custom_mp4box_tmp_dir), conf_fileName);
    GetPrivateProfileString(ini_section_main, "stg_dir",  default_stg_dir, s_local.stg_dir,               _countof(s_local.stg_dir),               conf_fileName);
    GetPrivateProfileString(ini_section_main, "last_app_dir",          "", s_local.app_dir,               _countof(s_local.app_dir),               conf_fileName);
    GetPrivateProfileString(ini_section_main, "last_bat_dir",          "", s_local.bat_dir,               _countof(s_local.bat_dir),               conf_fileName);

    //設定ファイル保存場所をチェックする
    if (!str_has_char(s_local.stg_dir) || !PathRootExists(s_local.stg_dir))
        strcpy_s(s_local.stg_dir, _countof(s_local.stg_dir), default_stg_dir);


    s_local.large_cmdbox = 0;
    s_local.audio_buffer_size   = min(GetPrivateProfileInt(ini_section_main, "audio_buffer",        AUDIO_BUFFER_DEFAULT, conf_fileName), AUDIO_BUFFER_MAX);

    GetPrivateProfileString(INI_SECTION_VID, "QSVENCC", "", s_vid.fullpath, _countof(s_vid.fullpath), conf_fileName);
    for (int i = 0; i < s_aud_count; i++)
        GetPrivateProfileString(INI_SECTION_AUD, s_aud[i].keyName, "", s_aud[i].fullpath,     _countof(s_aud[i].fullpath),     conf_fileName);
    for (int i = 0; i < s_mux_count; i++)
        GetPrivateProfileString(INI_SECTION_MUX, s_mux[i].keyName, "", s_mux[i].fullpath,     _countof(s_mux[i].fullpath),     conf_fileName);
}

void guiEx_settings::load_log_win() {
    clear_log_win();
    s_log.minimized          = GetPrivateProfileInt(   ini_section_main, "log_start_minimized",  DEFAULT_LOG_START_MINIMIZED,  conf_fileName);
    s_log.wine_compat        = GetPrivateProfileInt(   ini_section_main, "log_wine_compat",      DEFAULT_LOG_WINE_COMPAT,      conf_fileName);
    s_log.log_level          = GetPrivateProfileInt(   ini_section_main, "log_level",            DEFAULT_LOG_LEVEL,            conf_fileName);
    s_log.transparent        = GetPrivateProfileInt(   ini_section_main, "log_transparent",      DEFAULT_LOG_TRANSPARENT,      conf_fileName);
    s_log.transparency       = GetPrivateProfileInt(   ini_section_main, "log_transparency",     DEFAULT_LOG_TRANSPARENCY,     conf_fileName);
    s_log.auto_save_log      = GetPrivateProfileInt(   ini_section_main, "log_auto_save",        DEFAULT_LOG_AUTO_SAVE,        conf_fileName);
    s_log.auto_save_log_mode = GetPrivateProfileInt(   ini_section_main, "log_auto_save_mode",   DEFAULT_LOG_AUTO_SAVE_MODE,   conf_fileName);
    GetPrivateProfileString(ini_section_main, "log_auto_save_path", "", s_log.auto_save_log_path, _countof(s_log.auto_save_log_path), conf_fileName);
    s_log.show_status_bar    = GetPrivateProfileInt(   ini_section_main, "log_show_status_bar",  DEFAULT_LOG_SHOW_STATUS_BAR,  conf_fileName);
    s_log.taskbar_progress   = GetPrivateProfileInt(   ini_section_main, "log_taskbar_progress", DEFAULT_LOG_TASKBAR_PROGRESS, conf_fileName);
    s_log.save_log_size      = GetPrivateProfileInt(   ini_section_main, "save_log_size",        DEFAULT_LOG_SAVE_SIZE,        conf_fileName);
    s_log.log_width          = GetPrivateProfileInt(   ini_section_main, "log_width",            DEFAULT_LOG_WIDTH,            conf_fileName);
    s_log.log_height         = GetPrivateProfileInt(   ini_section_main, "log_height",           DEFAULT_LOG_HEIGHT,           conf_fileName);
    s_log.log_pos[0]         = GetPrivateProfileInt(   ini_section_main, "log_pos_x",            DEFAULT_LOG_POS[0],           conf_fileName);
    s_log.log_pos[1]         = GetPrivateProfileInt(   ini_section_main, "log_pos_y",            DEFAULT_LOG_POS[1],           conf_fileName);
    GetColorInfo(ini_section_main, "log_color_background",   s_log.log_color_background, DEFAULT_LOG_COLOR_BACKGROUND, conf_fileName);
    GetColorInfo(ini_section_main, "log_color_text_info",    s_log.log_color_text[0],    DEFAULT_LOG_COLOR_TEXT[0],    conf_fileName);
    GetColorInfo(ini_section_main, "log_color_text_warning", s_log.log_color_text[1],    DEFAULT_LOG_COLOR_TEXT[1],    conf_fileName);
    GetColorInfo(ini_section_main, "log_color_text_error",   s_log.log_color_text[2],    DEFAULT_LOG_COLOR_TEXT[2],    conf_fileName);
    GetFontInfo(ini_section_main, "log_font", &s_log.log_font, conf_fileName);
}

void guiEx_settings::load_append() {
    clear_append();
    GetPrivateProfileString(INI_SECTION_APPENDIX, "tc_appendix",         "_tc.txt",      s_append.tc,         _countof(s_append.tc),         ini_fileName);
    GetPrivateProfileString(INI_SECTION_APPENDIX, "qp_appendix",         "_qp.txt",      s_append.qp,         _countof(s_append.qp),         ini_fileName);
    GetPrivateProfileString(INI_SECTION_APPENDIX, "chap_appendix",       "_chapter.txt", s_append.chap,       _countof(s_append.chap),       ini_fileName);
    GetPrivateProfileString(INI_SECTION_APPENDIX, "chap_apple_appendix", "_chapter.txt", s_append.chap_apple, _countof(s_append.chap_apple), ini_fileName);
    GetPrivateProfileString(INI_SECTION_APPENDIX, "wav_appendix",        "_tmp.wav",     s_append.wav,        _countof(s_append.wav),        ini_fileName);
}

void guiEx_settings::load_fbc() {
    clear_fbc();
    s_fbc.calc_bitrate         = GetPrivateProfileInt(   INI_SECTION_FBC, "calc_bitrate",         DEFAULT_FBC_CALC_BITRATE,         conf_fileName);
    s_fbc.calc_time_from_frame = GetPrivateProfileInt(   INI_SECTION_FBC, "calc_time_from_frame", DEFAULT_FBC_CALC_TIME_FROM_FRAME, conf_fileName);
    s_fbc.last_frame_num       = GetPrivateProfileInt(   INI_SECTION_FBC, "last_frame_num",       DEFAULT_FBC_LAST_FRAME_NUM,       conf_fileName);
    s_fbc.last_fps             = GetPrivateProfileDouble(INI_SECTION_FBC, "last_fps",             DEFAULT_FBC_LAST_FPS,             conf_fileName);
    s_fbc.last_time_in_sec     = GetPrivateProfileInt(   INI_SECTION_FBC, "last_time_in_sec",     DEFAULT_FBC_LAST_TIME_IN_SEC,     conf_fileName);
    s_fbc.initial_size         = GetPrivateProfileDouble(INI_SECTION_FBC, "initial_size",         DEFAULT_FBC_INITIAL_SIZE,         conf_fileName);
}

void guiEx_settings::save_local() {
    WritePrivateProfileIntWithDefault(   ini_section_main, "large_cmdbox",              s_local.large_cmdbox,             DEFAULT_LARGE_CMD_BOX,         conf_fileName);
    WritePrivateProfileIntWithDefault(   ini_section_main, "auto_afs_disable",          s_local.auto_afs_disable,         DEFAULT_AUTO_AFS_DISABLE,      conf_fileName);
    WritePrivateProfileIntWithDefault(   ini_section_main, "default_output_ext",        s_local.default_output_ext,       DEFAULT_OUTPUT_EXT,            conf_fileName);
    WritePrivateProfileIntWithDefault(   ini_section_main, "auto_del_chap",             s_local.auto_del_chap,            DEFAULT_AUTO_DEL_CHAP,         conf_fileName);
    WritePrivateProfileIntWithDefault(   ini_section_main, "keep_qp_file",              s_local.keep_qp_file,             DEFAULT_KEEP_QP_FILE,          conf_fileName);
    WritePrivateProfileIntWithDefault(   ini_section_main, "disable_tooltip_help",      s_local.disable_tooltip_help,     DEFAULT_DISABLE_TOOLTIP_HELP,  conf_fileName);
    WritePrivateProfileIntWithDefault(   ini_section_main, "disable_visual_styles",     s_local.disable_visual_styles,    DEFAULT_DISABLE_VISUAL_STYLES, conf_fileName);
    WritePrivateProfileIntWithDefault(   ini_section_main, "enable_stg_esc_key",        s_local.enable_stg_esc_key,       DEFAULT_ENABLE_STG_ESC_KEY,    conf_fileName);
    WritePrivateProfileIntWithDefault(   ini_section_main, "chap_nero_convert_to_utf8", s_local.chap_nero_convert_to_utf8,DEFAULT_CHAP_NERO_TO_UTF8,     conf_fileName);
    WritePrivateProfileIntWithDefault(   ini_section_main, "get_relative_path",         s_local.get_relative_path,        DEFAULT_SAVE_RELATIVE_PATH,    conf_fileName);
    WritePrivateProfileIntWithDefault(   ini_section_main, "thread_tuning",             s_local.thread_tuning,            DEFAULT_THREAD_TUNING,         conf_fileName);
    WritePrivateProfileIntWithDefault(   ini_section_main, "timer_period_tuning",       s_local.timer_period_tuning,      DEFAULT_TIMER_PERIOD_TUNING,   conf_fileName);
    WritePrivateProfileIntWithDefault(   ini_section_main, "run_bat_minimized",         s_local.run_bat_minimized,        DEFAULT_RUN_BAT_MINIMIZED,     conf_fileName);
    WritePrivateProfileIntWithDefault(   ini_section_main, "force_bluray",              s_local.force_bluray,             DEFAULT_FORCE_BLURAY,          conf_fileName);
    WritePrivateProfileIntWithDefault(   ini_section_main, "default_audio_encoder",     s_local.default_audio_encoder,    DEFAULT_AUDIO_ENCODER,         conf_fileName);
    WritePrivateProfileIntWithDefault(   ini_section_main, "perf_monitor",              s_local.perf_monitor,             DEFAULT_PERF_MONITOR,          conf_fileName);
    WritePrivateProfileIntWithDefault(   ini_section_main, "perf_monitor_plot",         s_local.perf_monitor_plot,        DEFAULT_PERF_MONITOR_PLOT,     conf_fileName);


    WriteFontInfo(ini_section_main, "conf_font", &s_local.conf_font, conf_fileName);

    PathRemoveBlanks(s_local.custom_tmp_dir);
    PathRemoveBackslash(s_local.custom_tmp_dir);
    WritePrivateProfileString(ini_section_main, "custom_tmp_dir",        s_local.custom_tmp_dir,        conf_fileName);

    PathRemoveBlanks(s_local.custom_audio_tmp_dir);
    PathRemoveBackslash(s_local.custom_audio_tmp_dir);
    WritePrivateProfileString(ini_section_main, "custom_audio_tmp_dir",  s_local.custom_audio_tmp_dir,  conf_fileName);

    PathRemoveBlanks(s_local.custom_mp4box_tmp_dir);
    PathRemoveBackslash(s_local.custom_mp4box_tmp_dir);
    WritePrivateProfileString(ini_section_main, "custom_mp4box_tmp_dir", s_local.custom_mp4box_tmp_dir, conf_fileName);

    PathRemoveBlanks(s_local.stg_dir);
    PathRemoveBackslash(s_local.stg_dir);
    WritePrivateProfileString(ini_section_main, "stg_dir",               s_local.stg_dir,               conf_fileName);

    PathRemoveBlanks(s_local.app_dir);
    PathRemoveBackslash(s_local.app_dir);
    WritePrivateProfileString(ini_section_main, "last_app_dir",          s_local.app_dir,               conf_fileName);

    PathRemoveBlanks(s_local.bat_dir);
    PathRemoveBackslash(s_local.bat_dir);
    WritePrivateProfileString(ini_section_main, "last_bat_dir",          s_local.bat_dir,               conf_fileName);

    for (int i = 0; i < s_aud_count; i++) {
        PathRemoveBlanks(s_aud[i].fullpath);
        WritePrivateProfileString(INI_SECTION_AUD, s_aud[i].keyName, s_aud[i].fullpath, conf_fileName);
    }
    for (int i = 0; i < s_mux_count; i++) {
        PathRemoveBlanks(s_mux[i].fullpath);
        WritePrivateProfileString(INI_SECTION_MUX, s_mux[i].keyName, s_mux[i].fullpath, conf_fileName);
    }
    WritePrivateProfileString(INI_SECTION_VID, "QSVENCC", s_vid.fullpath, conf_fileName);
}

void guiEx_settings::save_log_win() {
    WritePrivateProfileIntWithDefault(   ini_section_main, "log_start_minimized",   s_log.minimized,          DEFAULT_LOG_START_MINIMIZED,  conf_fileName);
    WritePrivateProfileIntWithDefault(   ini_section_main, "log_wine_compat",       s_log.wine_compat,        DEFAULT_LOG_WINE_COMPAT,      conf_fileName);
    WritePrivateProfileIntWithDefault(   ini_section_main, "log_level",             s_log.log_level,          DEFAULT_LOG_LEVEL,            conf_fileName);
    WritePrivateProfileIntWithDefault(   ini_section_main, "log_transparent",       s_log.transparent,        DEFAULT_LOG_TRANSPARENT,      conf_fileName);
    WritePrivateProfileIntWithDefault(   ini_section_main, "log_transparency",      s_log.transparency,       DEFAULT_LOG_TRANSPARENCY,     conf_fileName);
    WritePrivateProfileIntWithDefault(   ini_section_main, "log_auto_save",         s_log.auto_save_log,      DEFAULT_LOG_AUTO_SAVE,        conf_fileName);
    WritePrivateProfileIntWithDefault(   ini_section_main, "log_auto_save_mode",    s_log.auto_save_log_mode, DEFAULT_LOG_AUTO_SAVE_MODE,   conf_fileName);
    WritePrivateProfileString(ini_section_main, "log_auto_save_path",    s_log.auto_save_log_path, conf_fileName);
    WritePrivateProfileIntWithDefault(   ini_section_main, "log_show_status_bar",   s_log.show_status_bar,    DEFAULT_LOG_SHOW_STATUS_BAR,  conf_fileName);
    WritePrivateProfileIntWithDefault(   ini_section_main, "log_taskbar_progress",  s_log.taskbar_progress,   DEFAULT_LOG_TASKBAR_PROGRESS, conf_fileName);
    WritePrivateProfileIntWithDefault(   ini_section_main, "save_log_size",         s_log.save_log_size,      DEFAULT_LOG_SAVE_SIZE,        conf_fileName);
    WritePrivateProfileIntWithDefault(   ini_section_main, "log_width",             s_log.log_width,          DEFAULT_LOG_WIDTH,            conf_fileName);
    WritePrivateProfileIntWithDefault(   ini_section_main, "log_height",            s_log.log_height,         DEFAULT_LOG_HEIGHT,           conf_fileName);
    WritePrivateProfileIntWithDefault(   ini_section_main, "log_pos_x",             s_log.log_pos[0],         DEFAULT_LOG_POS[0],           conf_fileName);
    WritePrivateProfileIntWithDefault(   ini_section_main, "log_pos_y",             s_log.log_pos[1],         DEFAULT_LOG_POS[1],           conf_fileName);
    WriteColorInfo(ini_section_main, "log_color_background",   s_log.log_color_background, DEFAULT_LOG_COLOR_BACKGROUND, conf_fileName);
    WriteColorInfo(ini_section_main, "log_color_text_info",    s_log.log_color_text[0],    DEFAULT_LOG_COLOR_TEXT[0],    conf_fileName);
    WriteColorInfo(ini_section_main, "log_color_text_warning", s_log.log_color_text[1],    DEFAULT_LOG_COLOR_TEXT[1],    conf_fileName);
    WriteColorInfo(ini_section_main, "log_color_text_error",   s_log.log_color_text[2],    DEFAULT_LOG_COLOR_TEXT[2],    conf_fileName);
    WriteFontInfo(ini_section_main,  "log_font", &s_log.log_font, conf_fileName);
}

void guiEx_settings::save_fbc() {
    WritePrivateProfileIntWithDefault(   INI_SECTION_FBC, "calc_bitrate",         s_fbc.calc_bitrate,         DEFAULT_FBC_CALC_BITRATE,         conf_fileName);
    WritePrivateProfileIntWithDefault(   INI_SECTION_FBC, "calc_time_from_frame", s_fbc.calc_time_from_frame, DEFAULT_FBC_CALC_TIME_FROM_FRAME, conf_fileName);
    WritePrivateProfileIntWithDefault(   INI_SECTION_FBC, "last_frame_num",       s_fbc.last_frame_num,       DEFAULT_FBC_LAST_FRAME_NUM,       conf_fileName);
    WritePrivateProfileDoubleWithDefault(INI_SECTION_FBC, "last_fps",             s_fbc.last_fps,             DEFAULT_FBC_LAST_FPS,             conf_fileName);
    WritePrivateProfileDoubleWithDefault(INI_SECTION_FBC, "last_time_in_sec",     s_fbc.last_time_in_sec,     DEFAULT_FBC_LAST_TIME_IN_SEC,     conf_fileName);
    WritePrivateProfileDoubleWithDefault(INI_SECTION_FBC, "initial_size",         s_fbc.initial_size,         DEFAULT_FBC_INITIAL_SIZE,         conf_fileName);
}

void guiEx_settings::clear_vid() {
    s_vid_mc.clear();
    s_vid_refresh = TRUE;
}

void guiEx_settings::clear_aud() {
    s_aud_mc.clear();
    s_aud_count = 0;
    s_aud_faw_index = FAW_INDEX_ERROR;
}

void guiEx_settings::clear_mux() {
    s_mux_mc.clear();
    s_mux_count = 0;
}

void guiEx_settings::clear_local() {
    ZeroMemory(&s_local, sizeof(s_local));
}

void guiEx_settings::clear_fn_replace() {
    fn_rep_mc.clear();
    fn_rep.clear();
}

void guiEx_settings::clear_log_win() {
    ZeroMemory(&s_log, sizeof(s_log));
}

void guiEx_settings::clear_fbc() {
    ZeroMemory(&s_fbc, sizeof(s_fbc));
}

void guiEx_settings::clear_append() {
    ZeroMemory(&s_append, sizeof(s_append));
}

void guiEx_settings::apply_fn_replace(char *target_filename, DWORD nSize) {
    for (auto i_rep : fn_rep)
        replace(target_filename, nSize, i_rep.from, i_rep.to);
}
