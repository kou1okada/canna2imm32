/*  esecannaserver --- pseudo canna server that wraps another IME.
 *  Copyright (C) 1999-2000 Yasuhiro Take
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
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
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <pwd.h>
#include <sys/types.h>
#include <signal.h>
#include <dlfcn.h>

#include "def.h"

#include "misc.h"
#include "winimm.h"

#define IW_ERROR16(_buf) { \
  cannaheader_t *he; \
  he = (cannaheader_t *)(_buf); \
  he->datalen = LSBMSB16(2); \
  he->err.e16 = LSBMSB16(-1); \
}

#define IW_ERROR8(_buf) { \
  cannaheader_t *he; \
  he = (cannaheader_t *)(_buf); \
  he->datalen = LSBMSB16(1); \
  he->err.e8 = -1; \
}

typedef int (*imefunc_t)();

static char *funcsymbols[] = {
  "_finalize",
  "_create_context",
  "_duplicate_context",
  "_close_context",
  "_define_word",
  "_delete_word",
  "_begin_convert",
  "_end_convert",
  "_get_candidacy_list",
  "_get_yomi",
  "_subst_yomi",
  "_store_yomi",
  "_store_range",
  "_get_lastyomi",
  "_flush_yomi",
  "_remove_yomi",
  "_get_simplekanji",
  "_resize_pause",
  "_get_hinshi",
  "_get_lex",
  "_get_status",
  "_set_locale",
  "_auto_convert",
  "_initialize",
  "_init_rootclient",
  "_end_client",
  "_end_rootclient",
  "_clear_client_data"
};

/* ダイナミックリンクで得る関数と番号との対応付け */
/*
  _finalize 0
  _create_context 1
  _duplicate_context 2
  _close_context 3
  _define_word 4
  _delete_word 5
  _begin_convert 6
  _end_convert 7
  _get_candidacy_list 8
  _get_yomi 9
  _subst_yomi 10
  _store_yomi 11
  _store_range 12
  _get_lastyomi 13
  _flush_yomi 14
  _remove_yomi 15
  _get_simplekanji 16
  _resize_pause 17
  _get_hinshi 18
  _get_lex 19
  _get_status 20
  _set_locale 21
  _auto_convert 22
  _initialize 23
  _init_rootclient 24
  _end_client 25
  _end_rootclient 26
  _clear_client_data 27
  */

#define F_initialize 23
#define F_init_rootclient 24
#define F_end_client 25
#define F_end_rootclient 26
#define F_clear_client_data 27

#define F_COMMON_END 27

static imefunc_t imm32func[28] = 
{
	imm32wrapper_finalize,
	imm32wrapper_create_context,
	imm32wrapper_duplicate_context,
	imm32wrapper_close_context,
	imm32wrapper_define_word,
	imm32wrapper_delete_word,
	imm32wrapper_begin_convert,
	imm32wrapper_end_convert,
	imm32wrapper_get_candidacy_list,
	imm32wrapper_get_yomi,
	imm32wrapper_subst_yomi,
	imm32wrapper_store_yomi,
	imm32wrapper_store_range,
	imm32wrapper_get_lastyomi,
	imm32wrapper_flush_yomi,
	imm32wrapper_remove_yomi,
	imm32wrapper_get_simplekanji,
	imm32wrapper_resize_pause,
	imm32wrapper_get_hinshi,
	imm32wrapper_get_lex,
	imm32wrapper_get_status,
	imm32wrapper_set_locale,
	imm32wrapper_auto_convert,
	imm32wrapper_initialize,
	imm32wrapper_init_rootclient,
	imm32wrapper_end_client,
	imm32wrapper_end_rootclient,
	imm32wrapper_clear_client_data
};

static imefunc_t *imefunc[IME_END] = {imm32func};
static char ime_connected_flag[IME_END];
static void *ime_dl_handler[IME_END];

extern char *protocol_name[], *e_protocol_name[];
extern client_t client[];

