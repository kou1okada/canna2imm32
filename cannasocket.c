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
#include <unistd.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/un.h>
#include <netdb.h>
#include <errno.h>

#include "def.h"
#include "misc.h"

static struct sockaddr_un unaddr;
static struct sockaddr_in inaddr;
static int canna_unixfd, canna_inetfd;

extern client_t client[];
extern char inetmode;

#define MAXDATA 1024
#define BUFSIZE 4096
#define ACCESS_FILE "/etc/hosts.canna2imm32"

/*
 * canna_socket_open_unix : unix ドメインのソケットを生成する
 *                        : ファイルディスクリプタを返す
 */

static int canna_socket_open_unix()
{
  int old_umask;
  int fd;
  
  old_umask = umask(0);
  
  mkdir(CANNA_UNIX_DOMAIN_DIR, 0777);

  if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
    m_msg("Cannot open unix domain socket.\n");
    return -1;
  }
  
  unaddr.sun_family = AF_UNIX;
  strcpy(unaddr.sun_path, CANNA_UNIX_DOMAIN_PATH);

  if (bind(fd, (struct sockaddr *)(&unaddr), sizeof unaddr)) {
    close(fd);
    m_msg("Cannot bind.\n");
    m_msg("Another cannaserver detected.\n");
    m_msg("If you're sure cannaserver is not running,\n");
    m_msg("remove /tmp/.iroha_unix/IROHA.\n");
    return -1;
  }

  if (listen(fd, 5)) {
    close(fd);
    m_msg("Cannot listen.\n");
    return -1;
  }

  umask(old_umask);

  return fd;
}

/*
 * canna_socket_open_inet : inet ドメインのソケットを生成する
 *                        : ファイルディスクリプタを返す
 */
  
static int canna_socket_open_inet()
{
  struct servent *s;
  int one = 1, fd, i;

  if (inetmode == 0)
    return -1;

  if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    m_msg("Cannot open inet domain socket.\n");
    return -1;
  }
  
#ifdef SO_REUSEADDR
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *)(&one),
	     sizeof (int));
#endif

  s = getservbyname(CANNA_SERVICE_NAME, "tcp");

  memset((char *)&inaddr, 0, sizeof inaddr);
  inaddr.sin_family = AF_INET;
  inaddr.sin_port = s ? s->s_port : htons(CANNA_DEFAULT_PORT);
  inaddr.sin_addr.s_addr = htonl(INADDR_ANY);

  if ((i = bind(fd, (struct sockaddr *)(&inaddr), sizeof inaddr)) != 0) {
    m_msg("Bind refused.\n");
    return -1;
  }

  if (listen (fd, 5)) {
    close(fd);
    m_msg("Cannot listen.\n");
    return -1;
  }

  return fd;
}

/*
 * canna_socket_open : unix, inet ドメインのソケットを生成する。
 */
      
int canna_socket_open(int *ufd, int *ifd)
{
  if ((canna_unixfd = canna_socket_open_unix()) == -1)
    m_msg("unix domain not created.\n");
  if ((canna_inetfd = canna_socket_open_inet()) == -1)
    m_msg("inet domain not created.\n");

  if (canna_unixfd == -1)
    return -1;
  if ((inetmode == 1) && (canna_inetfd == -1))
    return -1;

  *ufd = canna_unixfd;
  *ifd = canna_inetfd;

  return 0;
}

int canna_socket_close()
{
  close(canna_unixfd);
  if (inetmode == 1)
    close(canna_inetfd);

  unlink(CANNA_UNIX_DOMAIN_PATH);

  return 0;
}

/*
 * canna_socket_accept_new_connection : 接続要求を受ける
 *                                      fd = canna_unixfd or canna_inetfd
 *                                    : ファイルディスクリプタを返す
 */

