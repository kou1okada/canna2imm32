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

#include "def.h"
#include "misc.h"
#include "imewrapper.h"
#include "cannaproto.h"

typedef int (*intfunc_t)();

intfunc_t wrapperfunc[] = {
  imewrapper_initialize,
  imewrapper_finalize,
  imewrapper_create_context,
  imewrapper_duplicate_context,
  imewrapper_close_context,
  imewrapper_get_dictionary_list,
  imewrapper_get_directory_list,
  imewrapper_mount_dictionary,
  imewrapper_unmount_dictionary,
  imewrapper_remount_dictionary,
  imewrapper_get_mountdictionary_list,
  imewrapper_query_dictionary,
  imewrapper_define_word,
  imewrapper_delete_word,
  imewrapper_begin_convert,
  imewrapper_end_convert,
  imewrapper_get_candidacy_list,
  imewrapper_get_yomi,
  imewrapper_subst_yomi,
  imewrapper_store_yomi,
  imewrapper_store_range,
  imewrapper_get_lastyomi,
  imewrapper_flush_yomi,
  imewrapper_remove_yomi,
  imewrapper_get_simplekanji,
  imewrapper_resize_pause,
  imewrapper_get_hinshi,
  imewrapper_get_lex,
  imewrapper_get_status,
  imewrapper_set_locale,
  imewrapper_auto_convert,
  imewrapper_query_extensions,
  imewrapper_set_applicationname,
  imewrapper_notice_groupname,
  imewrapper_through,
  imewrapper_kill_server
};

intfunc_t wrapperfunc_e[] = {
  imewrapper_get_serverinfo,
  imewrapper_get_access_control_list,
  imewrapper_create_dictionary,
  imewrapper_delete_dictionary,
  imewrapper_rename_dictionary,
  imewrapper_get_wordtext_dictionary,
  imewrapper_list_dictionary,
  imewrapper_sync,
  imewrapper_chmod_dictionary,
  imewrapper_copy_dictionary
};

static buffer_t packetbuf;

char *protocol_name[] = {
  "Initialize",
  "Finalize",
  "CreateContext",
  "DupricateContext",
  "CloseContext",
  "GetDictionaryList",
  "GetDirectoryList",
  "MountDictionary",
  "UnmountDictionary",
  "RemountDictionary",
  "GetMountDictionaryList",
  "QueryDictionary",
  "DefineWord",
  "DeleteWord",
  "BeginConvert",
  "EndConvert",
  "GetCandidacyList",
  "GetYomi",
  "SubstYomi",
  "StoreYomi",
  "StoreRange",
  "GetLastYomi",
  "FlushYomi",
  "RemoveYomi",
  "GetSimpleKanji",
  "ResizePause",
  "GetHinshi",
  "GetLex",
  "GetStatus",
  "SetLocale",
  "AutoConvert",
  "QueryExtensions",
  "SetApplicationName",
  "NoticeGroupName",
  "Through",
  "KillServer",
  NULL
};

char *e_protocol_name[] = {
  "GetServerInfo",
  "GetAccessControlList",
  "CreateDictionary",
  "DeleteDictionary",
  "RenameDictionary",
  "GetWordTextDictionary",
  "ListDictionary",
  "Sync",
  "ChmodDictionary",
  "CopyDictionary",
  NULL
};

extern client_t client[];

int canna_proto_recv_request(int id)
{
  int datalen, extflag, type;
  cannaheader_t *header;
  
  buffer_check(&packetbuf, 24);

  if (m_socket_read(client[id].sockfd, packetbuf.buf, 4) < 0) {
    /* read エラーはクライアントが勝手に落ちたと判断する */
    return -1;
  }
  
  header = (cannaheader_t *)(packetbuf.buf);
  
  if (header->type == 0x00) { /* Initialize */
    if (m_socket_read(client[id].sockfd, (char *)(&datalen), 4) < 0)
      return -1;
    datalen = LSBMSB32(datalen);

    buffer_check(&packetbuf, datalen);
    
    if (m_socket_read(client[id].sockfd, packetbuf.buf, datalen) < 0)
      return -1;

    return 0x01;
  } else {
    datalen = LSBMSB16(header->datalen);
    extflag = header->extra ? 0x1000 : 0x0000;
    type = header->type;

    if (datalen > 0) {
      buffer_check(&packetbuf, datalen + 4);
      
      if (m_socket_read(client[id].sockfd, &(packetbuf.buf[4]), datalen) < 0)
	return -1;
    }
    
    return (type | extflag);
  }
}

int canna_proto_send_request(int id)
{
  int datalen;
  cannaheader_t *header;
  
  header = (cannaheader_t *)(packetbuf.buf);

  datalen = LSBMSB16(header->datalen) + 4;
  
  if (m_socket_write(client[id].sockfd, packetbuf.buf, datalen) < 0)
    return -1;

  return 0;
}

int canna_proto_main(int id)
{
  int type, ret;
  int (*callfunc)();
  char *reqname;

  if ((type = canna_proto_recv_request(id)) <= 0) {
    client[id].need_terminate = TRUE; /* main.c で終了処理をしてもらう */
    return 0;
  }

  if ((0x01 <= type && type <= 0x24) || (0x1001 <= type && type <= 0x100a)) {
    if (type & 0x1000) {
      reqname = e_protocol_name[(type & 0xff) - 1];
      callfunc = wrapperfunc_e[(type & 0xff) - 1];
    } else {
      reqname = protocol_name[type - 1];
      callfunc = wrapperfunc[type - 1];
    }

    m_msg_dbg("REQUEST %s called by %s@%s\n", reqname, client[id].user,
	      client[id].host);
    
    ret = (*callfunc)(id, &packetbuf);

    m_msg_dbg("Returned from request processing.\n");
  } else {
    /* ヘッダが変。main.c で終了処理をしてもらう。リクエストはオウム返し */
    client[id].need_terminate = TRUE;
    ret = 1;
  }
    
  /* ret..1 成功 ret..0 未実装 ret..-1 IME 落ちた */

  if (type == 0x01)
    write(client[id].sockfd, packetbuf.buf, 4);
  else
    canna_proto_send_request(id);

  if (ret == 0)
    m_msg_dbg("Request %x is under construction.\n", type);
  else if (ret == -1) {
    m_msg("IME terminated.\n");

    /* 再起動処理をする。失敗した場合は、-ime が返り、main.c で
     * 全てのクライアントの終了処理をするため、-ime を返す
     */
    
    return imewrapper_ime_aborted(client[id].ime);
  }
  
  return 0;
}
