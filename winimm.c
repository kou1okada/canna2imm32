/*  esecannaserver --- pseudo canna server that wraps another IME.
 *  Copyright (C) 1999-2000 Yasuhiro Take
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  ree Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <iconv.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <pwd.h>
#include <sys/types.h>
#include <pthread.h>

#include "def.h"
#include "misc.h"
#include "winimm.h"

#include <errno.h>
extern int errno;
extern void sig_terminate();

typedef struct _winimm_t
{
	void *dummy; /* ���ߡ� */
} winimm_t;

/* �������ݻ����Ƥ���szCompReadStr,szCompStr���ϡ������ޤǤ��Ѵ���Τ��٤Ƥξ���Ǥ��ꡢresize_pause�����椫��λ������Τ����äƤ��� */
typedef struct _context_t
{
	struct _context_t *prev, *next;	/* ����ƥ����ȤΥ���� */
	short context_num;				/* ����ƥ������ֹ� */
	int client_id;					/* ���饤����ȡ�Wnn�ѤȤ�ATOK�ѤȤ��ˤμ��̻� */
	int fIME;						/* ���ߤ����ϥ������IME�����뤫�ɤ��� */
	HIMC hIMC;						/* �����ץ󤷤Ƥ���IM�Υϥ�ɥ��0�ʤ饯�������֡� */

	BOOL fOpen;						/* �����ץ��椫�ݤ� */
	DWORD fdwConversion;			/* ���ʴ������ϥ⡼�� */
	DWORD fdwSentence;				/* ���ʴ����Ѵ��⡼�� */

	/* >> �Ѵ���ξ����ݻ���	*/
	ushort*	szYomiStr;				/* ��������ꤵ�줿�ɤߡʤ����Canna�Υ磻��ʸ����� */
									/* �ϻߤ�ơ�szCompReadStr�����ѡ�Canna�Υ磻��ʸ���ˤ�����Τ�����Ƥ��� */
	LPDWORD	dwYomiCls;				/* �����ɤߤξ���ʥХ���ñ�̡� */
	DWORD dwYomiClsLen;				/* dwYomiCls��Ĺ�� */

	LPWSTR	szCompStr;				/* ���ߤ��Ѵ���ʸ���� */
	LPDWORD	dwCompCls;				/* ʸ��ξ���ʥХ���ñ�̡� */
	DWORD dwCompClsLen;				/* dwCompCls��Ĺ�� */
	LPBYTE	bCompAttr;				/* °���ξ��� */
	DWORD dwCompAttrLen;			/* bCompAttr��Ĺ�� */

	LPWSTR	szCompReadStr;			/* ���ߤ��Ѵ�����ɤߡʤɤ����Ⱦ�Ѥ����� */
	LPDWORD	dwCompReadCls;			/* �ʥХ���ñ�̡� */
	DWORD dwCompReadClsLen;			/*	*/
	LPBYTE	bCompReadAttr;			/* */
	DWORD dwCompReadAttrLen;		/*	*/
	/* << �Ѵ���ξ����ݻ���	*/
} context_t;

static context_t *cx_top;	/* esecanna�ѤΥ���ƥ����ȤΥꥹ�ȡ�context_t�ˤ���Ƭ���ǤؤΥݥ��� */
static client_t *client;	/* esecanna�����äƤ��륯�饤����ȡ�wnn�ѤΥ⥸�塼��Ȥ��ˤؤΥݥ��� */
HWND hWnd_IMM;		/* ���ʴ�ư���ѤΥ�����ɥ� */
/* static HIMC DefaultIMMContext;	*//* hWnd_IMM�Υǥե�������ϥ���ƥ����� */
static short last_context_num;	/* ���ߤΥ���ƥ������ֹ�ʥ���ƥ����Ȥ��Ȥξ��֤�������ѹ��ѡ� */

static HDESK   hdeskCurrent;	/* ������֤Υǥ����ȥå� */
static HDESK   hdesk;			/* �����ץ󤷤��ǥ����ȥå� */
static HWINSTA hwinsta;			/* */
static HWINSTA hwinstaCurrent;	/* */

static int wm_create_done;		/* */

#define WW_ERROR16(_buf) { \
	cannaheader_t *he; \
	he = (cannaheader_t *)(_buf); \
	he->datalen = LSBMSB16(2); \
	he->err.e16 = LSBMSB16(-1); \
}

#define WW_ERROR8(_buf) { \
	cannaheader_t *he; \
	he = (cannaheader_t *)(_buf); \
	he->datalen = LSBMSB16(1); \
	he->err.e8 = -1; \
}

/* kana_table���� �� �򳰤������ */
#define MOD_TEN  0x01 /* �� �������褿���,����(�� + ��= ��)���뤫 */
#define MOD_MARU 0x02 /* �� �������褿���,�������뤫 */
#define MOD_TM   0x03 /* �ϹԤ�, �� �� �� ξ�������դ��� */

/* cannawcs�С������ */
struct {
	ushort han_start, han_end;
	uchar modifiers:4,
			offset:4;
	ushort zenkaku2_start;
} hira_table[] =
{
/*	han_start, han_end, modifiers, offset, zenkaku2_start	*/
	{0xa600,	0xa600,       0,	0,		0xf2},	/* �� */
	{0xa700,	0xab00,       0,	2,		0xa1},	/* ��-�� */
	{0xac00,	0xae00,       0,	2,		0xe3},	/* ��-�� */
	{0xaf00,	0xaf00,       0,	0,		0xc3},	/* �� */
	{0xb100,	0xb500,       0,	2,		0xa2},	/* ��-�� */
	{0xb600,	0xba00,	MOD_TEN,	2,		0xab},	/* ��-�� */
	{0xbb00,	0xbf00,	MOD_TEN,	2,		0xb5},	/* ��-�� */
	{0xc000,	0xc100,	MOD_TEN,	2,		0xbf},	/* ��-�� */
	{0xc200,	0xc200,	MOD_TEN,	0,		0xc4},	/* �� */
	{0xc300,	0xc400,	MOD_TEN,	2,		0xc6},	/* ��-�� */
	{0xc500,	0xc900,       0,	1,		0xca},	/* ��-�� */
	{0xca00,	0xce00,	 MOD_TM,	3,		0xcf},	/* ��-�� */
	{0xcf00,	0xd300,       0,	1,		0xde},	/* ��-�� */
	{0xd400,	0xd600,       0,	2,		0xe4},	/* ��-�� */
	{0xd700,	0xdb00,       0,	1,		0xe9},	/* ��-�� */
	{0xdc00,	0xdc00,       0,	0,		0xef},	/* �� */
	{0xdd00,	0xdd00,       0,	0,		0xf3},	/* �� */
	{     0,	     0,       0,	0,		   0}
};

ushort daku_table[] = 
{
	0xACA4, 0xAEA4, 0xB0A4, 0xB2A4, 0xB4A4,			/* ���������� */
	0xB6A4, 0xB8A4, 0xBAA4, 0xBCA4, 0xBEA4,			/* ���������� */
	0xC0A4, 0xC2A4, 0xC5A4, 0xC7A4, 0xC9A4,			/* ���¤ŤǤ� */
	0xD0A4, 0xD3A4, 0xD6A4, 0xD9A4, 0xDCA4,			/* �ФӤ֤٤� */
	0xD1A4, 0xD4A4, 0xD7A4, 0xDAA4, 0xDDA4,			/* �ѤԤפڤ� */
	0
};


/*
 * wrapper functions ����Ȥ��� �桼�ƥ���ƥ��ؿ�s
 */

/* ����ƥ����ȴ�Ϣ */
/*
  mw_switch_context
  
  ���ꤵ�줿����ƥ����Ⱦ���򸵤ˤ��ʴ��ξ��֤��ڤ��ؤ���
*/
static short mw_switch_context(context_t *cx)
{
	HIMC hIMC = 0;

	if (hWnd_IMM == 0)
		return 0;
	hIMC = ImmGetContext(hWnd_IMM);
	if (hIMC == 0)
		return 0;

	/* �Ѵ���ξ����ݻ����ѿ������� */
	if (cx->szYomiStr != NULL)
		MYFREE(cx->szYomiStr);
	if (cx->dwYomiCls != NULL)
		MYFREE(cx->szYomiStr);
	if (cx->szCompStr != NULL)
		MYFREE(cx->szCompStr);
	if (cx->szCompReadStr != NULL)
		MYFREE(cx->szCompReadStr);
	if (cx->bCompAttr != NULL)
		MYFREE(cx->bCompAttr);
	if (cx->dwCompCls != NULL)
		MYFREE(cx->dwCompCls);
	if (cx->bCompReadAttr != NULL)
		MYFREE(cx->bCompReadAttr);
	if (cx->dwCompReadCls != NULL)
		MYFREE(cx->dwCompReadCls);
	cx->dwYomiClsLen = cx->dwCompClsLen = cx->dwCompAttrLen = cx->dwCompReadClsLen = cx->dwCompReadAttrLen = 0;

	ImmReleaseContext(hWnd_IMM, hIMC);
	return 1;
}

/*
  mw_new_context
  
  ����������ƥ����Ȥξ�����������
*/
static short mw_new_context(int id)
{
	context_t *cx, *new_context;
	short cx_num;

	/* ����ƥ����Ȥξ���Υ��꤬���ʤ��ä��饨�顼 */
	if ((new_context = (context_t *)calloc(1, sizeof(context_t))) == NULL)
		return -1;

	/* new_context�򥳥�ƥ����ȤΥꥹ�Ȥ��Ȥ߹��� */
	cx = cx_top;
	if (cx)
	{
		while (cx->next)
		  cx = cx->next;
		cx->next = new_context;
		new_context->prev = cx;
	} else
	{
		cx_top = new_context;
	}

	/* �ꥹ�ȤκǸ�ʶ������Ǥμ��ˤ�õ���ƶ������Ǥ˸��ߤΥ��饤����Ⱦ�������� */
	cx_num = 1;
	for (;;)
	{
		cx = cx_top;

		for (;;)
		{
			if (cx == NULL)
			{
				new_context->context_num = cx_num;
				new_context->client_id = id;
				new_context->fIME = 0;
				mw_switch_context(new_context);
				return cx_num;
			}

			if (cx->context_num == cx_num)
			{
				cx_num++;
				break;
			}

			cx = cx->next;
		}
	}
}

