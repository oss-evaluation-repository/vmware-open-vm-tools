/*********************************************************
 * Copyright (C) 1998 VMware, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation version 2.1 and no later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the Lesser GNU General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA.
 *
 *********************************************************/


/*
 * guestApp.c --
 *
 *    Utility functions common to all guest applications
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "vmware.h"
#include "vm_version.h"
#include "vm_tools_version.h"
#include "guestApp.h"
#include "backdoor.h"
#include "backdoor_def.h"
#include "conf.h"
#include "rpcout.h"
#include "debug.h"
#include "strutil.h"
#include "str.h"
#include "msg.h"
#include "file.h"
#include "posix.h"
#include "vmware/guestrpc/tclodefs.h"

#ifdef _MSC_VER
#include <windows.h>
#include <shlobj.h>
#include "productState.h"
#include "winregistry.h"
#include "win32util.h"
#endif

/*
 * For Netware/Linux/BSD/Solaris, the install path
 * is the hardcoded value below. For Windows, it is
 * determined dynamically in GuestApp_GetInstallPath(),
 * so the empty string here is just for completeness.
 */

#if defined _WIN32
#   define GUESTAPP_TOOLS_INSTALL_PATH ""
#elif defined __APPLE__
#   define GUESTAPP_TOOLS_INSTALL_PATH "/Library/Application Support/VMware Tools"
#else
#   define GUESTAPP_TOOLS_INSTALL_PATH "/etc/vmware-tools"
#endif


/*
 *-----------------------------------------------------------------------------
 *
 * GuestApp_OldGetOptions --
 *
 *    Retrieve the tools options from VMware using the old (deprecated) method.
 *
 * Return value:
 *    The tools options
 *
 * Side effects:
 *    None
 *
 *-----------------------------------------------------------------------------
 */

uint32
GuestApp_OldGetOptions(void)
{
   Backdoor_proto bp;

   Debug("Retrieving tools options (old)\n");

   bp.in.cx.halfs.low = BDOOR_CMD_GETGUIOPTIONS;
   Backdoor(&bp);
   return bp.out.ax.word;
}


/*
 *----------------------------------------------------------------------
 *
 * GuestApp_SetOptionInVMX --
 *
 *      Send an option's value to VMware.
 *      NOTE: vmware should have unified loop capability to accept
 *            this option.
 *
 * Results:
 *      TRUE:  success
 *      FALSE: failure to due an RpcOut error or an invalid
 *             currentVal
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

Bool
GuestApp_SetOptionInVMX(const char *option,     // IN
                        const char *currentVal, // IN
                        const char *newVal)     // IN
{
   return RpcOut_sendOne(NULL, NULL, "vmx.set_option %s %s %s",
                         option, currentVal, newVal);
}


/*
 *-----------------------------------------------------------------------------
 *
 * GuestApp_GetDefaultScript --
 *
 *    Returns the default power script for the given configuration option.
 *
 * Results:
 *    Script name on success, NULL of the option is not recognized.
 *
 * Side effects:
 *    None.
 *
 *-----------------------------------------------------------------------------
 */

const char *
GuestApp_GetDefaultScript(const char *confName) // IN
{
   const char *value = NULL;
   if (strcmp(confName, CONFNAME_SUSPENDSCRIPT) == 0) {
      value = CONFVAL_SUSPENDSCRIPT_DEFAULT;
   } else if (strcmp(confName, CONFNAME_RESUMESCRIPT) == 0) {
      value = CONFVAL_RESUMESCRIPT_DEFAULT;
   } else if (strcmp(confName, CONFNAME_POWEROFFSCRIPT) == 0) {
      value = CONFVAL_POWEROFFSCRIPT_DEFAULT;
   } else if (strcmp(confName, CONFNAME_POWERONSCRIPT) == 0) {
      value = CONFVAL_POWERONSCRIPT_DEFAULT;
   }
   return value;
}

#ifdef _WIN32

