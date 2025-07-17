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

/*
Versions of interface functions that use 8-bit ANSI strings are used by Total
Commander only on platforms where Unicode is not supported: Windows 9x/Me.
Otherwise it calls Unicode versions that end with 'W'. Ghisler recommends to
implement ANSI versions as well, but we will leave them unimplemented.
*/

extern "C" __declspec(dllexport)
HANDLE __stdcall OpenArchive(tOpenArchiveData* archiveData)
{
    assert(0);
    return NULL;
}

extern "C" __declspec(dllexport)
int __stdcall ReadHeader(HANDLE hArcData, tHeaderData* headerData)
{
    assert(0);
    return E_NOT_SUPPORTED;
}

extern "C" __declspec(dllexport)
int __stdcall ReadHeaderEx(HANDLE hArcData, tHeaderDataEx* headerData)
{
    assert(0);
    return E_NOT_SUPPORTED;
}

extern "C" __declspec(dllexport)
int __stdcall ProcessFile(HANDLE hArcData, int operation, char* destPath, char* destName)
{
    assert(0);
    return E_NOT_SUPPORTED;
}

extern "C" __declspec(dllexport)
void __stdcall SetChangeVolProc(HANDLE hArcData, tChangeVolProc pChangeVolProc1)
{
    // Nothing here.
}

extern "C" __declspec(dllexport)
void __stdcall SetProcessDataProc(HANDLE hArcData, tProcessDataProc pProcessDataProc)
{
    // Not supported. It actually gets called together with SetProcessDataProcW.
}

extern "C" __declspec(dllexport)
int __stdcall PackFiles(char* packedFile, char* subPath, char* srcPath, char* addList, int flags)
{
    assert(0);
    return E_NOT_SUPPORTED;
}

extern "C" __declspec(dllexport)
int __stdcall DeleteFiles(char* packedFile, char* deleteList)
{
    assert(0);
    return E_NOT_SUPPORTED;
}

extern "C" __declspec(dllexport)
BOOL __stdcall CanYouHandleThisFile(char* FileName)
{
    assert(0);
    return FALSE;
}
