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
#include "third_party/zlib-1.3.1/zlib.h"

// Deleter for STL smart pointers like std::unique_ptr that calls deflateEnd on
// destruction.
struct DeflateEndDeleter
{
    void operator()(z_stream* s) const
    {
        deflateEnd(s);
    }
};

// Deleter for STL smart pointers like std::unique_ptr that calls inflateEnd on
// destruction.
struct InflateEndDeleter
{
    void operator()(z_stream* s) const
    {
        inflateEnd(s);
    }
};

enum FILE_ATTR
{
    // From WCX Writer's Reference, tHeaderDataEx::FileAttr member.
    FILE_ATTR_READ_ONLY = 0x1,
    FILE_ATTR_HIDDEN    = 0x2,
    FILE_ATTR_SYSTEM    = 0x4,
    FILE_ATTR_VOLUME_ID = 0x8,
    FILE_ATTR_DIRECTORY = 0x10,
    FILE_ATTR_ARCHIVE   = 0x20,
    FILE_ATTR_ANY_FILE  = 0x3F,
};

static const size_t kMaxFileNameLen = 1024; // countof(tHeaderDataEx::FileName).
static const bool kEnableCompression = true;
static constexpr std::string_view kFileHeader = "SMPA100A";
static const uint32_t kEntryMagic = 0x1743C8F1;
static const size_t kBufSize = 0x10000; // 64 KB
static const uint64_t kProgressUpdateIntervalMilliseconds = 40; // 25 times per second.
static const uint64_t kMinFileSizeForCompression = 16;

static uint8_t WindowsAttributesToWcxAttributes(DWORD windows_attr)
{
    uint8_t wcx_attr = 0;
    if(windows_attr & FILE_ATTRIBUTE_READONLY)
        wcx_attr |= FILE_ATTR_READ_ONLY;
    if(windows_attr & FILE_ATTRIBUTE_HIDDEN)
        wcx_attr |= FILE_ATTR_HIDDEN;
    if(windows_attr & FILE_ATTRIBUTE_SYSTEM)
        wcx_attr |= FILE_ATTR_SYSTEM;
    if(windows_attr & FILE_ATTRIBUTE_DIRECTORY)
        wcx_attr |= FILE_ATTR_DIRECTORY;
    if(windows_attr & FILE_ATTRIBUTE_ARCHIVE)
        wcx_attr |= FILE_ATTR_ARCHIVE;
    return wcx_attr;
}

static void ZlibResultToWcxException(int zlib_result)
{
    switch(zlib_result)
    {
    case Z_OK:
        break;
    case Z_MEM_ERROR:
        throw E_NO_MEMORY;
    case Z_STREAM_ERROR:
        throw E_BAD_ARCHIVE;
    default:
        throw E_BAD_DATA;
    }
}

static inline bool EnableCompressionForFile(uint64_t file_size)
{
    return kEnableCompression && file_size >= kMinFileSizeForCompression;
}

/*
In some old version of Total Commander I've noticed call to SetProcessDataProc
with hArcData == NULL. That's why I support saving this pointer in global
variable as well, not only inside Archive class.
*/
tProcessDataProcW g_global_process_data_proc = nullptr;

void ReadingArchive::OpenArchiveW(tOpenArchiveDataW *archiveData)
{
    archiveData->OpenResult = 0;

    switch(archiveData->OpenMode)
    {
    case PK_OM_LIST:
        mode_ = ArchiveMode::kList;
        break;
    case PK_OM_EXTRACT:
        mode_ = ArchiveMode::kExtract;
        break;
    default:
        assert(0);
        throw E_NOT_SUPPORTED;
    }

    if(UpdateBytesProcessedProgress())
        throw E_EABORTED;

    FILE *f = nullptr;
    errno_t e = _wfopen_s(&f, archiveData->ArcName, L"rb");
    if(e != 0)
        throw E_EOPEN;
    archive_file_.reset(f);
    if(UpdateBytesProcessedProgress())
        throw E_EABORTED;

    ReadAndCheckHeader();
    if(UpdateBytesProcessedProgress())
        throw E_EABORTED;
}