/*
  mw_get_context
  
  ���ꤵ�줿����ƥ������ֹ�Υ���ƥ����Ⱦ�����������
*/
static context_t *mw_get_context(short cx_num)
{
	context_t *cx;

	if (cx_num == -1)
		return NULL;

	cx = cx_top;

	while (cx)
	{
		if (cx->context_num == cx_num)
		{
			if (cx->context_num != last_context_num)
			{
				mw_switch_context(cx);
				last_context_num = cx->context_num;
			}
			return cx;
		}
		cx = cx->next;
	}

	return NULL;
}

/*
  mw_clear_context
  
  ���ꤵ�줿����ƥ������ֹ�Υ���ƥ����Ⱦ������Ȥ򥯥ꥢ����
*/
static int mw_clear_context(short cx_num)
{
	context_t *cx;

	cx = mw_get_context(cx_num);

	if (cx->szYomiStr != NULL)
		MYFREE(cx->szYomiStr);
	if (cx->dwYomiCls != NULL)
		MYFREE(cx->szYomiStr);
	if (cx->szCompStr != NULL)
		MYFREE(cx->szCompStr);
	if (cx->szCompReadStr != NULL)
		MYFREE(cx->szCompReadStr);
	if (cx->bCompAttr != NULL)
		MYFREE(cx->bCompAttr);
	if (cx->dwCompCls != NULL)
		MYFREE(cx->dwCompCls);
	if (cx->bCompReadAttr != NULL)
		MYFREE(cx->bCompReadAttr);
	if (cx->dwCompReadCls != NULL)
		MYFREE(cx->dwCompReadCls);
	cx->dwYomiClsLen = cx->dwCompClsLen = cx->dwCompAttrLen = cx->dwCompReadClsLen = cx->dwCompReadAttrLen = 0;

	return 0;
}

/*
  mw_free_context
  
  ���ꤵ�줿����ƥ������ֹ�Υ���ƥ����Ⱦ������Τ�������
*/
static int mw_free_context(short cx_num)
{
	context_t *cx;

	cx = mw_get_context(cx_num);
	mw_clear_context(cx_num);

	if (cx->prev)
	  cx->prev->next = cx->next;
	else
	  cx_top = cx->next;
	if (cx->next)
	  cx->next->prev = cx->prev;

	free(cx);

	return 0;
}

/*
 * Wnn ����ƤФ��ؿ�
 */

/* ���ľ�����äƤ�����ɬ�ס� */
static int mw_ime32_message(char *s)
{
  return 0;
}

/*
 *
 */

/*
  mw_open_imm32
  
  ���ʴ�ư���
  esecanna�Υ���ƥ��������Windows�����ϥ���ƥ����Ȥ��������
*/
static int mw_open_imm32(int id, context_t *cx, char *envname)
{
	HIMC hIMC;
	if (hWnd_IMM == 0)
		return 0;

	hIMC = ImmGetContext(hWnd_IMM);

	if (hIMC == 0)
		return 0;
	else
	{
		/* ���֤γ�ǧ */
		HKL hKL = GetKeyboardLayout(0);
		cx->fIME = ImmIsIME(hKL);
		if (cx->fIME != 0)
		{
			ImmGetConversionStatus(hIMC, &(cx->fdwConversion), &(cx->fdwSentence));
			cx->fOpen = ImmGetOpenStatus(hIMC);
		}
		ImmReleaseContext(hWnd_IMM, hIMC);
	}
	return 1;
}

/*
  mw_close_imm32
  
  ��������������ϥ���ƥ����Ȥ��˴������
*/
static int mw_close_imm32(context_t *cx)
{
	if (cx->fIME)
	{
		HIMC hIMC;
		if (hWnd_IMM == 0)
			return 0;
		hIMC = ImmGetContext(hWnd_IMM);
		if (hIMC == 0)
			return 0;

		/* ���������� */
		if (ImmGetOpenStatus(hIMC) == TRUE)
		{
			ImmSetOpenStatus(hIMC, FALSE);
			cx->fOpen = FALSE;
		}
		ImmReleaseContext(hWnd_IMM, hIMC);
	}
	return 1;
}

/*
 * wcs2ucs() - ����ʤǻȤ���磻�ɥ���饯������UCS-2�� 
 * len��ʸ�������Ȥꤢ����cannawc2euc()��euc2sjis()��Ȥäƽ񤤤Ƥ������ɡ�
 * ��ǰ�Ĥˤ��褦
 */
static LPWSTR mw_wcs2ucs(ushort *src)
{
	int wclen, euclen;
	char *workeuc;
	LPWSTR dst;
	static buffer_t zbuf;
	iconv_t cd = (iconv_t)-1;
	size_t status, inbytesleft, outbytesleft;
	char *inbuf, *outbuf;

	/* ����ǻ��Ѥ���euc�ѤΥХåե����� */
	wclen = cannawcstrlen(src);
	workeuc = (char*)calloc(1, wclen * 3);

	/* ��öeuc�� */
	euclen = cannawc2euc(src, wclen, workeuc, wclen * 3);	/* cannawc2euc()��src��Ĺ����ʸ������dest��Ĺ���ϥХ��ȿ�������ͤϥХ��ȿ� */

	outbytesleft = (wclen + 1) * 2;
	buffer_check(&zbuf, outbytesleft);	/* UCS�ϰ�ʸ��2byte */
	dst = (LPWSTR)(zbuf.buf);

	/* euc����UCS2�� */
	cd = iconv_open("UCS-2-INTERNAL", "EUC-JP");
	if (cd == (iconv_t)-1)
	{
		dst = NULL;
		goto end_exit;
	}

	inbytesleft = euclen;
	inbuf = workeuc;
	outbuf = (char*)dst;
	status = iconv(cd, (const char **)&inbuf, &inbytesleft, &outbuf, &outbytesleft);
	if (status == (size_t)-1)
	{
		dst = NULL;
		goto end_exit;
	}

	dst[(((wclen + 1) * 2) - outbytesleft) / 2] = L'\0';
end_exit:
	free(workeuc);
	if (cd != (iconv_t)-1)
		iconv_close(cd);
	return dst;
}

/*
 * ucs2wcs() -UCS-2���� ����ʤǻȤ���磻�ɥ���饯���� 
 *
 * srclen: ʸ����
 */
static ushort *mw_ucs2wcs(LPCWSTR src, int srclen)
{
	int euclen;
	char *workeuc;
	ushort *dst;
	static buffer_t zbuf;
	iconv_t cd = (iconv_t)-1;
	size_t inbytesleft, status, outbytesleft;
	uchar *inbuf;
	char *outbuf;
	int i;
	ushort *workucs;

	/* ����ǻ��Ѥ���euc�ѤΥХåե����� */
	workeuc = (char*)calloc(1, srclen * 3 + 1);

	/* ����ǻ��Ѥ���UCS�ѤΥХåե����� */
	workucs = (ushort*)calloc(1, srclen * 2);

	/* UCS-2�����öeuc�� */
	cd = iconv_open("EUC-JP", "UCS-2-INTERNAL");
	if (cd == (iconv_t)-1)
	{
		dst = NULL;
		goto end_exit;
	}

	/* Windows��ͭ�Υ����ɤ�ѥå����� */
	for (i=0; i<srclen; i++)
	{
		switch(src[i])
		{
			case 0x005C:	/* \ */
				workucs[i] = 0x00A5;
				break;
			case 0xFF5E:	/* �� */
				workucs[i] = 0x301C;
				break;
			case 0x2225:	/* �� */
				workucs[i] = 0x2016;
				break;
			case 0xFF0D:	/* �� */
				workucs[i] = 0x2015;
				break;
			case 0xFFE0:	/* �� */
				workucs[i] = 0x00A2;
				break;
			case 0xFFE1:	/* �� */
				workucs[i] = 0x00A3;
				break;
			case 0xFFE2:	/* �� */
				workucs[i] = 0x00AC;
				break;
			default:
				workucs[i] = src[i];
				break;
		}
	}

	inbytesleft = srclen * 2;
	outbytesleft = srclen * 3 + 1;
	inbuf = (char*)&workucs[0];
	outbuf = workeuc;

	while(inbytesleft > 0)
	{
		status = iconv(cd, (const char **)&inbuf, &inbytesleft, &outbuf, &outbytesleft);
		if (status == (size_t)-1)
		{
			/* ɽ���Ǥ��ʤ�ʸ���������Τ�'��'������ */
			*outbuf++ = 0xA1;
			*outbuf++ = 0xA9;
			inbuf += 2;
			inbytesleft -= 2;
			outbytesleft -= 2;
		}
	}

	workeuc[(srclen * 3 + 1) - outbytesleft] = '\0';
	euclen = strlen(workeuc);

	buffer_check(&zbuf, srclen * 2 + 2);
	dst = (ushort *)(zbuf.buf);

	/* euc����cannawc�� */
	euc2cannawc(workeuc, euclen, dst, srclen * 2 + 2);	/* euc2cannawc()��src��Ĺ���ϥХ��ȿ���dest��Ĺ����ʸ����������ͤ�ʸ���� */

end_exit:
	free(workeuc);
	if (cd != (iconv_t)-1)
		iconv_close(cd);
	return dst;
}