/*
 *------------------------------------------------------------------------------
 *
 * GuestApp_GetInstallPathW --
 *
 *    Returns the tools installation path as a UTF-16 encoded string, or NULL on
 *    error. The caller must deallocate the returned string using free.
 *
 * Results:
 *    See above.
 *
 * Side effects:
 *    None.
 *------------------------------------------------------------------------------
 */

LPWSTR
GuestApp_GetInstallPathW(void)
{
   static LPCWSTR TOOLS_KEY_NAME = L"Software\\VMware, Inc.\\VMware Tools";
   static LPCWSTR INSTALLPATH_VALUE_NAME = L"InstallPath";

   HKEY   key    = NULL;
   LONG   rc     = ERROR_SUCCESS;
   DWORD  cbData = 0;
   DWORD  temp   = 0;
   PWCHAR data   = NULL;

   rc = RegOpenKeyExW(HKEY_LOCAL_MACHINE, TOOLS_KEY_NAME, 0, KEY_READ, &key);
   if (ERROR_SUCCESS != rc) {
      Debug("%s: Couldn't open key \"%S\".\n", __FUNCTION__, TOOLS_KEY_NAME);
      Debug("%s: RegOpenKeyExW error 0x%x.\n", __FUNCTION__, GetLastError());
      goto exit;
   }

   rc = RegQueryValueExW(key, INSTALLPATH_VALUE_NAME, 0, NULL, NULL, &cbData);
   if (ERROR_SUCCESS != rc) {
      Debug("%s: Couldn't get length of value \"%S\".\n", __FUNCTION__,
            INSTALLPATH_VALUE_NAME);
      Debug("%s: RegQueryValueExW error 0x%x.\n", __FUNCTION__, GetLastError());
      goto exit;
   }

   /*
    * The data in the registry may not be null terminated. So allocate enough
    * space for one extra WCHAR and use that space to write our own NULL.
    */
   data = (LPWSTR) malloc(cbData + sizeof(WCHAR));
   if (NULL == data) {
      Debug("%s: Couldn't allocate %d bytes.\n", __FUNCTION__, cbData);
      goto exit;
   }

   temp = cbData;
   rc = RegQueryValueExW(key, INSTALLPATH_VALUE_NAME, 0, NULL, (LPBYTE) data,
                         &temp);
   if (ERROR_SUCCESS != rc) {
      Debug("%s: Couldn't get data for value \"%S\".\n", __FUNCTION__,
            INSTALLPATH_VALUE_NAME);
      Debug("%s: RegQueryValueExW error 0x%x.\n", __FUNCTION__, GetLastError());
      goto exit;
   }

   data[cbData / sizeof(WCHAR)] = L'\0';

exit:
   if (NULL != key) {
      RegCloseKey(key);
   }

   return data;
}

#endif

/*
 *----------------------------------------------------------------------
 *
 * GuestApp_GetInstallPath --
 *
 *      Get the tools installation path. The caller is responsible for
 *      freeing the memory allocated for the path.
 *
 * Results:
 *      The path in UTF-8 if successful.
 *      NULL otherwise.
 *
 * Side effects:
 *      Allocates memory.
 *
 *----------------------------------------------------------------------
 */

char *
GuestApp_GetInstallPath(void)
{
   char *pathUtf8 = NULL;

#if defined(_WIN32)
   size_t pathLen = 0;

   if (WinReg_GetSZ(HKEY_LOCAL_MACHINE,
                    CONF_VMWARE_TOOLS_REGKEY,
                    "InstallPath",
                    &pathUtf8) != ERROR_SUCCESS) {
      Warning("%s: Unable to retrieve install path: %s\n",
               __FUNCTION__, Msg_ErrString());
      return NULL;
   }

   /* Strip off the trailing backslash, if present */

   pathLen = strlen(pathUtf8);
   if (pathLen > 0) {
      if (pathUtf8[pathLen - 1] == '\\') {
         pathUtf8[pathLen - 1] = '\0';
      }
   }

#else
   pathUtf8 = Str_Asprintf(NULL, "%s", GUESTAPP_TOOLS_INSTALL_PATH);
#endif

   return pathUtf8;
}