int ReadingArchive::ReadHeaderExW(tHeaderDataExW *headerData)
{
    assert(mode_ == ArchiveMode::kList || mode_ == ArchiveMode::kExtract);

    ZeroMemory(headerData, sizeof(headerData));

    for(;;)
    {
        if(!ReadEntryHeader())
            return E_END_ARCHIVE;
        if(UpdateBytesProcessedProgress())
            return E_EABORTED;
        if((last_header_.flags & kEntryFlagDeleted) != 0)
        {
            // Skip contents and read header again.
            if(last_header_.pack_size != 0)
            {
                SeekOrThrow(archive_file_.get(), (long long)last_header_.pack_size, SEEK_CUR);
                bytes_processed_since_previous_progress_ += last_header_.pack_size;
                if(UpdateBytesProcessedProgress())
                    throw E_EABORTED;
            }
        }
        else
        {
            // Header of a non-deleted entry read successfully.
            break;
        }
    }

    // Validate parameters.
    if((last_header_.attributes & FILE_ATTR_DIRECTORY) &&
        (last_header_.pack_size > 0 || last_header_.unp_size > 0))
    {
        return E_BAD_ARCHIVE;
    }
    if((last_header_.flags & kEntryFlagCompressed) == 0 &&
        last_header_.unp_size != last_header_.pack_size)
    {
        return E_BAD_ARCHIVE;
    }

    headerData->FileAttr = (int)last_header_.attributes;
    wcscpy_s(headerData->FileName, last_header_path_.c_str());
    headerData->FileTime = (int)last_header_.time;
    headerData->PackSize = (unsigned int)last_header_.pack_size;
    headerData->PackSizeHigh = (unsigned int)(last_header_.pack_size >> 32);
    headerData->UnpSize = (unsigned int)last_header_.unp_size;
    headerData->UnpSizeHigh = (unsigned int)(last_header_.unp_size >> 32);

    return 0;
}

int ReadingArchive::ProcessFileW(int operation, wchar_t *destPath, wchar_t *destName)
{
    assert(mode_ == ArchiveMode::kList || mode_ == ArchiveMode::kExtract);

    switch(operation)
    {
    case PK_SKIP:
    case PK_TEST:
        if(last_header_.pack_size != 0)
        {
            SeekOrThrow(archive_file_.get(), (long long)last_header_.pack_size, SEEK_CUR);
            bytes_processed_since_previous_progress_ += last_header_.pack_size;
            if(UpdateBytesProcessedProgress())
                throw E_EABORTED;
        }
        return 0;

    case PK_EXTRACT:
        ExtractFile(destPath, destName);
        return 0;

    default:
        assert(0);
        return 0;
    }
}

void ReadingArchive::ExtractFile(const wstr_view& dest_path, const wstr_view& dest_name)
{
    std::wstring full_dest_path = CombinePath(dest_path, dest_name);
    StripTrailingSlash(full_dest_path);
    if (full_dest_path.empty())
        throw E_EWRITE;

    // Directory
    if (last_header_.attributes & FILE_ATTR_DIRECTORY)
    {
        const BOOL b = ::CreateDirectoryW(full_dest_path.c_str(), NULL);
        if (!b)
            throw E_ECREATE;
        if (UpdateBytesProcessedProgress())
        {
            ::RemoveDirectoryW(full_dest_path.c_str());
            throw E_EABORTED;
        }
    }
    // File
    else
    {
        try
        {
            FILE* dest_file_ptr = nullptr;
            errno_t e = _wfopen_s(&dest_file_ptr, full_dest_path.c_str(), L"wb");
            if (e != 0)
                throw E_ECREATE;
            UniqueFilePtr file(dest_file_ptr);
            if (UpdateBytesProcessedProgress())
                throw E_EABORTED;

            bool is_compressed = (last_header_.flags & kEntryFlagCompressed) != 0;

            UnpackFileContent(
                dest_file_ptr, archive_file_.get(),
                last_header_.unp_size, last_header_.pack_size, is_compressed);
        }
        catch (int e)
        {
            if (e == E_EABORTED)
                ::DeleteFileW(full_dest_path.c_str());
            throw;
        }
    }

    SetFileAttributes(full_dest_path.c_str(), last_header_.attributes);
    SetFileTime(full_dest_path, last_header_.time);

    if (UpdateBytesProcessedProgress())
        throw E_EABORTED;
}