extern HWND hWnd_IMM;		/* かな漢動作用のウィンドウ */

/*
 * コンフィギュレーションファイルを読み、どちらの IME に接続するか返す
 */

static char *iw_get_conf_file_path(char *home)
{
  char *path = NULL;

  if (strlen(home) != 0)
  {
    if ((path = m_makepath(home, ".canna2imm32rc")) == NULL)
    {
      m_msg("out of memory!\n");
      return NULL;
    }

    if (access(path, R_OK) == 0)
    {
      m_msg("Config file %s\n", path);
      return path;
    }
  }

  /* home に .canna2imm32rc が無いときは /etc/canna2imm32rc を探す */
  MYFREE(path);
  if ((path = strdup(ESECANNA_RC_PATH)) == NULL)
  {
    m_msg("out of memory!\n");
    return NULL;
  }

  if (access(path, R_OK))
  {
    m_msg("No %s found.\n", path);
    return NULL;
  }

  m_msg("Config file %s\n", path);
  return path;
}

static int iw_read_conf_file(char *path)
{
  FILE *fp;
  char buf[1024];
  char *ope, *val;
  int ret = IME_NON;

  if ((fp = fopen(path, "r")) == NULL) {
    m_msg("Cannot open Conffile %s.\n", path);
    return IME_NON;
  }
  
  while (fgets(buf, 1024, fp)) {
    if (buf[0] != '#' && m_conf1_parse(buf, &ope, &val) == 0) {
      if (m_conf_isequal(ope, "IME", val, "IMM32") == 2)
        ret = IME_IMM32;
      if (ret == IME_NON && m_conf_isequal(ope, "IME", val, "dummy"))
        m_msg("Unsupported IME: [%s]\n", val);
      
      if (ret != IME_NON)
        break;
    }
  }

  fclose(fp);

  return ret;
}

/*
 * ダイナミックモジュールをロードする
 */
static int iw_retrieve_imm32_symbols(void *handler)
{
	return imm32wrapper_dl_started(client);
}

static int iw_load_library(int ime)
{
	iw_retrieve_imm32_symbols(ime_dl_handler[ime - 1]);
	m_msg("Use internal Module.\n");
	return 0;
}

/*
 * ダイナミックモジュールをアンロードする
 */

static int iw_unload_library(int ime)
{
  return 0;
}

/*
 *  クライアントの終了処理をする 
 */

int imewrapper_end_client(int id)
{
  int ime = client[id].ime;
  
  if (ime > 0 && ime_dl_handler[ime - 1]) {
    SendMessage(hWnd_IMM, WM_USER + F_end_client, id, 0);
  }
  
  return 0;
}

/*
 * ある IME の終了処理をする。
 */
 
int imewrapper_end_rootclient(int ime)
{
  imefunc_t *func;
  
  if (ime > 0 && ime_dl_handler[ime - 1] != NULL) {
    /* ダイナミックモジュールがロードされているのなら終了処理をする */
    SendMessage(hWnd_IMM, WM_USER + F_end_rootclient, 0, 0);
    
    /* モジュールを開放する */
    iw_unload_library(ime);

    /* 接続フラグをクリア */
    ime_connected_flag[ime - 1] = FALSE;
  }
  
  return 0;
}

/*
 * client[] のデータをクリアする関数を呼ぶ
 */

int imewrapper_clear_client_data(int id)
{
  int ime = client[id].ime;
  
  if (ime > 0 && ime_dl_handler[ime - 1]) {
    SendMessage(hWnd_IMM, WM_USER + F_clear_client_data, id, 0);
  }
  
  MYFREE(client[id].host);
  MYFREE(client[id].homedir);
  
  client[id].ime = IME_NON;

  memset(&(client[id]), 0, sizeof(client_t));
  
  client[id].sockfd = -1;

  return 0;
}
      


/*
 * IME が異常終了したときの処理関係の関数s
 */

