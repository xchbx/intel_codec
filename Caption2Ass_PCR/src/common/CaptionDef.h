//------------------------------------------------------------------------------
// CaptionDef.h
//------------------------------------------------------------------------------
#ifndef __CAPTION_DEF_H__
#define __CAPTION_DEF_H__

#define ERR_FALSE   FALSE   //�ėp�G���[
#define NO_ERR      TRUE    //����

#define ERR_INIT        10
#define ERR_NOT_INIT    11
#define ERR_SIZE        12

#define ERR_NEED_NEXT_PACKET    13  //����TS�p�P�b�g����Ȃ��Ɖ�͂ł��Ȃ�
#define ERR_CAN_NOT_ANALYZ      14  //�{����TS�p�P�b�g�H��͕s�\
#define ERR_NOT_FIRST           15  //�ŏ���TS�p�P�b�g������
#define ERR_INVALID_PACKET      16  //�{����TS�p�P�b�g�H�p�P�b�g���ŉ��Ă邩��

#define CHANGE_VERSION      20
#define NO_ERR_TAG_INFO     21
#define NO_ERR_CAPTION      22

#define SAFE_DELETE(p)       { if (p) { delete (p);   (p) = NULL; } }
#define SAFE_DELETE_ARRAY(p) { if (p) { delete[] (p); (p) = NULL; } }

#define STRING_BUFFER_SIZE      (1024)

#define UNICODE_OFFSET          (2000)

typedef struct _CLUT_DAT_DLL {
    unsigned char ucR;
    unsigned char ucG;
    unsigned char ucB;
    unsigned char ucAlpha;
} CLUT_DAT_DLL;

typedef struct _CAPTION_CHAR_DATA_DLL {
    char *pszDecode;
    DWORD wCharSizeMode;

    CLUT_DAT_DLL stCharColor;
    CLUT_DAT_DLL stBackColor;
    CLUT_DAT_DLL stRasterColor;

    BOOL bUnderLine;
    BOOL bShadow;
    BOOL bBold;
    BOOL bItalic;
    BYTE bFlushMode;
    BYTE bHLC;  //must ignore low 4bits

    WORD wCharW;
    WORD wCharH;
    WORD wCharHInterval;
    WORD wCharVInterval;
} CAPTION_CHAR_DATA_DLL;

typedef struct _CAPTION_DATA_DLL {
    BOOL bClear;
    WORD wSWFMode;
    WORD wClientX;
    WORD wClientY;
    WORD wClientW;
    WORD wClientH;
    WORD wPosX;
    WORD wPosY;
    DWORD dwListCount;
    CAPTION_CHAR_DATA_DLL *pstCharList;
    DWORD dwWaitTime;
} CAPTION_DATA_DLL;

typedef struct _LANG_TAG_INFO_DLL {
    unsigned char ucLangTag;
    unsigned char ucDMF;
    unsigned char ucDC;
    char szISOLangCode[4];
    unsigned char ucFormat;
    unsigned char ucTCS;
    unsigned char ucRollupMode;
} LANG_TAG_INFO_DLL;

#endif // __CAPTION_DEF_H__