/*
 * wcs2sjis() - ����ʤǻȤ���磻�ɥ���饯������sjis�� 
 * len��ʸ�������Ȥꤢ����cannawc2euc()��euc2sjis()��Ȥäƽ񤤤Ƥ������ɡ�
 * ��ǰ�Ĥˤ��褦
 */
static LPSTR mw_wcs2sjis(ushort *src)
{
	int wclen, euclen;
	char *workeuc;
	LPSTR dst;
	static buffer_t zbuf;

	/* ����ǻ��Ѥ���euc�ѤΥХåե����� */
	wclen = cannawcstrlen(src);
	workeuc = (char*)calloc(1, wclen * 3);

	/* ��öeuc�� */
	euclen = cannawc2euc(src, wclen, workeuc, wclen * 3);	/* cannawc2euc()��src��Ĺ����ʸ������dest��Ĺ���ϥХ��ȿ�������ͤϥХ��ȿ� */

	buffer_check(&zbuf, wclen * 2 + 1);	/* sjis��dbcs���������2byte */
	dst = (LPSTR)(zbuf.buf);

	/* euc����sjis�� */
	euc2sjis(workeuc, euclen, dst, wclen * 2 + 1);	/* euc2sjis()��src,dest�Ȥ��Ĺ���ϥХ��ȿ�������ͤϥХ��ȿ� */

	free(workeuc);

	return dst;
}

/*
 * sjis2wcs() -sjis���� ����ʤǻȤ���磻�ɥ���饯���� 
 */
static ushort *mw_sjis2wcs(LPCSTR src, int srclen)
{
/*	int srclen, euclen; */
	int euclen;
	char *workeuc;
	ushort *dst;
	static buffer_t zbuf;

	/* ����ǻ��Ѥ���euc�ѤΥХåե����� */
	workeuc = (char*)calloc(1, srclen * 3);

	/* sjis�����öeuc�� */
	euclen = sjis2euc((uchar*)src, srclen, workeuc, srclen * 3);

	buffer_check(&zbuf, srclen * 2 + 2);
	dst = (ushort *)(zbuf.buf);

	/* euc����cannawc�� */
	euc2cannawc(workeuc, euclen, dst, srclen * 2 + 2);	/* euc2cannawc()��src��Ĺ���ϥХ��ȿ���dest��Ĺ����ʸ����������ͤ�ʸ���� */

	free(workeuc);

	return dst;
}

/*
 * mw_after_conversion() - �Ѵ���Ƥ֡�����ʸ�ᤫ��γ�ʸ��κ�ͥ���������롣
 *
 * ����                  - bun		����ʸ��
 * �����                - nbun		ʸ���
 *                         len_r	
 *                         
 */
static ushort *mw_after_conversion(context_t *cx, HIMC hIMC, int *nbun, int bun, int *len_r)
{
	BOOL nRet = TRUE;
	long BufLen;
	ushort *ret;

	/* ����ޤǤθ���� */
	mw_clear_context(cx->context_num);

	/*  ʸ����֤ξ��� */
	BufLen = ImmGetCompositionStringW(hIMC, GCS_COMPCLAUSE, NULL, 0);
	if ((BufLen == IMM_ERROR_NODATA) || (BufLen == IMM_ERROR_GENERAL) || (BufLen == 0))
	{
		nRet = FALSE;
		goto end_exit;
	} else
	{
		cx->dwCompCls = (LPDWORD)calloc(1, BufLen);
		cx->dwCompClsLen = ImmGetCompositionStringW(hIMC, GCS_COMPCLAUSE, cx->dwCompCls, BufLen);
		*nbun = cx->dwCompClsLen / sizeof(DWORD) - 1; /* �Х��ȥ���������������ǿ����Ѵ� */
	}

	/* �Ѵ���ʸ������� */
	BufLen = ImmGetCompositionStringW(hIMC, GCS_COMPSTR, NULL, 0);	/* �Ѵ������ */
	if ((BufLen == IMM_ERROR_NODATA) || (BufLen == IMM_ERROR_GENERAL) || (BufLen == 0))
	{
		nRet = FALSE;
		goto end_exit;
	} else
	{
		cx->szCompStr = (LPWSTR)calloc(1, BufLen + 1);
		ImmGetCompositionStringW(hIMC, GCS_COMPSTR, cx->szCompStr, BufLen + 1);
	}

	/* �ɤ�ʸ������� */
	BufLen = ImmGetCompositionStringW(hIMC, GCS_COMPREADSTR, NULL, 0);
	if ((BufLen == IMM_ERROR_NODATA) || (BufLen == IMM_ERROR_GENERAL) || (BufLen == 0))
	{
		nRet = FALSE;
		goto end_exit;
	} else
	{
		cx->szCompReadStr = (LPWSTR)calloc(1, BufLen + 1);
		ImmGetCompositionStringW(hIMC, GCS_COMPREADSTR, cx->szCompReadStr, BufLen + 1);
	}

	/* ʸ��°���ξ��� */
	BufLen = ImmGetCompositionStringW(hIMC, GCS_COMPATTR, NULL, 0);
	if ((BufLen == IMM_ERROR_NODATA) || (BufLen == IMM_ERROR_GENERAL) || (BufLen == 0))
	{
		nRet = FALSE;
		goto end_exit;
	} else
	{
		cx->bCompAttr = (LPBYTE)calloc(1, BufLen);
		cx->dwCompAttrLen = ImmGetCompositionStringW(hIMC,GCS_COMPATTR,cx->bCompAttr,BufLen);
	}

	/* �ɤ�ʸ��ξ��� */
	BufLen = ImmGetCompositionStringW(hIMC, GCS_COMPREADCLAUSE, NULL, 0);
	if ((BufLen == IMM_ERROR_NODATA) || (BufLen == IMM_ERROR_GENERAL) || (BufLen == 0))
	{
		nRet = FALSE;
		goto end_exit;
	} else
	{
		cx->dwCompReadCls = (LPDWORD)calloc(1, BufLen);
		cx->dwCompReadClsLen = ImmGetCompositionStringW(hIMC,GCS_COMPREADCLAUSE,cx->dwCompReadCls,BufLen);
	}

	/* �ɤ�°���ξ��� */
	BufLen = ImmGetCompositionStringW(hIMC, GCS_COMPREADATTR, NULL, 0);
	if ((BufLen == IMM_ERROR_NODATA) || (BufLen == IMM_ERROR_GENERAL) || (BufLen == 0))
	{
		nRet = FALSE;
		goto end_exit;
	} else
	{
		cx->bCompReadAttr = (LPBYTE)calloc(1, BufLen);
		cx->dwCompReadAttrLen = ImmGetCompositionStringW(hIMC,GCS_COMPREADATTR,cx->bCompReadAttr,BufLen);
	}

end_exit:
	if (nRet == FALSE)
	{	/* �Ѵ��˼��Ԥ����褦�ʤΤǸ���� */
		mw_clear_context(cx->context_num);
		ret = NULL;
		*len_r = 0;
	} else
	{	/* ���������Τ�cannawc���äƽ�λ */
		ushort *workp;
		int worklen;
		int i;
		static buffer_t zbuf;

		/* �ޤ�ʸ����������� */
		workp = mw_ucs2wcs(&(cx->szCompStr[cx->dwCompCls[bun]]), wcslen(&(cx->szCompStr[cx->dwCompCls[bun]])));
		if (workp == NULL)
		{
			mw_clear_context(cx->context_num);
			ret = NULL;
			*len_r = 0;
			goto end_exit_2;
		}
		worklen = cannawcstrlen(workp);

		/* ɬ�פȤʤ�Хåե������ */
		buffer_check(&zbuf, (worklen + *nbun + 1) * 2);	/* ʸ����ʸ��֤Υ��ڡ����ܥ����ߥ͡��� */
		ret = (ushort *)(zbuf.buf);
		memset((LPVOID)ret, 0, (worklen + *nbun + 1) * 2);

		/* ʸ�ᤴ�Ȥ�sjis->cannawc�Ѵ����� */
		*len_r = 0;

		for (i=bun; i<*nbun; i++)
		{
			workp = mw_ucs2wcs(&(cx->szCompStr[cx->dwCompCls[i]]), cx->dwCompCls[i+1] - cx->dwCompCls[i]);
			if (workp == NULL)
			{
				mw_clear_context(cx->context_num);
				ret = NULL;
				*len_r = 0;
				goto end_exit_2;
			}
			worklen = cannawcstrlen(workp);
			memcpy(ret, workp, worklen * 2);
			*len_r += (worklen + 1);
			ret += (worklen + 1);
		}

		/* �Ǹ������ͤ�ʸ�������Ƭ�ˤ��� */
		ret = (ushort *)(zbuf.buf);
	}
end_exit_2:
	return ret;
}

/*
 * mw_set_target_clause()
 *
 * ���ꤵ�줿ʸ����Ѵ��о�ʸ��ˤ���
 *
 * ����͡�	����: -1		����: ���ߤ�ʸ���ֹ�
 *
 */
