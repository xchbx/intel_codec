//------------------------------------------------------------------------------
// ARIBGaiji.h
//------------------------------------------------------------------------------
#ifndef __ARIB_GAIJI_H__
#define __ARIB_GAIJI_H__

#include <string>

#ifdef GLOBAL
#define EXTERN
#else
#define EXTERN extern
#endif

// ARIB�̒ǉ��L�� �� �ǉ������̃e�[�u����`
// ���̂́A�uCaptionMain.cpp�v�Ő錾

#define ARIB_MAX        495
#define ARIB_MAX2       137

typedef struct _GAIJI_TABLE {
    string usARIB8;
    string strChar;
} GAIJI_TABLE;

EXTERN GAIJI_TABLE GaijiTable[ARIB_MAX];

EXTERN GAIJI_TABLE GaijiTbl2[ARIB_MAX2];

EXTERN BOOL m_bUnicode;

EXTERN BOOL m_bHalfConv;

#endif // __ARIB_GAIJI_H__
