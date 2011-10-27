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
 * fileIO.c --
 *
 *    Basic (non internationalized) implementation of error messages for the
 *    Files library.
 *
 *    File locking/unlocking routines.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include "vmware.h"
#include "util.h"
#include "fileIO.h"
#include "fileLock.h"
#include "fileInt.h"
#include "msg.h"
#include "unicodeOperations.h"
#include "hostType.h"
#if defined(_WIN32)
#include <io.h>
#else
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#if defined(VMX86_SERVER)
#include "fs_public.h"
#endif


/*
 *----------------------------------------------------------------------
 *
 * FileIO_ErrorEnglish --
 *
 *      Return the message associated with a status code
 *
 * Results:
 *      The message
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

const char *
FileIO_ErrorEnglish(FileIOResult status) // IN
{
   return Msg_StripMSGID(FileIO_MsgError(status));
}


/*
 *----------------------------------------------------------------------
 *
 * FileIO_MsgError --
 *
 *      Return the message associated with a status code
 *
 * Results:
 *      The message.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

const char *
FileIO_MsgError(FileIOResult status) // IN
{
   const char *result = NULL;

   switch (status) {
   case FILEIO_SUCCESS:
      /*
       * Most of the time, you don't call this function with this value
       * because there is no error
       */
      result = MSGID(fileio.success) "Success";
      break;

   case FILEIO_CANCELLED:
      /*
       * Most of the time, you don't call this function with this value
       * because you don't want to display error messages after a user has
       * cancelled an operation.
       */
      result = MSGID(fileio.cancel) "The operation was canceled by the user";
      break;

   case FILEIO_ERROR:
      /*
       * Most of the time, you don't call this function with this value
       * because you can call your native function to retrieve a more
       * accurate message.
       */
      result = MSGID(fileio.generic) "Error";
      break;

   case FILEIO_OPEN_ERROR_EXIST:
      result = MSGID(fileio.exists) "The file already exists";
      break;

   case FILEIO_LOCK_FAILED:
      result = MSGID(fileio.lock) "Failed to lock the file";
      break;

   case FILEIO_READ_ERROR_EOF:
      result = MSGID(fileio.eof) "Tried to read beyond the end of the file";
      break;

   case FILEIO_FILE_NOT_FOUND:
      result = MSGID(fileio.notfound) "Could not find the file";
      break;

   case FILEIO_NO_PERMISSION:
      result = MSGID(fileio.noPerm) "Insufficient permission to access the file";
      break;

   case FILEIO_FILE_NAME_TOO_LONG:
      result = MSGID(fileio.namelong) "The file name is too long";
      break;

   case FILEIO_WRITE_ERROR_FBIG:
      result = MSGID(fileio.fBig) "The file is too large";
      break;

   case FILEIO_WRITE_ERROR_NOSPC:
      result = MSGID(fileio.noSpc) "There is no space left on the device";
      break;

   case FILEIO_WRITE_ERROR_DQUOT:
      result = MSGID(fileio.dQuot) "There is no space left on the device";
      break;

   case FILEIO_ERROR_LAST:
      NOT_IMPLEMENTED();
      break;

      /*
       * We do not provide a default case on purpose, so that the compiler can
       * detect changes in the error set and reminds us to implement the
       * associated messages --hpreg
       */
   }

   if (!result) {
      Warning("%s: bad code %d\n", __FUNCTION__, status);
      ASSERT(0);
      result = MSGID(fileio.unknown) "Unknown error";
   }

   return result;
}