static int iw_imm32_aborted(int ime)
{
  return -ime;
}

int imewrapper_ime_aborted(int ime)
{
  switch (ime) {
    case IME_IMM32:
      return iw_imm32_aborted(ime);
  }

  return -1;
}

/*
 * えせかんなを終了させるための関数
 */

static int iw_send_term_signal()
{
  raise(SIGTERM);

  return 0;
}

/*
 * Windows IMM にまだ接続していない場合に呼ばれる
 */

static int iw_imm32_connect(int ime)
{
	/* モジュールをロード */
	if (iw_load_library(ime) == 0)
	{
		/* 初期化 */
		if (SendMessage(hWnd_IMM, WM_USER + F_init_rootclient, 0, 0) == 0)
		{
			/* Wnn 接続 & 初期化に成功。*/
			client[IMM32_ROOT_CLIENT].ime = ime;
			ime_connected_flag[ime - 1] = TRUE;

			return 0;
		} else {
			SendMessage(hWnd_IMM, WM_USER + F_end_rootclient, 0, 0);
		}

		iw_unload_library(ime);
	}

	return -1;
}
    
/*
 * IME にまだ接続していない場合に呼ばれる
 */

static int iw_ime_connect(int ime)
{
	switch (ime)
	{
		case IME_IMM32:
			return iw_imm32_connect(ime);
		default:
			break;
	}

	return -1;
}

/*
 * かんなプロトコルを wrap する関数s
 */

/*
  かな漢字変換サーバとクライアントとの接続を確立し，かな漢字変換環境を構築する．
*/
int imewrapper_initialize(int id, buffer_t *cbuf)
{
	int ime = IME_NON, errflag, *ip = (int *)cbuf->buf;
	short *sp = (short *)cbuf->buf;
	short cx_num;
	char *p, *major_p, *minor_p, *user = NULL, *home;
	short major, minor;
	struct passwd *pwd;
	char *conffile = NULL;

	errflag = 0;

	major_p = &(cbuf->buf[0]); /* Initialize に限り, cbuf->buf にヘッダーは
	                              入らない */
	home = NULL;
	major = minor = -1;

	if ((p = strchr(major_p, '.')) == NULL)
		errflag = 1;
	else
	{
		*p = 0;

		minor_p = p + 1;

		if ((p = strchr(minor_p, ':')) == NULL)
			errflag = 2;
		else
		{
			*p = 0;
			user = p + 1;

			if (user[0] == 0)
				errflag = 3;
			else
			{
				if ((pwd = getpwnam(user)) == NULL)
					errflag = 4;
				else
				{
					if ((home = pwd->pw_dir) == NULL)
						errflag = 5;
				}
			}
			/* ユーザが無くても /etc/canna2imm32rc があれば OK にする */
			if (errflag  != 0)
			{
				MYFREE(home);
				home = (char*)malloc(1);
				home[0] = '\0';
			}
			if ((conffile = iw_get_conf_file_path(home)) == NULL)
				errflag = 6;
			else
				errflag = 0;

			major = atoi(major_p);
			minor = atoi(minor_p);
		}
	}

	/* FIXME: major, minor で場合によってはエラーを返すよう修正 */

	if (errflag)
		m_msg("Header invalid. Maybe server version mismatch? (%d)\n", errflag);

	if (errflag == 0)
	{
		if ((ime = client[id].ime = iw_read_conf_file(conffile)) != 0)
		{
			if (ime_connected_flag[ime - 1] == FALSE)
			{
				/* 接続要求された IME にまだ接続していない場合 */
				iw_ime_connect(ime);
			}
		} else
		{
			m_msg("IME not determined.\n");
		}
	}

	if (errflag == 0 && ime > 0 && ime_connected_flag[ime - 1] == TRUE)
	{
		/* client[] に user 名と, ホームディレクトリのパスを保管 */
		strncpy(client[id].user, user, 10);
		client[id].user[9] = 0;
		client[id].homedir = strdup(home);

		cx_num = SendMessage(hWnd_IMM, WM_USER + F_initialize, id, (LPARAM)conffile);
	} else
	{
		cx_num = -1;
	}

	if (cx_num == -1)
	{
		m_msg("Initialize failed. #%d %s@%s refused.\n", id, user ? user : "", client[id].host);

		/* リソースファイルを読み込む段階での失敗、またはコンテキストの獲得に
		   失敗。クライアントとの接続は切ってしまう */
		client[id].need_terminate = TRUE; /* main.c で終了処理をしてもらう */
		*ip = LSBMSB32(-1);
	} else
	{	/* Success */
		sp[0] = LSBMSB16(3);
		sp[1] = LSBMSB16(cx_num);
	}

	if (conffile)
		free(conffile);

	return 1;
}

