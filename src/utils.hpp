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
#pragma once

/*
Calculates 0...100 percent from current value and maximum value.
*/
template<typename T>
inline T CalcPercent(T number, T count)
{
    if(count)
        return (number * 100 + count / 2) / count;
    else
    {
        assert(0);
        return 0;
    }
}

/*
Custom deleter for STL smart pointers for FILE* pointer that calls fclose() on
destruction.
*/
struct FcloseDeleter
{
    void operator()(FILE* f) const
    {
        fclose(f);
    }
};
using UniqueFilePtr = std::unique_ptr<FILE, FcloseDeleter>;

/*
Custom deleter for STL smart pointers for HANDLE (not a pointer!) that calls
CloseHandle() on destruction.
*/
struct CloseHandleDeleter
{
    typedef HANDLE pointer;
    void operator()(HANDLE handle) const
    {
        CloseHandle(handle);
    }
};

/*
Predicate functor to compare two std::wstring-s if first one is less
lexiconographically, case-insensitive.
*/
struct StricmpPred
{
    bool operator()(const std::wstring& lhs, const std::wstring& rhs) const
    {
        return _wcsicmp(lhs.c_str(), rhs.c_str()) < 0;
    }
};

/*
Converts inout string to upper-case, in place.
*/
void UpperCase(std::wstring& inout);

/*
Combines absolute or relative directory name "path" with relative path or file
name "name", inserting '\\' between them if necessary. path and name can be null
or empty.
*/
std::wstring CombinePath(const wstr_view& path, const wstr_view& name);

/*
Given path (absolute or relative), function returns file name and extension,
without leading directories. When path is file name only, returns path.

Examples:

    "Dir\\File2" -> "File2"
    "C:\\Dir\\SubDir\\File3.tar.gz" -> "File3.tar.gz"
    "File1.txt" -> "File1.txt"
*/
std::wstring ExtractFileName(const wstr_view& path);

/*
If given path ends with '\\' or '/', it removes the character in-place.
*/
void StripTrailingSlash(std::wstring& inout_path);

/*
Given unsorted sequence of file paths, the function removes duplicates leaving
only last one that has same file name (in the sense as returned by function
ExtractFileName). For example:

    [0] "File1.txt" // This one is removed because of duplicated file name at [2].
    [1] "File2.txt"
    [2] "SubDir\FILE1.TXT"

*/
void RemoveFileNameDuplicates(std::vector<std::wstring>& paths);

/*
Given path, it traverses one directory up. Examples:

	"Dir\\SubDir\\File1" -> "Dir\\SubDir"
	"Dir\\SubDir" -> "Dir"
	"Dir" -> ""
*/
void UpDir(std::wstring& inout);

// Calls fread(). On error or too few data read, throws exception.
void ReadOrThrow(void* dst_buf, size_t elem_size, size_t elem_count, FILE* file);
// Calls fwrite(). On error or too few data written, throws exception.
void WriteOrThrow(const void* buf, size_t elem_size, size_t elem_count, FILE* file);
// Calls fseek(). On error, throws exception.
void SeekOrThrow(FILE* stream, int64_t offset, int origin);
