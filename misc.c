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

#define __USE_GNU
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>

#define __misc_c__

#include "misc.h"

struct {
  uchar zen[3];
  uchar c;
} zen_han_table[] = {
  {{0x81, 0x49, 0}, '!'},
  {{0x81, 0x68, 0}, '"'},
  {{0x81, 0x94, 0}, '#'},
  {{0x81, 0x90, 0}, '$'},
  {{0x81, 0x93, 0}, '%'},
  {{0x81, 0x95, 0}, '&'},
  {{0x81, 0x66, 0}, 0x27}, /* ' */
  {{0x81, 0x69, 0}, '('},
  {{0x81, 0x6a, 0}, ')'},
  {{0x81, 0x96, 0}, '*'},
  {{0x81, 0x7b, 0}, '+'},
  {{0x81, 0x43, 0}, ','},
  {{0x81, 0x7c, 0}, '-'},
  {{0x81, 0x44, 0}, '.'},
  {{0x81, 0x5e, 0}, '/'},
  {{0x81, 0x46, 0}, ':'},
  {{0x81, 0x47, 0}, ';'},
  {{0x81, 0x83, 0}, '<'},
  {{0x81, 0x81, 0}, '='},
  {{0x81, 0x84, 0}, '!'},
  {{0x81, 0x48, 0}, '?'},
  {{0x81, 0x97, 0}, '@'},
  {{0x81, 0x6b, 0}, '['},
  {{0x81, 0x8f, 0}, '\\'},
  {{0x81, 0x6e, 0}, ']'},
  {{0x81, 0x4f, 0}, '^'},
  {{0x81, 0x51, 0}, '_'},
  {{0x81, 0x4d, 0}, '`'},
  {{0x81, 0x6f, 0}, '{'},
  {{0x81, 0x62, 0}, '|'},
  {{0x81, 0x70, 0}, '}'},
  {{0x81, 0x60, 0}, '~'},
  {{0x81, 0x50, 0}, '~'},
  {{0x81, 0x42, 0}, 0xa1}, /*。*/
  {{0x81, 0x75, 0}, 0xa2}, /*「*/
  {{0x81, 0x76, 0}, 0xa3}, /*」*/
  {{0x81, 0x41, 0}, 0xa4}, /*、*/
  {{0x81, 0x45, 0}, 0xa5}, /*・*/
  {{0x81, 0x5b, 0}, 0xb0}, /*ー*/
  {{0x81, 0x4a, 0}, 0xde}, /*゛*/
  {{0x81, 0x4b, 0}, 0xdf}, /*゜*/
  {{   0,    0, 0},    0}
};

#define MOD_TEN  0x01 /* ゛ が次に来た場合,合成(カ + ゛= ガ)するか */
#define MOD_MARU 0x02 /* ゜ が次に来た場合,合成するか */
#define MOD_TM   0x03 /* ハ行は, ゛ と ゜ 両方受け付ける */

/*
  MOD_xxx は、単なるフラグとしてだけはない。
  文字の並びは、「てんてんなし」「てんてんあり」「まるあり」の順。
  すなわち、「カガキギ...」 「ハバパヒビピ...」のように並ぶ。
  よって、例えば 「カ」に, MOD_TEN を足せば、「ガ」になるのだ。
*/
   

struct {
  uchar han_start, han_end;  /* 0xa1 - 0xdf */
  uchar modifiers:4,         /* MOD_xxx の OR */
    offset:4;                /* 何バイトごとに現れるか */
  uchar zenkaku2_start;      /* euc の２バイト目 */
} kana_table[] = {
  {0xa6, 0xa6,       0, 0, 0xf2}, /* ヲ */
  {0xa7, 0xab,       0, 2, 0xa1}, /* ァ - ォ */
  {0xac, 0xae,       0, 2, 0xe3}, /* ャ - ョ */
  {0xaf, 0xaf,       0, 0, 0xc3}, /* ッ */
  {0xb1, 0xb5,       0, 2, 0xa2}, /* ア - オ */
  {0xb3, 0xb3, MOD_TEN, 0, 0xf3}, /* ヴ */
  {0xb6, 0xba, MOD_TEN, 2, 0xab}, /* カ - コ ガ - ゴ */
  {0xbb, 0xbf, MOD_TEN, 2, 0xb5}, /* サ - ソ ザ ｰ ゾ */
  {0xc0, 0xc1, MOD_TEN, 2, 0xbf}, /* タ - チ ダ ｰ ヂ */
  {0xc2, 0xc2, MOD_TEN, 0, 0xc4}, /* ツ ヅ */
  {0xc3, 0xc4, MOD_TEN, 2, 0xc6}, /* テ - ト デ - ド */
  {0xc5, 0xc9,       0, 1, 0xca}, /* ナ ｰ ノ */
  {0xca, 0xce,  MOD_TM, 3, 0xcf}, /* ハ ｰ ホ バ - ボ パ ｰ ポ */
  {0xcf, 0xd3,       0, 1, 0xde}, /* マ - モ */
  {0xd4, 0xd6,       0, 2, 0xe4}, /* ヤ - ヨ */
  {0xd7, 0xdb,       0, 1, 0xe9}, /* ラ - ロ */
  {0xdc, 0xdc,       0, 0, 0xef}, /* ワ */
  {0xdd, 0xdd,       0, 0, 0xf3}, /* ン */
  {   0,    0,       0, 0,    0}
};