/*
 *----------------------------------------------------------------------
 *
 * GuestApp_GetConfPath --
 *
 *      Get the path to the Tools configuration file.
 *
 *      The return conf path is a dynamically allocated UTF-8 encoded
 *      string that should be freed by the caller.
 *
 *      However, the function will also return NULL if we fail to create
 *      a "VMware/VMware Tools" directory. This can occur if we're not running
 *      as Administrator, which VMwareUser doesn't. But I believe that
 *      VMwareService will always come up before VMwareUser, so by the time
 *      a non-root user process calls this function, the directory exists.
 *
 * Results:
 *      The path in UTF-8, or NULL on failure.
 *
 * Side effects:
 *      Allocates memory.
 *
 *----------------------------------------------------------------------
 */

char *
GuestApp_GetConfPath(void)
{
#if defined(_WIN32)
   char *path = W32Util_GetVmwareCommonAppDataFilePath(NULL);

   if (path != NULL) {
      char *tmp = Str_SafeAsprintf(NULL, "%s%c%s", path, DIRSEPC,
                                   ProductState_GetName());
      free(path);
      path = tmp;

      if (!File_EnsureDirectory(path)) {
         free(path);
         path = NULL;
      }
   }

   return path;
#else
    /* Just call into GuestApp_GetInstallPath. */
   return GuestApp_GetInstallPath();
#endif
}


/*
 *----------------------------------------------------------------------------
 *
 * GuestApp_IsMouseAbsolute
 *
 *    Are the host/guest capable of using absolute mouse mode?
 *
 * Results:
 *    TRUE if host is in absolute mouse mode, FALSE otherwise.
 *
 * Side effects:
 *    Issues Tools RPC.
 *
 *----------------------------------------------------------------------------
 */

GuestAppAbsoluteMouseState
GuestApp_GetAbsoluteMouseState(void)
{
   Backdoor_proto bp;
   GuestAppAbsoluteMouseState state = GUESTAPP_ABSMOUSE_UNKNOWN;

   bp.in.cx.halfs.low = BDOOR_CMD_ISMOUSEABSOLUTE;
   Backdoor(&bp);
   if (bp.out.ax.word == 0) {
      state = GUESTAPP_ABSMOUSE_UNAVAILABLE;
   } else if (bp.out.ax.word == 1) {
      state = GUESTAPP_ABSMOUSE_AVAILABLE;
   }

   return state;
}


/*
 *-----------------------------------------------------------------------------
 *
 * GuestApp_IsDiskShrinkCapable --
 *
 *      Is the host capable of doing disk shrinks?
 *
 * Results:
 *      TRUE if the host is capable of disk shrink operations
 *      FALSE if the host is not capable of disk shrink operations
 * Side effects:
 *	None
 *
 *-----------------------------------------------------------------------------
 */

Bool
GuestApp_IsDiskShrinkCapable(void)
{
   return RpcOut_sendOne(NULL, NULL, "disk.wiper.enable");
}


/*
 *-----------------------------------------------------------------------------
 *
 * GuestApp_IsDiskShrinkEnabled --
 *
 *      Is disk shrinking enabled
 *
 * Results:
 *      TRUE if disk shrinking is enabled
 *      FALSE if disk shrinking is not enabled
 * Side effects:
 *	None
 *
 *-----------------------------------------------------------------------------
 */

Bool
GuestApp_IsDiskShrinkEnabled(void) {
   char *result;
   size_t resultLen;
   Bool enabled = FALSE;
   if (RpcOut_sendOne(&result, &resultLen, "disk.wiper.enable")) {
      if (resultLen == 1 && strcmp(result, "1") == 0) {
         enabled = TRUE;
      } else {
         enabled = FALSE;
      }
   }
   free(result);
   return enabled;
}