/*
 *----------------------------------------------------------------------
 *
 * FileIO_Init --
 *
 *      Initialize invalid FileIODescriptor.  Expects that caller
 *	prepared structure with FileIO_Invalidate.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

void
FileIO_Init(FileIODescriptor *fd,   // IN/OUT:
            ConstUnicode pathName)  // IN:
{
   ASSERT(fd);
   ASSERT(pathName);

   fd->fileName = Unicode_Duplicate(pathName);
}


/*
 *----------------------------------------------------------------------
 *
 * FileIO_Cleanup --
 *
 *      Undo resource allocation done by FileIO_Init.  You do not want to
 *	call this function directly, you most probably want FileIO_Close.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

void
FileIO_Cleanup(FileIODescriptor *fd)  // IN/OUT:
{
   ASSERT(fd);

   if (fd->fileName) {
      Unicode_Free(fd->fileName);
      fd->fileName = NULL;
   }
}


/*
 *----------------------------------------------------------------------
 *
 * FileIO_Lock --
 *
 *      Call the FileLock module to lock the given file.
 *
 * Results:
 *      FILEIO_ERROR               A serious error occured.
 *      FILEIO_SUCCESS             All is well
 *      FILEIO_LOCK_FAILED         Requested lock on file was not acquired
 *      FILEIO_FILE_NOT_FOUND      Unable to find the specified file
 *      FILEIO_NO_PERMISSION       Permissions issues
 *      FILEIO_FILE_NAME_TOO_LONG  The path name is too long
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

FileIOResult
FileIO_Lock(FileIODescriptor *file,  // IN/OUT:
            int access)              // IN:
{
   FileIOResult ret = FILEIO_SUCCESS;

   /*
    * Lock the file if necessary.
    */

   ASSERT(file);

#if !defined(__FreeBSD__) && !defined(sun)
   if (access & FILEIO_OPEN_LOCKED) {
      int err = 0;

      ASSERT(file->lockToken == NULL);

      file->lockToken = FileLock_Lock(file->fileName,
                                      (access & FILEIO_OPEN_ACCESS_WRITE) == 0,
                                      FILELOCK_DEFAULT_WAIT,
                                      &err,
                                      NULL);

      if (file->lockToken == NULL) {
         /* Describe the lock not acquired situation in detail */
         Warning(LGPFX" %s on '%s' failed: %s\n",
                 __FUNCTION__, UTF8(file->fileName),
                 (err == 0) ? "Lock timed out" : strerror(err));

         /* Return a serious failure status if the locking code did */
         switch (err) {
         case 0:             // File is currently locked
         case EROFS:         // Attempt to lock for write on RO FS
            ret = FILEIO_LOCK_FAILED;
            break;
         case ENAMETOOLONG:  // Path is too long
            ret = FILEIO_FILE_NAME_TOO_LONG;
            break;
         case ENOENT:        // No such file or directory
            ret = FILEIO_FILE_NOT_FOUND;
            break;
         case EACCES:       // Permissions issues
            ret = FILEIO_NO_PERMISSION;
            break;
         default:            // Some sort of locking error
            ret = FILEIO_ERROR;
         }
      }
   }
#else
   ASSERT(file->lockToken == NULL);
#endif // !__FreeBSD__ && !sun

   return ret;
}


/*
 *----------------------------------------------------------------------
 *
 * FileIO_UnLock --
 *
 *      Call the FileLock module to unlock the given file.
 *
 * Results:
 *      FILEIO_SUCCESS  All is well
 *      FILEIO_ERROR    A serious error occured.
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

FileIOResult
FileIO_Unlock(FileIODescriptor *file)  // IN/OUT:
{
   FileIOResult ret = FILEIO_SUCCESS;

   ASSERT(file);

#if !defined(__FreeBSD__) && !defined(sun)
   if (file->lockToken != NULL) {
      int err = 0;

      if (!FileLock_Unlock(file->lockToken, &err, NULL)) {
         Warning(LGPFX" %s on '%s' failed: %s\n",
                 __FUNCTION__, UTF8(file->fileName), strerror(err));

         ret = FILEIO_ERROR;
      }

      file->lockToken = NULL;
   }
#else
   ASSERT(file->lockToken == NULL);
#endif // !__FreeBSD__ && !sun

   return ret;
}


/*
 *----------------------------------------------------------------------
 *
 * FileIO_GetSize --
 *
 *      Get size of file.
 *
 * Results:
 *      Size of file or -1.
 *
 * Side effects:
 *      errno is set on error.
 *
 *----------------------------------------------------------------------
 */

int64
FileIO_GetSize(const FileIODescriptor *fd)  // IN:
{
   int64 logicalBytes;

   return (FileIO_GetAllocSize(fd, &logicalBytes, NULL) != FILEIO_SUCCESS) ?
      -1 : logicalBytes;
}


/*
 *----------------------------------------------------------------------
 *
 * FileIO_GetSizeByPath --
 *
 *      Get size of a file specified by path. 
 *
 * Results:
 *      Size of file or -1.
 *
 * Side effects:
 *      errno is set on error
 *
 *----------------------------------------------------------------------
 */

