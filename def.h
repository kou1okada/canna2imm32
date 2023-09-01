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

#ifndef __def_h__
#define __def_h__

#ifndef __const_def__
#include "constdef.h"
#endif

#include <sys/types.h>

#ifndef __uchar_t__
#define __uchar_t__
typedef unsigned char uchar;
#endif


typedef struct {
  int sockfd;

  int ime;

  char user[10]; /* ユーザー名 */
  char *host; /* ホスト名 */
  char *homedir; /* ホームディレクトリのパス */

  /* IME に固有のデータを malloc して保持 */
  union {
    struct _winimm_t *winimm;
  } data;

  int data_received:1; /* ソケットからデータを受信した？ */
  int need_terminate:1; /* main.c で終了処理をする必要あり？ */

} client_t;

#define IME_NON 0
#define IME_START 1
#define IME_IMM32 1
#define IME_END 1

typedef struct {
  unsigned char type;
  unsigned char extra;
  unsigned short datalen;
  union {
    unsigned short e16;
    unsigned char e8;
  } err;
} cannaheader_t;

#ifndef WORDS_BIGENDIAN
#define LSBMSB16(_s) ((((_s) >> 8) & 0xff) | (((_s) & 0xff) << 8))
#define LSBMSB32(_s) ((((_s) >> 24) & 0xff) | (((_s) & 0xff) << 24) | \
                      (((_s) >> 8) & 0xff00) | (((_s) & 0xff00) << 8))
#else
#define LSBMSB16(_s) (_s)
#define LSBMSB32(_s) (_s)
#endif

#endif