/*
 *-----------------------------------------------------------------------------
 *
 * GuestApp_GetPos --
 *
 *      Retrieve the host notion of the guest pointer location. --hpreg
 *
 * Results:
 *      '*x' and '*y' are the coordinates (top left corner is 0, 0) of the
 *      host notion of the guest pointer location. (-100, -100) means that the
 *      mouse is not grabbed on the host.
 *
 * Side effects:
 *	None
 *
 *-----------------------------------------------------------------------------
 */

void
GuestApp_GetPos(int16 *x, // OUT
                int16 *y) // OUT
{
   Backdoor_proto bp;

   bp.in.cx.halfs.low = BDOOR_CMD_GETPTRLOCATION;
   Backdoor(&bp);
   *x = bp.out.ax.word >> 16;
   *y = bp.out.ax.word;
}


/*
 *-----------------------------------------------------------------------------
 *
 * GuestApp_SetPos --
 *
 *      Update the host notion of the guest pointer location. 'x' and 'y' are
 *      the coordinates (top left corner is 0, 0). --hpreg
 *
 * Results:
 *      None
 *
 * Side effects:
 *	None
 *
 *-----------------------------------------------------------------------------
 */

void
GuestApp_SetPos(uint16 x, // IN
                uint16 y) // IN
{
   Backdoor_proto bp;

   bp.in.cx.halfs.low = BDOOR_CMD_SETPTRLOCATION;
   bp.in.size = (x << 16) | y;
   Backdoor(&bp);
}


/*
 * XXX The 5 functions below should be re-implemented using the message layer,
 *     to benefit from:
 *     . The high-bandwidth backdoor or the generic "send 4 bytes at a time"
 *       logic of the low-bandwidth backdoor
 *     . The restore/resume detection logic
 *     --hpreg
 */

/*
 *-----------------------------------------------------------------------------
 *
 * GuestApp_GetHostSelectionLen --
 *
 *      Retrieve the length of the clipboard (if any) to receive from the
 *      VMX. --hpreg
 *
 * Results:
 *      Length >= 0 if a clipboard must be retrieved from the host.
 *      < 0 on error (VMWARE_DONT_EXCHANGE_SELECTIONS or
 *                    VMWARE_SELECTION_NOT_READY currently)
 *
 * Side effects:
 *	None
 *
 *-----------------------------------------------------------------------------
 */

int32
GuestApp_GetHostSelectionLen(void)
{
   Backdoor_proto bp;

   bp.in.cx.halfs.low = BDOOR_CMD_GETSELLENGTH;
   Backdoor(&bp);
   return bp.out.ax.word;
}


/*
 *-----------------------------------------------------------------------------
 *
 * GuestAppGetNextPiece --
 *
 *      Retrieve the next 4 bytes of the host clipboard. --hpreg
 *
 * Results:
 *      The next 4 bytes of the host clipboard.
 *
 * Side effects:
 *	None
 *
 *-----------------------------------------------------------------------------
 */

static uint32
GuestAppGetNextPiece(void)
{
   Backdoor_proto bp;

   bp.in.cx.halfs.low = BDOOR_CMD_GETNEXTPIECE;
   Backdoor(&bp);
   return bp.out.ax.word;
}


/*
 *-----------------------------------------------------------------------------
 *
 * GuestApp_GetHostSelection --
 *
 *      Retrieve the host clipboard. 'data' must be a buffer whose size is at
 *      least (('size' + 4 - 1) / 4) * 4 bytes. --hpreg
 *
 * Results:
 *      The host clipboard in 'data'.
 *
 * Side effects:
 *	None
 *
 *-----------------------------------------------------------------------------
 */