extern char debugmode;
extern char daemonizemode;
extern char logmode;

/*
 * Daemonize. from iMultiMouse...
 */

static pid_t child;
static char *pid_file_path;

static void m_quit_parent()
{
	FILE *fp;

	if (pid_file_path != NULL && (fp = fopen(pid_file_path, "w")) != NULL)
	{
		fprintf(fp, "%d\n", child);
		fclose(fp);
	}

	exit(0);
}

int m_daemonize(char *pid_path)
{
	pid_t parent;
	int fd;

	pid_file_path = pid_path;

	parent = getpid();

	close(fileno(stdin));
	close(fileno(stdout));

	if ((child = fork()) == -1)
	{
		perror("");
		return(EOF);
	}

	if (child)
		m_quit_parent();

	signal(SIGHUP, SIG_IGN); 

	setsid();

	close(fileno(stderr));

	if ((fd = open("/dev/null", 2)) >= 0)
	{
		if (fd != 0)
			dup2(fd, 0);
		if (fd != 1)
			dup2(fd, 1);
		if (fd != 2)
			dup2(fd, 2);
		if (fd != 0 && fd != 1 && fd != 2)
			close(fd);
	}

	chdir("/");

	return(0);
}

/*
 * m_message ... derived from Report* function, written by Morita Akio.
 */

static FILE *logfp = NULL;
static ino_t loginode = 0;
static char *logfile;

static int m_message_log_open()
{
  struct stat st;
  
  if (logfile) {
    if ((logfp = fopen(logfile, "a")) != NULL) {
      if (fstat(fileno(logfp), &st) == 0) {
	loginode = st.st_ino;

	return 0;
      }

      loginode = 0;
      
      fclose(logfp);
    }
    
    free(logfile);
    logfile = NULL;
  }

  logfp = stderr;

  m_msg_dbg("Logging to file failed.\n");

  return -1;
}

static int m_message_log_close()
{
  if (logfp != NULL && logfp != stderr) {
    fclose(logfp);
    logfp = stderr;
  }

  return 0;
}

static int m_message_log_check()
{
  struct stat st;
  static char recall_flag;

  if (recall_flag)
    return 0;

  recall_flag = 1;

  if (loginode) {
    if (stat(logfile, &st) < 0 || st.st_ino != loginode) {
      m_msg("Logfile rotated. Try to reopen...\n");

      m_message_log_close();
      if (m_message_log_open() == 0)
	m_msg("Reopening logfile succeeded.\n");
      else
	m_msg("Reopening logfile failed.\n");
    }
  }

  recall_flag = 0;

  return 0;
}

int m_message_init(char *logfilepath)
{
  if (logfilepath) {
    logfile = strdup(logfilepath);

    m_message_log_open();
  } else
    logfp = stderr;
  
  return 0;
}

int m_message_term()
{
  m_message_log_close();

  return 0;
}

int m_message_output(int level, const char *msg)
{
  time_t tm;
  char *timebuf, buf[26];
  
  if (logfp && level == MSG_NOTICE) {
    m_message_log_check();
    
    tm = time(0);
    timebuf = ctime(&tm);
    memcpy(buf, &timebuf[4], 16);
    buf[15] = 0;

    fprintf(logfp, "%s: %s", buf, msg);
    fflush(logfp);
  } else if (logfp && level == MSG_DEBUG) {
    m_message_log_check();
    
    tm = time(0);
    timebuf = ctime(&tm);
    memcpy(buf, &timebuf[4], 16);
    buf[15] = 0;

    fprintf(logfp, "%s: DBG: %s", buf, msg);
    fflush(logfp);
  } else {
    fprintf(stderr, "%s", msg);
    fflush(stderr);
  }

  return 0;
}

int m_message_debug(const char *fmt, ...)
{
#if HAVE_VASPRINTF
  char *formated;
#else
  char formated[1024]; /* FIXME:too ad-hoc!! */
#endif
  va_list ap;

  if (debugmode == 0)
    return 0;

  va_start(ap, fmt);
#if HAVE_VASPRINTF
  vasprintf(&formated, fmt, ap);
#else
#if HAVE_VSNPRINTF
  vsnprintf(formated, 1024, fmt, ap);
#else
  vsprintf(formated, fmt, ap);
#endif
#endif
  va_end(ap);

  if (formated != NULL) {
    m_message_output(MSG_DEBUG, formated);
#if HAVE_VASPRINTF
    free((void *)formated);
#endif
    return 0;
  } else {
    m_message_output(MSG_DEBUG, "out of memory!\n");
    return -1;
  }
}

