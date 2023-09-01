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
#include <sys/types.h>

#include <windows.h>
#include <windowsx.h>

#include "def.h"
#include "constdef.h"
#include "cannaproto.h"
#include "cannasocket.h"
#include "imewrapper.h"
#include "misc.h"

char debugmode = 0;
char daemonizemode = 1;	/* 0: 普通のアプリとして動作	1: デーモンとして動作 */
char inetmode = 0;		/* 0: inetドメインソケットを使わない	1: inetドメインソケットを使う */
char logmode = 1;		/* 0: コンソールにログを出力する	1: ESECANNA_LOG_PATH にログを出力する */
client_t client[MAX_CLIENT_NUM];

#ifdef USEDLL
extern char *ime_dl_name[];
#endif

BOOL WINAPI handler_routine(DWORD dwCtrlType);

int term_all(char *msg)
{
	int i;

	for (i = USER_CLIENT; i < MAX_CLIENT_NUM; i++)
	{
		if (client[i].sockfd != -1)
		{
			imewrapper_end_client(i);
			close(client[i].sockfd);
			imewrapper_clear_client_data(i);
		}
	}

	imewrapper_clear_client_data(IMM32_ROOT_CLIENT);
	imewrapper_end_rootclient(IMM32_ROOT_CLIENT);

	canna_socket_close();

	m_msg(msg);
	m_message_term();

	SetConsoleCtrlHandler(handler_routine, FALSE);

	exit(0);
}

void sig_terminate()
{
  term_all("Terminated by signal.\n");
}

/* windows上でコンソールアプリがシャットダウン等のイベントを受けるためのルーチン */
BOOL WINAPI handler_routine(DWORD dwCtrlType)
{
	/* print out what control event was received to the current console */
	switch(dwCtrlType)
	{
		case CTRL_C_EVENT:
		case CTRL_BREAK_EVENT:
		case CTRL_SHUTDOWN_EVENT:
		case CTRL_LOGOFF_EVENT:
			term_all("Terminated by windows event.\n");
			return(TRUE);

		case CTRL_CLOSE_EVENT:		/* ある種のデーモンとして動きたいのでこれは却下 */
			return(TRUE);

		default:
			break;
	}
	return(FALSE);
}

int init_private()
{
  int i;

  for (i = USER_CLIENT; i < MAX_CLIENT_NUM; i++) {
    memset(&(client[i]), 0, sizeof(client_t));

    client[i].sockfd = -1;
  }

  return 0;
}

int init_all()
{
	int canna_ufd, canna_ifd;

	init_private();

	if (canna_socket_open(&canna_ufd, &canna_ifd) < 0)
	{
		return -1;
	}

	m_system_register_file(canna_ufd);
	if (inetmode == 1)
		m_system_register_file(canna_ifd);

	m_message_init(logmode ? ESECANNA_LOG_PATH : NULL);

	if (daemonizemode == 1)
	{
		m_daemonize(ESECANNA_PID_PATH);
		m_msg("%s started by UID[%d]\n", ESECANNA_VERSION, getuid());
	}

	m_setup_signal((signalhandler_t)sig_terminate);

	SetConsoleCtrlHandler(handler_routine, TRUE);

	/* かな漢を行うためのダミーウィンドウの作成 */
	if (mw_InitWindow() != 0)
		return -1;

	return 0;
}

void loop()
{
	int i, ii, ret, flag;
	char down_flag[IME_END], end_flag[IME_END];

	for (;;)
	{
		memset(down_flag, 0, IME_END);
		memset(end_flag, 0, IME_END);

		canna_socket_wait_trigger();

		for (i = USER_CLIENT; i < MAX_CLIENT_NUM; i++)
		{
			if (client[i].data_received)
			{
				client[i].data_received = FALSE;
				ret = canna_proto_main(i);

				if (ret < 0)
				{
					/* IME が落ちた。-(IME_???) が返される。*/
					down_flag[-(ret) - 1] = TRUE;
				}
			}

			if (client[i].need_terminate)
			{
				if (client[i].ime > 0)
					end_flag[client[i].ime - 1] = TRUE;

				client[i].need_terminate = FALSE;
				imewrapper_end_client(i);
				close(client[i].sockfd);
				imewrapper_clear_client_data(i);
			}
		}


		for (i = IME_START; i <= IME_END; i++)
		{
			if (down_flag[i - 1] == TRUE)
			{
				/* IME が落ちたので、終了処理をする */

				for (ii = USER_CLIENT; ii < MAX_CLIENT_NUM; ii++)
				{
					if (client[ii].ime == i)
					{
						imewrapper_end_client(ii);
						close(client[ii].sockfd);
						imewrapper_clear_client_data(ii);
					}
				}

				/* rootclient も終了。同時にモジュールも開放される。*/
				imewrapper_end_rootclient(i);
			}
		}

		/* IME が落ちたのではなく,クライアントの終了処理をした場合は,
		 * 他に IME に接続しているクライアントがいるかどうかを確かめて
		 * IME を場合によっては終了させる
		 */
		for (i = IME_START; i <= IME_END; i++)
		{
			if (end_flag[i - 1] == TRUE && down_flag[i - 1] == FALSE)
			{
				flag = 0;

				for (ii = USER_CLIENT; ii < MAX_CLIENT_NUM; ii++)
				{
					if (client[ii].ime == i)
						flag = 1;
				}

				if (flag == 0)
					imewrapper_end_rootclient(i);
			}
		}
	}
}

static void show_help(void)
{
	fprintf(stderr, "%s\n", ESECANNA_VERSION);
	fprintf(stderr, "options are:\n");
	fprintf(stderr, "    -inet       use inet domain\n");
	fprintf(stderr, "    -f          run foreground\n");
	fprintf(stderr, "    -nolog      no log output\n");
	fprintf(stderr, "    -d          debug mode\n");
}

int main(int argc, char **argv)
{
	int i;
	char buf[1024];

	for (i = 1; i < argc; i++)
	{
		if (strcmp(argv[i], "-d") == 0)
			debugmode = 1;
		else if (strcmp(argv[i], "-f") == 0)
			daemonizemode = 0;
		else if (strcmp(argv[i], "-inet") == 0)
			inetmode = 1;
		else if (strcmp(argv[i], "-nolog") == 0)
			logmode = 0;
		else if (strcmp(argv[i], "--help") == 0)
		{
			show_help();
			return 0;
		}
	}

	fprintf(stderr, "%s, start\n", ESECANNA_VERSION);
#ifdef USEDLL
	fprintf(stderr, "\tPlugins: ");

	for (i = IME_START; i <= IME_END; i++)
	{
		sprintf(buf, "%s/%s", ESECANNA_DL_PATH, ime_dl_name[i - 1]);
		if (access(buf, F_OK) == 0)
			fprintf(stderr, "%s ", ime_dl_name[i - 1]);
	}

	fprintf(stderr, "\n");
#endif
	i = init_all();

	if (i < 0)
	{
		return -1;
	}

	loop();

	fprintf(stderr, "%s, end.\n", ESECANNA_VERSION);
	return 0;
}
