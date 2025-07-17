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
#include "utils.hpp"
#include <map>
#include <cctype>

void UpperCase(std::wstring& inout)
{
    for(size_t i = 0, len = inout.length(); i < len; ++i)
        inout[i] = (wchar_t)towupper(inout[i]);
}

std::wstring CombinePath(const wstr_view& path, const wstr_view& name)
{
    std::wstring result = path.to_string();
    if(!path.empty() && !name.empty() && result.back() != L'\\' && result.back() != L'/')
        result += L'\\';
    result += name.c_str();
    return result;
}

std::wstring ExtractFileName(const wstr_view& path)
{
    size_t it = path.find_last_of(L"\\/");
    if(it == SIZE_MAX)
        return path.to_string();
    else
        return path.substr(it + 1).to_string();
}

void StripTrailingSlash(std::wstring& inout_path)
{
    if(!inout_path.empty() && (inout_path.back() == L'\\' || inout_path.back() == L'/'))
        inout_path.pop_back();
}

void RemoveFileNameDuplicates(std::vector<std::wstring>& paths)
{
    /*
    Key will be ExtractFileName(paths[i]), sorted and searched case-insensitive.
    Value will be index to paths vector.
    */
    std::map<std::wstring, size_t, StricmpPred> file_names;
    std::vector<size_t> path_indices_to_remove;

    for(size_t path_index = 0, path_count = paths.size(); path_index < path_count; ++path_index)
    {
        auto file_name = ExtractFileName(paths[path_index]);
        auto it = file_names.find(file_name);
        // fileName not found: no duplicate, just add it.
        if(it == file_names.end())
            file_names.insert(std::make_pair(file_name, path_index));
        // Entry with this fileName already exists: mark previous one for removal.
        else
        {
            path_indices_to_remove.push_back(it->second);
            it->second = path_index;
        }
    }

    /*
    Because paths are unsorted anyway, instead of removing each path with erase()
    method that moves each following path one element back, giving O(n^2)
    complexity, we can do a trick of swapping path to remove with the last one and
    then remove the last one.
    */

    std::sort(path_indices_to_remove.begin(), path_indices_to_remove.end());

    for(size_t i = path_indices_to_remove.size(); i--; )
    {
        size_t index_to_remove = path_indices_to_remove[i];
        size_t path_count = paths.size();
        if(index_to_remove + 1 < path_count)
            std::swap(paths[index_to_remove], paths[path_count-1]);
        paths.pop_back();
    }
}

void UpDir(std::wstring& inout)
{
    size_t last_slash = inout.find_last_of(L"\\/");
    if(last_slash == std::wstring::npos)
        inout.clear();
    else
        inout.erase(last_slash);
}

void ReadOrThrow(void* dst_buf, size_t elem_size, size_t elem_count, FILE* file)
{
    size_t elements_read = fread(dst_buf, elem_size, elem_count, file);
    if(elements_read != elem_count)
        throw E_EREAD;
}

void WriteOrThrow(const void* buf, size_t elem_size, size_t elem_count, FILE* file)
{
    size_t elements_written = fwrite(buf, elem_size, elem_count, file);
    if(elements_written != elem_count)
        throw E_EWRITE;
}

void SeekOrThrow(FILE* stream, int64_t offset, int origin)
{
    if(_fseeki64(stream, offset, origin) != 0)
        throw E_NOT_SUPPORTED;
}