#define CALLFUNC(_num) return SendMessage(hWnd_IMM, WM_USER+_num, (WPARAM)id, (LPARAM)cbuf);

/*
  かな漢字変換処理を終了する．サーバ及びクライアントに確保されていたすべての資源を解放する．
  また、学習内容で辞書に反映されていないものがあれば辞書に反映する．
*/
int imewrapper_finalize(int id, buffer_t *cbuf)
{
	CALLFUNC(0);
}

/*
  変換コンテクストを作成し，その変換コンテクストを表すコンテクスト番号を返す．
*/
int imewrapper_create_context(int id, buffer_t *cbuf)
{
	CALLFUNC(1);
}

/*
  指定された変換コンテクストを複製し，新しい変換コンテクストを生成しそれを表すコンテクスト
  番号を返す．
*/
int imewrapper_duplicate_context(int id, buffer_t *cbuf)
{
	CALLFUNC(2);
}

/*
  コンテクストが使用している資源を解放する．その後コンテクストは未定義となる．
*/
int imewrapper_close_context(int id, buffer_t *cbuf)
{
	CALLFUNC(3);
}

/*
  辞書テーブルに登録されている辞書一覧を取得する．
*/
int imewrapper_get_dictionary_list(int id, buffer_t *cbuf)
{
	cannaheader_t *header = (cannaheader_t *)cbuf->buf;

	header->datalen = LSBMSB16(2);
	header->err.e16 = 0;

	return 1;
}

/*
  辞書ディレクトリにある辞書の一覧を取得する．
*/
int imewrapper_get_directory_list(int id, buffer_t *cbuf)
{
	cannaheader_t *header = (cannaheader_t *)cbuf->buf;

	header->datalen = LSBMSB16(2);
	header->err.e16 = 0;

	return 1;
}

/*
  指定された辞書をかな漢字変換で利用されるようにする。
*/
int imewrapper_mount_dictionary(int id, buffer_t *cbuf)
{
	cannaheader_t *header = (cannaheader_t *)cbuf->buf;

	header->datalen = LSBMSB16(1);
	header->err.e8 = 0;

	return 1;
}

/*
  指定された辞書がかな漢字変換で利用されないようにする。
*/
int imewrapper_unmount_dictionary(int id, buffer_t *cbuf)
{
	cannaheader_t *header = (cannaheader_t *)cbuf->buf;

	header->datalen = LSBMSB16(1);
	header->err.e8 = 0;

	return 1;
}

/*
  使用辞書の辞書リストの順番を変更する．
*/
int imewrapper_remount_dictionary(int id, buffer_t *cbuf)
{
	cannaheader_t *header = (cannaheader_t *)cbuf->buf;

	header->datalen = LSBMSB16(1);
	header->err.e8 = 0;

	return 1;
}

/*
  辞書テーブルに登録されている辞書リスト
*/
int imewrapper_get_mountdictionary_list(int id, buffer_t *cbuf)
{
	cannaheader_t *header = (cannaheader_t *)cbuf->buf;

	header->datalen = LSBMSB16(2);
	header->err.e16 = 0;

	return 1;
}