void ReadingArchive::UnpackFileContent(FILE* dst_file, FILE* src_file,
    uint64_t dst_file_size, uint64_t src_file_size, bool enable_compression)
{
    if (enable_compression)
    {
        z_stream zlib_stream;
        ZeroMemory(&zlib_stream, sizeof(zlib_stream));
        int zlib_result = inflateInit(&zlib_stream);
        ZlibResultToWcxException(zlib_result);
        std::unique_ptr<z_stream, InflateEndDeleter> zlib_stream_ptr(&zlib_stream);

        std::vector<char> src_buf(kBufSize), dst_buf(kBufSize);
        char* src_buf_ptr = src_buf.data();
        char* dst_buf_rtr = dst_buf.data();

        uint64_t src_bytes_left = src_file_size;
        uint64_t total_bytes_written = 0;
        for (;;)
        {
            bool made_progress = false;

            // If the source buffer is empty, read more data from the source file.
            if (zlib_stream.avail_in == 0 && src_bytes_left > 0)
            {
                size_t bytes_to_read = (size_t)std::min<uint64_t>(src_bytes_left, kBufSize);
                size_t bytes_read = fread(src_buf_ptr, 1, bytes_to_read, src_file);
                if (bytes_read < bytes_to_read)
                    throw E_EREAD;
                bytes_processed_since_previous_progress_ += bytes_read;

                zlib_stream.next_in = (Bytef*)src_buf_ptr;
                zlib_stream.avail_in = (uInt)bytes_read;
                src_bytes_left -= bytes_read;
                made_progress = true;
            }

            // Prepare destination buffer.
            zlib_stream.next_out = (Bytef*)dst_buf_rtr;
            zlib_stream.avail_out = (uInt)kBufSize;

            // Decompress!
            zlib_result = inflate(&zlib_stream, 0);
            if (zlib_result != Z_OK && zlib_result != Z_STREAM_END)
                ZlibResultToWcxException(zlib_result);

            // If any destination data has been produced, write it to the destination file.
            if (zlib_stream.avail_out < kBufSize)
            {
                size_t bytes_to_write = kBufSize - zlib_stream.avail_out;
                WriteOrThrow(dst_buf_rtr, 1, bytes_to_write, dst_file);
                total_bytes_written += bytes_to_write;
                made_progress = true;
            }

            if (UpdateBytesProcessedProgress())
                throw E_EABORTED;
            if (zlib_result == Z_STREAM_END)
                break;
            if (!made_progress)
                throw E_BAD_ARCHIVE;
        }

        if (total_bytes_written != dst_file_size)
            throw E_BAD_ARCHIVE;
    }
    else
    {
        std::vector<char> buf(kBufSize);
        char* buf_ptr = buf.data();
        uint64_t bytes_left = src_file_size;
        while (bytes_left > 0)
        {
            size_t bytes_to_process = (size_t)std::min<uint64_t>(bytes_left, kBufSize);
            size_t bytes_read = fread(buf_ptr, 1, bytes_to_process, src_file);
            if (bytes_read < bytes_to_process)
                throw E_EREAD;
            bytes_processed_since_previous_progress_ += bytes_read;
            WriteOrThrow(buf_ptr, 1, bytes_read, dst_file);
            bytes_left -= bytes_to_process;
            if (UpdateBytesProcessedProgress())
                throw E_EABORTED;
        }
    }
}