int m_message_notice(const char *fmt, ...)
{
#if HAVE_VASPRINTF
  char *formated;
#else
  char formated[1024];
#endif
  va_list ap;

  if (logmode == 0)
    return 0;

  va_start(ap, fmt);
#if HAVE_VASPRINTF
  vasprintf(&formated, fmt, ap);
#else
#if HAVE_VSNPRINTF
  vsnprintf(formated, 1024, fmt, ap);
#else
  vsprintf(formated, fmt, ap);
#endif
#endif
  va_end(ap);

  if (formated != NULL) {
    m_message_output(MSG_NOTICE, formated);
#if HAVE_VASPRINTF
    free((void *)formated);
#endif
    return 0;
  } else {
    m_message_output(MSG_NOTICE, "out of memory!\n");
    return -1;
  }
}

/*
 * buffer_
 */

int buffer_free(buffer_t *b)
{
  if (b->buf && b->size)
    free(b->buf);

  b->buf = NULL;
  b->size = 0;

  return 0;
}

int buffer_check(buffer_t *b, size_t required_size)
{
  char *buf;
  size_t size;
  
#ifdef DEBUG /* for Memory Allocate Stress Test */
  size = required_size;
  if (size > 0) {
    if ((buf = malloc(size)) != NULL) {
      memset(buf, 0, size);
      if (b->size > 0) {
	if (size > b->size) {
	  memcpy(buf, b->buf, b->size); /* Copy  Buffer */
	} else {
	  memcpy(buf, b->buf, size); /* Copy  Buffer */
	}
	memset(b->buf, 0, b->size);   /* Clear Old Buffer */
	free(b->buf);                 /* Free  Old Buffer */
      }

      /*
      m_message_debug("Buffer Rellocate Request %d (%d -> %d reallcated)",
		      equired_size, b->size, size);
		      */
      
      b->buf = buf;
      b->size = size;

      return 0;
    } else {
      return -1;
    }
  } else {
    memset(b->buf, 0, b->size); /* Clear Old Buffer */
    free(b->buf);               /* Free  Old Buffer */
    b->size = 0;

    /*
      m_message_debug("Buffer Rellocate Request %d (%d -> %d reallcated)",
                      required_size, b->size, size);
		      */
    return 0;
  }
#else
  if (required_size > b->size) {
    size = ((required_size + 127) / 128) * 128;
    if ((buf = realloc(b->buf, size)) != NULL) {
      if (b->size == 0)
	memset(buf, 0, size);

      /*
      m_message_debug("Buffer Rellocate Request %d (%d -> %d reallcated)",
                      required_size, b->size, size);
		      */

      b->buf = buf;
      b->size = size;

      return 0;
    } else {
      return -1;
    }
  }
#endif
  return 0;
}

int buffer_clear(buffer_t *b)
{
  if (b->buf)
    memset(b->buf, 0, b->size);
  return 0;
}

/* m_conf1_parse() : 設定ファイルのある一行を解析する
 *            文法 : OpeCode=String
 *          帰り値 : 0..Success -1..Error
 *                 - line  ... ファイルの行。保存されない。
 *                 - ope_r ... OpeCode の文字列を返す
 *                 - val_r ... String の文字列を返す
 */

int m_conf1_parse(char *line, char **ope_r, char **val_r)
{
  char *p;
  
  if ((p = strchr(line, '\n')) != NULL)
    *p = '\0';
  if ((p = strchr(line, '=')) != NULL) {
    *p = '\0';
    *val_r = p + 1;
    *ope_r = line;

    return 0;
  }

  return -1;
}

int m_conf_string(char *ope, char *opestr, char *val, char **val_r)
{
  if (strcmp(ope, opestr) == 0) {
    if (*val_r)
      free(*val_r);

    if (val[0])
      *val_r = strdup(val);
    else
      *val_r = NULL;

    return 1;
  }

  return 0;
}

int m_conf_multiple_choice(char *ope, char *opestr, char *val, char **choice,
			   int choice_n, int case_sensitive)
{
  int i;
  int (*compare)(const char *, const char *);

  compare = (case_sensitive) ? strcmp : strcasecmp;
  
  if (strcmp(ope, opestr) == 0) {
    for (i = 0; i < choice_n; i++) {
      if (compare(val, choice[i]) == 0)
	return i + 1;
    }
  }

  return 0;
}

int m_conf_tof(char *ope, char *opestr, char *val, char *str1, char *str2,
	       int case_sensitive)
{
  char *choice[2];

  choice[0] = str1;
  choice[1] = str2;

  return m_conf_multiple_choice(ope, opestr, val, choice, 2, case_sensitive);
}

int m_conf_isequal(char *ope, char *opestr, char *val, char *valstr)
{
  if (strcmp(ope, opestr) == 0) {
    if (strcmp(val, valstr) == 0)
      return 2;
    else
      return 1;
  }

  return 0;
}

/*
 * m_memdup()
 */

void *m_memdup(void *src, size_t n)
{
  char *dst;

  dst = malloc(n);
  memcpy(dst, src, n);

  return (void *)dst;
}


/*
 * m_makepath m_splitpath
 */

char *m_makepath(char *dir, char *file)
{
  int dirlen, filelen;
  char *path;
  
  dirlen = strlen(dir);
  filelen = strlen(file);

  path = malloc(dirlen + 1 + filelen + 1);

  if (path) {
    strcpy(path, dir);
    
    if (dirlen > 0 && dir[dirlen - 1] != '/')
      strcat(path, "/");

    strcat(path, file);
  }
  
  return path;
}

