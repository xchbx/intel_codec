#include <windows.h>
#include <stdio.h>
#include <shlwapi.h>
#pragma comment(lib, "shlwapi.lib")
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib") 

#include "output.h"
#include "auo.h"
#include "auo_frm.h"
#include "auo_util.h"
#include "auo_error.h"
#include "auo_version.h"
#include "auo_conf.h"
#include "auo_system.h"

#include "auo_video.h"
#include "auo_audio.h"
#include "auo_faw2aac.h"
#include "auo_mux.h"
#include "auo_encode.h"
#include "auo_runbat.h"

#include "qsv_prm.h"
#include "qsv_query.h"

//---------------------------------------------------------------------
//        输出插件内部变量
//---------------------------------------------------------------------

static CONF_GUIEX g_conf = { 0 };
static SYSTEM_DATA g_sys_dat = { 0 };
static char g_auo_filefilter[1024] = { 0 };


//---------------------------------------------------------------------
//        输出插件结构定义
//---------------------------------------------------------------------
OUTPUT_PLUGIN_TABLE output_plugin_table = {
    NULL,                         //@@@ 标志
    AUO_FULL_NAME,                //@@@ 插件名称
    AUO_EXT_FILTER,               //@@@ 输出文件过滤器
    AUO_VERSION_INFO,             //@@@ 插件信息
    func_init,                    //@@@ 指向DLL开始时要调用的函数的指针（如果为NULL，则不调用）
    func_exit,                    //@@@ 指向DLL结束时要调用的函数的指针（如果为NULL，则不调用）
    func_output,                  //@@@ 指向在输出处调用的函数的指针
    func_config,                  //@@@ 指向请求输出配置对话框时要调用的函数的指针（如果为NULL，则不会调用）
    func_config_get,              //@@@ 获取输出配置数据时要调用的函数的指针（如果为NULL，则不调用）
    func_config_set,              //@@@ 指向设置输出配置数据时调用的函数的指针（如果为NULL，则不调用）
};


//---------------------------------------------------------------------
//@@@        将指针传递到输出插件结构的函数
//---------------------------------------------------------------------
EXTERN_C OUTPUT_PLUGIN_TABLE __declspec(dllexport) * __stdcall GetOutputPluginTable( void ) {
    init_SYSTEM_DATA(&g_sys_dat);
    make_file_filter(NULL, 0, g_sys_dat.exstg->s_local.default_output_ext);
    output_plugin_table.filefilter = g_auo_filefilter;
    return &output_plugin_table;
}

// ------------------------------------------------ ---------------------
//@@@                  输出插件输出功能
// ------------------------------------------------ ---------------------
//
// int flag;    //@@@ 标志
//              //@@@ OUTPUT_INFO_FLAG_VIDEO：带有图像数据
//              //@@@ OUTPUT_INFO_FLAG_AUDIO：带音频数据
//              //@@@ OUTPUT_INFO_FLAG_BATCH：批输出
// int w，h;    //@@@ 垂直和水平尺寸
// int rate，scale;      //@@@ 帧速率
// int n;           //@@@ 帧数
// int size;        //@@@ 一帧中的字节数
// int audio_rate;  //@@@ 音频采样率
// int audio_ch;    //@@@ 音频通道数
// int audio_n;     //@@@ 音频样本数
// int audio_size;  //@@@ 一个音频样本中的字节数
// LPSTR savefile;  //@@@ 指向保存文件名的指针

//void    *(*func_get_video)( int frame );
//                  //@@@ 功能:获取指向DIB格式（RGB24bit）图像数据的指针。
//                  //@@@ 帧：帧号
//                  //@@@ 返回值：指向数据的指针
//                  //@@@         图像数据指针的内容在下一次使用外部函数或将处理返回到主函数之前一直有效

//void    *(*func_get_audio)( int start,int length,int *readed );
//                  //@@@ 功能：获取指向16位PCM格式的音频数据的指针。
//                  //@@@ start：开始样品编号；
//                  //@@@ length：要读取的样本数；
//                  //@@@ readed：已读取的数据指针；
//                  //@@@ return:指向数据的指针
//                  //@@@        音频数据指针的内容在下一次使用外部函数或将处理返回到主函数之前一直有效
//BOOL    (*func_is_abort)( void );
//                  //@@@ 检查是否中断。
//                  //@@@ return:如果中止，则为TRUE
//
//BOOL    (*func_rest_time_disp)( int now,int total );
//                  //@@@ 功能：显示剩余时间。
//                  //@@@ new:  正在处理的帧号
//                  //@@@ total: 要处理的帧总数
//                  //@@@ return : 如果成功，则返回TRUE
// 
//int        (*func_get_flag)( int frame );
//                  //@@@ 功能：获取标志。
//                  //@@@ frame : 帧号
//                  //@@@ return：标志
//                  //@@@ OUTPUT_INFO_FRAME_FLAG_KEYFRAME:建议使用关键帧
//                  //@@@ OUTPUT_INFO_FRAME_FLAG_COPYFRAME:建议复制框架
// 
//BOOL    (*func_update_preview)( void );
//                  //@@@ 功能： 更新预览屏幕。
//                  //@@@ 显示func_get_video读取的最后一帧。
//                  //@@@return:如果成功，则返回TRUE
//void    *(*func_get_video_ex)( int frame,DWORD format );
//                  //@@@ 功能： 以DIB格式获取图像数据。
//                  //@@@ frame：帧号
//                  //@@@ format：图像格式（NULL = RGB24bit /'Y''U''Y''2'= YUY2 /'Y''C''4''8'= PIXEL_YC）
//                  //@@@         PIXEL_YC格式不能在YUY2过滤器模式下使用。
//                  //@@@ return:指向数据的指针
//                  //@@@        图像数据指针的内容在下一次使用外部函数或将处理返回到主函数之前一直有效


