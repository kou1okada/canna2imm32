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
	void *dummy; /* ダミー */
} winimm_t;

/* ここで保持しているszCompReadStr,szCompStr等は、あくまでも変換中のすべての情報であり、resize_pauseで途中からの時も全体が入っている */
typedef struct _context_t
{
	struct _context_t *prev, *next;	/* コンテキストのリンク用 */
	short context_num;				/* コンテキスト番号 */
	int client_id;					/* クライアント（Wnn用とかATOK用とか）の識別子 */
	int fIME;						/* 現在の入力ロケールにIMEがあるかどうか */
	HIMC hIMC;						/* オープンしているIMのハンドル（0ならクローズ状態） */

	BOOL fOpen;						/* オープン中か否か */
	DWORD fdwConversion;			/* かな漢の入力モード */
	DWORD fdwSentence;				/* かな漢の変換モード */

	/* >> 変換中の状態保持用	*/
	ushort*	szYomiStr;				/* 外から指定された読み（これはCannaのワイド文字列） */
									/* は止めて、szCompReadStrを全角・Cannaのワイド文字にしたものを入れておく */
	LPDWORD	dwYomiCls;				/* 全角読みの情報（バイト単位） */
	DWORD dwYomiClsLen;				/* dwYomiClsの長さ */

	LPWSTR	szCompStr;				/* 現在の変換後文字列 */
	LPDWORD	dwCompCls;				/* 文節の情報（バイト単位） */
	DWORD dwCompClsLen;				/* dwCompClsの長さ */
	LPBYTE	bCompAttr;				/* 属性の情報 */
	DWORD dwCompAttrLen;			/* bCompAttrの長さ */

	LPWSTR	szCompReadStr;			/* 現在の変換後の読み（どうやら半角で来る） */
	LPDWORD	dwCompReadCls;			/* （バイト単位） */
	DWORD dwCompReadClsLen;			/*	*/
	LPBYTE	bCompReadAttr;			/* */
	DWORD dwCompReadAttrLen;		/*	*/
	/* << 変換中の状態保持用	*/
} context_t;

static context_t *cx_top;	/* esecanna用のコンテキストのリスト（context_t）の先頭要素へのポインタ */
static client_t *client;	/* esecannaが持っているクライアント（wnn用のモジュールとか）へのポインタ */
HWND hWnd_IMM;		/* かな漢動作用のウィンドウ */
/* static HIMC DefaultIMMContext;	*//* hWnd_IMMのデフォルト入力コンテキスト */
static short last_context_num;	/* 現在のコンテキスト番号（コンテキストごとの状態の設定と変更用） */

static HDESK   hdeskCurrent;	/* 初期状態のデスクトップ */
static HDESK   hdesk;			/* オープンしたデスクトップ */
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

/* kana_tableから ｳ を外したもの */
#define MOD_TEN  0x01 /* ゛ が次に来た場合,合成(カ + ゛= ガ)するか */
#define MOD_MARU 0x02 /* ゜ が次に来た場合,合成するか */
#define MOD_TM   0x03 /* ハ行は, ゛ と ゜ 両方受け付ける */

/* cannawcsバージョン */
struct {
	ushort han_start, han_end;
	uchar modifiers:4,
			offset:4;
	ushort zenkaku2_start;
} hira_table[] =
{
/*	han_start, han_end, modifiers, offset, zenkaku2_start	*/
	{0xa600,	0xa600,       0,	0,		0xf2},	/* ｦ */
	{0xa700,	0xab00,       0,	2,		0xa1},	/* ｧ-ｫ */
	{0xac00,	0xae00,       0,	2,		0xe3},	/* ｬ-ｮ */
	{0xaf00,	0xaf00,       0,	0,		0xc3},	/* ｯ */
	{0xb100,	0xb500,       0,	2,		0xa2},	/* ｱ-ｵ */
	{0xb600,	0xba00,	MOD_TEN,	2,		0xab},	/* ｶ-ｺ */
	{0xbb00,	0xbf00,	MOD_TEN,	2,		0xb5},	/* ｻ-ｿ */
	{0xc000,	0xc100,	MOD_TEN,	2,		0xbf},	/* ﾀ-ﾁ */
	{0xc200,	0xc200,	MOD_TEN,	0,		0xc4},	/* ﾂ */
	{0xc300,	0xc400,	MOD_TEN,	2,		0xc6},	/* ﾃ-ﾄ */
	{0xc500,	0xc900,       0,	1,		0xca},	/* ﾅ-ﾉ */
	{0xca00,	0xce00,	 MOD_TM,	3,		0xcf},	/* ﾊ-ﾎ */
	{0xcf00,	0xd300,       0,	1,		0xde},	/* ﾏ-ﾓ */
	{0xd400,	0xd600,       0,	2,		0xe4},	/* ﾔ-ﾖ */
	{0xd700,	0xdb00,       0,	1,		0xe9},	/* ﾗ-ﾛ */
	{0xdc00,	0xdc00,       0,	0,		0xef},	/* ﾜ */
	{0xdd00,	0xdd00,       0,	0,		0xf3},	/* ﾝ */
	{     0,	     0,       0,	0,		   0}
};