int m_splitpath(char *path, char **dir, char **file)
{
  char *lastslash, *buf;
  
  buf = strdup(path);
  lastslash = strrchr(buf, '/');

  if (dir != NULL) {
    if (lastslash != NULL) {
      *lastslash = '\0';
      
      if ((*dir = strdup(buf)) == NULL) {
	free(buf);
	return -1;
      }
      
      lastslash++;
    } else {
      *dir = NULL;
      lastslash = buf;
    }
  } else {
    if (lastslash == NULL)
      lastslash = buf;
  }

  if (file != NULL) {
    if ((*file = strdup(lastslash)) == NULL) {
      free(buf);
      return -1;
    }
  }

  free(buf);

  return 0;
}

/*
 * m_netaddr2ascii()
 */

char *m_netaddr2ascii(uint netaddr, char *ascii)
{
#ifdef WORDS_BIGENDIAN
  sprintf(ascii, "%d,%d,%d,%d",
	  (netaddr >> 24) & 0xff, (netaddr >> 16) & 0xff,
	  (netaddr >> 8) & 0xff, (netaddr) & 0xff);
#else 
  sprintf(ascii, "%d.%d.%d.%d",
	  (netaddr) & 0xff, (netaddr >> 8) & 0xff,
	  (netaddr >> 16) & 0xff, (netaddr >> 24) & 0xff);
#endif /* WORDS_BIGENDIAN */

  return ascii;
}

/*
 * analyze canna converting mode...
 */

int m_count_canna_mode(int mode)
{
  int ret = 0;
  
  while (mode) {
    mode = mode >> 4;
    ret++;
  }

  return ret;
}

int m_get_canna_mode(int mode, int n)
{
  int ret;

  ret = (mode >> (4 * n)) & 0x0f;

  /* ret..1: HIRAGANA 2: HANKAKU 3: KATAKANA 4: ZENKAKU */
  
  return ret;
}

int m_count_valid_canna_mode(int mode)
{
  int ret = 0;
  int i, max, cmode;

  max = m_count_canna_mode(mode);

  for (i = 0; i < max; i++) {
    cmode = m_get_canna_mode(mode, i);

    if (cmode == 1 || cmode == 3)
      ret++;
  }

  return ret;
}

/*
 * convert coding system...
 */
  
int cannawc2euc(ushort *src, int srclen, char *dest, int destlen)
{
  register int i, j;
  register uchar *c;
  
  for (i = 0, j = 0 ; i < srclen && j + 2 < destlen ; i++) {
    c = (unsigned char *)(&src[i]);
    switch ((c[0] | (c[1] << 8)) & 0x8080) {
      case 0:
	/* ASCII */
	// dest[j++] = (char)(((unsigned)wc & 0x7f00) >> 8);
	dest[j++] = c[1];
	break;
      case 0x8000:
	/* 半角カナ */
	dest[j++] = (char)0x8e; /* SS2 */
	// dest[j++] = (char)((((unsigned)wc & 0x7f00) >> 8) | 0x80);
	dest[j++] = c[1] | 0x80;
	break;
      case 0x0080:
	/* 外字 */
	dest[j++] = (char)0x8f;
	// dest[j++] = (char)(((unsigned)wc & 0x7f) | 0x80);
	// dest[j++] = (char)((((unsigned)wc & 0x7f00) >> 8) | 0x80);
	dest[j++] = c[0] | 0x80;
	dest[j++] = c[1] | 0x80;
	break;
      case 0x8080:
	/* 漢字 */
	// dest[j++] = (char)(((unsigned)wc & 0x7f) | 0x80);
	// dest[j++] = (char)((((unsigned)wc & 0x7f00) >> 8) | 0x80);
	dest[j++] = c[0] | 0x80;
	dest[j++] = c[1] | 0x80;
	break;
    }
  }
  dest[j] = '\0';
  return j;
}

int euc2cannawc(char *src, int srclen, ushort *dest, int destlen)
{
  register int i, j;
  register unsigned ec;
  uchar *c;
  for (i = 0, j = 0 ; i < srclen && j + 1 < destlen ; i++) {
    ec = (unsigned)(unsigned char)src[i];
    c = (unsigned char *)(&dest[j++]);
    if (ec & 0x80) {
      switch (ec) {
	case 0x8e: /* SS2 */
	  // dest[j++] = (ushort)((0x80 | ((unsigned)src[++i] & 0x7f)) << 8);
	  c[0] = 0;
	  c[1] = src[++i] | 0x80;
	  break;
	case 0x8f: /* SS3 */
	  // dest[j++] = (ushort)(0x0080
	  //	       | ((unsigned)src[i + 1] & 0x7f)
	  //	       | (((unsigned)src[i + 2] & 0x7f) << 8));
	  c[0] = 0x80 | src[i + 1];
	  c[1] = src[i + 2] & 0x7f;
	  
	  i += 2;
	  break;
	default:
	  // dest[j++] = (ushort)(0x8080 | ((unsigned)src[i] & 0x7f)
	  //		       | (((unsigned)src[i + 1] & 0x7f) << 8));
	  c[0] = 0x80 | src[i];
	  c[1] = 0x80 | src[i + 1];
	  
	  i++;
	  break;
      }
    }
    else {
      // dest[j++] = (ushort)(ec << 8);
      c[0] = 0;
      c[1] = ec;
    }
  }
  dest[j] = 0;
  return j;
}
    