static int canna_socket_accept_new_connection(int fd)
{
  int i, newfd;
  int /* localhost_inaddr, */myhost_inaddr, *ip;
  char buf[MAXDATA], *host = NULL;
  int size;
  struct sockaddr_un addr_un;
  struct sockaddr_in addr_in;
  struct hostent *hp;
  unsigned int addr_list[BUFSIZE];	/* とりあえず4096あればいいか・・・ */
  FILE   *fp;
  char   *wp;

  size = sizeof(struct sockaddr_un);

  if ((newfd = accept(fd, (struct sockaddr *)(&addr_un), &size)) < 0) {
    m_msg("Cannot open new socket. Connection refused.\n");
    return -1;
  }

/* >> 問題はここから */
  if (addr_un.sun_family == AF_INET)
  {	/* このブロックが INET	Y.A. */
    memcpy(&addr_in, (struct sockaddr *)(&addr_un),sizeof(struct sockaddr_in));

    /* >> /etc/hosts.canna 対応 */
    i = 0;	/* addr_list のオフセット */
    /* check my host name */
    if (gethostname(buf, MAXDATA) == 0)
    {
      if ((hp = gethostbyname(buf)) != NULL)
      {
        ip = (int *)(hp->h_addr_list[0]);
        addr_list[i] = *ip;
        i++;
      }
    }

    /* check "localhost" */
    if ((hp = gethostbyname("localhost")) != NULL)
    {
      ip = (int *)(hp->h_addr_list[0]);
      addr_list[i] = *ip;
      i++;
    }

    /* check /etc/hosts.canna */
    if( (fp = fopen( ACCESS_FILE, "r" )) != (FILE *)NULL )
    {
      while( fgets( (char *)buf, BUFSIZE, fp ) != (char *)NULL )
      {
        buf[ strlen( (char *)buf )-1 ] = '\0' ;
        wp = buf ;
        if( !strtok( (char *)wp, ":" ) )
          continue ;

        if ((hp = gethostbyname(wp)) != NULL)
        {
          ip = (int *)(hp->h_addr_list[0]);
          if (i < BUFSIZE - 1)
          {
            addr_list[i] = *ip;
            i++;
          } else
            break;	/* これ以上入らない */
        }
      }
    }

    addr_list[i] = 0;	/* terminate */

    /*  */
    for (i=0; i<BUFSIZE; i++)
    {
      if (addr_list[i] == 0) break;
      if (addr_in.sin_addr.s_addr == addr_list[i])
      {
        m_netaddr2ascii(addr_in.sin_addr.s_addr, buf);

        hp = gethostbyaddr((char *)&addr_in.sin_addr, sizeof(struct in_addr), AF_INET);

        if (hp && hp->h_name)
          host = strdup(hp->h_name);
        else
          host = strdup(buf);
        break;
      }
    }

    if (host == NULL)
    {
      m_netaddr2ascii(addr_in.sin_addr.s_addr, buf);

      m_msg("REFUSE THE CONNECTION REQUEST FROM %s.\n", buf);
      close(newfd);
      return -1;
    }

    /* << /etc/hosts.canna 対応 */
/* << ここまで */
  } else {	/* このブロックが UNIXドメイン	Y.A. */
    host = strdup("UNIX");
  }

  for (i = USER_CLIENT; i < MAX_CLIENT_NUM; i++) {
    if (client[i].sockfd == -1) {
      client[i].sockfd = newfd;
      client[i].host = host;
      client[i].user[0] = 0;

      m_msg("New client #%d@%s accepted.\n", i, host);
      
      return 0;
    }
  }

  m_msg("Too many users. Connection from %s refused.\n", host);
  close(newfd);

  if (host != NULL) 
    free(host);

  return -1;
}

/*
 * canna_socket_check_connection : 接続が有効か調べる
 */

static int canna_socket_check_connection()
{
  struct timeval tv;
  fd_set set;
  int i;
  
  for (i = USER_CLIENT; i < MAX_CLIENT_NUM; i++) {
    if (client[i].sockfd != -1) {
      FD_ZERO(&set);
      FD_SET(client[i].sockfd, &set);

      tv.tv_sec = tv.tv_usec = 0;

      if (select(client[i].sockfd + 1, &set, NULL, NULL, &tv) < 0)
	client[i].need_terminate = TRUE; /* main.c で終了処理をしてもらう */
    }
  }

  return 0;
}
 
int canna_socket_wait_trigger()
{
	fd_set set;
	int i;
	int maxfd = 0;

	FD_ZERO(&set);

	for (i = USER_CLIENT; i < MAX_CLIENT_NUM; i++)
		if (client[i].sockfd != -1)
		{
			FD_SET(client[i].sockfd, &set);
			maxfd = client[i].sockfd > maxfd ? client[i].sockfd : maxfd;
		}
	if (canna_unixfd != -1) FD_SET(canna_unixfd, &set);
	if (canna_inetfd != -1) FD_SET(canna_inetfd, &set);
	if (canna_unixfd > maxfd) maxfd = canna_unixfd;
	if (canna_inetfd > maxfd) maxfd = canna_inetfd;

	if (select(maxfd + 1, &set, NULL, NULL, NULL) < 0)
	{
		if (errno == EBADF)
			canna_socket_check_connection();
		return -1;
	}

	for (i = USER_CLIENT; i < MAX_CLIENT_NUM; i++)
		if (client[i].sockfd != -1)
			if (FD_ISSET(client[i].sockfd, &set))
				client[i].data_received = 1;

	if (canna_unixfd != -1 && FD_ISSET(canna_unixfd, &set))
		canna_socket_accept_new_connection(canna_unixfd);

	if (canna_inetfd != -1 && FD_ISSET(canna_inetfd, &set))
		canna_socket_accept_new_connection(canna_inetfd);

	return 0;
}