/*
  指定した辞書の情報を得る
*/
int imewrapper_query_dictionary(int id, buffer_t *cbuf)
{
	cannaheader_t *header = (cannaheader_t *)cbuf->buf;

	header->datalen = LSBMSB16(1);
	header->err.e8 = -1;

	return 1;
}

/*
  辞書に新しい単語を登録する
*/
int imewrapper_define_word(int id, buffer_t *cbuf)
{
	CALLFUNC(4);
}

/*
  辞書から単語を削除する．
*/
int imewrapper_delete_word(int id, buffer_t *cbuf)
{
	CALLFUNC(5);
}

/*
  読みのかな文字列に対し，連文節変換モードでかな漢字変換を行う．
*/
int imewrapper_begin_convert(int id, buffer_t *cbuf)
{
	CALLFUNC(6);
}

/*
  現在のかな漢字変換作業を終了し，必要に応じて学習を行う．
*/
int imewrapper_end_convert(int id, buffer_t *cbuf)
{
	CALLFUNC(7);
}

/*
  指定された文節のすべての候補文字列と読みを取得する．
*/
int imewrapper_get_candidacy_list(int id, buffer_t *cbuf)
{
	CALLFUNC(8);
}

/*
  カレント文節の読みがなを取得する．
*/
int imewrapper_get_yomi(int id, buffer_t *cbuf)
{
	CALLFUNC(9);
}

/*
  自動変換モード時に読みバッファの内容を変更し，再度変換を行う．
*/
int imewrapper_subst_yomi(int id, buffer_t *cbuf)
{
	CALLFUNC(10);
}

/*
  カレント文節の読みがなを変更し，それ以降の文節を再変換する．
*/
int imewrapper_store_yomi(int id, buffer_t *cbuf)
{
	CALLFUNC(11);
}

/*
  カレント文節の読みがなを変更し，カレント文節のみを単文節変換する．
*/
int imewrapper_store_range(int id, buffer_t *cbuf)
{
	CALLFUNC(12);
}

/*
  未決文節の読みを取得する．
*/
int imewrapper_get_lastyomi(int id, buffer_t *cbuf)
{
	CALLFUNC(13);
}

/*
  未決定文節を強制的に変換する．
*/
int imewrapper_flush_yomi(int id, buffer_t *cbuf)
{
	CALLFUNC(14);
}

/*
  先頭文節からカレント文節まで読みを読みバッファから取り除く．
*/
int imewrapper_remove_yomi(int id, buffer_t *cbuf)
{
	CALLFUNC(15);
}

/*
  指定された辞書からその辞書に含まれている候補のみを取得する．
*/
int imewrapper_get_simplekanji(int id, buffer_t *cbuf)
{
	CALLFUNC(16);
}

/*
  指定された文節を，指定された長さに区切り直して，再度かな漢字変換する．
*/
int imewrapper_resize_pause(int id, buffer_t *cbuf)
{
	CALLFUNC(17);
}

/*
  カレント候補に対する品詞情報を文字列で取得する．
*/
int imewrapper_get_hinshi(int id, buffer_t *cbuf)
{
	CALLFUNC(18);
}

/*
  カレント文節の形態素情報を取得する．
*/
int imewrapper_get_lex(int id, buffer_t *cbuf)
{
	CALLFUNC(19);
}

/*
  カレント候補に関する解析情報を求める．
*/
int imewrapper_get_status(int id, buffer_t *cbuf)
{
	CALLFUNC(20);
}

/*
  locale 情報の変更を行う．
*/
int imewrapper_set_locale(int id, buffer_t *cbuf)
{
	CALLFUNC(21);
}

/*
  自動変換モードでかな漢字変換を行う．
*/
int imewrapper_auto_convert(int id, buffer_t *cbuf)
{
	CALLFUNC(22);
}