ushort daku_table[] = 
{
	0xACA4, 0xAEA4, 0xB0A4, 0xB2A4, 0xB4A4,			/* がぎぐげご */
	0xB6A4, 0xB8A4, 0xBAA4, 0xBCA4, 0xBEA4,			/* ざじずぜぞ */
	0xC0A4, 0xC2A4, 0xC5A4, 0xC7A4, 0xC9A4,			/* だぢづでど */
	0xD0A4, 0xD3A4, 0xD6A4, 0xD9A4, 0xDCA4,			/* ばびぶべぼ */
	0xD1A4, 0xD4A4, 0xD7A4, 0xDAA4, 0xDDA4,			/* ぱぴぷぺぽ */
	0
};


/*
 * wrapper functions から使われる ユーティリティ関数s
 */

/* コンテキスト関連 */
/*
  mw_switch_context
  
  指定されたコンテキスト情報を元にかな漢の状態を切り替える
*/
static short mw_switch_context(context_t *cx)
{
	HIMC hIMC = 0;

	if (hWnd_IMM == 0)
		return 0;
	hIMC = ImmGetContext(hWnd_IMM);
	if (hIMC == 0)
		return 0;

	/* 変換中の状態保持用変数を初期化 */
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
  
  新しいコンテキストの情報を作成する
*/
static short mw_new_context(int id)
{
	context_t *cx, *new_context;
	short cx_num;

	/* コンテキストの情報のメモリが取れなかったらエラー */
	if ((new_context = (context_t *)calloc(1, sizeof(context_t))) == NULL)
		return -1;

	/* new_contextをコンテキストのリストに組み込む */
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

	/* リストの最後（空の要素の次）を探して空の要素に現在のクライアント情報を埋める */
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
  
  指定されたコンテキスト番号のコンテキスト情報を取得する
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
  
  指定されたコンテキスト番号のコンテキスト情報の中身をクリアする
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
  
  指定されたコンテキスト番号のコンテキスト情報本体を開放する
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
 * Wnn から呼ばれる関数
 */

/* 作り直し、っていうか必要？ */
static int mw_ime32_message(char *s)
{
  return 0;
}

/*
 *
 */

/*
  mw_open_imm32
  
  かな漢動作開始
  esecannaのコンテキスト毎にWindowsの入力コンテキストを作成する
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
		/* 状態の確認 */
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
  
  クローズする（入力コンテキストを破棄する）
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

		/* クローズする */
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
 * wcs2ucs() - かんなで使われるワイドキャラクタからUCS-2へ 
 * lenは文字数。とりあえずcannawc2euc()とeuc2sjis()を使って書いておくけど、
 * 後で一個にしよう
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

	/* 途中で使用するeuc用のバッファ確保 */
	wclen = cannawcstrlen(src);
	workeuc = (char*)calloc(1, wclen * 3);

	/* 一旦eucへ */
	euclen = cannawc2euc(src, wclen, workeuc, wclen * 3);	/* cannawc2euc()のsrcの長さは文字数、destの長さはバイト数、戻り値はバイト数 */

	outbytesleft = (wclen + 1) * 2;
	buffer_check(&zbuf, outbytesleft);	/* UCSは一文字2byte */
	dst = (LPWSTR)(zbuf.buf);

	/* eucからUCS2へ */
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
 * ucs2wcs() -UCS-2から かんなで使われるワイドキャラクタへ 
 *
 * srclen: 文字数
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

	/* 途中で使用するeuc用のバッファ確保 */
	workeuc = (char*)calloc(1, srclen * 3 + 1);

	/* 途中で使用するUCS用のバッファ確保 */
	workucs = (ushort*)calloc(1, srclen * 2);

	/* UCS-2から一旦eucへ */
	cd = iconv_open("EUC-JP", "UCS-2-INTERNAL");
	if (cd == (iconv_t)-1)
	{
		dst = NULL;
		goto end_exit;
	}

	/* Windows固有のコードをパッチする */
	for (i=0; i<srclen; i++)
	{
		switch(src[i])
		{
			case 0x005C:	/* \ */
				workucs[i] = 0x00A5;
				break;
			case 0xFF5E:	/* 〜 */
				workucs[i] = 0x301C;
				break;
			case 0x2225:	/* ‖ */
				workucs[i] = 0x2016;
				break;
			case 0xFF0D:	/* − */
				workucs[i] = 0x2015;
				break;
			case 0xFFE0:	/* ¢ */
				workucs[i] = 0x00A2;
				break;
			case 0xFFE1:	/* £ */
				workucs[i] = 0x00A3;
				break;
			case 0xFFE2:	/* ¬ */
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
			/* 表現できない文字がきたので'？'で埋める */
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

	/* eucからcannawcへ */
	euc2cannawc(workeuc, euclen, dst, srclen * 2 + 2);	/* euc2cannawc()のsrcの長さはバイト数、destの長さは文字数、戻り値は文字数 */

end_exit:
	free(workeuc);
	if (cd != (iconv_t)-1)
		iconv_close(cd);
	return dst;
}

/*
 * wcs2sjis() - かんなで使われるワイドキャラクタからsjisへ 
 * lenは文字数。とりあえずcannawc2euc()とeuc2sjis()を使って書いておくけど、
 * 後で一個にしよう
 */
static LPSTR mw_wcs2sjis(ushort *src)
{
	int wclen, euclen;
	char *workeuc;
	LPSTR dst;
	static buffer_t zbuf;

	/* 途中で使用するeuc用のバッファ確保 */
	wclen = cannawcstrlen(src);
	workeuc = (char*)calloc(1, wclen * 3);

	/* 一旦eucへ */
	euclen = cannawc2euc(src, wclen, workeuc, wclen * 3);	/* cannawc2euc()のsrcの長さは文字数、destの長さはバイト数、戻り値はバイト数 */

	buffer_check(&zbuf, wclen * 2 + 1);	/* sjisはdbcsだから最大2byte */
	dst = (LPSTR)(zbuf.buf);

	/* eucからsjisへ */
	euc2sjis(workeuc, euclen, dst, wclen * 2 + 1);	/* euc2sjis()のsrc,destともに長さはバイト数、戻り値はバイト数 */

	free(workeuc);

	return dst;
}

/*
 * sjis2wcs() -sjisから かんなで使われるワイドキャラクタへ 
 */
static ushort *mw_sjis2wcs(LPCSTR src, int srclen)
{
/*	int srclen, euclen; */
	int euclen;
	char *workeuc;
	ushort *dst;
	static buffer_t zbuf;

	/* 途中で使用するeuc用のバッファ確保 */
	workeuc = (char*)calloc(1, srclen * 3);

	/* sjisから一旦eucへ */
	euclen = sjis2euc((uchar*)src, srclen, workeuc, srclen * 3);

	buffer_check(&zbuf, srclen * 2 + 2);
	dst = (ushort *)(zbuf.buf);

	/* eucからcannawcへ */
	euc2cannawc(workeuc, euclen, dst, srclen * 2 + 2);	/* euc2cannawc()のsrcの長さはバイト数、destの長さは文字数、戻り値は文字数 */

	free(workeuc);

	return dst;
}

/*
 * mw_after_conversion() - 変換後呼ぶ。指定文節からの各文節の最優先候補を得る。
 *
 * 引数                  - bun		指定文節
 * 戻り値                - nbun		文節数
 *                         len_r	
 *                         
 */
static ushort *mw_after_conversion(context_t *cx, HIMC hIMC, int *nbun, int bun, int *len_r)
{
	BOOL nRet = TRUE;
	long BufLen;
	ushort *ret;

	/* 前回までの後始末 */
	mw_clear_context(cx->context_num);

	/*  文節位置の情報 */
	BufLen = ImmGetCompositionStringW(hIMC, GCS_COMPCLAUSE, NULL, 0);
	if ((BufLen == IMM_ERROR_NODATA) || (BufLen == IMM_ERROR_GENERAL) || (BufLen == 0))
	{
		nRet = FALSE;
		goto end_exit;
	} else
	{
		cx->dwCompCls = (LPDWORD)calloc(1, BufLen);
		cx->dwCompClsLen = ImmGetCompositionStringW(hIMC, GCS_COMPCLAUSE, cx->dwCompCls, BufLen);
		*nbun = cx->dwCompClsLen / sizeof(DWORD) - 1; /* バイトサイズを配列の要素数に変換 */
	}

	/* 変換後文字列取得 */
	BufLen = ImmGetCompositionStringW(hIMC, GCS_COMPSTR, NULL, 0);	/* 変換後取得 */
	if ((BufLen == IMM_ERROR_NODATA) || (BufLen == IMM_ERROR_GENERAL) || (BufLen == 0))
	{
		nRet = FALSE;
		goto end_exit;
	} else
	{
		cx->szCompStr = (LPWSTR)calloc(1, BufLen + 1);
		ImmGetCompositionStringW(hIMC, GCS_COMPSTR, cx->szCompStr, BufLen + 1);
	}

	/* 読み文字列取得 */
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

	/* 文節属性の情報 */
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

	/* 読み文節の情報 */
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

	/* 読み属性の情報 */
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
	{	/* 変換に失敗したようなので後始末 */
		mw_clear_context(cx->context_num);
		ret = NULL;
		*len_r = 0;
	} else
	{	/* 成功したのでcannawcを作って終了 */
		ushort *workp;
		int worklen;
		int i;
		static buffer_t zbuf;

		/* まず文字数を数える */
		workp = mw_ucs2wcs(&(cx->szCompStr[cx->dwCompCls[bun]]), wcslen(&(cx->szCompStr[cx->dwCompCls[bun]])));
		if (workp == NULL)
		{
			mw_clear_context(cx->context_num);
			ret = NULL;
			*len_r = 0;
			goto end_exit_2;
		}
		worklen = cannawcstrlen(workp);

		/* 必要となるバッファを確保 */
		buffer_check(&zbuf, (worklen + *nbun + 1) * 2);	/* 文字＋文節間のスペース＋ターミネート */
		ret = (ushort *)(zbuf.buf);
		memset((LPVOID)ret, 0, (worklen + *nbun + 1) * 2);

		/* 文節ごとにsjis->cannawc変換する */
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

		/* 最後に戻り値を文字列の先頭にする */
		ret = (ushort *)(zbuf.buf);
	}
end_exit_2:
	return ret;
}

/*
 * mw_set_target_clause()
 *
 * 指定された文節を変換対象文節にする
 *
 * 戻り値：	失敗: -1		成功: 現在の文節番号
 *
 */
static int mw_set_target_clause(context_t *cx, HIMC hIMC, int bun_no)
{
	int fRet = 0, CurClause = -1;
	UINT uClause, uMaxClause, uCnt, uCntRead;
	DWORD i, j;
	BOOL fAttrOK = FALSE, fAttrReadOK = FALSE;
	BYTE bAt;

/* >> 現在の変換対象の文節が指定された文節と一致するか？ */
	uMaxClause = (UINT)(cx->dwCompClsLen / sizeof(DWORD)) - 1;
	if (uMaxClause <= 0)
	{
		return -1;		/* 現在変換中の文字列は無い */
	}

	for (CurClause = 0; CurClause < (int)uMaxClause; CurClause++)
	{
		if ((cx->bCompAttr[cx->dwCompCls[CurClause]] == ATTR_TARGET_CONVERTED) ||
			(cx->bCompAttr[cx->dwCompCls[CurClause]] == ATTR_TARGET_NOTCONVERTED))
			break;
	}

	if (CurClause == -1)
	{
		return -1;	/* 変換対象の文節が無いのは多分エラーだろう */
	}
/* << 現在の変換対象の文節が指定された文節と一致するか？ */
/* >> 現在の変換対象の文節を指定された文節に移動 */
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
/* << 現在の変換対象の文節を指定された文節に移動 */

	return CurClause;
}


/*
 * mw_lookup_hira_table() -
 *
 */
/* cannawcバージョン */
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
 * cannawc の半角カタカナから全角ひらがなへ変換する
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
			{	/* ゛ */
				wcs[i] = 0xABA1;
			} else if ((wcs[i] & 0xFF00) == 0xDF00)
			{	/* ゜ */
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
 * mw_get_yomi() - 指定された文節の読みを取得する。
 *                 Windowsの読みは半角カナで入っているようなので、それを全角に直してやる。
 *
 * len_r: 文字数
 *
 * 戻るのはCannaワイド文字
 */

static ushort *mw_get_yomi(context_t *cx, int bun_no, int *len_r)
{
/* cannawcバージョン */
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
	{	/* 無かった・・・ */
		*len_r = 0;
	}

	return ret;
}

/*
 * mw_get_yomi_2() - 指定された文節 *以降* の読みを取得する。
 *
 * len_r: 文字数
 *
 * 戻るのはCannaワイド文字
 */

static ushort *mw_get_yomi_2(context_t *cx, int bun_no, int *len_r)
{
/* cannawcバージョン */
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
	{	/* 無かった・・・ */
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
			/* MSのサンプルは拾ってないけどいいのかな・・・		*/
			/* 本当はこれが、Immの完了通知のはずなんだけど、	*/
			/* APIを使って実行するときはいらないのかな？		*/
			return (DefWindowProc(hWnd, uMsg, wParam, lParam));

		case WM_CREATE:
m_msg_dbg("WM_CREATE\n");
			wm_create_done = 1;
			return 0;

		case WM_QUERYENDSESSION:
m_msg_dbg("WM_QUERYENDSESSION\n");
			return TRUE;

		case WM_CLOSE:
		case WM_ENDSESSION:		/* windowsの終了時にソケットを削除する */
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
					/* どうもinactiveになるとかな漢が出来なくなるので入れる。 */
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
					/* どうもinactiveになるとIMN_CLOSESTATUSWINDOWが来て */
					/* かな漢が出来なくなるので入れる。 */
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
	wc.hInstance = (struct HINSTANCE__ *)GetCurrentProcess();	/* これで”擬似”ハンドルが取れるらしい	*/
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
	hwnd = CreateWindow("WinIMM32", "", WS_POPUPWINDOW  | WS_SYSMENU, 0,0,0,0, NULL, 0, 0, 0);	/* HWND_MESSAGEウィンドウは入力を受け付けないらしい（ImmXXX系のAPIを受け付けない）	*/
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
	ShowWindow(hwnd, SW_SHOW);	/* どうやらShowWindow()しないとImmSetCompositionString()できないらしい	*/

	while (GetMessage(&msg, NULL, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
}

/*
 * 外に公開する関数s
 */
/*
 mw_InitWindow
 
*/
int mw_InitWindow(void)
{

	hWnd_IMM = 0;

	/* まずダミーのウィンドウ用のクラスを登録 */
	if (mw_RegIMMWindow() == 0)
	{
		return -1;
	}

	/* メッセージループ用のスレッド作成 */
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

  このモジュールをesecannaが読み込むときに一度呼び出される。
  esecannaの方が保持しているクライアント情報へのポインタが渡ってくるので
  それをローカルに保存する。

  Windowsでは、かな漢を使うのにウィンドウが必要（入力はウィンドウに付随する）なので
  ダミーのウィンドウを作成する
 */
int imm32wrapper_dl_started(client_t *cl)
{
	/* Windowsのバージョンチェック。Unicode系のAPIを使うので、Win2K以上に限定する */
	{
		OSVERSIONINFO osvi;

		/* OSVERSIONINFO 構造体の初期化 */ 
		memset(&osvi, 0, sizeof(OSVERSIONINFO));

		osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
		GetVersionEx((OSVERSIONINFO*)&osvi);

		/* バージョンのチェック */
		if ((osvi.dwMajorVersion < 5) || (osvi.dwPlatformId != VER_PLATFORM_WIN32_NT))
		{
			m_msg("Can't exec in this Windows version.\n");
			return -1;
		}
	}

	/* かな漢を行うためのダミーウィンドウの作成 */
/*
	if (mw_InitWindow() != 0)
		return -1;
*/

	/* esecannaのクライアント情報構造体へのポインタをこちら側に保存 */
	client = cl;

	cx_top = NULL;	/* 一応初期化しておこう（気持ち悪いから）	03.10.17 Y.A.	*/
	last_context_num = -1;	/*  */

	return 0;
}

/* かな漢開始 */
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

/* かな漢終了 */
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

/* かな漢終了（_end_client()を呼ぶ） */
int imm32wrapper_end_rootclient()
{
	imm32wrapper_end_client(IMM32_ROOT_CLIENT);

	return 0;
}

/* クライアントの情報を消去する */
int imm32wrapper_clear_client_data(int id)
{
	return 0;
}

/*
 * 「かんな」を Wnn で wrapping する関数s
 */
/* 初期化処理 */
int imm32wrapper_initialize(int id, char *conffile)
{
	return mw_new_context(id);
}

/* 終了処理 */
int imm32wrapper_finalize(int id, buffer_t *cbuf)
{
	cannaheader_t *header;
	HIMC hIMC;

	/* hIMCの確保 */
	if (hWnd_IMM != 0)
	{
		hIMC = ImmGetContext(hWnd_IMM);
		if (hIMC != 0)
		{
			/* 変換途中なら完了させる */
/*			ImmNotifyIME(hIMC, NI_COMPOSITIONSTR, CPS_COMPLETE, 0); */	/* 失敗しても無視 */
			ImmReleaseContext(hWnd_IMM, hIMC);
		}
	}

	client[id].need_terminate = TRUE; /* main.c で終了処理をしてもらう */

	header = (cannaheader_t *)cbuf->buf;
	header->type = 0x02;
	header->extra = 0;
	header->datalen = LSBMSB16(1);
	header->err.e8 = 0;

	return 1;
}

/* コンテクスト作成 */
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

/* コンテクスト複写 */
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

/* コンテクスト削除 */
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

/* 単語登録（出来るけどやってない） */
int imm32wrapper_define_word(int id, buffer_t *cbuf)
{
	cannaheader_t *header = (cannaheader_t *)cbuf->buf;

	header->datalen = LSBMSB16(1);
	header->err.e8 = -1;

	return 0;
}

/* 単語削除（出来るけどやってない） */
int imm32wrapper_delete_word(int id, buffer_t *cbuf)
{
	cannaheader_t *header = (cannaheader_t *)cbuf->buf;

	header->datalen = LSBMSB16(1);
	header->err.e8 = -1;

	return 0;
}

/*
  imm32wrapper_begin_convert()
  
  変換を開始する（変換動作はここから始まる）
  
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
	cyomi = &sp[5];		/* cyomi: kinput2->cannaの読み */

	cx = mw_get_context(cx_num);

	if (cx->fIME == 0)
		mw_open_imm32(id, cx, client[cx->client_id].user);

	if (cx->fIME != 0)
	{
		/* hIMCの確保 */
		if (hWnd_IMM == 0)
			goto error_exit;

		hIMC = ImmGetContext(hWnd_IMM);
		if (hIMC == 0)
		{
			goto error_exit;
		}

		/* オープンしていなかったらオープンする */
		if (ImmGetOpenStatus(hIMC) != TRUE)
		{
			if (ImmSetOpenStatus(hIMC, TRUE) != TRUE)
			{
				goto error_exit;
			}
		}

		/* クライアントから指定された読みを保存 */
		/* とりあえずとっておくけど必要なかったら止める */
		len = cannawcstrlen(cyomi);
		cx->szYomiStr = (ushort*)calloc(1, (len * 2) + 2);
		memcpy((void*)cx->szYomiStr, (void*)cyomi, len * 2);

		iyomi = mw_wcs2ucs(cyomi);	/* iyomi: Win32 Imm の読み */
		if (iyomi == NULL)
		{
			goto error_exit;
		}
		fRet = ImmSetCompositionStringW(hIMC, SCS_SETSTR, NULL, 0, (LPCVOID)(iyomi), wcslen(iyomi) * 2);	/* 読み設定 */
		if (fRet == FALSE)
		{
			goto error_exit;
		}
		fRet = ImmNotifyIME(hIMC, NI_COMPOSITIONSTR, CPS_CONVERT, 0);	/* 変換実行 */
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
				{	/* 最後の変換を確定させる */
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

				/* クローズする */
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
  
  指定された文節の候補と読みを返す
  
  
  
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
			/* 候補の取得 */
			CurClause = mw_set_target_clause(cx, hIMC, bun_no);
			if (CurClause < 0)
			{	/* 文節の移動に失敗した */
				errflag = 1;
			} else
			{	/* 変換候補リストを取得 */
				ImmNotifyIME(hIMC, NI_OPENCANDIDATE, 0, 0);			/* 変換候補リスト表示 */
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
  
  指定された文節の読みを返す
  
  
  
*/
int imm32wrapper_get_yomi(int id, buffer_t *cbuf)
{
/* cannawcバージョン */
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
  
  指定された文節を指定された長さに変更して再変換する
  
  2004.03.04 濁点、半濁点があるときに失敗するのを修正
  
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
		DWORD dwClsRead[512];	/* こいつは固定サイズじゃまずいかもしれん */

		HIMC hIMC = ImmGetContext(hWnd_IMM);
		if (hIMC != 0)
		{
			/* 対象文節の長さ決定 */
			if (mw_get_yomi(cx, bun_no, &curyomilen) == NULL)
			{
				ImmReleaseContext(hWnd_IMM, hIMC);
				goto error_exit;
			}
			dwTargetLen = curyomilen;

			switch(cannayomilen)
			{
				case -1:	/* 文節伸ばし */
					dwTargetLen ++;
					break;
				case -2:	/* 文節縮め */
					if (dwTargetLen != 0)
						dwTargetLen --;
					break;
				default:	/* 即値 */
					dwTargetLen = cannayomilen;
					break;
			}
			if (dwTargetLen > oldyomilen)
			{
				ckoho = mw_after_conversion(cx, hIMC, &nbun, bun_no, &len);	/* nbunはアドレスに	03.10.20 Y.A. */
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

			/* 長さの調整 */
			if (oldyomilen < dwTargetLen)
				dwTargetLen = oldyomilen;
			cyomi[dwTargetLen] = '\0';
			/* IMM が認識している長さに戻すために半角でカウントする */
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

			/* 当該文節の情報を変更 */
			for (i=0; i<uMaxClause+1; i++)	/* 最後は変更してはいけません */
			{
				if (i == (bun_no + 1))
					dwClsRead[i] = dwClsRead[i - 1] + dwTargetLen;
				else
					dwClsRead[i] = cx->dwCompReadCls[i];
			}

			if (ImmSetCompositionStringW(hIMC,SCS_CHANGECLAUSE,NULL,0,dwClsRead,(uMaxClause+1)*sizeof(DWORD)) == TRUE &&
				ImmNotifyIME(hIMC, NI_COMPOSITIONSTR, CPS_CONVERT, 0) == TRUE)
			{
				ckoho = mw_after_conversion(cx, hIMC, &nbun, bun_no, &len);	/* nbunはアドレスに	03.10.20 Y.A. */
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
  
  指定された文節の解析情報を求める
  
  
  
*/
int imm32wrapper_get_status(int id, buffer_t *cbuf)
{
	struct
	{
		int bunnum;		/* カレント文節の文節番号 */
		int candnum;	/* 候補の中の候補番号 */
		int maxcand;	/* カレント文節の候補数 */
		int diccand;	/* maxcand - モード指定分（でもとりあえずesecannaではmaxcandと同じ） */
		int ylen;		/* 読みの長さ */
		int klen;		/* カレント候補の読みがなのバイト数 */
		int tlen;		/* カレント候補の構成単語数 == 1 */
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
				/* 候補の取得 */
				CurClause = mw_set_target_clause(cx, hIMC, bun_no);
				if (CurClause < 0)
				{	/* 文節の移動に失敗した */
					errflag = 1;
				} else
				{	/* 変換候補リストを取得 */
					ImmNotifyIME(hIMC, NI_OPENCANDIDATE, 0, 0);			/* 変換候補リスト表示 */
					BufLen = ImmGetCandidateListW(hIMC, 0, NULL, 0);
					lpCandList = (LPCANDIDATELIST)calloc(1, BufLen);
					dwRet = ImmGetCandidateListW(hIMC, 0, lpCandList, BufLen);
					if (dwRet != 0 && lpCandList->dwCount > koho_no)
					{	/* 変換候補があれば対象候補番号の候補を確保しておく */
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
				stat.ylen = LSBMSB32(ylen);	/* カレント候補の読みがなのバイト数 */
				stat.klen = LSBMSB32(klen);	/* カレント候補の漢字候補のバイト数 */
				stat.tlen = LSBMSB32(1);	/* カレント候補の構成単語数 */
				stat.maxcand = LSBMSB32(koho_num);	/* カレント文節の候補数 */
				stat.diccand = LSBMSB32(koho_num);	/* FIXME: maxcand - モード指定分 */
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