BOOL func_init() {
    return TRUE;
}

BOOL func_exit() {
    delete_SYSTEM_DATA(&g_sys_dat);
    return TRUE;
}

BOOL func_output( OUTPUT_INFO *oip ) {
    AUO_RESULT ret = AUO_RESULT_SUCCESS;
    static const encode_task task[3][2] = { { video_output, audio_output }, { audio_output, video_output }, { audio_output_parallel, video_output }  };
    PRM_ENC pe = { 0 };
    CONF_GUIEX conf_out = g_conf;
    const DWORD tm_start_enc = timeGetTime();

    //@@@ 时间初始化
    init_SYSTEM_DATA(&g_sys_dat);
    if (!g_sys_dat.exstg->get_init_success()) return FALSE;

    //@@@ 打开日志窗口
    open_log_window(oip->savefile, &g_sys_dat, 1, 1);
    set_prevent_log_close(TRUE); //※1 start

    //@@@ 进行各种设置
    set_enc_prm(&conf_out, &pe, oip, &g_sys_dat);
    pe.h_p_aviutl = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, GetCurrentProcessId()); //※2 start

    //@@@ 检查并开始编码是否可以编码
    if (check_output(&conf_out, oip, &pe, g_sys_dat.exstg) && setup_afsvideo(oip, &g_sys_dat, &conf_out, &pe)) { //※3 start

        ret |= run_bat_file(&conf_out, oip, &pe, &g_sys_dat, RUN_BAT_BEFORE_PROCESS);

        for (int i = 0; !ret && i < 2; i++)
            ret |= task[conf_out.aud.audio_encode_timing][i](&conf_out, oip, &pe, &g_sys_dat);

        if (!ret)
            ret |= mux(&conf_out, oip, &pe, &g_sys_dat);

        ret |= move_temporary_files(&conf_out, &pe, &g_sys_dat, oip, ret);

        write_log_auo_enc_time("All Enc Time: ", timeGetTime() - tm_start_enc);

        close_afsvideo(&pe); //※3 end

    } else {
        ret |= AUO_RESULT_ERROR;
    }

    if (ret & AUO_RESULT_ABORT) info_encoding_aborted();

    CloseHandle(pe.h_p_aviutl); //※2 end
    set_prevent_log_close(FALSE); //※1 end
    auto_save_log(&conf_out, oip, &pe, &g_sys_dat); //※1 end 结束后处理

    if (!(ret & (AUO_RESULT_ERROR | AUO_RESULT_ABORT)))
        ret |= run_bat_file(&conf_out, oip, &pe, &g_sys_dat, RUN_BAT_AFTER_PROCESS);

    return (ret & AUO_RESULT_ERROR) ? FALSE : TRUE;
}

// ------------------------------------------------ ---------------------
//@@@ 输出插件设置功能
// ------------------------------------------------ ---------------------
//@@@ 部分静音警告C4100
// C4100：该函数的主体中永远不会引用该参数。
#pragma警告（推送）
#pragma warning( push )
#pragma warning( disable: 4100 )
BOOL func_config(HWND hwnd, HINSTANCE dll_hinst) {
    init_SYSTEM_DATA(&g_sys_dat);
    overwrite_aviutl_ini_name();
    if (g_sys_dat.exstg->get_init_success())
        ShowfrmConfig(&g_conf, &g_sys_dat);
    return TRUE;
}
#pragma warning( pop )

int func_config_get( void *data, int size ) {
    if (data && size == sizeof(CONF_GUIEX)) {
        memcpy(data, &g_conf, sizeof(g_conf));
    }

    return sizeof(g_conf);
}

int func_config_set( void *data,int size ) {
    init_SYSTEM_DATA(&g_sys_dat);
    if (!g_sys_dat.exstg->get_init_success(TRUE)) {
        return NULL;
    }
    init_CONF_GUIEX(&g_conf, FALSE);
    return (guiEx_config::adjust_conf_size(&g_conf, data, size)) ? size : NULL;
}