/*
  そのサーバの拡張プロトコルの問い合わせ．
*/
int imewrapper_query_extensions(int id, buffer_t *cbuf)
{
	cannaheader_t *header = (cannaheader_t *)cbuf->buf;

	header->datalen = LSBMSB16(1);
	header->err.e8 = 0;

	return 1;
}

/*
  クライアントのアプリケーション名をそのサーバに通知し，登録する．
*/
int imewrapper_set_applicationname(int id, buffer_t *cbuf)
{
	cannaheader_t *header = (cannaheader_t *)cbuf->buf;

	header->datalen = LSBMSB16(1);
	header->err.e8 = 0;

	return 1;
}

/*
  クライアントのグループ名をサーバに通知し、登録する。
*/
int imewrapper_notice_groupname(int id, buffer_t *cbuf)
{
	cannaheader_t *header = (cannaheader_t *)cbuf->buf;

	header->datalen = LSBMSB16(1);
	header->err.e8 = 0;

	return 1;
}

/*
  
*/
int imewrapper_through(int id, buffer_t *cbuf)
{
	cannaheader_t *header = (cannaheader_t *)cbuf->buf;

	header->datalen = LSBMSB16(2);
	header->err.e16 = 0;

	return 1;
}

/*
  サーバに終了要求を通知し、サーバを終了させる。
*/
int imewrapper_kill_server(int id, buffer_t *cbuf)
{
	cannaheader_t *header = (cannaheader_t *)cbuf->buf;
	int err = 0;
	char buf[128];
	struct passwd *pw;
	uid_t pid;

	if (gethostname(buf, 128))
		buf[0] = 0;
	pw = getpwnam(client[id].user);
	pid = getuid();

	if (strcmp(client[id].host, "UNIX") &&
		strcmp(client[id].host, buf) &&
		strcmp(client[id].host, "localhost"))
	{
		err = -111; /* NOTUXSRV */
	} else if (pw->pw_uid != 0 && pw->pw_gid != 0 && pw->pw_uid != pid)
		err = -112; /* NOTOWNSRV */

	header->datalen = LSBMSB16(1);
	header->err.e8 = err;

	if (err == 0)
	{
		signal(SIGALRM, (void(*)())iw_send_term_signal);
		alarm(1);

		m_msg("KillServer from %s@%s accepted.\n", client[id].user, client[id].host);
	}

	return 1;
}

/*
  サーバに接続しているクライアント数などのサーバ情報を取得する．
*/
int imewrapper_get_serverinfo(int id, buffer_t *cbuf)
{
	cannaheader_t *header = (cannaheader_t *)cbuf->buf;
	int protocol_num, e_protocol_num, datalen, protocol_datalen, pnt;
	int i;
	uint ui;
	short s;

	for (protocol_num = 0; protocol_name[protocol_num] != NULL; protocol_num++)
		;
	for (e_protocol_num = 0; e_protocol_name[e_protocol_num] != NULL;
		e_protocol_num++);

	protocol_datalen = 0;
	for (i = 0; i < protocol_num; i++)
		protocol_datalen += strlen(protocol_name[i]) + 1;
	for (i = 0; i < e_protocol_num; i++)
		protocol_datalen += strlen(e_protocol_name[i]) + 1;
	protocol_datalen++;

	datalen = 17 + protocol_datalen + (protocol_num + e_protocol_num) * 4;

	buffer_check(cbuf, datalen + 4);
	header = (cannaheader_t *)cbuf->buf;

	header->type = 0x01;
	header->extra = 0x01;
	header->datalen = LSBMSB16(datalen);

	cbuf->buf[4] = 0;
	cbuf->buf[5] = 3; /* Major Server Version */
	cbuf->buf[6] = 5; /* Minor Server Version */

	ui = (uint)time(NULL); ui = LSBMSB32(ui);
	memcpy(&(cbuf->buf[7]), &ui, 4);

	i = protocol_num + e_protocol_num;
	s = LSBMSB16(i);
	memcpy(&(cbuf->buf[11]), &s, 2);

	s = LSBMSB16(protocol_datalen);
	memcpy(&(cbuf->buf[13]), &s, 2);

	pnt = 15;
	for (i = 0; i < protocol_num; i++)
	{
		strcpy(&(cbuf->buf[pnt]), protocol_name[i]);
		pnt += strlen(protocol_name[i]) + 1;
	}

	for (i = 0; i < e_protocol_num; i++)
	{
		strcpy(&(cbuf->buf[pnt]), e_protocol_name[i]);
		pnt += strlen(e_protocol_name[i]) + 1;
	}

	cbuf->buf[pnt++] = 0;

	memset(&(cbuf->buf[pnt]), 0, 4 * (protocol_num + e_protocol_num));
	pnt += 4 * (protocol_num + e_protocol_num);

	memset(&(cbuf->buf[pnt]), 0, 6);
	pnt += 6;

	return 1;
}