int64
FileIO_GetSizeByPath(ConstUnicode pathName)  // IN:
{
   int64 logicalBytes;

   return (FileIO_GetAllocSizeByPath(pathName, &logicalBytes, NULL) !=
      FILEIO_SUCCESS) ? -1 : logicalBytes;
}


/*
 *----------------------------------------------------------------------
 *
 * FileIO_Filename --
 *
 *      Returns the filename that was used to open a FileIODescriptor
 *
 * Results:
 *      Filename. You DON'T own the memory - use Unicode_Duplicate if
 *      you want to keep it for yourself. In particular, if the file
 *      gets closed the string will almost certainly become invalid.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

ConstUnicode
FileIO_Filename(FileIODescriptor *fd)  // IN:
{
   ASSERT(fd);

   return fd->fileName;
}


/*
 *----------------------------------------------------------------------
 *
 * FileIO_CloseAndUnlink
 *
 *      Closes and unlinks the file associated with a FileIODescriptor.
 *
 * Results:
 *      TRUE: An error occurred.
 *      FALSE: The file was closed and unlinked.
 *
 * Side effects:
 *      File is probably closed and unlinked.
 *
 *----------------------------------------------------------------------
 */

Bool
FileIO_CloseAndUnlink(FileIODescriptor *fd)  // IN:
{
   Unicode path;
   Bool ret;

   ASSERT(fd);

   path = Unicode_Duplicate(fd->fileName);
   ret = FileIO_Close(fd) || File_Unlink(path);
   Unicode_Free(path);

   return ret;
}


#if defined(_WIN32) || defined(__linux__) || defined(__APPLE__) || \
    defined(__FreeBSD__) || defined(sun)
/*
 *----------------------------------------------------------------------
 *
 * FileIO_Pread --
 *
 *      Reads from a file starting at a specified offset.
 *
 *      Note: This function may update the file pointer so you will need to
 *      call FileIO_Seek before calling FileIO_Read/Write afterwards.
 *
 * Results:
 *      FILEIO_SUCCESS, FILEIO_ERROR
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

FileIOResult
FileIO_Pread(FileIODescriptor *fd,  // IN: File descriptor
             void *buf,             // IN: Buffer to read into
             size_t len,            // IN: Length of the buffer
             uint64 offset)         // IN: Offset to start reading
{
   struct iovec iov;

   ASSERT(fd);

   iov.iov_base = buf;
   iov.iov_len = len;

   return FileIO_Preadv(fd, &iov, 1, offset, len);
}


/*
 *----------------------------------------------------------------------
 *
 * FileIO_Pwrite --
 *
 *      Writes to a file starting at a specified offset.
 *
 *      Note: This function may update the file pointer so you will need to
 *      call FileIO_Seek before calling FileIO_Read/Write afterwards.
 *
 * Results:
 *      FILEIO_SUCCESS, FILEIO_ERROR
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

FileIOResult
FileIO_Pwrite(FileIODescriptor *fd,  // IN: File descriptor
              void const *buf,       // IN: Buffer to write from
              size_t len,            // IN: Length of the buffer
              uint64 offset)         // IN: Offset to start writing
{
   struct iovec iov;

   ASSERT(fd);

   /* The cast is safe because FileIO_Pwritev() will not write to '*buf'. */
   iov.iov_base = (void *)buf;
   iov.iov_len = len;

   return FileIO_Pwritev(fd, &iov, 1, offset, len);
}
#endif