static int mw_set_target_clause(context_t *cx, HIMC hIMC, int bun_no)
{
	int fRet = 0, CurClause = -1;
	UINT uClause, uMaxClause, uCnt, uCntRead;
	DWORD i, j;
	BOOL fAttrOK = FALSE, fAttrReadOK = FALSE;
	BYTE bAt;

/* >> ���ߤ��Ѵ��оݤ�ʸ�᤬���ꤵ�줿ʸ��Ȱ��פ��뤫�� */
	uMaxClause = (UINT)(cx->dwCompClsLen / sizeof(DWORD)) - 1;
	if (uMaxClause <= 0)
	{
		return -1;		/* �����Ѵ����ʸ�����̵�� */
	}

	for (CurClause = 0; CurClause < (int)uMaxClause; CurClause++)
	{
		if ((cx->bCompAttr[cx->dwCompCls[CurClause]] == ATTR_TARGET_CONVERTED) ||
			(cx->bCompAttr[cx->dwCompCls[CurClause]] == ATTR_TARGET_NOTCONVERTED))
			break;
	}

	if (CurClause == -1)
	{
		return -1;	/* �Ѵ��оݤ�ʸ�᤬̵���Τ�¿ʬ���顼���� */
	}
/* << ���ߤ��Ѵ��оݤ�ʸ�᤬���ꤵ�줿ʸ��Ȱ��פ��뤫�� */
/* >> ���ߤ��Ѵ��оݤ�ʸ�����ꤵ�줿ʸ��˰�ư */
	if (CurClause != bun_no)
	{
		uMaxClause = (cx->dwCompClsLen / sizeof(DWORD)) - 1;
		uClause = bun_no;
		uCnt = 0;
		if (uClause < uMaxClause)
		{
			for (i=0; i < uMaxClause; i++)
			{
				if (i == uClause)
				{
					switch (cx->bCompAttr[cx->dwCompCls[i]])
					{
						case ATTR_INPUT:
							bAt = ATTR_TARGET_NOTCONVERTED;
							break;
						case ATTR_CONVERTED:
							bAt = ATTR_TARGET_CONVERTED;
							break;
						default:
							bAt = cx->bCompAttr[cx->dwCompCls[i]];
							break;
					}
				} else
				{
					switch (cx->bCompAttr[cx->dwCompCls[i]])
					{
						case ATTR_TARGET_CONVERTED:
							bAt = ATTR_CONVERTED;
							break;
						case ATTR_TARGET_NOTCONVERTED:
							bAt = ATTR_INPUT;
							break;
						default:
							bAt = cx->bCompAttr[cx->dwCompCls[i]];
							break;
					}
				}

				for (j = 0; j < (cx->dwCompCls[i+1] - cx->dwCompCls[i]); j++)
				{
					cx->bCompAttr[uCnt++] = bAt;
				}
			}
			fAttrOK = TRUE;
		}

		uCntRead = 0;

		if (uClause < uMaxClause)
		{
			for (i=0; i < uMaxClause; i++)
			{
				if (i == uClause)
				{
					switch (cx->bCompReadAttr[cx->dwCompReadCls[i]])
					{
						case ATTR_INPUT:
							bAt = ATTR_TARGET_NOTCONVERTED;
							break;
						case ATTR_CONVERTED:
							bAt = ATTR_TARGET_CONVERTED;
							break;
						default:
							bAt = cx->bCompReadAttr[cx->dwCompReadCls[i]];
							break;
					}
				} else
				{
					switch (cx->bCompReadAttr[cx->dwCompReadCls[i]])
					{
						case ATTR_TARGET_CONVERTED:
							bAt = ATTR_CONVERTED;
							break;
						case ATTR_TARGET_NOTCONVERTED:
							bAt = ATTR_INPUT;
							break;
						default:
							bAt = cx->bCompReadAttr[cx->dwCompReadCls[i]];
							break;
					}
				}

				for (j = 0; j < (cx->dwCompReadCls[i+1] - cx->dwCompReadCls[i]); j++)
				{
					cx->bCompReadAttr[uCntRead++] = bAt;
				}
			}
			fAttrReadOK = TRUE;
		}

		if (fAttrReadOK && fAttrOK)
		{
			fRet = ImmSetCompositionStringW(hIMC,SCS_CHANGEATTR,cx->bCompAttr,uCnt,cx->bCompReadAttr,uCntRead);
			if (fRet == FALSE)
			{
				return -1;
			}
		}
	}
/* << ���ߤ��Ѵ��оݤ�ʸ�����ꤵ�줿ʸ��˰�ư */

	return CurClause;
}


/*
 * mw_lookup_hira_table() -
 *
 */
/* cannawc�С������ */
static int mw_lookup_hira_table(ushort kana, int mod)
{
	int i, j;

	j = -1; i = 0;
	while (hira_table[i].han_start)
	{
		if (hira_table[i].han_start <= kana && kana <= hira_table[i].han_end)
		{
			j = i;

			if (mod == 0 || (hira_table[i].modifiers & mod) != 0)
				break;
		}
		i++;
	}

	return j;
}

/*
 * mw_convert_hankana2zenhira() -
 *
 * cannawc ��Ⱦ�ѥ������ʤ������ѤҤ餬�ʤ��Ѵ�����
 *
 */
static int mw_convert_hankana2zenhira(ushort *wcs, int len)
{
	int i, j;
	uchar mod, c;
	ushort c1, c2;
	for (i = 0; i < len; i++)
	{
		mod = 0;
		if ((wcs[i] & 0x8000) == 0x8000)
		{
			if ((wcs[i] & 0xFF00) == 0xDE00)
			{	/* �� */
				wcs[i] = 0xABA1;
			} else if ((wcs[i] & 0xFF00) == 0xDF00)
			{	/* �� */
				wcs[i] = 0xACA1;
			} else
			{
				if (i + 1 < len)
				{
					if ((wcs[i + 1] & 0xFF00) == 0xDE00)
						mod = MOD_TEN;
					if ((wcs[i + 1] & 0xFF00) == 0xDF00)
						mod = MOD_MARU;
				}

				if ((j = mw_lookup_hira_table((wcs[i] & 0xFF00), mod)) != -1)
				{
					mod &= hira_table[j].modifiers; 
					c = hira_table[j].zenkaku2_start;
					c1 = (wcs[i] & 0xFF00) >> 8;
					c2 = (hira_table[j].han_start & 0xFF00) >> 8;
					c += (c1 - c2) * hira_table[j].offset;
					c += mod;

					wcs[i] = 0x00a4 + (((ushort)c) << 8);

					if (mod)
					{
						memmove(&(wcs[i + 1]), &(wcs[i + 2]), (len - (i + 1) + 1) * 2);
						len --;
					}
				}
			}
		}
	}

	wcs[len] = '\0';

	return len;
}

/*
 * mw_get_yomi() - ���ꤵ�줿ʸ����ɤߤ�������롣
 *                 Windows���ɤߤ�Ⱦ�ѥ��ʤ����äƤ���褦�ʤΤǡ���������Ѥ�ľ���Ƥ�롣
 *
 * len_r: ʸ����
 *
 * ���Τ�Canna�磻��ʸ��
 */

static ushort *mw_get_yomi(context_t *cx, int bun_no, int *len_r)
{
/* cannawc�С������ */
	ushort *ret = NULL;
	int len;
	static buffer_t zbuf;
	ushort *workp;
	DWORD i;

	DWORD uMaxClause = (cx->dwCompReadClsLen / sizeof(DWORD)) - 1;

	if (bun_no < uMaxClause)
	{
		len = cx->dwCompReadCls[bun_no + 1] - cx->dwCompReadCls[bun_no];

		buffer_check(&zbuf, (len + 1) * 2);
		ret = (ushort *)(zbuf.buf);
		workp = mw_ucs2wcs(&(cx->szCompReadStr[cx->dwCompReadCls[bun_no]]), len);
		mw_convert_hankana2zenhira(workp, len + 1);
		memcpy(ret, workp, (len + 1) * 2);
		*len_r = cannawcstrlen(ret);
	}

	if (ret == NULL)
	{	/* ̵���ä������� */
		*len_r = 0;
	}

	return ret;
}

/*
 * mw_get_yomi_2() - ���ꤵ�줿ʸ�� *�ʹ�* ���ɤߤ�������롣
 *
 * len_r: ʸ����
 *
 * ���Τ�Canna�磻��ʸ��
 */

static ushort *mw_get_yomi_2(context_t *cx, int bun_no, int *len_r)
{
/* cannawc�С������ */
	ushort *ret = NULL;
	int len;
	static buffer_t zbuf;
	ushort *workp;
	DWORD i;

	DWORD uMaxClause = (cx->dwCompReadClsLen / sizeof(DWORD)) - 1;
	if (bun_no < uMaxClause)
	{
		len = cx->dwCompReadCls[uMaxClause] - cx->dwCompReadCls[bun_no];

		buffer_check(&zbuf, (len + 1) * 2);
		ret = (ushort *)(zbuf.buf);
		workp = mw_ucs2wcs(&(cx->szCompReadStr[cx->dwCompReadCls[bun_no]]), len);
		mw_convert_hankana2zenhira(workp, len + 1);
		memcpy(ret, workp, (len + 1) * 2);
		*len_r = cannawcstrlen(ret);
	}

	if (ret == NULL)
	{	/* ̵���ä������� */
		*len_r = 0;
	}

	return ret;
}

/*
  
  
*/
LRESULT CALLBACK mw_WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
/*
		case WM_INITMENU:
			EnableMenuItem((HMENU)wParam, SC_CLOSE, MF_BYCOMMAND | MF_GRAYED);
			return 0;
*/
		case WM_IME_SETCONTEXT:
m_msg_dbg("WM_IME_SETCONTEXT\n");
			lParam &= ~ISC_SHOWUICANDIDATEWINDOW;
			return (DefWindowProc(hWnd, uMsg, wParam, lParam));

		case WM_IME_COMPOSITION:
m_msg_dbg("WM_IME_COMPOSITION\n");
			/* MS�Υ���ץ�Ͻ��äƤʤ����ɤ����Τ��ʡ�����		*/
			/* �����Ϥ��줬��Imm�δ�λ���ΤΤϤ��ʤ�����ɡ�	*/
			/* API��ȤäƼ¹Ԥ���Ȥ��Ϥ���ʤ��Τ��ʡ�		*/
			return (DefWindowProc(hWnd, uMsg, wParam, lParam));

		case WM_CREATE:
m_msg_dbg("WM_CREATE\n");
			wm_create_done = 1;
			return 0;

		case WM_QUERYENDSESSION:
