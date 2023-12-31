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

#ifndef __const_def__
#define __const_def__

#define ESECANNA_VERSION "Canna2IMM32 1.0.1"
#define CANNA_UNIX_DOMAIN_DIR "/tmp/.iroha_unix"
#define CANNA_UNIX_DOMAIN_PATH "/tmp/.iroha_unix/IROHA"
#define CANNA_SERVICE_NAME "canna"
#define CANNA_DEFAULT_PORT 5680

#define ESECANNA_PID_PATH PIDPATH "/canna2imm32.pid"
#define ESECANNA_LOG_PATH LOGPATH "/canna2imm32.log"
#define ESECANNA_RC_PATH RCPATH "/canna2imm32rc"

#define ESECANNA_DL_PATH DLLPATH

#define MAX_CLIENT_NUM 16
#define USER_CLIENT 3
#define IMM32_ROOT_CLIENT 0

#ifndef TRUE

#define TRUE 1
#define FALSE 0

#endif

#endif
