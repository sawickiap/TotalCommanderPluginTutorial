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

#include "utils.hpp"

enum EntryFlag
{
    kEntryFlagDeleted    = 0x01,
    kEntryFlagCompressed = 0x02,
};

#pragma pack(push, 1)
struct EntryHeader
{
    // kEntryMagic.
    uint32_t magic;
    // Use EntryFlag constants.
    uint8_t flags;
    // File or directory attributes, in format used by WCX interface. See enum FILE_ATTR.
    uint8_t attributes;
    // Date and time of last modification, in format used by WCX interface.
    uint32_t time;
    // Packed data size in archive, in bytes.
    uint64_t pack_size;
    // Original (unpacked) data size, in bytes.
    uint64_t unp_size;
    // Length of the path.
    uint16_t path_len;
};
#pragma pack(pop)

extern tProcessDataProcW g_global_process_data_proc;

class ArchiveBase
{
public:
    virtual ~ArchiveBase() = default;

    void SetProcessDataProcW(tProcessDataProcW processDataProc) { process_data_proc_ = processDataProc; }

protected:
    // Leaves cursor to beginning of the file!
    static uint64_t GetFileSize(FILE* f);

    tProcessDataProcW process_data_proc_ = nullptr;
    UniqueFilePtr archive_file_;
    uint64_t original_archive_size_ = 0;
    uint64_t bytes_processed_since_previous_progress_ = 0;
    uint64_t last_progress_time_ = 0;
    EntryHeader last_header_ = {};
    std::wstring last_header_path_;

    // Returns 0 if user pressed Cancel button.
    int CallProcessDataProc(wchar_t* file_name, int size);
    // Returns true if user pressed Cancel button.
    bool UpdateBytesProcessedProgress();
    bool UpdateDirectProgress(wchar_t* file_name, int size);
    // Reads and checks the main file format header. If invalid, throws exception.
    void ReadAndCheckHeader();
    // Uses archive_file_ to read header into last_header_.
    // Returns false if end of file was reached and the header was not read.
    bool ReadEntryHeader();
    // archive_file_ is open for read and write. Cursor is at the beginning of an
    // entry. Loop over all entries until the end of archive. For each entry, if
    // predicate returns true, mark this entry as deleted. Predicate should read
    // last_header_.
    template<typename Pred>
    void DeleteIf(Pred pred);
};

class ReadingArchive : public ArchiveBase
{
public:
    void OpenArchiveW(tOpenArchiveDataW* archiveData);
    int ReadHeaderExW(tHeaderDataExW* headerData);
    int ProcessFileW(int operation, wchar_t* destPath, wchar_t* destName);

private:
    enum class ArchiveMode
    {
        kList,
        kExtract,
        kCount
    } mode_;

    void ExtractFile(const wstr_view& dest_path, const wstr_view& dest_name);
    void UnpackFileContent(FILE* dst_file, FILE* src_file,
        uint64_t dst_file_size, uint64_t src_file_size, bool enable_compression);
    // On failure does nothing, not throwing exception.
    static void SetFileTime(const wstr_view& file_path, uint32_t file_time);
};

class PackingArchive : public ArchiveBase
{
public:
    int PackFilesW(wchar_t* packedFile, wchar_t* subPath, wchar_t* srcPath,
        wchar_t* addList, int flags);

private:
    bool created_new_archive_ = false;

    // Opens archive_file_ for writing. Also sets original_archive_size_ and created_new_archive_.
    void OpenForPack(const wstr_view& archive_path);
    void PackFile(bool& out_is_directory, const wstr_view& absolute_path,
        const wstr_view& archive_path, bool save_paths);
    void DeleteSrcFile(const wstr_view& path, bool is_directory);
    // Fills members: UnpSize, Time, Flags.
    static void GetFileAttributes(EntryHeader& header, const wstr_view& full_path);
    void WriteEntryHeader(const EntryHeader& header, const wstr_view& path);
    void PackFileContent(
        uint64_t& out_bytes_written, uint64_t& out_bytes_read,
        FILE* dst_file, FILE* src_file, uint64_t src_file_size, bool enable_compression);
};

class DeletingArchive : public ArchiveBase
{
public:
    int DeleteFilesW(wchar_t *packedFile, wchar_t *deleteList);

private:
    static bool ShouldDelete(const wstr_view& curr_path, std::span<const std::wstring> paths_to_delete);

    void OpenForDelete(const wstr_view& archive_path);
};

class HeaderCheckingArchive : public ArchiveBase
{
public:
    BOOL CanYouHandleThisFileW(const wchar_t* filePath);
};