m_msg_dbg("WM_QUERYENDSESSION\n");
			return TRUE;

		case WM_CLOSE:
		case WM_ENDSESSION:		/* windows�ν�λ���˥����åȤ������� */
m_msg_dbg("WM_CLOSE / WM_ENDSESSION\n");
			sig_terminate();
			return 0;

		case WM_DESTROY:
m_msg_dbg("WM_DESTROY\n");
			PostQuitMessage (0);
			return 0;

		case WM_ACTIVATE:
m_msg_dbg("WM_ACTIVATE\n");
			switch(wParam)
			{
				case WA_INACTIVE:
m_msg_dbg("  WA_INACTIVE\n");
#if 0
					/* �ɤ���inactive�ˤʤ�Ȥ��ʴ�������ʤ��ʤ�Τ�����롣 */
					if (SetActiveWindow(hWnd_IMM) == NULL)
						m_msg("Can't activate\n");
#endif
					break;
				case WA_ACTIVE:
m_msg_dbg("  WA_ACTIVE\n");
					break;
				case WA_CLICKACTIVE:
m_msg_dbg("  WA_CLICKACTIVE\n");
					break;
				default:
m_msg_dbg("  OTHER\n");
					break;
			}
			return 0;

		case WM_IME_NOTIFY:
		{
			switch (wParam)
			{
				case IMN_OPENSTATUSWINDOW:
m_msg_dbg("IMN_OPENSTATUSWINDOW\n");
					break;
				case IMN_CLOSESTATUSWINDOW:
m_msg_dbg("IMN_CLOSESTATUSWINDOW\n");
#if 1
					/* �ɤ���inactive�ˤʤ��IMN_CLOSESTATUSWINDOW����� */
					/* ���ʴ�������ʤ��ʤ�Τ�����롣 */
					if (SetActiveWindow(hWnd_IMM) == NULL)
						m_msg_dbg("Can't activate\n");
#endif
					break;
				case IMN_SETOPENSTATUS:
m_msg_dbg("IMN_SETOPENSTATUS\n");
					break;
				case IMN_SETCONVERSIONMODE:
m_msg_dbg("IMN_SETCONVERSIONMODE\n");
					break;
				case IMN_OPENCANDIDATE:
m_msg_dbg("IMN_OPENCANDIDATE\n");
					break;
				case IMN_CHANGECANDIDATE:
m_msg_dbg("IMN_CHANGECANDIDATE\n");
					break;
				case IMN_CLOSECANDIDATE:
m_msg_dbg("IMN_CLOSECANDIDATE\n");
					break;
				case IMN_GUIDELINE:
m_msg_dbg("IMN_GUIDELINE\n");
					break;
				default:
m_msg_dbg("WM_IME_NOTIFY OTHER: %lx\n", wParam);
					break;
			}
		}
			break;

		case WM_CANNA_FINALIZE :
m_msg_dbg("WM_CANNA_FINALIZE\n");
			return imm32wrapper_finalize((int)wParam, (buffer_t *)lParam);
		case WM_CANNA_CREATE_CONTEXT :
m_msg_dbg("WM_CANNA_CREATE_CONTEXT\n");
			return imm32wrapper_create_context((int)wParam, (buffer_t *)lParam);
		case WM_CANNA_DUPLICATE_CONTEXT :
m_msg_dbg("WM_CANNA_DUPLICATE_CONTEXT\n");
			return imm32wrapper_duplicate_context((int)wParam, (buffer_t *)lParam);
		case WM_CANNA_CLOSE_CONTEXT :
m_msg_dbg("WM_CANNA_CLOSE_CONTEXT\n");
			return imm32wrapper_close_context((int)wParam, (buffer_t *)lParam);
		case WM_CANNA_DEFINE_WORD :
m_msg_dbg("WM_CANNA_DEFINE_WORD\n");
			return imm32wrapper_define_word((int)wParam, (buffer_t *)lParam);
		case WM_CANNA_DELETE_WORD :
m_msg_dbg("WM_CANNA_DELETE_WORD\n");
			return imm32wrapper_delete_word((int)wParam, (buffer_t *)lParam);
		case WM_CANNA_BEGIN_CONVERT :
m_msg_dbg("WM_CANNA_BEGIN_CONVERT\n");
			return imm32wrapper_begin_convert((int)wParam, (buffer_t *)lParam);
		case WM_CANNA_END_CONVERT :
m_msg_dbg("WM_CANNA_END_CONVERT\n");
			return imm32wrapper_end_convert((int)wParam, (buffer_t *)lParam);
		case WM_CANNA_GET_CANDIDACY_LIST :
m_msg_dbg("WM_CANNA_GET_CANDIDACY_LIST\n");
			return imm32wrapper_get_candidacy_list((int)wParam, (buffer_t *)lParam);
		case WM_CANNA_GET_YOMI :
m_msg_dbg("WM_CANNA_GET_YOMI\n");
			return imm32wrapper_get_yomi((int)wParam, (buffer_t *)lParam);
		case WM_CANNA_SUBST_YOMI :
m_msg_dbg("WM_CANNA_SUBST_YOMI\n");
			return imm32wrapper_subst_yomi((int)wParam, (buffer_t *)lParam);
		case WM_CANNA_STORE_YOMI :
m_msg_dbg("WM_CANNA_STORE_YOMI\n");
			return imm32wrapper_store_yomi((int)wParam, (buffer_t *)lParam);
		case WM_CANNA_STORE_RANGE :
m_msg_dbg("WM_CANNA_STORE_RANGE\n");
			return imm32wrapper_store_range((int)wParam, (buffer_t *)lParam);
		case WM_CANNA_GET_LASTYOMI :
m_msg_dbg("WM_CANNA_GET_LASTYOMI\n");
			return imm32wrapper_get_lastyomi((int)wParam, (buffer_t *)lParam);
		case WM_CANNA_FLUSH_YOMI :
m_msg_dbg("WM_CANNA_FLUSH_YOMI\n");
			return imm32wrapper_flush_yomi((int)wParam, (buffer_t *)lParam);
		case WM_CANNA_REMOVE_YOMI :
m_msg_dbg("WM_CANNA_REMOVE_YOMI\n");
			return imm32wrapper_remove_yomi((int)wParam, (buffer_t *)lParam);
		case WM_CANNA_GET_SIMPLEKANJI :
m_msg_dbg("WM_CANNA_GET_SIMPLEKANJI\n");
			return imm32wrapper_get_simplekanji((int)wParam, (buffer_t *)lParam);
		case WM_CANNA_RESIZE_PAUSE :
m_msg_dbg("WM_CANNA_RESIZE_PAUSE\n");
			return imm32wrapper_resize_pause((int)wParam, (buffer_t *)lParam);
		case WM_CANNA_GET_HINSHI :
m_msg_dbg("WM_CANNA_GET_HINSHI\n");
			return imm32wrapper_get_hinshi((int)wParam, (buffer_t *)lParam);
		case WM_CANNA_GET_LEX :
m_msg_dbg("WM_CANNA_GET_LEX\n");
			return imm32wrapper_get_lex((int)wParam, (buffer_t *)lParam);
		case WM_CANNA_GET_STATUS :
m_msg_dbg("WM_CANNA_GET_STATUS\n");
			return imm32wrapper_get_status((int)wParam, (buffer_t *)lParam);
		case WM_CANNA_SET_LOCALE :
m_msg_dbg("WM_CANNA_SET_LOCALE\n");
			return imm32wrapper_set_locale((int)wParam, (buffer_t *)lParam);
		case WM_CANNA_AUTO_CONVERT :
m_msg_dbg("WM_CANNA_AUTO_CONVERT\n");
			return imm32wrapper_auto_convert((int)wParam, (buffer_t *)lParam);

		case WM_CANNA_INITIALIZE :
m_msg_dbg("WM_CANNA_INITIALIZE\n");
			return imm32wrapper_initialize((int)wParam, (char *)lParam);
		case WM_CANNA_INIT_ROOTCLIENT :
m_msg_dbg("WM_CANNA_INIT_ROOTCLIENT\n");
			return imm32wrapper_init_rootclient();
		case WM_CANNA_END_CLIENT :
m_msg_dbg("WM_CANNA_END_CLIENT\n");
			return imm32wrapper_end_client((int)wParam);
		case WM_CANNA_END_ROOTCLIENT :
m_msg_dbg("WM_CANNA_END_ROOTCLIENT\n");
			return imm32wrapper_end_rootclient();
		case WM_CANNA_CLEAR_CLIENT_DATA :
m_msg_dbg("WM_CANNA_CLEAR_CLIENT_DATA\n");
			return imm32wrapper_clear_client_data((int)wParam);

		default:
m_msg_dbg("WM_ othe Message: 0x%lx\n", uMsg);
			return (DefWindowProc(hWnd, uMsg, wParam, lParam));
	}
	return 0L;
}

/*
  
  
*/
static BOOL mw_RegIMMWindow(void)
{
	WNDCLASS wc;
	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = mw_WndProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = (struct HINSTANCE__ *)GetCurrentProcess();	/* ����ǡɵ����ɥϥ�ɥ뤬����餷��	*/
	wc.hIcon = 0;
	wc.hCursor = 0;
	wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
	wc.lpszMenuName = NULL;
	wc.lpszClassName = "WinIMM32";
	return (RegisterClass(&wc));
}