#if defined(sun) && __GNUC__ < 3
/*
 *-----------------------------------------------------------------------------
 *
 * FileIO_IsSuccess --
 *
 *      XXX: See comment in fileIO.h.  For reasonable compilers, this
 *      function is implemented as "static inline" in fileIO.h; for
 *      unreasonable compilers, it can't be static so we implement it here.
 *
 * Results:
 *      TRUE if the input indicates success.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

Bool
FileIO_IsSuccess(FileIOResult res)  // IN:
{
   return res == FILEIO_SUCCESS;
}
#endif


/*
 *-----------------------------------------------------------------------------
 *
 * FileIOAtomicTempPath
 *
 *      Return a temp path name in the same directory as the argument file.
 *      The path is the full path of the source file with a '~' appended.
 *      The caller must free the path when done.
 *
 * Results:
 *      Unicode path if successful, NULL on failure.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

static Unicode 
FileIOAtomicTempPath(FileIODescriptor *fileFD)  // IN:
{
   Unicode path;
   Unicode srcPath;

   ASSERT(FileIO_IsValid(fileFD));

   srcPath = File_FullPath(FileIO_Filename(fileFD));
   if (!srcPath) {
      return NULL;
   }
   path = Unicode_Join(srcPath, "~", NULL);
   Unicode_Free(srcPath);

   return path;
}


/*
 *-----------------------------------------------------------------------------
 *
 * FileIO_AtomicTempFile
 *
 *      Create a temp file in the same directory as the argument file. 
 *      On non-Windows attempts to create the temp file with the same
 *      permissions and owner/group as the argument file.
 *
 * Results:
 *      TRUE if successful, FALSE on failure.
 *
 * Side effects:
 *      Creates a new file.
 *
 *-----------------------------------------------------------------------------
 */

Bool
FileIO_AtomicTempFile(FileIODescriptor *fileFD,  // IN:
                      FileIODescriptor *tempFD)  // OUT:
{
   Unicode tempPath = NULL;
   int permissions;
   FileIOResult status;
#if !defined(_WIN32)
   struct stat stbuf;
#endif

   ASSERT(FileIO_IsValid(fileFD));
   ASSERT(tempFD && !FileIO_IsValid(tempFD));

   tempPath = FileIOAtomicTempPath(fileFD);
   if (!tempPath) {
      goto bail;
   }

#if defined(_WIN32)
   permissions = 0;
   File_UnlinkIfExists(tempPath);
#else
   if (fstat(fileFD->posix, &stbuf)) {
      ASSERT(!vmx86_server); // For APD, hosted can fall-back and write directly

      goto bail;
   }
   permissions = stbuf.st_mode;
   Posix_Unlink(tempPath);
#endif

   status = FileIO_Create(tempFD, tempPath,
                          FILEIO_ACCESS_READ | FILEIO_ACCESS_WRITE,
                          FILEIO_OPEN_CREATE, permissions);
   if (!FileIO_IsSuccess(status)) {
      Log("%s: Failed to create temporary file\n", __FUNCTION__);
#if defined(VMX86_SERVER)
      ASSERT_BUG_DEBUGONLY(615124, errno != EBUSY);
#endif
      ASSERT(!vmx86_server); // For APD, hosted can fall-back and write directly
      goto bail;
   }

#if !defined(_WIN32)
   if (fchmod(tempFD->posix, stbuf.st_mode)) {
      Log("%s: Failed to chmod temporary file, errno: %d\n",
          __FUNCTION__, errno);
      ASSERT(!vmx86_server); // For APD, hosted can fall-back and write directly
      goto bail;
   }
   if (fchown(tempFD->posix, stbuf.st_uid, stbuf.st_gid)) {
      Log("%s: Failed to chown temporary file, errno: %d\n",
          __FUNCTION__, errno);
      ASSERT(!vmx86_server); // For APD, hosted can fall-back and write directly
      goto bail;
   }
#endif

   Unicode_Free(tempPath);
   return TRUE;

bail:
   if (FileIO_IsValid(tempFD)) {
      FileIO_Close(tempFD);
      File_Unlink(tempPath);
   }
   Unicode_Free(tempPath);
   return FALSE;
}
   

/*
 *-----------------------------------------------------------------------------
 *
 * FileIO_AtomicExchangeFiles --
 *
 *      On ESX, exchanges the contents of two files using code modeled from
 *      VmkfsLib_SwapFiles.  Both "curr" and "new" are left open.
 *
 *      On Hosted replaces "curr" with "new" using rename/link.
 *      Path to "new" no longer exists on success.
 *
 * Results:
 *      TRUE if successful, FALSE on failure.
 *
 * Side effects:
 *      Disk I/O.
 *
 *-----------------------------------------------------------------------------
 */


