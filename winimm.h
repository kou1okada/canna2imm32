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

#ifndef __winimm32_h__
#define __winimm32_h__

#define ESECANNA_MODULE_VERSION "Imm32 1.0.0"

#include <windows.h>
#include <windowsx.h>
#include <imm.h>
/* #include <mbstring.h> */

/* proto */
int imm32wrapper_finalize(int id, buffer_t *cbuf);
int imm32wrapper_create_context(int id, buffer_t *cbuf);
int imm32wrapper_duplicate_context(int id, buffer_t *cbuf);
int imm32wrapper_close_context(int id, buffer_t *cbuf);
int imm32wrapper_define_word(int id, buffer_t *cbuf);
int imm32wrapper_delete_word(int id, buffer_t *cbuf);
int imm32wrapper_begin_convert(int id, buffer_t *cbuf);
int imm32wrapper_end_convert(int id, buffer_t *cbuf);
int imm32wrapper_get_candidacy_list(int id, buffer_t *cbuf);
int imm32wrapper_get_yomi(int id, buffer_t *cbuf);
int imm32wrapper_subst_yomi(int id, buffer_t *cbuf);
int imm32wrapper_store_yomi(int id, buffer_t *cbuf);
int imm32wrapper_store_range(int id, buffer_t *cbuf);
int imm32wrapper_get_lastyomi(int id, buffer_t *cbuf);
int imm32wrapper_flush_yomi(int id, buffer_t *cbuf);
int imm32wrapper_remove_yomi(int id, buffer_t *cbuf);
int imm32wrapper_get_simplekanji(int id, buffer_t *cbuf);
int imm32wrapper_resize_pause(int id, buffer_t *cbuf);
int imm32wrapper_get_hinshi(int id, buffer_t *cbuf);
int imm32wrapper_get_lex(int id, buffer_t *cbuf);
int imm32wrapper_get_status(int id, buffer_t *cbuf);
int imm32wrapper_set_locale(int id, buffer_t *cbuf);
int imm32wrapper_auto_convert(int id, buffer_t *cbuf);
int imm32wrapper_initialize(int id, char *conffile);
int imm32wrapper_init_rootclient();
int imm32wrapper_end_client(int id);
int imm32wrapper_end_rootclient();
int imm32wrapper_clear_client_data(int id);

/* >> メッセージ化対応 */
#define WM_CANNA_FINALIZE WM_USER+0
#define WM_CANNA_CREATE_CONTEXT WM_USER+1
#define WM_CANNA_DUPLICATE_CONTEXT WM_USER+2
#define WM_CANNA_CLOSE_CONTEXT WM_USER+3
#define WM_CANNA_DEFINE_WORD WM_USER+4
#define WM_CANNA_DELETE_WORD WM_USER+5
#define WM_CANNA_BEGIN_CONVERT WM_USER+6
#define WM_CANNA_END_CONVERT WM_USER+7
#define WM_CANNA_GET_CANDIDACY_LIST WM_USER+8
#define WM_CANNA_GET_YOMI WM_USER+9
#define WM_CANNA_SUBST_YOMI WM_USER+10
#define WM_CANNA_STORE_YOMI WM_USER+11
#define WM_CANNA_STORE_RANGE WM_USER+12
#define WM_CANNA_GET_LASTYOMI WM_USER+13
#define WM_CANNA_FLUSH_YOMI WM_USER+14
#define WM_CANNA_REMOVE_YOMI WM_USER+15
#define WM_CANNA_GET_SIMPLEKANJI WM_USER+16
#define WM_CANNA_RESIZE_PAUSE WM_USER+17
#define WM_CANNA_GET_HINSHI WM_USER+18
#define WM_CANNA_GET_LEX WM_USER+19
#define WM_CANNA_GET_STATUS WM_USER+20
#define WM_CANNA_SET_LOCALE WM_USER+21
#define WM_CANNA_AUTO_CONVERT WM_USER+22
#define WM_CANNA_INITIALIZE WM_USER+23
#define WM_CANNA_INIT_ROOTCLIENT WM_USER+24
#define WM_CANNA_END_CLIENT WM_USER+25
#define WM_CANNA_END_ROOTCLIENT WM_USER+26
#define WM_CANNA_CLEAR_CLIENT_DATA WM_USER+27
/* << メッセージ化対応 */


#endif /* __winimm32_h__ */