int cannawcstrlen(ushort *ws)
{
  int res = 0;
  while (*ws++) {
    res++;
  }
  return res;
}

ushort *cannawcstrdup(ushort *ws)
{
  ushort *p;
  int len;

  len = cannawcstrlen(ws) * 2 + 2;

  p = (ushort *)malloc(len);
  memcpy(p, ws, len);

  return (ushort *)p;
}

ushort *cannawcstrcat(ushort *p1, ushort *p2)
{
  uint l1, l2;

  l1 = cannawcstrlen(p1);
  l2 = cannawcstrlen(p2);

  memcpy(&(p1[l1]), p2, l2);
  p1[l1 + l2] = 0;

  return p1;
}

/* src 中にいくつの key が現れるかを得る */

int cannawcnumstr(ushort *src, ushort *key)
{
  int i, len1, len2, ret;

  len1 = cannawcstrlen(src);
  len2 = cannawcstrlen(key);
  ret = 0;
  
  for (i = 0; i <= len1 - len2;) {
    if (memcmp(&(src[i]), key, len2 * 2) == 0) {
      ret++;
      i += len2;
    } else {
      i++;
    }
  }

  return ret;
}


int euc2sjis(uchar *euc, int euclen, uchar *sjis, int sjislen)
{
  int euc_pnt = 0, sjis_pnt = 0;
  uint hi, lo;

  while (euc[euc_pnt] && euc_pnt < euclen && sjis_pnt < sjislen) {
    if (euc[euc_pnt] & 0x80) {
      if (euc[euc_pnt] == 0x8e) { /* 半角カナ */
	euc_pnt++;
	sjis[sjis_pnt++] = euc[euc_pnt++];
      } else { /* 全角文字 */
	hi = euc[euc_pnt++] & 0x7f;
	lo = euc[euc_pnt++] & 0x7f;

	if ((hi & 1) == 0) {
	  lo += 0x5e;
	  hi--;
	}

	hi = ((hi - 0x21) >> 1) + 0x81;
	lo += (0x40 - 0x21);
	
	if (lo >= 0x7f)
	  lo++;
	if (hi >= 0xa0)
	  hi += 0x40;

	sjis[sjis_pnt++] = hi;
	sjis[sjis_pnt++] = lo;
      }
    } else { /* ANK */
      sjis[sjis_pnt++] = euc[euc_pnt++];
    }
  }

  if (sjis_pnt < sjislen)
    sjis[sjis_pnt] = '\0';
  else
    sjis[sjislen - 1] = '\0';

  return sjis_pnt;
}

int sjis2euc(uchar *sjis, int sjislen, uchar *euc, int euclen)
{
  int euc_pnt = 0, sjis_pnt = 0;
  int hi, lo;
  
  while (sjis[sjis_pnt] && sjis_pnt < sjislen && euc_pnt < euclen) {
    if (ISSJISKANJI1(sjis[sjis_pnt])) { /* 全角文字 */
      hi = sjis[sjis_pnt++] & 0xff;
      lo = sjis[sjis_pnt++] & 0xff;

      if (hi >= 0xe0)
	hi -= 0x40;
      if (lo >= 0x80)
	lo--;

      lo -= (0x40 - 0x21);
      hi = ((hi - 0x81) << 1) + 0x21;

      if (lo >= 0x7f) {
	lo -= 0x5e;
	hi++;
      }

      euc[euc_pnt++] = hi | 0x80;
      euc[euc_pnt++] = lo | 0x80;
    } else if (ISSJISKANA(sjis[sjis_pnt])) { /* 半角カナ */
      euc[euc_pnt++] = 0x8e;
      euc[euc_pnt++] = sjis[sjis_pnt++];
    } else {
      euc[euc_pnt++] = sjis[sjis_pnt++];
    }
  }

  if (euc_pnt < euclen)
    euc[euc_pnt] = '\0';
  else
    euc[euclen - 1] = '\0';

  return euc_pnt;
}

/*
 * signal setup...
 */ 

int m_setup_signal(signalhandler_t handler)
{
  signal(SIGINT, (void(*)())handler);
  signal(SIGTERM, (void(*)())handler);
  if (daemonizemode == 1)
    signal(SIGHUP, SIG_IGN);
  else
    signal(SIGHUP, (void(*)())handler);
  signal(SIGQUIT, SIG_IGN);
  signal(SIGPIPE, SIG_IGN);

  return 0;
}

/*
 * copy file...
 */

int m_copy_file_fp(FILE *from, FILE *to)
{
  char buf[1024];
  int i;
  
  do {
    i = fread(buf, 1, 1024, from);
    fwrite(buf, 1, i, to);
  } while (i == 1024);

  return 0;
}

