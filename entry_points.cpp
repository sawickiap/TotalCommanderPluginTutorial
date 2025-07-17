/*
MIT License

Copyright (c) 2025 Adam Sawicki, https://asawicki.info

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/
#include "precompiled_header.hpp"
#include "archive.hpp"

/*
This file contains definitions of functions exported from our DLL - interface
between Total Commander and our code.
*/

// Error code returned on unknown exception.
#define UNKNOWN_ERROR_CODE E_NO_MEMORY

extern "C" __declspec(dllexport)
int __stdcall GetPackerCaps()
{
    return
        // Archive format can contain multiple files - quite obvious...
        PK_CAPS_MULTIPLE
        // Total Commander can perform full-text search inside.
        // Nothing has to be done on our side to make it work.
        | PK_CAPS_SEARCHTEXT
        // Plugin can create new archives.
        // Functions: OpenArchive, ReadHeaderEx, ProcessFile, CloseArchive.
        | PK_CAPS_NEW
        // Plugin can modify existing archives (add new files).
        // Function: PackFiles.
        | PK_CAPS_MODIFY
        // Plugin can delete files from the archive.
        // Function: DeleteFiles.
        | PK_CAPS_DELETE
        // Plugin can recognize archive file format by content, not file extension.
        // Function: CanYouHandleThisFIle.
        | PK_CAPS_BY_CONTENT;
}

extern "C" __declspec(dllexport)
int __stdcall GetBackgroundFlags()
{
    // Our packing and unpacking functions are thread-safe.
    return BACKGROUND_UNPACK | BACKGROUND_PACK;
}

/*
This function is called as first in a sequence of calls dedicated to listing,
testing or extracting from existing archive. Archive should be opened for
reading.

Function should return some "handle" (context) that will be passed to subsequent
functions. We will use it to pass pointer to dynamically allocated object of our
main class Archive.

Result code should be returned in archiveData->OpenResult.
*/
extern "C" __declspec(dllexport)
HANDLE __stdcall OpenArchiveW(tOpenArchiveDataW* archiveData)
{
    /*
    DLLs expose only C interface, so they cannot pass STL containers or other
    complex data structures or throw exceptions, unless we are sure that both main
    program and the DLL are compiled using same compiler in same version. That's why
    we catch all exceptions here and report them as error codes.
    */
    try
    {
        auto archive = new ReadingArchive();
        archive->OpenArchiveW(archiveData);
        return (HANDLE)archive;
    }
    catch(int error_code)
    {
        archiveData->OpenResult = error_code;
        return nullptr;
    }
    catch(...)
    {
        archiveData->OpenResult = UNKNOWN_ERROR_CODE;
        return nullptr;
    }
}

/*
This function is called at the end of sequence began with OpenArchiveW. It
should close the archive.
*/
extern "C" __declspec(dllexport)
int __stdcall CloseArchive(HANDLE hArcData)
{
    auto archive = (ArchiveBase*)hArcData;
    delete archive;
    return 0;
}

/*
This function is called multiple times to fetch headers of subsequent entries
from the archive.
*/
extern "C" __declspec(dllexport)
int __stdcall ReadHeaderExW(HANDLE hArcData, tHeaderDataExW *headerData)
{
    auto archive = (ReadingArchive*)hArcData;
    try
    {
        return archive->ReadHeaderExW(headerData);
    }
    catch(int error_code)
    {
        return error_code;
    }
    catch(...)
    {
        return UNKNOWN_ERROR_CODE;
    }
}

/*
This function is called multiple times to request testing, skipping or
extracting file that was last met by function ReadHeaderExW. Total Commander
always calls:

    ReadHeaderExW, ProcessFileW, ReadHeaderExW, ProcessFileW, ...
*/
extern "C" __declspec(dllexport)
int __stdcall ProcessFileW(HANDLE hArcData, int operation, wchar_t *destPath, wchar_t *destName)
{
    auto archive = (ReadingArchive*)hArcData;
    try
    {
        return archive->ProcessFileW(operation, destPath, destName);
    }
    catch(int error_code)
    {
        return error_code;
    }
    catch(...)
    {
        return UNKNOWN_ERROR_CODE;
    }
}

extern "C" __declspec(dllexport)
void __stdcall SetChangeVolProcW(HANDLE hArcData, tChangeVolProcW pChangeVolProc1)
{
    // Nothing here.
}

/*
This function is called to provide our plugin with a callback to a function that
we can call to update progress bar.
*/
extern "C" __declspec(dllexport)
void __stdcall SetProcessDataProcW(HANDLE hArcData, tProcessDataProcW pProcessDataProc)
{
    // Surprisingly, it happened to me that hArcData was 0xFFFFFFFFFFFFFFFF!
    if(hArcData && (UINT_PTR)hArcData != UINTPTR_MAX)
    {
        auto archive = (ArchiveBase*)hArcData;
        archive->SetProcessDataProcW(pProcessDataProc);
    }
    else
        g_global_process_data_proc = pProcessDataProc;
}

/*
This standalone function is called to request packing a sequence of files and
directories, listed in addList parameter, to a new or existing archive indicated
by packedFile.
*/
extern "C" __declspec(dllexport)
int __stdcall PackFilesW(wchar_t *packedFile, wchar_t *subPath, wchar_t *srcPath, wchar_t *addList, int flags)
{
    try
    {
        auto archive = std::make_unique<PackingArchive>();
        return archive->PackFilesW(packedFile, subPath, srcPath, addList, flags);
    }
    catch(int error_code)
    {
        return error_code;
    }
    catch(...)
    {
        return UNKNOWN_ERROR_CODE;
    }
}

/*
This standalone function is called to request deleting a sequence of files and
directories, enlisted in deleteList parameter, from inside archive indicated by
packedFile.
*/
extern "C" __declspec(dllexport)
int __stdcall DeleteFilesW(wchar_t *packedFile, wchar_t *deleteList)
{
    try
    {
        auto archive = std::make_unique<DeletingArchive>();
        return archive->DeleteFilesW(packedFile, deleteList);
    }
    catch(int error_code)
    {
        return error_code;
    }
    catch(...)
    {
        return UNKNOWN_ERROR_CODE;
    }
}

// PK_CAPS_BY_CONTENT
/*
This standalone function is called to test whether a file indicated by FileName
parameter is an archive in the file format supported by this plugin.
*/
extern "C" __declspec(dllexport)
BOOL __stdcall CanYouHandleThisFileW(wchar_t* FileName)
{
    try
    {
        auto archive = std::make_unique<HeaderCheckingArchive>();
        return archive->CanYouHandleThisFileW(FileName);
    }
    catch(int)
    {
        return FALSE;
    }
    catch(...)
    {
        return FALSE;
    }
}
