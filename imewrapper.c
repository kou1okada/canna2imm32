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

/* �����ʥߥå���󥯤�����ؿ����ֹ�Ȥ��б��դ� */
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

extern HWND hWnd_IMM;		/* ���ʴ�ư���ѤΥ�����ɥ� */

/*
 * ����ե�����졼�����ե�������ɤߡ��ɤ���� IME ����³���뤫�֤�
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

  /* home �� .canna2imm32rc ��̵���Ȥ��� /etc/canna2imm32rc ��õ�� */
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
 * �����ʥߥå��⥸�塼�����ɤ���
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
 * �����ʥߥå��⥸�塼��򥢥���ɤ���
 */

static int iw_unload_library(int ime)
{
  return 0;
}

/*
 *  ���饤����Ȥν�λ�����򤹤� 
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
 * ���� IME �ν�λ�����򤹤롣
 */
 
int imewrapper_end_rootclient(int ime)
{
  imefunc_t *func;
  
  if (ime > 0 && ime_dl_handler[ime - 1] != NULL) {
    /* �����ʥߥå��⥸�塼�뤬���ɤ���Ƥ���Τʤ齪λ�����򤹤� */
    SendMessage(hWnd_IMM, WM_USER + F_end_rootclient, 0, 0);
    
    /* �⥸�塼��������� */
    iw_unload_library(ime);

    /* ��³�ե饰�򥯥ꥢ */
    ime_connected_flag[ime - 1] = FALSE;
  }
  
  return 0;
}

/*
 * client[] �Υǡ����򥯥ꥢ����ؿ���Ƥ�
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
 * IME ���۾ｪλ�����Ȥ��ν����ط��δؿ�s
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
 * ��������ʤ�λ�����뤿��δؿ�
 */

static int iw_send_term_signal()
{
  raise(SIGTERM);

  return 0;
}

/*
 * Windows IMM �ˤޤ���³���Ƥ��ʤ����˸ƤФ��
 */