/*
  サーバの使用許可の参照および設定を行う．
*/
int imewrapper_get_access_control_list(int id, buffer_t *cbuf)
{
	cannaheader_t *header = (cannaheader_t *)cbuf->buf;

	header->datalen = LSBMSB16(2);
	header->err.e16 = LSBMSB16(-1);

	return 1;
}

/*
  テキスト辞書を作成し，dics.dir の内容を更新する．
*/
int imewrapper_create_dictionary(int id, buffer_t *cbuf)
{
	cannaheader_t *header = (cannaheader_t *)cbuf->buf;

	header->datalen = LSBMSB16(1);
	header->err.e8 = -1;

	return 1;
}

/*
  テキスト辞書を削除し，dics.dir の内容を更新する．
*/
int imewrapper_delete_dictionary(int id, buffer_t *cbuf)
{
	cannaheader_t *header = (cannaheader_t *)cbuf->buf;

	header->datalen = LSBMSB16(1);
	header->err.e8 = -1;

	return 1;
}

/*
  ユーザ辞書の辞書名を変更する．
*/
int imewrapper_rename_dictionary(int id, buffer_t *cbuf)
{
	cannaheader_t *header = (cannaheader_t *)cbuf->buf;

	header->datalen = LSBMSB16(1);
	header->err.e8 = -1;

	return 1;
}

/*
  テキスト辞書の単語情報を取得する．
*/
int imewrapper_get_wordtext_dictionary(int id, buffer_t *cbuf)
{
	cannaheader_t *header = (cannaheader_t *)cbuf->buf;

	header->datalen = LSBMSB16(2);
	header->err.e16 = LSBMSB16(-1);

	return 1;
}

/*
  指定した辞書ディレクトリにある辞書テーブルを取得する．
*/
int imewrapper_list_dictionary(int id, buffer_t *cbuf)
{
	cannaheader_t *header = (cannaheader_t *)cbuf->buf;

	header->datalen = LSBMSB16(1);
	header->err.e8 = -1;

	return 1;
}

/*
  メモリ内の保持している辞書情報を辞書に書き込む．
*/
int imewrapper_sync(int id, buffer_t *cbuf)
{
	cannaheader_t *header = (cannaheader_t *)cbuf->buf;

	header->datalen = LSBMSB16(1);
	header->err.e8 = 0;

	return 1;
}

/*
  辞書のREAD/WRITE 権の変更を行う。
*/
int imewrapper_chmod_dictionary(int id, buffer_t *cbuf)
{
	cannaheader_t *header = (cannaheader_t *)cbuf->buf;

	header->datalen = LSBMSB16(1);
	header->err.e8 = -1;

	return 1;
}

/*
  辞書の複写
*/
int imewrapper_copy_dictionary(int id, buffer_t *cbuf)
{
	cannaheader_t *header = (cannaheader_t *)cbuf->buf;

	header->datalen = LSBMSB16(1);
	header->err.e8 = -1;

	return 1;
}