/*
  
  
*/
void mw_IMMWindowMsgLoop(void* pParam)	/* pParam == NULL */
{
	MSG msg;
	HWND hwnd;
	hwnd = CreateWindow("WinIMM32", "", WS_POPUPWINDOW  | WS_SYSMENU, 0,0,0,0, NULL, 0, 0, 0);	/* HWND_MESSAGE������ɥ������Ϥ�����դ��ʤ��餷����ImmXXX�Ϥ�API������դ��ʤ���	*/
	if (hwnd == 0)
	{
		wm_create_done = 1;
		return;
	} else
	{
/*		*((HWND *)pParam) = hwnd;*/
		hWnd_IMM = hwnd;
	}

	SetWindowText(hwnd, "Canna2IMM32");
	ShowWindow(hwnd, SW_SHOW);	/* �ɤ����ShowWindow()���ʤ���ImmSetCompositionString()�Ǥ��ʤ��餷��	*/

	while (GetMessage(&msg, NULL, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
}

/*
 * ���˸�������ؿ�s
 */
/*
 mw_InitWindow
 
*/
int mw_InitWindow(void)
{

	hWnd_IMM = 0;

	/* �ޤ����ߡ��Υ�����ɥ��ѤΥ��饹����Ͽ */
	if (mw_RegIMMWindow() == 0)
	{
		return -1;
	}

	/* ��å������롼���ѤΥ���åɺ��� */
	{
		pthread_t thread;
		pthread_attr_t attr_thread;
		int i;

		wm_create_done = 0;
		i = pthread_create(&thread, NULL, (void *)mw_IMMWindowMsgLoop, (void *)NULL);
		while (wm_create_done == 0)	/* wait */
			Sleep(100);
	}

/*
	{
		HMENU hMenu = GetSystemMenu( hWnd_IMM, FALSE);
		EnableMenuItem(hMenu, SC_MOVE, MF_BYCOMMAND | MF_GRAYED);
	}
*/
	return 0;
}

/*
  imm32wrapper_dl_started

  ���Υ⥸�塼���esecanna���ɤ߹���Ȥ��˰��ٸƤӽФ���롣
  esecanna�������ݻ����Ƥ��륯�饤����Ⱦ���ؤΥݥ��󥿤��ϤäƤ���Τ�
  �������������¸���롣

  Windows�Ǥϡ����ʴ���Ȥ��Τ˥�����ɥ���ɬ�ס����Ϥϥ�����ɥ����տ魯��ˤʤΤ�
  ���ߡ��Υ�����ɥ����������
 */
int imm32wrapper_dl_started(client_t *cl)
{
	/* Windows�ΥС����������å���Unicode�Ϥ�API��Ȥ��Τǡ�Win2K�ʾ�˸��ꤹ�� */
	{
		OSVERSIONINFO osvi;

		/* OSVERSIONINFO ��¤�Τν���� */ 
		memset(&osvi, 0, sizeof(OSVERSIONINFO));

		osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
		GetVersionEx((OSVERSIONINFO*)&osvi);

		/* �С������Υ����å� */
		if ((osvi.dwMajorVersion < 5) || (osvi.dwPlatformId != VER_PLATFORM_WIN32_NT))
		{
			m_msg("Can't exec in this Windows version.\n");
			return -1;
		}
	}

	/* ���ʴ���Ԥ�����Υ��ߡ�������ɥ��κ��� */
/*
	if (mw_InitWindow() != 0)
		return -1;
*/

	/* esecanna�Υ��饤����Ⱦ���¤�ΤؤΥݥ��󥿤򤳤���¦����¸ */
	client = cl;

	cx_top = NULL;	/* �����������Ƥ������ʵ��������������	03.10.17 Y.A.	*/
	last_context_num = -1;	/*  */

	return 0;
}

/* ���ʴ����� */
int imm32wrapper_init_rootclient()
{
	short cx_num;
	context_t *cx;
	int ret;

	m_msg("Initializing root client for IMM.\n");

	if ((cx_num = mw_new_context(IMM32_ROOT_CLIENT)) == -1)
	{
		m_msg("Out of Memory.\n");
		return -1;
	}

	cx = mw_get_context(cx_num);

	ret = mw_open_imm32(IMM32_ROOT_CLIENT, cx, "canna2imm32");

	if (ret != 1)
	{
		m_msg("Cannot connect to IMM. Aborting.\n");
		return -1;
	}

	m_msg("Initialize succeeded.\n");

	return 0;
}

/* ���ʴ���λ */
int imm32wrapper_end_client(int id)
{
	context_t *cx, *cx2;

	cx = cx_top;

	while (cx)
	{
		if (cx->client_id == id)
		{
			cx2 = cx->next;

			mw_close_imm32(cx);
			mw_free_context(cx->context_num);

			cx = cx2;
		} else
			cx = cx->next;
	}

	return 0;
}

/* ���ʴ���λ��_end_client()��Ƥ֡� */
int imm32wrapper_end_rootclient()
{
	imm32wrapper_end_client(IMM32_ROOT_CLIENT);

	return 0;
}

/* ���饤����Ȥξ����õ�� */
int imm32wrapper_clear_client_data(int id)
{
	return 0;
}

/*
 * �֤���ʡפ� Wnn �� wrapping ����ؿ�s
 */
/* ��������� */
int imm32wrapper_initialize(int id, char *conffile)
{
	return mw_new_context(id);
}

/* ��λ���� */
int imm32wrapper_finalize(int id, buffer_t *cbuf)
{
	cannaheader_t *header;
	HIMC hIMC;

	/* hIMC�γ��� */
	if (hWnd_IMM != 0)
	{
		hIMC = ImmGetContext(hWnd_IMM);
		if (hIMC != 0)
		{
			/* �Ѵ�����ʤ鴰λ������ */
/*			ImmNotifyIME(hIMC, NI_COMPOSITIONSTR, CPS_COMPLETE, 0); */	/* ���Ԥ��Ƥ�̵�� */
			ImmReleaseContext(hWnd_IMM, hIMC);
		}
	}

	client[id].need_terminate = TRUE; /* main.c �ǽ�λ�����򤷤Ƥ�餦 */

	header = (cannaheader_t *)cbuf->buf;
	header->type = 0x02;
	header->extra = 0;
	header->datalen = LSBMSB16(1);
	header->err.e8 = 0;

	return 1;
}

/* ����ƥ����Ⱥ��� */
int imm32wrapper_create_context(int id, buffer_t *cbuf)
{
	cannaheader_t *header = (cannaheader_t *)cbuf->buf;
	short *sp = (short *)cbuf->buf;
	short cx_num;

	cx_num = mw_new_context(id);

	header->type = 0x03;
	header->extra = 0;
	header->datalen = LSBMSB16(2);

	sp[2] = LSBMSB16(cx_num);

	return 1;
}

/* ����ƥ�����ʣ�� */
int imm32wrapper_duplicate_context(int id, buffer_t *cbuf)
{
	cannaheader_t *header = (cannaheader_t *)cbuf->buf;
	short *sp = (short *)cbuf->buf;
	short cx_n_new, cx_n_orig;

	cx_n_orig = LSBMSB16(sp[2]);
	cx_n_new = mw_new_context(id);

	header->type = 0x04;
	header->extra = 0;
	header->datalen = LSBMSB16(2);

	sp[2] = LSBMSB16(cx_n_new);

	return 1;
}

/* ����ƥ����Ⱥ�� */
int imm32wrapper_close_context(int id, buffer_t *cbuf)
{
	cannaheader_t *header = (cannaheader_t *)cbuf->buf;
	short *sp = (short *)cbuf->buf;
	short cx_num;
	context_t *cx;

	cx_num = LSBMSB16(sp[2]);
	cx = mw_get_context(cx_num);

	mw_close_imm32(cx);
	mw_free_context(cx->context_num);

	header->type = 0x05;
	header->extra = 0;
	header->datalen = LSBMSB16(1);
	header->err.e8 = 0;

	return 1;
}

/* ñ����Ͽ�ʽ���뤱�ɤ�äƤʤ��� */
int imm32wrapper_define_word(int id, buffer_t *cbuf)
{
	cannaheader_t *header = (cannaheader_t *)cbuf->buf;

	header->datalen = LSBMSB16(1);
	header->err.e8 = -1;

	return 0;
}

/* ñ�����ʽ���뤱�ɤ�äƤʤ��� */
int imm32wrapper_delete_word(int id, buffer_t *cbuf)
{
	cannaheader_t *header = (cannaheader_t *)cbuf->buf;

	header->datalen = LSBMSB16(1);
	header->err.e8 = -1;

	return 0;
}

/*
  imm32wrapper_begin_convert()
  
  �Ѵ��򳫻Ϥ�����Ѵ�ư��Ϥ�������Ϥޤ��
  
*/
int imm32wrapper_begin_convert(int id, buffer_t *cbuf)
{
	cannaheader_t *header = (cannaheader_t *)cbuf->buf;
	ushort *sp = (ushort *)cbuf->buf;
	ushort *cyomi, *ckoho;
	int *ip = (int *)cbuf->buf;
	int cmode, nbun, len;
	ushort cx_num, datalen;
	context_t *cx;

	int nRet = 1;
	BOOL fRet;
	HIMC hIMC = 0;
	LPWSTR iyomi;

	cx_num = LSBMSB16(sp[4]);
	cmode = LSBMSB32(ip[1]);
	cyomi = &sp[5];		/* cyomi: kinput2->canna���ɤ� */

	cx = mw_get_context(cx_num);

	if (cx->fIME == 0)
		mw_open_imm32(id, cx, client[cx->client_id].user);

	if (cx->fIME != 0)
	{
		/* hIMC�γ��� */
		if (hWnd_IMM == 0)
			goto error_exit;

		hIMC = ImmGetContext(hWnd_IMM);
		if (hIMC == 0)
		{
			goto error_exit;
		}

		/* �����ץ󤷤Ƥ��ʤ��ä��饪���ץ󤹤� */
		if (ImmGetOpenStatus(hIMC) != TRUE)
		{
			if (ImmSetOpenStatus(hIMC, TRUE) != TRUE)
			{
				goto error_exit;
			}
		}

		/* ���饤����Ȥ�����ꤵ�줿�ɤߤ���¸ */
		/* �Ȥꤢ�����ȤäƤ�������ɬ�פʤ��ä���ߤ�� */
		len = cannawcstrlen(cyomi);
		cx->szYomiStr = (ushort*)calloc(1, (len * 2) + 2);
		memcpy((void*)cx->szYomiStr, (void*)cyomi, len * 2);

		iyomi = mw_wcs2ucs(cyomi);	/* iyomi: Win32 Imm ���ɤ� */
		if (iyomi == NULL)
		{
			goto error_exit;
		}
		fRet = ImmSetCompositionStringW(hIMC, SCS_SETSTR, NULL, 0, (LPCVOID)(iyomi), wcslen(iyomi) * 2);	/* �ɤ����� */
		if (fRet == FALSE)
		{
			goto error_exit;
		}
		fRet = ImmNotifyIME(hIMC, NI_COMPOSITIONSTR, CPS_CONVERT, 0);	/* �Ѵ��¹� */
		if (fRet == FALSE)
		{
			goto error_exit;
		}

		ckoho = mw_after_conversion(cx, hIMC, &nbun, 0, &len);
		datalen = 2 + len * 2 + 2;

		buffer_check(cbuf, 4 + datalen);
		header = (cannaheader_t *)cbuf->buf;
		sp = (ushort *)cbuf->buf;

		header->type = 0x0f;
		header->extra = 0;
		header->datalen = LSBMSB16(datalen);
		sp[2] = LSBMSB16(nbun);
		memcpy(&(sp[3]), ckoho, len * 2);
		sp[3 + len] = 0;

		if (hIMC != 0)
			ImmReleaseContext(hWnd_IMM, hIMC);

		return 1;
	}

error_exit:
	header->datalen = LSBMSB16(2);
	header->err.e16 = LSBMSB16(-1);

	if (hIMC != 0)
		ImmReleaseContext(hWnd_IMM, hIMC);

	return 1;
}

/*
  imm32wrapper_end_convert()
  
  
  
*/
int imm32wrapper_end_convert(int id, buffer_t *cbuf)
{
	cannaheader_t *header = (cannaheader_t *)cbuf->buf;
	short *sp = (short *)cbuf->buf;
	long *lp = (long *)cbuf->buf;
	short cx_num;
	short bun_num;
	context_t *cx;
	HIMC hIMC = 0;
	short *pList;
	DWORD nMode;

	cx_num = LSBMSB16(sp[2]);
	bun_num = LSBMSB16(sp[3]);
	nMode = LSBMSB32(lp[2]);
	pList = &(sp[6]);
	cx = mw_get_context(cx_num);

	if ((cx->fIME != 0) && (hWnd_IMM != 0))
	{
		hIMC = ImmGetContext(hWnd_IMM);
		if (hIMC != 0)
		{
			if (ImmGetOpenStatus(hIMC) == TRUE)
			{
				if (nMode != 0)
				{	/* �Ǹ���Ѵ�����ꤵ���� */
					int i;
					for (i=0; i<bun_num; i++)
					{
						if (mw_set_target_clause(cx, hIMC, i) >= 0)
						{
							if (LSBMSB16(pList[i]) != 0)
								ImmNotifyIME(hIMC, NI_SELECTCANDIDATESTR, 0, LSBMSB16(pList[i]));
						}
					}
					ImmNotifyIME(hIMC, NI_COMPOSITIONSTR, CPS_COMPLETE, 0);
				}

				/* ���������� */
				ImmSetOpenStatus(hIMC, FALSE);
			}
		}
	}
	mw_clear_context(cx_num);

	header->type = 0x10;
	header->extra = 0;
	header->datalen = LSBMSB16(1);
	header->err.e8 = 0;

	if (hIMC != 0)
		ImmReleaseContext(hWnd_IMM, hIMC);

	return 1;
}

/*
  imm32wrapper_get_candidacy_list()
  
  ���ꤵ�줿ʸ��θ�����ɤߤ��֤�
  
  
  
*/
int imm32wrapper_get_candidacy_list(int id, buffer_t *cbuf)
{
	context_t *cx;
	cannaheader_t *header = (cannaheader_t *)cbuf->buf;
	ushort *sp = (short *)cbuf->buf;
	int bun_no, koho_num = 0, len, pnt, errflag = 0, i;
	short cx_num, datalen;
	ushort *ckoho;
	HIMC hIMC = 0;
	int CurClause;
	DWORD dwRet;
	LPCANDIDATELIST lpCandList;
	long BufLen;

	cx_num = LSBMSB16(sp[2]);
	bun_no = LSBMSB16(sp[3]);

	cx = mw_get_context(cx_num);

	datalen = 6;
	pnt = 6;

	if ((cx->fIME != 0) && (hWnd_IMM != 0))
	{
		hIMC = ImmGetContext(hWnd_IMM);
		if (hIMC != 0)
		{
			/* ����μ��� */
			CurClause = mw_set_target_clause(cx, hIMC, bun_no);
			if (CurClause < 0)
			{	/* ʸ��ΰ�ư�˼��Ԥ��� */
				errflag = 1;
			} else
			{	/* �Ѵ�����ꥹ�Ȥ���� */
				ImmNotifyIME(hIMC, NI_OPENCANDIDATE, 0, 0);			/* �Ѵ�����ꥹ��ɽ�� */
				BufLen = ImmGetCandidateListW(hIMC, 0, NULL, 0);
				lpCandList = (LPCANDIDATELIST)calloc(1, BufLen);
				dwRet = ImmGetCandidateListW(hIMC, 0, lpCandList, BufLen);
				if (dwRet != 0)
				{
					DWORD i;
					LPDWORD lpdwOffset;
					lpdwOffset = &lpCandList->dwOffset[0];

					for (i = 0; i < lpCandList->dwCount; i++)
					{
						LPWSTR lpstr = (LPWSTR)((LPSTR)lpCandList + *lpdwOffset++);

						ckoho = mw_ucs2wcs(lpstr, wcslen(lpstr));
						len = (cannawcstrlen(ckoho) * 2) + 2;

						datalen += len;
						buffer_check(cbuf, datalen);

						memcpy(&(cbuf->buf[pnt]), ckoho, len);
						pnt += len;
						koho_num ++;
					}
				} else
				{
					errflag = 1;
				}

				MYFREE(lpCandList);
			}
		} else
		{
			errflag = 1;
		}

		if (errflag == 0)
		{
			datalen += 2;
			buffer_check(cbuf, datalen);
			header = (cannaheader_t *)cbuf->buf;
			sp = (ushort *)cbuf->buf;
			cbuf->buf[pnt++] = 0;
			cbuf->buf[pnt++] = 0;

			sp[2] = LSBMSB16(koho_num);

			header->type = 0x11;
			header->extra = 0;
			header->datalen = LSBMSB16(datalen);

			if (hIMC != 0)
				ImmReleaseContext(hWnd_IMM, hIMC);

			return 1;
		}
	}

	header->datalen = LSBMSB16(2);
	header->err.e16 = LSBMSB16(-1);

	if (hIMC != 0)
		ImmReleaseContext(hWnd_IMM, hIMC);

	return 1;
}

/*
  imm32wrapper_get_yomi()
  
  ���ꤵ�줿ʸ����ɤߤ��֤�
  
  
  
*/
int imm32wrapper_get_yomi(int id, buffer_t *cbuf)
{
/* cannawc�С������ */
	cannaheader_t *header = (cannaheader_t *)cbuf->buf;
	ushort *sp = (short *)cbuf->buf;
	ushort *cyomi;
	short cx_num, bun_no, datalen;
	context_t *cx;
	int len, byte;

	cx_num = LSBMSB16(sp[2]);
	bun_no = LSBMSB16(sp[3]);

	cx = mw_get_context(cx_num);

	if ((cyomi = mw_get_yomi(cx, bun_no, &len)) != NULL)
	{
		byte = (len + 1) * 2;

		datalen = 2 + byte;

		buffer_check(cbuf, datalen + 4);
		sp = (ushort *)cbuf->buf;
		header = (cannaheader_t *)cbuf->buf;

		header->type = 0x12;
		header->extra = 0;
		header->datalen = LSBMSB16(datalen);
		sp[2] = LSBMSB16(len);
		memcpy(&(cbuf->buf[6]), cyomi, byte);
	} else
	{
		header->type = 0x12;
		header->extra = 0;
		header->datalen = LSBMSB16(2);
		header->err.e16 = LSBMSB16(-1);
	}

	return 1;
}

int imm32wrapper_subst_yomi(int id, buffer_t *cbuf)
{
	WW_ERROR16(cbuf);
	return 1;
}

int imm32wrapper_store_yomi(int id, buffer_t *cbuf)
{
	WW_ERROR16(cbuf);
	return 1;
}

int imm32wrapper_store_range(int id, buffer_t *cbuf)
{
	WW_ERROR8(cbuf);
	return 1;
}

int imm32wrapper_get_lastyomi(int id, buffer_t *cbuf)
{
	WW_ERROR16(cbuf);
	return 1;
}

int imm32wrapper_flush_yomi(int id, buffer_t *cbuf)
{
	WW_ERROR16(cbuf);
	return 1;
}

int imm32wrapper_remove_yomi(int id, buffer_t *cbuf)
{
	WW_ERROR16(cbuf);
	return 1;
}

int imm32wrapper_get_simplekanji(int id, buffer_t *cbuf)
{
	WW_ERROR16(cbuf);
	return 1;
}

/*
  imm32wrapper_resize_pause()
  
  ���ꤵ�줿ʸ�����ꤵ�줿Ĺ�����ѹ����ƺ��Ѵ�����
  
  2004.03.04 ������Ⱦ����������Ȥ��˼��Ԥ���Τ���
  
*/
int imm32wrapper_resize_pause(int id, buffer_t *cbuf)
{
	int curyomilen, oldyomilen;
	short cannayomilen, bun_no, cx_num, datalen;
	int nbun, len;
	short *sp = (short *)cbuf->buf;
	ushort *ckoho;
	ushort *cyomi;
	LPWSTR iyomi;
	cannaheader_t *header = (cannaheader_t *)cbuf->buf;
	context_t *cx;

	UINT uMaxClause;

	cx_num = LSBMSB16(sp[2]);
	bun_no = LSBMSB16(sp[3]);
	cannayomilen = LSBMSB16(sp[4]);

	cx = mw_get_context(cx_num);
	uMaxClause = (cx->dwCompReadClsLen / sizeof(DWORD)) - 1;

	if ((cx->fIME != 0) && (cyomi = mw_get_yomi_2(cx, bun_no, &oldyomilen)) != NULL && hWnd_IMM != 0 && bun_no < uMaxClause)
	{
		DWORD i;
		DWORD dwTargetLen;
		BOOL fRet;
		DWORD dwClsRead[512];	/* �����Ĥϸ��ꥵ��������ޤ������⤷��� */

		HIMC hIMC = ImmGetContext(hWnd_IMM);
		if (hIMC != 0)
		{
			/* �о�ʸ���Ĺ������ */
			if (mw_get_yomi(cx, bun_no, &curyomilen) == NULL)
			{
				ImmReleaseContext(hWnd_IMM, hIMC);
				goto error_exit;
			}
			dwTargetLen = curyomilen;

			switch(cannayomilen)
			{
				case -1:	/* ʸ�῭�Ф� */
					dwTargetLen ++;
					break;
				case -2:	/* ʸ��̤� */
					if (dwTargetLen != 0)
						dwTargetLen --;
					break;
				default:	/* ¨�� */
					dwTargetLen = cannayomilen;
					break;
			}
			if (dwTargetLen > oldyomilen)
			{
				ckoho = mw_after_conversion(cx, hIMC, &nbun, bun_no, &len);	/* nbun�ϥ��ɥ쥹��	03.10.20 Y.A. */
				datalen = 2 + len * 2 + 2;

				buffer_check(cbuf, 4 + datalen);
				header = (cannaheader_t *)cbuf->buf;
				sp = (ushort *)cbuf->buf;

				header->type = 0x1a;
				header->extra = 0;
				header->datalen = LSBMSB16(datalen);
				sp[2] = LSBMSB16(nbun);
				memcpy(&(sp[3]), ckoho, len * 2);
				sp[3 + len] = 0;

				ImmReleaseContext(hWnd_IMM, hIMC);
				return 1;
			}

			/* Ĺ����Ĵ�� */
			if (oldyomilen < dwTargetLen)
				dwTargetLen = oldyomilen;
			cyomi[dwTargetLen] = '\0';
			/* IMM ��ǧ�����Ƥ���Ĺ�����᤹�����Ⱦ�Ѥǥ�����Ȥ��� */
			{
				int ii, len = 0;
				for (ii=0; ii<dwTargetLen; ii++)
				{
					int ij;
					for (ij=0; daku_table[ij] != 0; ij++)
					{
						if (daku_table[ij] == cyomi[ii])
						{
							len++;
							break;
						}
					}
				}
				dwTargetLen += len;
			}

			/* ����ʸ��ξ�����ѹ� */
			for (i=0; i<uMaxClause+1; i++)	/* �Ǹ���ѹ����ƤϤ����ޤ��� */
			{
				if (i == (bun_no + 1))
					dwClsRead[i] = dwClsRead[i - 1] + dwTargetLen;
				else
					dwClsRead[i] = cx->dwCompReadCls[i];
			}

			if (ImmSetCompositionStringW(hIMC,SCS_CHANGECLAUSE,NULL,0,dwClsRead,(uMaxClause+1)*sizeof(DWORD)) == TRUE &&
				ImmNotifyIME(hIMC, NI_COMPOSITIONSTR, CPS_CONVERT, 0) == TRUE)
			{
				ckoho = mw_after_conversion(cx, hIMC, &nbun, bun_no, &len);	/* nbun�ϥ��ɥ쥹��	03.10.20 Y.A. */
				datalen = 2 + len * 2 + 2;

				buffer_check(cbuf, 4 + datalen);
				header = (cannaheader_t *)cbuf->buf;
				sp = (ushort *)cbuf->buf;

				header->type = 0x1a;
				header->extra = 0;
				header->datalen = LSBMSB16(datalen);
				sp[2] = LSBMSB16(nbun);
				memcpy(&(sp[3]), ckoho, len * 2);
				sp[3 + len] = 0;

				ImmReleaseContext(hWnd_IMM, hIMC);
				return 1;
			}
		}
		ImmReleaseContext(hWnd_IMM, hIMC);
	}

error_exit:
	header->datalen = LSBMSB16(2);
	header->err.e16 = LSBMSB16(-1);

	return 1;
}

int imm32wrapper_get_hinshi(int id, buffer_t *cbuf)
{
	WW_ERROR8(cbuf);
	return 1;
}

int imm32wrapper_get_lex(int id, buffer_t *cbuf)
{
	WW_ERROR16(cbuf);
	return 1;
}

/*
  imm32wrapper_get_status()
  
  ���ꤵ�줿ʸ��β��Ͼ�������
  
  
  
*/
int imm32wrapper_get_status(int id, buffer_t *cbuf)
{
	struct
	{
		int bunnum;		/* ������ʸ���ʸ���ֹ� */
		int candnum;	/* �������θ����ֹ� */
		int maxcand;	/* ������ʸ��θ���� */
		int diccand;	/* maxcand - �⡼�ɻ���ʬ�ʤǤ�Ȥꤢ����esecanna�Ǥ�maxcand��Ʊ���� */
		int ylen;		/* �ɤߤ�Ĺ�� */
		int klen;		/* �����ȸ�����ɤߤ��ʤΥХ��ȿ� */
		int tlen;		/* �����ȸ���ι���ñ��� == 1 */
	} stat;

	short bun_no, koho_no, cx_num;
	short *sp = (short *)cbuf->buf;
	int len, koho_num, errflag = 0, ylen, klen;
	cannaheader_t *header = (cannaheader_t *)cbuf->buf;
	context_t *cx;
	HIMC hIMC = 0;
	int CurClause;
	DWORD dwRet;
	LPCANDIDATELIST lpCandList;
	ushort *ckoho;
	long BufLen;

	cx_num = LSBMSB16(sp[2]);
	bun_no = LSBMSB16(sp[3]);
	koho_no = LSBMSB16(sp[4]);

	cx = mw_get_context(cx_num);

	if ((cx->fIME != 0))
	{
		if (mw_get_yomi(cx, bun_no, &ylen) != NULL)
		{
			hIMC = ImmGetContext(hWnd_IMM);
			if (hIMC != 0)
			{
				/* ����μ��� */
				CurClause = mw_set_target_clause(cx, hIMC, bun_no);
				if (CurClause < 0)
				{	/* ʸ��ΰ�ư�˼��Ԥ��� */
					errflag = 1;
				} else
				{	/* �Ѵ�����ꥹ�Ȥ���� */
					ImmNotifyIME(hIMC, NI_OPENCANDIDATE, 0, 0);			/* �Ѵ�����ꥹ��ɽ�� */
					BufLen = ImmGetCandidateListW(hIMC, 0, NULL, 0);
					lpCandList = (LPCANDIDATELIST)calloc(1, BufLen);
					dwRet = ImmGetCandidateListW(hIMC, 0, lpCandList, BufLen);
					if (dwRet != 0 && lpCandList->dwCount > koho_no)
					{	/* �Ѵ����䤬������оݸ����ֹ�θ������ݤ��Ƥ��� */
						LPWSTR lpstr;
						koho_num = (int)(lpCandList->dwCount);
						lpstr = (LPWSTR)((LPSTR)lpCandList + lpCandList->dwOffset[koho_no]);
						ckoho = mw_ucs2wcs(lpstr, wcslen(lpstr));
						klen = cannawcstrlen(ckoho) * 2;
					} else
						errflag = 1;

					MYFREE(lpCandList);
				}
			} else
				errflag = 1;

			if (errflag == 0)
			{
				stat.ylen = LSBMSB32(ylen);	/* �����ȸ�����ɤߤ��ʤΥХ��ȿ� */
				stat.klen = LSBMSB32(klen);	/* �����ȸ���δ�������ΥХ��ȿ� */
				stat.tlen = LSBMSB32(1);	/* �����ȸ���ι���ñ��� */
				stat.maxcand = LSBMSB32(koho_num);	/* ������ʸ��θ���� */
				stat.diccand = LSBMSB32(koho_num);	/* FIXME: maxcand - �⡼�ɻ���ʬ */
				stat.bunnum = LSBMSB32(bun_no);
				stat.candnum = LSBMSB32(koho_no);

				buffer_check(cbuf, 33);
				header->type = 0x1d;
				header->extra = 0;
				header->datalen = LSBMSB16(29);

				cbuf->buf[4] = 0;

				memcpy(&(cbuf->buf[5]), (char *)&stat, 28);

				if (hIMC != 0)
					ImmReleaseContext(hWnd_IMM, hIMC);

				return 1;
			}
		}
	}

	if (hIMC != 0)
		ImmReleaseContext(hWnd_IMM, hIMC);
	header->datalen = LSBMSB16(1);
	header->err.e8 = -1;

	return 1;
}

int imm32wrapper_set_locale(int id, buffer_t *cbuf)
{
	WW_ERROR8(cbuf);
	return 1;
}

int imm32wrapper_auto_convert(int id, buffer_t *cbuf)
{
	WW_ERROR8(cbuf);
	return 1;
}