void ReadingArchive::SetFileTime(const wstr_view& file_path, uint32_t file_time)
{
    HANDLE file_handle = CreateFileW(
        file_path.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file_handle == INVALID_HANDLE_VALUE)
        return;
    std::unique_ptr<HANDLE, CloseHandleDeleter> file(file_handle);

    WORD dos_date = (WORD)(file_time >> 16);
    WORD dos_time = (WORD)(file_time);

    FILETIME winapi_local_file_time;
    BOOL b = DosDateTimeToFileTime(dos_date, dos_time, &winapi_local_file_time);
    if (!b)
        return;

    FILETIME winapi_file_time;
    b = LocalFileTimeToFileTime(&winapi_local_file_time, &winapi_file_time);
    if (!b)
        return;

    ::SetFileTime(file_handle, &winapi_file_time, &winapi_file_time, &winapi_file_time);
}

void PackingArchive::WriteEntryHeader(const EntryHeader& header, const wstr_view& path)
{
    if(header.attributes & FILE_ATTR_DIRECTORY)
        assert(header.pack_size == 0);

    FILE* archive_file_ptr = archive_file_.get();
    WriteOrThrow(&header, sizeof(header), 1, archive_file_ptr);
    WriteOrThrow(path.data(), sizeof(wchar_t), path.length(), archive_file_ptr);
}

void PackingArchive::PackFileContent(
    uint64_t& out_bytes_written, uint64_t& out_bytes_read,
    FILE* dst_file, FILE* src_file, uint64_t src_file_size, bool enable_compression)
{
    out_bytes_written = 0;
    out_bytes_read = 0;

    if(enable_compression)
    {
        z_stream zlib_stream;
        ZeroMemory(&zlib_stream, sizeof(zlib_stream));
        int zlib_result = deflateInit(&zlib_stream, Z_DEFAULT_COMPRESSION);
        ZlibResultToWcxException(zlib_result);
        std::unique_ptr<z_stream, DeflateEndDeleter> zlib_stream_ptr(&zlib_stream);

        std::vector<char> src_buf(kBufSize), dstBuf(kBufSize);
        char* src_buf_ptr = src_buf.data();
        char* dst_buf_ptr = dstBuf.data();

        bool is_src_end = false;
        for(;;)
        {
            bool made_progress = false;

            // If the source buffer is empty, read more data from the source file.
            if(zlib_stream.avail_in == 0 && !is_src_end)
            {
                size_t bytes_read = fread(src_buf_ptr, 1, kBufSize, src_file);
                if(bytes_read < kBufSize)
                {
                    if(feof(src_file))
                        is_src_end = true;
                    else
                        throw E_EREAD;
                }
                zlib_stream.next_in = (Bytef*)src_buf_ptr;
                zlib_stream.avail_in = (uInt)bytes_read;
                out_bytes_read += bytes_read;
                made_progress = true;
            }

            // Prepare destination buffer.
            zlib_stream.next_out = (Bytef*)dst_buf_ptr;
            zlib_stream.avail_out = (uInt)kBufSize;
                
            // Compress!
            zlib_result = deflate(&zlib_stream, is_src_end ? Z_FINISH : Z_NO_FLUSH);
            if(zlib_result != Z_OK && zlib_result != Z_STREAM_END)
                ZlibResultToWcxException(zlib_result);

            // If any destination data has been produced, write it to the destination file.
            if(zlib_stream.avail_out < kBufSize)
            {
                size_t bytes_to_write = kBufSize - zlib_stream.avail_out;
                WriteOrThrow(dst_buf_ptr, 1, bytes_to_write, dst_file);
                out_bytes_written += bytes_to_write;
                made_progress = true;
            }

            if(zlib_result == Z_STREAM_END)
                break;
            if(!made_progress)
                throw E_BAD_ARCHIVE;
        }
    }
    else
    {
        if(src_file_size == 0)
            return;

        std::vector<char> buf(kBufSize);
        char* buf_ptr = buf.data();
        size_t bytes_read = 0;
        do
        {
            bytes_read = fread(buf_ptr, 1, kBufSize, src_file);
            if(bytes_read < kBufSize && !feof(src_file))
                throw E_EREAD;

            if(bytes_read)
            {
                WriteOrThrow(buf_ptr, 1, bytes_read, dst_file);
                out_bytes_read += bytes_read;
            }
        }
        while(bytes_read == kBufSize);
        out_bytes_written = out_bytes_read;
    }

    if(out_bytes_read != src_file_size)
        throw E_EREAD;
}