//---------------------------------------------------------------------
//@@@        QSVEnc的其他功能
//---------------------------------------------------------------------
void init_SYSTEM_DATA(SYSTEM_DATA *_sys_dat) {
    if (_sys_dat->init)
        return;
    get_auo_path(_sys_dat->auo_path, _countof(_sys_dat->auo_path));
    get_aviutl_dir(_sys_dat->aviutl_dir, _countof(_sys_dat->aviutl_dir));
    _sys_dat->exstg = new guiEx_settings();
    //set_ex_stg_ptr(_sys_dat->exstg);
    _sys_dat->init = TRUE;
}
void delete_SYSTEM_DATA(SYSTEM_DATA *_sys_dat) {
    if (_sys_dat->init) {
        delete _sys_dat->exstg;
        _sys_dat->exstg = NULL;
        //set_ex_stg_ptr(_sys_dat->exstg);
    }
    _sys_dat->init = FALSE;
}
#pragma warning( push )
#pragma warning( disable: 4100 )
void init_CONF_GUIEX(CONF_GUIEX *conf, BOOL use_10bit) {
    ZeroMemory(conf, sizeof(CONF_GUIEX));
    guiEx_config::write_conf_header(conf);
    conf->vid.resize_width = 1280;
    conf->vid.resize_height = 720;
    conf->aud.encoder = g_sys_dat.exstg->s_local.default_audio_encoder;
    conf->size_all = CONF_INITIALIZED;
}
#pragma warning( pop )
void write_log_auo_line_fmt(int log_type_index, const char *format, ... ) {
    va_list args;
    int len;
    char *buffer;
    va_start(args, format);
    len = _vscprintf(format, args) // _vscprintf doesn't count
                              + 1; // terminating '\0'
    buffer = (char *)malloc(len * sizeof(buffer[0]));
    vsprintf_s(buffer, len, format, args);
    write_log_auo_line(log_type_index, buffer);
    free(buffer);
}


//@@@显示编码时间
void write_log_auo_enc_time(const char *mes, DWORD time) {
    time = ((time + 50) / 100) * 100;
    write_log_auo_line_fmt(LOG_INFO, "%s : %d Hour %2d Min %2d.%1d Sec", 
        mes, 
        time / (60*60*1000),
        (time % (60*60*1000)) / (60*1000), 
        (time % (60*1000)) / 1000,
        ((time % 1000)) / 100);
}

void overwrite_aviutl_ini_name() {
    char ini_file[1024];
    get_aviutl_dir(ini_file, _countof(ini_file));
    PathAddBackSlashLong(ini_file);
    strcat_s(ini_file, _countof(ini_file), "aviutl.ini");
    WritePrivateProfileString(AUO_NAME, "name", NULL, ini_file);
    WritePrivateProfileString(AUO_NAME, "name", AUO_FULL_NAME, ini_file);
}

void overwrite_aviutl_ini_file_filter(int idx) {
    char ini_file[1024];
    get_aviutl_dir(ini_file, _countof(ini_file));
    PathAddBackSlashLong(ini_file);
    strcat_s(ini_file, _countof(ini_file), "aviutl.ini");
    
    char filefilter_ini[1024] = { 0 };
    make_file_filter(filefilter_ini, _countof(filefilter_ini), idx);
    WritePrivateProfileString(AUO_NAME, "filefilter", filefilter_ini, ini_file);
}

void make_file_filter(char *filter, size_t nSize, int default_index) {
    static const char *const TOP = "All Support Formats (*.*)";
    const char separator = (filter) ? '\\' : '\0';
    if (filter == NULL) {
        filter = g_auo_filefilter;
        nSize = _countof(g_auo_filefilter);
    }
    char *ptr = filter;
    
#define ADD_FILTER(str, appendix) { \
    size_t len = strlen(str); \
    if (nSize - (ptr - filter) <= len + 1) return; \
    memcpy(ptr, (str), sizeof(ptr[0]) * len); \
    ptr += len; \
    *ptr = (appendix); \
    ptr++; \
}
#define ADD_DESC(idx) { \
    size_t len = sprintf_s(ptr, nSize - (ptr - filter), "%s (%s)", OUTPUT_FILE_EXT_DESC[idx], OUTPUT_FILE_EXT_FILTER[idx]); \
    ptr += len; \
    *ptr = separator; \
    ptr++; \
    len = strlen(OUTPUT_FILE_EXT_FILTER[idx]); \
    if (nSize - (ptr - filter) <= len + 1) return; \
    memcpy(ptr, OUTPUT_FILE_EXT_FILTER[idx], sizeof(ptr[0]) * len); \
    ptr += len; \
    *ptr = separator; \
    ptr++; \
}
    ADD_FILTER(TOP, separator);
    ADD_FILTER(OUTPUT_FILE_EXT_FILTER[default_index], ';');
    for (int idx = 0; idx < _countof(OUTPUT_FILE_EXT_FILTER); idx++)
        if (idx != default_index)
            ADD_FILTER(OUTPUT_FILE_EXT_FILTER[idx], ';');
    ADD_FILTER(OUTPUT_FILE_EXT_FILTER[default_index], separator);
    ADD_DESC(default_index);
    for (int idx = 0; idx < _countof(OUTPUT_FILE_EXT_FILTER); idx++)
        if (idx != default_index)
            ADD_DESC(idx);
    ptr[0] = '\0';
#undef ADD_FILTER
#undef ADD_DESC
}