static int iw_imm32_connect(int ime)
{
	/* �⥸�塼������ */
	if (iw_load_library(ime) == 0)
	{
		/* ����� */
		if (SendMessage(hWnd_IMM, WM_USER + F_init_rootclient, 0, 0) == 0)
		{
			/* Wnn ��³ & �������������*/
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
 * IME �ˤޤ���³���Ƥ��ʤ����˸ƤФ��
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
 * ����ʥץ�ȥ���� wrap ����ؿ�s
 */

/*
  ���ʴ����Ѵ������Фȥ��饤����ȤȤ���³���Ω�������ʴ����Ѵ��Ķ����ۤ��롥
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

	major_p = &(cbuf->buf[0]); /* Initialize �˸¤�, cbuf->buf �˥إå�����
	                              ����ʤ� */
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
			/* �桼����̵���Ƥ� /etc/canna2imm32rc ������� OK �ˤ��� */
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

	/* FIXME: major, minor �Ǿ��ˤ�äƤϥ��顼���֤��褦���� */

	if (errflag)
		m_msg("Header invalid. Maybe server version mismatch? (%d)\n", errflag);

	if (errflag == 0)
	{
		if ((ime = client[id].ime = iw_read_conf_file(conffile)) != 0)
		{
			if (ime_connected_flag[ime - 1] == FALSE)
			{
				/* ��³�׵ᤵ�줿 IME �ˤޤ���³���Ƥ��ʤ���� */
				iw_ime_connect(ime);
			}
		} else
		{
			m_msg("IME not determined.\n");
		}
	}

	if (errflag == 0 && ime > 0 && ime_connected_flag[ime - 1] == TRUE)
	{
		/* client[] �� user ̾��, �ۡ���ǥ��쥯�ȥ�Υѥ����ݴ� */
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

		/* �꥽�����ե�������ɤ߹����ʳ��Ǥμ��ԡ��ޤ��ϥ���ƥ����Ȥγ�����
		   ���ԡ����饤����ȤȤ���³���ڤäƤ��ޤ� */
		client[id].need_terminate = TRUE; /* main.c �ǽ�λ�����򤷤Ƥ�餦 */
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
  ���ʴ����Ѵ�������λ���롥�����еڤӥ��饤����Ȥ˳��ݤ���Ƥ������٤Ƥλ񸻤�������롥
  �ޤ����ؽ����ƤǼ����ȿ�Ǥ���Ƥ��ʤ���Τ�����м����ȿ�Ǥ��롥
*/
int imewrapper_finalize(int id, buffer_t *cbuf)
{
	CALLFUNC(0);
}

/*
  �Ѵ�����ƥ����Ȥ�������������Ѵ�����ƥ����Ȥ�ɽ������ƥ������ֹ���֤���
*/
int imewrapper_create_context(int id, buffer_t *cbuf)
{
	CALLFUNC(1);
}

/*
  ���ꤵ�줿�Ѵ�����ƥ����Ȥ�ʣ�������������Ѵ�����ƥ����Ȥ������������ɽ������ƥ�����
  �ֹ���֤���
*/
int imewrapper_duplicate_context(int id, buffer_t *cbuf)
{
	CALLFUNC(2);
}

/*
  ����ƥ����Ȥ����Ѥ��Ƥ���񸻤�������롥���θ女��ƥ����Ȥ�̤����Ȥʤ롥
*/
int imewrapper_close_context(int id, buffer_t *cbuf)
{
	CALLFUNC(3);
}

/*
  ����ơ��֥����Ͽ����Ƥ��뼭�������������롥
*/
int imewrapper_get_dictionary_list(int id, buffer_t *cbuf)
{
	cannaheader_t *header = (cannaheader_t *)cbuf->buf;

	header->datalen = LSBMSB16(2);
	header->err.e16 = 0;

	return 1;
}

/*
  ����ǥ��쥯�ȥ�ˤ��뼭��ΰ�����������롥
*/
int imewrapper_get_directory_list(int id, buffer_t *cbuf)
{
	cannaheader_t *header = (cannaheader_t *)cbuf->buf;

	header->datalen = LSBMSB16(2);
	header->err.e16 = 0;

	return 1;
}

/*
  ���ꤵ�줿����򤫤ʴ����Ѵ������Ѥ����褦�ˤ��롣
*/
int imewrapper_mount_dictionary(int id, buffer_t *cbuf)
{
	cannaheader_t *header = (cannaheader_t *)cbuf->buf;

	header->datalen = LSBMSB16(1);
	header->err.e8 = 0;

	return 1;
}

/*
  ���ꤵ�줿���񤬤��ʴ����Ѵ������Ѥ���ʤ��褦�ˤ��롣
*/
int imewrapper_unmount_dictionary(int id, buffer_t *cbuf)
{
	cannaheader_t *header = (cannaheader_t *)cbuf->buf;

	header->datalen = LSBMSB16(1);
	header->err.e8 = 0;

	return 1;
}

/*
  ���Ѽ���μ���ꥹ�Ȥν��֤��ѹ����롥
*/
int imewrapper_remount_dictionary(int id, buffer_t *cbuf)
{
	cannaheader_t *header = (cannaheader_t *)cbuf->buf;

	header->datalen = LSBMSB16(1);
	header->err.e8 = 0;

	return 1;
}

/*
  ����ơ��֥����Ͽ����Ƥ��뼭��ꥹ��
*/
int imewrapper_get_mountdictionary_list(int id, buffer_t *cbuf)
{
	cannaheader_t *header = (cannaheader_t *)cbuf->buf;

	header->datalen = LSBMSB16(2);
	header->err.e16 = 0;

	return 1;
}

/*
  ���ꤷ������ξ��������
*/
int imewrapper_query_dictionary(int id, buffer_t *cbuf)
{
	cannaheader_t *header = (cannaheader_t *)cbuf->buf;

	header->datalen = LSBMSB16(1);
	header->err.e8 = -1;

	return 1;
}

/*
  ����˿�����ñ�����Ͽ����
*/
int imewrapper_define_word(int id, buffer_t *cbuf)
{
	CALLFUNC(4);
}

/*
  ���񤫤�ñ��������롥
*/
int imewrapper_delete_word(int id, buffer_t *cbuf)
{
	CALLFUNC(5);
}

/*
  �ɤߤΤ���ʸ������Ф���Ϣʸ���Ѵ��⡼�ɤǤ��ʴ����Ѵ���Ԥ���
*/
int imewrapper_begin_convert(int id, buffer_t *cbuf)
{
	CALLFUNC(6);
}

/*
  ���ߤΤ��ʴ����Ѵ���Ȥ�λ����ɬ�פ˱����Ƴؽ���Ԥ���
*/
int imewrapper_end_convert(int id, buffer_t *cbuf)
{
	CALLFUNC(7);
}

/*
  ���ꤵ�줿ʸ��Τ��٤Ƥθ���ʸ������ɤߤ�������롥
*/
int imewrapper_get_candidacy_list(int id, buffer_t *cbuf)
{
	CALLFUNC(8);
}

/*
  ������ʸ����ɤߤ��ʤ�������롥
*/
int imewrapper_get_yomi(int id, buffer_t *cbuf)
{
	CALLFUNC(9);
}

/*
  ��ư�Ѵ��⡼�ɻ����ɤߥХåե������Ƥ��ѹ����������Ѵ���Ԥ���
*/
int imewrapper_subst_yomi(int id, buffer_t *cbuf)
{
	CALLFUNC(10);
}

/*
  ������ʸ����ɤߤ��ʤ��ѹ���������ʹߤ�ʸ�����Ѵ����롥
*/
int imewrapper_store_yomi(int id, buffer_t *cbuf)
{
	CALLFUNC(11);
}

/*
  ������ʸ����ɤߤ��ʤ��ѹ�����������ʸ��Τߤ�ñʸ���Ѵ����롥
*/
int imewrapper_store_range(int id, buffer_t *cbuf)
{
	CALLFUNC(12);
}

/*
  ̤��ʸ����ɤߤ�������롥
*/
int imewrapper_get_lastyomi(int id, buffer_t *cbuf)
{
	CALLFUNC(13);
}

/*
  ̤����ʸ�����Ū���Ѵ����롥
*/
int imewrapper_flush_yomi(int id, buffer_t *cbuf)
{
	CALLFUNC(14);
}

/*
  ��Ƭʸ�ᤫ�饫����ʸ��ޤ��ɤߤ��ɤߥХåե������������
*/
int imewrapper_remove_yomi(int id, buffer_t *cbuf)
{
	CALLFUNC(15);
}

/*
  ���ꤵ�줿���񤫤餽�μ���˴ޤޤ�Ƥ������Τߤ�������롥
*/
int imewrapper_get_simplekanji(int id, buffer_t *cbuf)
{
	CALLFUNC(16);
}

/*
  ���ꤵ�줿ʸ��򡤻��ꤵ�줿Ĺ���˶��ڤ�ľ���ơ����٤��ʴ����Ѵ����롥
*/
int imewrapper_resize_pause(int id, buffer_t *cbuf)
{
	CALLFUNC(17);
}

/*
  �����ȸ�����Ф����ʻ�����ʸ����Ǽ������롥
*/
int imewrapper_get_hinshi(int id, buffer_t *cbuf)
{
	CALLFUNC(18);
}

/*
  ������ʸ��η����Ǿ����������롥
*/
int imewrapper_get_lex(int id, buffer_t *cbuf)
{
	CALLFUNC(19);
}

/*
  �����ȸ���˴ؤ�����Ͼ������롥
*/
int imewrapper_get_status(int id, buffer_t *cbuf)
{
	CALLFUNC(20);
}

/*
  locale ������ѹ���Ԥ���
*/
int imewrapper_set_locale(int id, buffer_t *cbuf)
{
	CALLFUNC(21);
}

/*
  ��ư�Ѵ��⡼�ɤǤ��ʴ����Ѵ���Ԥ���
*/
int imewrapper_auto_convert(int id, buffer_t *cbuf)
{
	CALLFUNC(22);
}

/*
  ���Υ����Фγ�ĥ�ץ�ȥ�����䤤��碌��
*/
int imewrapper_query_extensions(int id, buffer_t *cbuf)
{
	cannaheader_t *header = (cannaheader_t *)cbuf->buf;

	header->datalen = LSBMSB16(1);
	header->err.e8 = 0;

	return 1;
}

/*
  ���饤����ȤΥ��ץꥱ�������̾�򤽤Υ����Ф����Τ�����Ͽ���롥
*/
int imewrapper_set_applicationname(int id, buffer_t *cbuf)
{
	cannaheader_t *header = (cannaheader_t *)cbuf->buf;

	header->datalen = LSBMSB16(1);
	header->err.e8 = 0;

	return 1;
}

/*
  ���饤����ȤΥ��롼��̾�򥵡��Ф����Τ�����Ͽ���롣
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
  �����Ф˽�λ�׵�����Τ��������Ф�λ�����롣
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
  �����Ф���³���Ƥ��륯�饤����ȿ��ʤɤΥ����о����������롥
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
  �����Фλ��ѵ��Ĥλ��Ȥ���������Ԥ���
*/
int imewrapper_get_access_control_list(int id, buffer_t *cbuf)
{
	cannaheader_t *header = (cannaheader_t *)cbuf->buf;

	header->datalen = LSBMSB16(2);
	header->err.e16 = LSBMSB16(-1);

	return 1;
}

/*
  �ƥ����ȼ�����������dics.dir �����Ƥ򹹿����롥
*/
int imewrapper_create_dictionary(int id, buffer_t *cbuf)
{
	cannaheader_t *header = (cannaheader_t *)cbuf->buf;

	header->datalen = LSBMSB16(1);
	header->err.e8 = -1;

	return 1;
}

/*
  �ƥ����ȼ����������dics.dir �����Ƥ򹹿����롥
*/
int imewrapper_delete_dictionary(int id, buffer_t *cbuf)
{
	cannaheader_t *header = (cannaheader_t *)cbuf->buf;

	header->datalen = LSBMSB16(1);
	header->err.e8 = -1;

	return 1;
}

/*
  �桼������μ���̾���ѹ����롥
*/
int imewrapper_rename_dictionary(int id, buffer_t *cbuf)
{
	cannaheader_t *header = (cannaheader_t *)cbuf->buf;

	header->datalen = LSBMSB16(1);
	header->err.e8 = -1;

	return 1;
}

/*
  �ƥ����ȼ����ñ������������롥
*/
int imewrapper_get_wordtext_dictionary(int id, buffer_t *cbuf)
{
	cannaheader_t *header = (cannaheader_t *)cbuf->buf;

	header->datalen = LSBMSB16(2);
	header->err.e16 = LSBMSB16(-1);

	return 1;
}

/*
  ���ꤷ������ǥ��쥯�ȥ�ˤ��뼭��ơ��֥��������롥
*/
int imewrapper_list_dictionary(int id, buffer_t *cbuf)
{
	cannaheader_t *header = (cannaheader_t *)cbuf->buf;

	header->datalen = LSBMSB16(1);
	header->err.e8 = -1;

	return 1;
}

/*
  ��������ݻ����Ƥ��뼭�����򼭽�˽񤭹��ࡥ
*/
int imewrapper_sync(int id, buffer_t *cbuf)
{
	cannaheader_t *header = (cannaheader_t *)cbuf->buf;

	header->datalen = LSBMSB16(1);
	header->err.e8 = 0;

	return 1;
}

/*
  �����READ/WRITE �����ѹ���Ԥ���
*/
int imewrapper_chmod_dictionary(int id, buffer_t *cbuf)
{
	cannaheader_t *header = (cannaheader_t *)cbuf->buf;

	header->datalen = LSBMSB16(1);
	header->err.e8 = -1;

	return 1;
}

/*
  �����ʣ��
*/
int imewrapper_copy_dictionary(int id, buffer_t *cbuf)
{
	cannaheader_t *header = (cannaheader_t *)cbuf->buf;

	header->datalen = LSBMSB16(1);
	header->err.e8 = -1;

	return 1;
}
