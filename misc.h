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

#ifndef __misc_h__
#define __misc_h__

#ifndef TRUE

#define TRUE 1
#define FALSE 0

#endif

typedef struct {
  size_t size;
  char *buf;
} buffer_t;

#ifndef __uchar_t__
#define __uchar_t__
typedef unsigned char uchar;
#endif

#define ISSJISKANJI1(_c) ((((_c) & 0xff) >= 0x81 && ((_c) & 0xff) <= 0x9f) || \
			  (((_c) & 0xff) >= 0xe0 && ((_c) & 0xff) <= 0xfc))

#define ISSJISKANA(_c) (((_c) & 0xff) >= 0xa0 && ((_c) & 0xff) <= 0xdf)


#define ISSJIS_ZENKAKU_HIRAGANA(_c1,_c2) (((_c1) & 0xff) == 0x82 && \
					  ((_c2) & 0xff) >= 0x9f && \
					  ((_c2) & 0xff) <= 0xf1)

#define ISSJIS_ZENKAKU_KATAKANA(_c1,_c2) (((_c1) & 0xff) == 0x83 && \
					  ((_c2) & 0xff) >= 0x40 && \
					  ((_c2) & 0xff) <= 0x94)

#define ISSJIS_ZENKAKU_VU(_c1,_c2) (((_c1) & 0xff) == 0x83 && \
				    ((_c2) & 0xff) == 0x94)

#define MYFREE(_p) {if(_p) {free(_p); _p=NULL;} }


typedef void (*signalhandler_t)(int);

int m_daemonize(char *pid_path);

#ifndef __misc_c__
extern int m_message_level;
#endif

#define MSG_DEBUG 0
#define MSG_NOTICE 1

int m_message_init(char *logfilepath);
int m_message_term();
int m_message_debug(const char *fmt, ...);
int m_message_notice(const char *fmt, ...);

#define m_msg_dbg m_message_debug
#define m_msg m_message_notice

int buffer_free(buffer_t *b);
int buffer_check(buffer_t *b, size_t required_size);
int buffer_clear(buffer_t *b);

void *m_memdup(void *src, size_t n);

char *m_makepath(char *dir, char *file);
int m_splitpath(char *path, char **dir, char **file);

char *m_netaddr2ascii(uint netaddr, char *ascii);

int m_socket_read(int fd, char *ptr, int totalsize);
int m_socket_write(int fd, char *ptr, int totalsize);

int m_count_canna_mode(int mode);
int m_get_canna_mode(int mode, int n);
int m_count_valid_canna_mode(int mode);

int cannawc2euc(ushort *src, int srclen, char *dest, int destlen);
int euc2cannawc(char *src, int srclen, ushort *dest, int destlen);
int cannawcstrlen(ushort *ws);
int euc2sjis(uchar *euc, int euclen, uchar *sjis, int sjislen);
int sjis2euc(uchar *sjis, int sjislen, uchar *euc, int euclen);

int m_replace_string(uchar *eucbuf, char *pre, char *post);
int m_convert_zen2han(uchar *sjisbuf);
int m_convert_hankana2zenkana(uchar *euc, int len);
int m_convert_zenkana2zenhira(uchar *src, uchar *dst, int len);
int m_convert_zenhira2zenkata(uchar *euc_hira, int slen, uchar *euc_kata);

int m_setup_signal(signalhandler_t handler);
int m_copy_file(char *from, char *to);
int m_copy_file_fp(FILE *from, FILE *to);

int m_exist_hankata(uchar *euc);
int m_is_zenkata_string(uchar *euc);
int m_is_hiragana_string(uchar *euc);

int m_reconvroma(uchar *src, uchar *dst);

/* system() */

int m_system_clear();
int m_system_register_file(int fd);
int m_system(char *command);

/* 設定ファイル読み込み関数 */

int m_conf1_parse(char *line, char **ope_r, char **val_r);
int m_conf_string(char *ope, char *opestr, char *val, char **val_r);
int m_conf_multiple_choice(char *ope, char *opestr, char *val, char **choice,
			   int choice_n, int case_sensitive);
int m_conf_tof(char *ope, char *opestr, char *val, char *str1, char *str2,
	       int case_sensitive);
int m_conf_isequal(char *ope, char *opestr, char *val, char *valstr);

#define M_CONF_YESNO(_ope,_opestr,_val) m_conf_tof((_ope),(_opestr),(_val), \
						   "NO","YES",TRUE)

#define M_CONF_IYESNO(_ope,_opestr,_val) m_conf_tof((_ope),(_opestr),(_val), \
						    "NO","YES",FALSE)

#define M_CONF_ONOFF(_ope,_opestr,_val) m_conf_tof((_ope),(_opestr),(_val), \
						   "OFF","ON",TRUE)

#define M_CONF_IONOFF(_ope,_opestr,_val) m_conf_tof((_ope),(_opestr),(_val), \
						    "OFF","ON",FALSE)

#define M_CONF_TORF(_ope,_opestr,_val) m_conf_tof((_ope),(_opestr),(_val), \
						  "FALSE", "ON", TRUE)

#define M_CONF_ITORF(_ope,_opestr,_val) m_conf_tof((_ope),(_opestr),(_val), \
						   "FALSE", "TRUE", FALSE)

						   




#endif