int m_copy_file(char *from, char *to)
{
  FILE *fp1, *fp2;
  char buf[1024];
  int i;

  if ((fp1 = fopen(from, "r")) != NULL) {
    if ((fp2 = fopen(to, "w")) != NULL) {
      do {
	i = fread(buf, 1, 1024, fp1);
	fwrite(buf, 1, i, fp2);
      } while (i == 1024);

      fclose(fp2);
      fclose(fp1);

      return 0;
    }
    fclose(fp1);
  }

  return -1;
}

/*
 * system() と同等の関数
 */

static char m_system_opened_file[FOPEN_MAX];

int m_system_clear()
{
  memset(m_system_opened_file, 0, FOPEN_MAX);
  return 0;
}

int m_system_register_file(int fd)
{
  m_system_opened_file[fd] = 0xff;
  return 0;
}

int m_system(char *command)
{
	int i, status, pid;
	char *argv[4];

	if (command == NULL)
		return -1;

	pid = fork();

	if (pid == -1)
		return -1;

	if (pid == 0)
	{
		for (i = 0; i < FOPEN_MAX; i++)
			if (m_system_opened_file[i])
				close(m_system_opened_file[i]);

		argv[0] = "sh";
		argv[1] = "-c";
		argv[2] = command;
		argv[3] = NULL;

		execv("/bin/sh", argv);

		exit(127);
	}

	for (;;)
	{
		if (waitpid(pid, &status, 0) == -1)
		{
			if (errno != EINTR)
				return -1;
		} else
			return status;
	}
}

/*
 * read/write from/to socket functions...
 */

int m_socket_read(int fd, char *ptr, int totalsize)
{
  int size, ret;

  size = 0;
  while (size < totalsize) {
    if ((ret = read(fd, &ptr[size], totalsize - size)) <= 0)
      return -1;

    size += ret;
  }

  return 0;
}

int m_socket_write(int fd, char *ptr, int totalsize)
{
  int size, ret;

  size = 0;
  while (size < totalsize) {
    if ((ret = write(fd, &ptr[size], totalsize - size)) < 0)
      return -1;

    size += ret;
  }

  return 0;
}

/*
 * string modifier...
 */

int m_replace_string(uchar *eucbuf, char *pre, char *post)
{
  char *p;
  int slen, prelen, postlen;

  prelen = strlen(pre);
  postlen = strlen(post);
  
  while ((p = strstr(eucbuf, pre)) != NULL) {
    slen = strlen(p);

    memmove(p + postlen, p + prelen, slen - prelen + 1);
    memcpy(p, post, postlen);
  }

  return strlen(eucbuf);
}
    
int m_convert_zen2han(uchar *sjisbuf)
{
  int i = 0;
  
  if (sjisbuf[0] == 0x82) { /* Alphabet */
    if (0x60 <= sjisbuf[1] && sjisbuf[1] < 0x60 + 26) /* A-Z */
      return 'A' + sjisbuf[1] - 0x60;
    if (0x81 <= sjisbuf[1] && sjisbuf[1] < 0x81 + 26) /* a-z */
      return 'a' + sjisbuf[1] - 0x81;
  }
  if (sjisbuf[0] == 0x82 && 0x4f <= sjisbuf[1] && sjisbuf[1] <= 0x58) /* 0-9 */
    return '0' + sjisbuf[1] - 0x4f;

  if (sjisbuf[0] == 0x81) { /* きごう */
    while (zen_han_table[i].c) {
      if (zen_han_table[i].zen[1] == sjisbuf[1])
	return zen_han_table[i].c;
      i++;
    }
  }

  return 0;
}

static int m_lookup_kana_table(uchar kana, int mod)
{
  int i, j;

  j = -1; i = 0;
  while (kana_table[i].han_start) {
    if (kana_table[i].han_start <= kana && kana <= kana_table[i].han_end) {
      j = i;

      if (mod == 0 || (kana_table[i].modifiers & mod) != 0)
	break;
    }
    i++;
  }

  return j;
}
	
int m_convert_hankana2zenkana(uchar *euc, int len)
{
  int i, j;
  uchar mod, c;

  for (i = 0; i < len; i++) {
    mod = 0;
    
    if (euc[i] == 0x8e) { /* non-ascii の半角文字なら */
      if (euc[i + 1] == 0xde) { /* 今が ゛ なら */
	euc[i] = 0xa1;
	euc[i + 1] = 0xab;
	i++;
      } else if (euc[i + 1] == 0xdf) { /* 今が ゜ なら */
	euc[i] = 0xa1;
	euc[i + 1] = 0xac;
	i++;
      } else { /* 普通の半角カタカナなら */
	if (i + 3 < len) {
	  if (euc[i + 2] == 0x8e && euc[i + 3] == 0xde)  /* 次が ゛ なら */
	    mod = MOD_TEN;
	  if (euc[i + 2] == 0x8e && euc[i + 3] == 0xdf) /* 次が ゜ なら */
	    mod = MOD_MARU;
	}
	
	if ((j = m_lookup_kana_table(euc[i + 1], mod)) != -1) {
	  mod &= kana_table[j].modifiers; /* mod は「てんてんなし」からの
					     オフセットを示す */
	  c = kana_table[j].zenkaku2_start;
	  c += (euc[i + 1] - kana_table[j].han_start) * kana_table[j].offset;
	  c += mod;

	  euc[i] = 0xa5; euc[i + 1] = c;

	  if (mod) {
	    memmove(&(euc[i + 2]), &(euc[i + 4]), len - (i + 4) + 1);
	    len -= 2;
	  }
	  
	  i++;
	}
      }
    }
  }

  euc[len] = '\0';

  return len;
}

