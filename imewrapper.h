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

#ifndef __imewrapper_h__
#define __imewrapper_h__

int imewrapper_end_client(int id);
int imewrapper_end_rootclient(int id);

int imewrapper_ime_aborted(int ime);

int imewrapper_clear_client_data(int id);

int  imewrapper_initialize();
int  imewrapper_finalize();
int  imewrapper_create_context();
int  imewrapper_duplicate_context();
int  imewrapper_close_context();
int  imewrapper_get_dictionary_list();
int  imewrapper_get_directory_list();
int  imewrapper_mount_dictionary();
int  imewrapper_unmount_dictionary();
int  imewrapper_remount_dictionary();
int  imewrapper_get_mountdictionary_list();
int  imewrapper_query_dictionary();
int  imewrapper_define_word();
int  imewrapper_delete_word();
int  imewrapper_begin_convert();
int  imewrapper_end_convert();
int  imewrapper_get_candidacy_list();
int  imewrapper_get_yomi();
int  imewrapper_subst_yomi();
int  imewrapper_store_yomi();
int  imewrapper_store_range();
int  imewrapper_get_lastyomi();
int  imewrapper_flush_yomi();
int  imewrapper_remove_yomi();
int  imewrapper_get_simplekanji();
int  imewrapper_resize_pause();
int  imewrapper_get_hinshi();
int  imewrapper_get_lex();
int  imewrapper_get_status();
int  imewrapper_set_locale();
int  imewrapper_auto_convert();
int  imewrapper_query_extensions();
int  imewrapper_set_applicationname();
int  imewrapper_notice_groupname();
int  imewrapper_through();
int  imewrapper_kill_server();

int  imewrapper_get_serverinfo();
int  imewrapper_get_access_control_list();
int  imewrapper_create_dictionary();
int  imewrapper_delete_dictionary();
int  imewrapper_rename_dictionary();
int  imewrapper_get_wordtext_dictionary();
int  imewrapper_list_dictionary();
int  imewrapper_sync();
int  imewrapper_chmod_dictionary();
int  imewrapper_copy_dictionary();



#endif