void PackingArchive::GetFileAttributes(EntryHeader& header, const wstr_view& full_path)
{
    header.unp_size = 0;
    header.time = 0;
    header.attributes = 0;

    WIN32_FILE_ATTRIBUTE_DATA windows_attr;
    BOOL b = ::GetFileAttributesExW(full_path.c_str(), GetFileExInfoStandard, &windows_attr);
    if(!b)
        throw E_EREAD;

    if((windows_attr.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
        header.unp_size = windows_attr.nFileSizeLow | ((uint64_t)windows_attr.nFileSizeHigh << 32);

    header.attributes = WindowsAttributesToWcxAttributes(windows_attr.dwFileAttributes);

    FILETIME local_time;
    b = FileTimeToLocalFileTime(&windows_attr.ftLastWriteTime, &local_time);
    if(!b)
        throw E_UNKNOWN_FORMAT;
    WORD dos_date, dos_time;
    b = FileTimeToDosDateTime(&local_time, &dos_date, &dos_time);
    if(!b)
        throw E_UNKNOWN_FORMAT;
    header.time = ((uint32_t)dos_date << 16) | (uint32_t)dos_time;
}

bool DeletingArchive::ShouldDelete(const wstr_view& curr_path, std::span<const std::wstring> paths_to_delete)
{
    auto final_curr_path = curr_path.to_string();
    UpperCase(final_curr_path);

    while(!final_curr_path.empty())
    {
        const auto it = std::lower_bound(
            paths_to_delete.begin(), paths_to_delete.end(), final_curr_path);
        if(it != paths_to_delete.end() && *it == final_curr_path)
            return true;
        UpDir(final_curr_path);
    }

    return false;
}

uint64_t ArchiveBase::GetFileSize(FILE* f)
{
    SeekOrThrow(f, 0, SEEK_END);
    uint64_t size = (uint64_t)_ftelli64(f);
    SeekOrThrow(f, 0, SEEK_SET);
    return size;
}

int ArchiveBase::CallProcessDataProc(wchar_t* file_name, int size)
{
    if (process_data_proc_)
        return (*process_data_proc_)(file_name, size);
    else if (g_global_process_data_proc)
        return (*g_global_process_data_proc)(file_name, size);
    else
        return 1;
}

bool ArchiveBase::UpdateBytesProcessedProgress()
{
    const uint64_t curr_time = GetTickCount64();
    if (curr_time > last_progress_time_ + kProgressUpdateIntervalMilliseconds)
    {
        const int size = (bytes_processed_since_previous_progress_ > (uint64_t)INT_MAX) ?
            INT_MAX : (int)bytes_processed_since_previous_progress_;
        const int result = CallProcessDataProc(nullptr, size);
        bytes_processed_since_previous_progress_ = 0;
        last_progress_time_ = curr_time;
        return result == 0; // 0 means user cancel.
    }
    return false;
}

bool ArchiveBase::UpdateDirectProgress(wchar_t* file_name, int size)
{
    uint64_t curr_time = GetTickCount64();
    if (curr_time > last_progress_time_ + kProgressUpdateIntervalMilliseconds)
    {
        int r = CallProcessDataProc(file_name, size);
        last_progress_time_ = curr_time;
        return r == 0; // 0 means user cancel.
    }
    return false;
}

void ArchiveBase::ReadAndCheckHeader()
{
    constexpr size_t header_len = kFileHeader.size();
    char header[header_len];
    ReadOrThrow(header, 1, header_len, archive_file_.get());
    if (memcmp(kFileHeader.data(), header, header_len) != 0)
        throw E_BAD_ARCHIVE;
    bytes_processed_since_previous_progress_ += header_len;
}

bool ArchiveBase::ReadEntryHeader()
{
    last_header_ = EntryHeader{};
    FILE* const archive_file_ptr = archive_file_.get();

    size_t read_count = fread(&last_header_, sizeof(last_header_), 1, archive_file_ptr);
    if(read_count == 0)
        return false;
    if (last_header_.magic != kEntryMagic)
        throw E_BAD_ARCHIVE;
    bytes_processed_since_previous_progress_ += sizeof(last_header_);

    const size_t path_len = last_header_.path_len;
    if (path_len == 0)
        throw E_BAD_ARCHIVE;
    if (path_len > kMaxFileNameLen - 1)
        throw E_SMALL_BUF;
    wchar_t name_buf[kMaxFileNameLen];
    ReadOrThrow(name_buf, sizeof(wchar_t), path_len, archive_file_ptr);
    bytes_processed_since_previous_progress_ += path_len;
    last_header_path_.assign(name_buf, name_buf + path_len);

    return true;
}

int PackingArchive::PackFilesW(wchar_t* packedFile, wchar_t* subPath, wchar_t* srcPath,
    wchar_t* addList, int flags)
{
    const bool delete_source_files = (flags & PK_PACK_MOVE_FILES) != 0;
    const bool save_paths = (flags & PK_PACK_SAVE_PATHS) != 0;

    /*
    save_paths == false is a special mode, enabled when in Files \ Pack dialog you
    don't check "Also pack path names (only recursed)" field. In this mode, we have
    to pack only files, all on the same level, without directories or any directory
    structure.

    According to my experiments with ZIP format, in this mode:

    - subPath should still work - files should be packed into indicated subdirectory
    in the archive.
    - New directories in the archive should not be created, only files, all on the
    same level.
    - When delete_source_files == true, source directories should not be deleted, only
    files.
    */

    if(CallProcessDataProc(packedFile, 0) == 0)
        throw E_EABORTED;
    last_progress_time_ = GetTickCount64();

    std::vector<std::wstring> relative_paths_to_add;
    std::wstring add_list_path;
    while(*addList != L'\0')
    {
        add_list_path = addList;

        /*
        In the special mode when !savePaths, process only files and not directories.
        Directories have trailing '\\' in their paths.
        */
        if(save_paths || add_list_path.back() != L'\\')
        {
            StripTrailingSlash(add_list_path);
            assert(!add_list_path.empty());
            relative_paths_to_add.push_back(add_list_path);
        }

        addList += wcslen(addList) + 1;
    }

    /*
    In the mode when save_paths == false, we have to remove duplicates because
    different subdirectories can contain files with same name that would be packed
    to same archive directory.
    */
    if(!save_paths)
        RemoveFileNameDuplicates(relative_paths_to_add);

    std::sort(relative_paths_to_add.begin(), relative_paths_to_add.end(), StricmpPred());

    std::vector<std::wstring> archive_paths_to_add(relative_paths_to_add.size());
    std::transform(
        relative_paths_to_add.begin(), relative_paths_to_add.end(),
        archive_paths_to_add.begin(),
        [save_paths, subPath](const std::wstring& relative_path) -> std::wstring
        {
            std::wstring archive_path;
            if(save_paths)
                archive_path = CombinePath(subPath, relative_path);
            else
            {
                auto name = ExtractFileName(relative_path);
                archive_path = CombinePath(subPath, name);
            }
            return archive_path;
        });

    std::vector<bool> path_is_directory(relative_paths_to_add.size());
    std::fill(path_is_directory.begin(), path_is_directory.end(), false);

    OpenForPack(packedFile);

    if(!created_new_archive_)
    {
        ReadAndCheckHeader();

        DeleteIf([this, &archive_paths_to_add]() -> bool
            {
                auto it = std::lower_bound(archive_paths_to_add.begin(), archive_paths_to_add.end(), last_header_path_, StricmpPred());
                return it != archive_paths_to_add.end() &&
                    _wcsicmp(last_header_path_.c_str(), it->c_str()) == 0;
            });
    }

    std::wstring absolute_path;
    for(size_t i = 0, count = relative_paths_to_add.size(); i < count; ++i)
    {
        const std::wstring& relative_path = relative_paths_to_add[i];
        const std::wstring& archive_path = archive_paths_to_add[i];

        absolute_path = CombinePath(srcPath, relative_path);
        assert(!absolute_path.empty());

        size_t file_count_percent = CalcPercent(i, count);
        int progress = -(int)file_count_percent;
        if(UpdateDirectProgress(const_cast<wchar_t*>(absolute_path.c_str()), progress))
            throw E_EABORTED;

        bool is_directory = false;
        PackFile(is_directory, absolute_path, archive_path, save_paths);
        path_is_directory[i] = is_directory;
    }

    if(delete_source_files)
    {
        // Items must be deleted in reverse order so files and subdirectories are
        // deleted before parent directories.
        for(size_t i = relative_paths_to_add.size(); i--; )
        {
            absolute_path = CombinePath(srcPath, relative_paths_to_add[i]);
            assert(!absolute_path.empty());
            DeleteSrcFile(absolute_path, path_is_directory[i]);
        }
    }

    return 0;
}

void PackingArchive::OpenForPack(const wstr_view& archive_path)
{
    // Open existing file for modification.
    FILE* f = nullptr;
    errno_t e = _wfopen_s(&f, archive_path.c_str(), L"r+b");
    if (e == 0)
    {
        archive_file_.reset(f);
        created_new_archive_ = false;
        original_archive_size_ = GetFileSize(f);
        return;
    }

    // Create new archive.
    e = _wfopen_s(&f, archive_path.c_str(), L"wb");
    if (e == 0)
    {
        archive_file_.reset(f);
        created_new_archive_ = true;
        original_archive_size_ = 0;
        // Write file header.
        WriteOrThrow(kFileHeader.data(), 1, kFileHeader.length(), f);
        return;
    }

    throw E_ECREATE;
}

void PackingArchive::PackFile(bool& out_is_directory, const wstr_view& absolute_path,
    const wstr_view& archive_path, bool save_paths)
{
    out_is_directory = false;

    auto path = archive_path.to_string();
    StripTrailingSlash(path);

    EntryHeader entry_header = {};

    GetFileAttributes(entry_header, absolute_path);

    entry_header.pack_size = entry_header.unp_size;

    const bool enable_compression_for_file = EnableCompressionForFile(entry_header.unp_size);

    if (enable_compression_for_file)
        entry_header.flags |= kEntryFlagCompressed;

    out_is_directory = (entry_header.attributes & FILE_ATTR_DIRECTORY) != 0;

    uint64_t entry_begin_offset = (uint64_t)_ftelli64(archive_file_.get());
    
    entry_header.magic = kEntryMagic;
    
    assert(path.length() <= USHRT_MAX);
    entry_header.path_len = (uint16_t)path.length();
    
    WriteEntryHeader(entry_header, path);

    // Write file contents.
    if (!out_is_directory)
    {
        bool cancelled = false;

        FILE* src_file_ptr = nullptr;
        errno_t e = _wfopen_s(&src_file_ptr, absolute_path.c_str(), L"rb");
        if (e != 0)
            throw E_EOPEN;
        UniqueFilePtr src_file(src_file_ptr);

        FILE* archive_file_ptr = archive_file_.get();

        uint64_t bytes_written = 0;
        uint64_t bytes_read = 0;
        PackFileContent(
            bytes_written, bytes_read,
            archive_file_ptr, src_file_ptr, entry_header.unp_size, enable_compression_for_file);

        if (cancelled)
            throw E_EABORTED;

        if (enable_compression_for_file)
        {
            if (bytes_written != bytes_read)
            {
                // Update pack_size in entry header.
                uint64_t entry_end_offset = (uint64_t)_ftelli64(archive_file_ptr);
                SeekOrThrow(archive_file_ptr, entry_begin_offset +
                    sizeof(kEntryMagic) +
                    sizeof(uint8_t) + // flags
                    sizeof(uint8_t) + // attributes
                    sizeof(uint32_t), // time
                    SEEK_SET);
                WriteOrThrow(&bytes_written, sizeof(bytes_written), 1, archive_file_ptr);
                SeekOrThrow(archive_file_ptr, entry_end_offset, SEEK_SET);
            }
        }
        else
            assert(bytes_written == bytes_read);
    }
}

void PackingArchive::DeleteSrcFile(const wstr_view& path, bool is_directory)
{
    BOOL b = FALSE;
    if (is_directory)
        b = ::RemoveDirectoryW(path.c_str());
    else
        b = ::DeleteFileW(path.c_str());
    if (!b)
        throw E_EWRITE;
}

int DeletingArchive::DeleteFilesW(wchar_t* packedFile, wchar_t* deleteList)
{
    if (CallProcessDataProc(packedFile, 0) == 0)
        throw E_EABORTED;
    last_progress_time_ = GetTickCount64();

    // Upper-case, sorted.
    std::vector<std::wstring> paths_to_delete;

    while (*deleteList != '\0')
    {
        std::wstring path = deleteList;
        if (path.ends_with(L"*.*"))
            path.erase(path.length() - 3);
        StripTrailingSlash(path);
        assert(!path.empty());
        UpperCase(path);
        paths_to_delete.push_back(path);

        deleteList += wcslen(deleteList) + 1;
    }

    std::sort(paths_to_delete.begin(), paths_to_delete.end());

    if (paths_to_delete.empty())
        return 0;

    OpenForDelete(packedFile);
    ReadAndCheckHeader();

    DeleteIf([this, &paths_to_delete]() -> bool
        {
            return ShouldDelete(last_header_path_, paths_to_delete);
        });

    return 0;
}

void DeletingArchive::OpenForDelete(const wstr_view& archive_path)
{
    FILE* f = nullptr;
    errno_t e = _wfopen_s(&f, archive_path.c_str(), L"r+b");
    if (e != 0)
        throw E_ECREATE;
    archive_file_.reset(f);
    original_archive_size_ = GetFileSize(f);
}

template<typename Pred>
void ArchiveBase::DeleteIf(Pred pred)
{
    FILE* archive_file_ptr = archive_file_.get();
    assert(archive_file_ptr);

    for (;;)
    {
        long long entry_begin_offset = 0;
        for (;;)
        {
            entry_begin_offset = _ftelli64(archive_file_ptr);
            if (!ReadEntryHeader())
                return;
            if (last_header_.flags & kEntryFlagDeleted)
            {
                // Skip contents and read header again.
                if (last_header_.pack_size != 0)
                    SeekOrThrow(archive_file_.get(), (long long)last_header_.pack_size, SEEK_CUR);
            }
            else
                // Header of non-deleted entry read successfully.
                break;
        }
        if (pred())
        {
            long long content_begin_offset = _ftelli64(archive_file_ptr);
            // Set offset to Flags.
            SeekOrThrow(archive_file_ptr, entry_begin_offset +
                sizeof(uint32_t), // For Magic.
                SEEK_SET);
            // Write new flags.
            uint8_t new_flags = last_header_.flags | kEntryFlagDeleted;
            WriteOrThrow(&new_flags, sizeof(new_flags), 1, archive_file_ptr);
            // Go back to content begin.
            SeekOrThrow(archive_file_ptr, content_begin_offset, SEEK_SET);
        }
        // Skip file content.
        if (last_header_.pack_size > 0)
            SeekOrThrow(archive_file_ptr, (long long)last_header_.pack_size, SEEK_CUR);

        uint64_t progress_percent = CalcPercent((uint64_t)entry_begin_offset, original_archive_size_);
        progress_percent = std::min(100ull, progress_percent);
        int progress = -(int)progress_percent;
        if (UpdateDirectProgress(nullptr, progress))
            throw E_EABORTED;
    }
}

BOOL HeaderCheckingArchive::CanYouHandleThisFileW(const wchar_t* filePath)
{
    FILE* f = nullptr;
    errno_t e = _wfopen_s(&f, filePath, L"rb");
    if (e != 0)
        throw E_EOPEN;
    archive_file_.reset(f);

    ReadAndCheckHeader();

    return TRUE;
}