int m_convert_zenkana2zenhira(uchar *src, uchar *dst, int len)
{
  int i = 0;
  uchar euc[10];

  while (i < len) {
    dst[i] = 0x82;
    dst[i + 1] = src[i + 1] + (0x9f - 0x40);
    if (src[i + 1] >= 0x80)
      dst[i + 1] -= 1;
    
    i += 2;
  }

  sjis2euc(dst, len, euc, 10);
  
  return 0;
}

int m_exist_hankata(uchar *euc)
{
  uchar *p;

  p = euc;
  
  while (*p) {
    if (*p & 0x80) {
      if (*p == 0x8e)
	return 1;
      else
	p += 2;
    } else {
      p++;
    }
  }

  return 0;
}
  
int m_is_zenkata_string(uchar *euc)
{
  uchar *p;

  p = euc;

  while (*p) {
    if (*p == 0xa5)
      p += 2;
    else
      return 0;
  }

  return 1;
}

int m_is_hiragana_string(uchar *euc)
{
  uchar *p;
  int vu_flag = 0, hira_flag = 0;

  p = euc;

  while (*p) {
    if (*p == 0xa4) {
      hira_flag = 1;
      p += 2;
    } else if (*p == 0xa5 && *(p + 1) == 0xf4) { /* ヴ の場合 */
      vu_flag = 1;
      p += 2;
    } else
      return 0;
  }

  if (vu_flag && hira_flag == 0)
    return 0; /* ヴ だけの場合はカタカナと判断 */

  return 1;
}

int m_convert_zenhira2zenkata(uchar *euc_hira, int slen, uchar *euc_kata)
{
  int i, j;

  for (i = j = 0; i < slen;) {
    if (euc_hira[i] == 0xa4) {
      if (i + 4 <= slen && euc_hira[i + 1] == 0xa6 &&
	  euc_hira[i + 2] == 0xa1 && euc_hira[i + 3] == 0xab) {
	/* う゛ の場合 */
	euc_kata[j] = 0xa5;
	euc_kata[j + 1] = 0xf4;
	j += 2;
	i += 4;
      } else {
	euc_kata[j] = 0xa5;
	euc_kata[j + 1] = euc_hira[i + 1];
	j += 2;
	i += 2;
      }
    } else if (euc_hira[i] & 0x80) {
      euc_kata[j++] = euc_hira[i++];
      euc_kata[j++] = euc_hira[i++];
    } else {
      euc_kata[j++] = euc_hira[i++];
    }
  }

  euc_kata[j] = 0;

  return j;
}
      
/* reconvroma.c */

typedef struct {
  uchar roma[5];
  uchar hira[10];
} romatbl_t;

static romatbl_t romatbl[] = {
  "a", "あ",
  "i", "い",
  "u", "う",
  "e", "え",
  "o", "お",
  "ka", "か",
  "ki", "き",
  "ku", "く",
  "ke", "け",
  "ko", "こ",
  "ga", "が",
  "gi", "ぎ",
  "gu", "ぐ",
  "ge", "げ",
  "go", "ご",
  "sa", "さ",
  "si", "し",
  "su", "す",
  "se", "せ",
  "so", "そ",
  "za", "ざ",
  "zi", "じ",
  "zu", "ず",
  "ze", "ぜ",
  "zo", "ぞ",
  "ta", "た",
  "ti", "ち",
  "tu", "つ",
  "te", "て",
  "to", "と",
  "da", "だ",
  "di", "ぢ",
  "du", "づ",
  "de", "で",
  "do", "ど",
  "na", "な",
  "ni", "に",
  "nu", "ぬ",
  "ne", "ね",
  "no", "の",
  "nn", "ん",
  "ha", "は",
  "hi", "ひ",
  "fu", "ふ",
  "he", "へ",
  "ho", "ほ",
  "ba", "ば",
  "bi", "び",
  "bu", "ぶ",
  "be", "べ",
  "bo", "ぼ",
  "pa", "ぱ",
  "pi", "ぴ",
  "pu", "ぷ",
  "pe", "ぺ",
  "po", "ぽ",
  "ma", "ま",
  "mi", "み",
  "mu", "む",
  "me", "め",
  "mo", "も",
  "ya", "や",
  "yu", "ゆ",
  "yo", "よ",
  "ra", "ら",
  "ri", "り",
  "ru", "る",
  "re", "れ",
  "ro", "ろ",
  "wa", "わ",
  "wi", "ゐ",
  "we", "ゑ",
  "wo", "を",
  "xa", "ぁ",
  "xi", "ぃ",
  "xu", "ぅ",
  "xe", "ぇ",
  "xo", "ぉ",
  "xwa", "ゎ",
  "xtu", "っ",
  "xya", "ゃ",
  "xyu", "ゅ",
  "xyo", "ょ",
};