Bool
FileIO_AtomicExchangeFiles(FileIODescriptor *newFD,   // IN/OUT: file IO descriptor
                           FileIODescriptor *currFD)  // IN/OUT: file IO descriptor
{
   char *currPath;
   char *newPath;
   uint32 currAccess;
   uint32 newAccess;
   Bool ret = FALSE;
   FileIOResult status;

   ASSERT(FileIO_IsValid(newFD));
   ASSERT(FileIO_IsValid(currFD));

   if (HostType_OSIsVMK()) {
#if defined(VMX86_SERVER)
      FS_SwapFilesArgs *args = NULL;
      char *dirName = NULL;
      char *fileName = NULL;
      char *dstDirName = NULL;
      char *dstFileName = NULL;
      int fd;

      currPath = File_FullPath(FileIO_Filename(currFD));
      newPath = File_FullPath(FileIO_Filename(newFD));

      ASSERT(currPath);
      ASSERT(newPath);

      File_GetPathName(newPath, &dirName, &fileName);
      File_GetPathName(currPath, &dstDirName, &dstFileName);

      ASSERT(dirName && *dirName);
      ASSERT(fileName && *fileName);
      ASSERT(dstDirName && *dstDirName);
      ASSERT(dstFileName && *dstFileName);
      ASSERT(!strcmp(dirName, dstDirName));

      args = (FS_SwapFilesArgs *) Util_SafeCalloc(1, sizeof(*args));
      if (Str_Snprintf(args->srcFile, sizeof(args->srcFile), "%s",
                       fileName) < 0) {
         Log("%s: Path too long \"%s\".\n", __FUNCTION__, fileName);
         goto swapdone;
      }
      if (Str_Snprintf(args->dstFilePath, sizeof(args->dstFilePath), "%s/%s",
                       dstDirName, dstFileName) < 0) {
         Log("%s: Path too long \"%s\".\n", __FUNCTION__, dstFileName);
         goto swapdone;
      }

      /*
       * Issue the ioctl on the directory rather than on the file,
       * because the file could be open.
       */

      fd = Posix_Open(dirName, O_RDONLY);
      if (fd < 0) {
         Log("%s: Open failed \"%s\" %d.\n", __FUNCTION__, dirName,
             errno);
         ASSERT_BUG_DEBUGONLY(615124, errno != EBUSY);
         goto swapdone;
      }

      if (ioctl(fd, IOCTLCMD_VMFS_SWAP_FILES, args) != 0) {
         Log("%s: ioctl failed %d.\n", __FUNCTION__, errno);
         ASSERT_BUG_DEBUGONLY(615124, errno != EBUSY);
      } else {
         ret = TRUE;
      }

      close(fd);

swapdone:
      free(args);
      free(dirName);
      free(fileName);
      free(dstDirName);
      free(dstFileName);
      free(currPath);
      free(newPath);

      return ret;
#else
      NOT_REACHED();
#endif
   }

   currPath = Unicode_Duplicate(FileIO_Filename(currFD));
   newPath = Unicode_Duplicate(FileIO_Filename(newFD));

   newAccess = newFD->flags;
   currAccess = currFD->flags;

   FileIO_Close(newFD);

   /*
    * The current file needs to be closed and reopened,
    * but we don't want to drop the file lock by calling 
    * FileIO_Close() on it.  Instead, use native close primitives.
    * We'll reopen it later with a temp FileIODescriptor, and
    * swap the file descriptor/handle.  Set the descriptor/handle
    * to an invalid value while we're in the middle of transferring
    * ownership.
    */

#if defined(_WIN32)
   CloseHandle(currFD->win32);
   currFD->win32 = INVALID_HANDLE_VALUE;
#else
   close(currFD->posix);
   currFD->posix = -1;
#endif
   if (File_RenameRetry(newPath, currPath, 10)) {
      goto bail;
   }

   ret = TRUE;
   
bail:

   /*
    * XXX - We shouldn't drop the file lock here.
    *       Need to implement FileIO_Reopen to close
    *       and reopen without dropping the lock.
    */

   FileIO_Close(currFD);  // XXX - PR 769296

   status = FileIO_Open(currFD, currPath, currAccess, 0);
   if (!FileIO_IsSuccess(status)) {
      Panic("Failed to reopen dictionary file.\n");
   }

   Unicode_Free(currPath);
   Unicode_Free(newPath);
   return ret;
}