void
GuestApp_GetHostSelection(unsigned int size, // IN
                          char *data)        // OUT
{
   uint32 *current;
   uint32 const *end;

   current = (uint32 *)data;
   end = current + (size + sizeof *current - 1) / sizeof *current;
   for (; current < end; current++) {
      *current = GuestAppGetNextPiece();
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * GuestApp_SetSelLength --
 *
 *      Tell the VMX about the length of the clipboard we are about to send
 *      to it. --hpreg
 *
 * Results:
 *      None
 *
 * Side effects:
 *	None
 *
 *-----------------------------------------------------------------------------
 */

void
GuestApp_SetSelLength(uint32 length) // IN
{
   Backdoor_proto bp;

   bp.in.cx.halfs.low = BDOOR_CMD_SETSELLENGTH;
   bp.in.size = length;
   Backdoor(&bp);
}


/*
 *-----------------------------------------------------------------------------
 *
 * GuestApp_SetNextPiece --
 *
 *      Send the next 4 bytes of the guest clipboard. --hpreg
 *
 * Results:
 *      None
 *
 * Side effects:
 *	None
 *
 *-----------------------------------------------------------------------------
 */

void
GuestApp_SetNextPiece(uint32 data) // IN
{
   Backdoor_proto bp;

   bp.in.cx.halfs.low = BDOOR_CMD_SETNEXTPIECE;
   bp.in.size = data;
   Backdoor(&bp);
}


/*
 *-----------------------------------------------------------------------------
 *
 * GuestApp_SetDeviceState --
 *
 *      Ask the VMX to change the connected state of a device. --hpreg
 *
 * Results:
 *      TRUE on success
 *      FALSE on failure
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

Bool
GuestApp_SetDeviceState(uint16 id,      // IN: Device ID
                        Bool connected) // IN
{
   Backdoor_proto bp;

   bp.in.cx.halfs.low = BDOOR_CMD_TOGGLEDEVICE;
   bp.in.size = (connected ? 0x80000000 : 0) | id;
   Backdoor(&bp);
   return bp.out.ax.word ? TRUE : FALSE;
}


/*
 * XXX The 2 functions below should be re-implemented using the message layer,
 *     to benefit from the high-bandwidth backdoor or the generic "send 4
 *     bytes at a time" logic. --hpreg
 */

/*
 *-----------------------------------------------------------------------------
 *
 * GuestAppGetDeviceListElement --
 *
 *      Retrieve 4 bytes of of information about a removable device. --hpreg
 *
 * Results:
 *      TRUE on success. '*data' is set
 *      FALSE on failure
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

static Bool
GuestAppGetDeviceListElement(uint16 id,     // IN : Device ID
                             uint16 offset, // IN : Offset in the RD_Info
                                            //      structure
                             uint32 *data)  // OUT: Piece of RD_Info structure
{
   Backdoor_proto bp;

   bp.in.cx.halfs.low = BDOOR_CMD_GETDEVICELISTELEMENT;
   bp.in.size = (id << 16) | offset;
   Backdoor(&bp);
   if (bp.out.ax.word == FALSE) {
      return FALSE;
   }
   *data = bp.out.bx.word;
   return TRUE;
}


/*
 *-----------------------------------------------------------------------------
 *
 * GuestApp_GetDeviceInfo --
 *
 *      Retrieve information about a removable device. --hpreg
 *
 * Results:
 *      TRUE on success
 *      FALSE on failure
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

Bool
GuestApp_GetDeviceInfo(uint16 id,     // IN: Device ID
                       RD_Info *info) // OUT
{
   uint16 offset;
   uint32 *p;

   /*
    * XXX It is theoretically possible to SEGV here as we can write up to 3
    *     bytes beyond the end of the 'info' structure. I think alignment
    *     saves us in practice. --hpreg
    */
   for (offset = 0, p = (uint32 *)info;
        offset < sizeof *info;
        offset += sizeof (uint32), p++) {
      if (GuestAppGetDeviceListElement(id, offset, p) == FALSE) {
         return FALSE;
      }
   }

   return TRUE;
}