static char *glyph =
" ,.,./:;?!  '` ^~_         -- /\\  |   ' \"()  []{}<>  []{}  +-   = <>       '\" \\$  %#&*@       ";

#define romatbl_num (sizeof(romatbl) / sizeof(romatbl_t))

static int reconvroma_lookup(uchar *hira)
{
  int i;

  for (i = 0; i < romatbl_num; i++) {
    if (hira[1] == romatbl[i].hira[1])
      return i;
  }

  return -1;
}

static uchar *reconvroma_katakana2hiragana(uchar *src)
{
  int slen, i, j;
  uchar *p;
  static buffer_t zbuf;
  
  slen = strlen(src);
  buffer_check(&zbuf, slen * 2);
  p = zbuf.buf;
  
  if (p) {
    for (i = j = 0; i < slen;) {
      if (src[i] == 0xa5) { /* カタカナの場合 */
	switch (src[i + 1]) {
	  case 0xf4: /* ヴ */
	    memcpy(&(p[j]), "う゛", 4);
	    j += 4;
	    break;
	  case 0xf5:
	  case 0xf6:
	    memcpy(&(p[j]), "か", 2);
	    j += 2;
	    break;
	  default:
	    p[j++] = 0xa4;
	    p[j++] = src[i + 1];
	}

	i += 2;
      } else if (src[i] & 0x80) {
	p[j++] = src[i++];
	p[j++] = src[i++];
      } else {
	p[j++] = src[i++];
      }
    }

    p[j] = 0;
  }

  return p;
}

/*
 * ひらがな、カタカナ、アルファベット、数字から構成される読みを、
 * ローマ字に変換する。
 * アルファベットに関しては、0x80 を OR する。
 */

int m_reconvroma(uchar *src, uchar *dst)
{
  int src_pnt, i, srclen;
  uchar *p, buf[2];

  p = reconvroma_katakana2hiragana(src); /* カタカナをひらがなに変換 */
  srclen = strlen(p);
  dst[0] = '\0';
  src_pnt = 0;

  while (p[src_pnt]) {
    switch (p[src_pnt]) {
      case 0xa4: /* ひらがな */
	if (src_pnt + 4 <= srclen && p[src_pnt + 1] == 0xa6 &&
	    p[src_pnt + 2] == 0xa1 && p[src_pnt + 3] == 0xab) {
	  /* 「う゛」のばあい */
	  strcat(dst, "vu");
	  src_pnt += 4;
	} else {
	  if ((i = reconvroma_lookup(&(p[src_pnt]))) != -1) {
	    strcat(dst, romatbl[i].roma);
	    src_pnt += 2;
	  } else {
	    fprintf(stderr, "UNEXPECTED YOMI %d:%s\n", src_pnt, &(p[src_pnt]));
	    free(p);
	    return -1;
	  }
	}
	break;
      case 0xa1: /* 記号 */
	if (glyph[p[src_pnt + 1] - 0xa1] != ' ') {
	  buf[0] = glyph[p[src_pnt + 1] - 0xa1];
	  buf[1] = 0;
	  strcat(dst, buf);
	  src_pnt += 2;
	} else {
	  fprintf(stderr, "UNEXPECTED GLYPH:%d:%s\n", src_pnt, &(p[src_pnt]));
	  free(p);
	  return -1;
	}
	break;
      case 0xa3: /* 数字,アルファベット */
	if (0xb0 <= p[src_pnt + 1] && p[src_pnt + 1] <= 0xb9) {
	  buf[0] = '0' + (p[src_pnt + 1] - 0xb0);
	  buf[1] = 0;
	  strcat(dst, buf);
	  src_pnt += 2;
	} else if (0xc1 <= p[src_pnt + 1] && p[src_pnt + 1] <= 0xda) {
	  buf[0] = ('A' + (p[src_pnt + 1] - 0xc1)) | 0x80;
	  buf[1] = 0;
	  strcat(dst, buf);
	  src_pnt += 2;
	} else if (0xe1 <= p[src_pnt + 1] && p[src_pnt + 1] <= 0xfa) {
	  buf[0] = ('a' + (p[src_pnt + 1] - 0xe1)) | 0x80;
	  buf[1] = 0;
	  strcat(dst, buf);
	  src_pnt += 2;
	} else {
	  fprintf(stderr, "UNEXPECTED ALNUM:%d:%s\n", src_pnt, &(p[src_pnt]));
	  free(p);
	  return -1;
	}
	break;
      default:
	if ((p[src_pnt] & 0x80) == 0) {
	  buf[0] = p[src_pnt] | 0x80;
	  buf[1] = 0;
	  strcat(dst, buf);
	  src_pnt++;
	} else {
	  fprintf(stderr, "UNEXPECTED:%d:%s\n", src_pnt, &(p[src_pnt]));
	  free(p);
	  return -1;
	}
    }
  }

  return 0;
}
	
  
