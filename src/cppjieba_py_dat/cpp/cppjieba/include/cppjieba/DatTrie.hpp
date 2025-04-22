#pragma once

#include <stdint.h>
#if defined(_WIN32) || defined(_WIN64)
#    include <shlwapi.h>
#    include <windows.h>
#else
#    include <sys/mman.h>
#    include <unistd.h>
#endif
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <algorithm>
#include <utility>
#include <stdexcept>

#include "Unicode.hpp"
#include "darts.h"
#include "limonp/Md5.hpp"

namespace cppjieba {

#if defined(_WIN32) || defined(_WIN64)
// RAII helpers for HANDLEs
const auto CloseHandleFunc = [](HANDLE h) { ::CloseHandle(h); };
typedef std::unique_ptr<void, decltype(CloseHandleFunc)> UniqueCloseHandlePtr;

uint64_t GetFileSize(const std::string &fname, uint64_t *size) {
    WIN32_FILE_ATTRIBUTE_DATA attrs;
    if (GetFileAttributesEx(fname.c_str(), GetFileExInfoStandard, &attrs)) {
        ULARGE_INTEGER file_size;
        file_size.HighPart = attrs.nFileSizeHigh;
        file_size.LowPart = attrs.nFileSizeLow;
        *size = file_size.QuadPart;
        return 0;
    }
    return GetLastError();
}
#endif

using std::pair;

struct DatElement {
    string word;
    string tag;
    double weight = 0;

    bool operator<(const DatElement &b) const {
        if (word == b.word) {
            return this->weight > b.weight;
        }

        return this->word < b.word;
    }
};

inline std::ostream &operator<<(std::ostream &os, const DatElement &elem) {
    return os << "word=" << elem.word << "/tag=" << elem.tag << "/weight=" << elem.weight;
}

struct DatMemElem {
    double weight = 0.0;
    char tag[8] = {};

    void SetTag(const string &str) {
        memset(&tag[0], 0, sizeof(tag));
#if defined(_WIN32) || defined(_WIN64)
        strncpy_s(&tag[0], sizeof(tag), str.c_str(), str.size());
#else
        strncpy(&tag[0], str.c_str(), std::min(str.size(), sizeof(tag) - 1));
#endif
    }

    string GetTag() const { return &tag[0]; }
};

inline std::ostream &operator<<(std::ostream &os, const DatMemElem &elem) {
    return os << "/tag=" << elem.GetTag() << "/weight=" << elem.weight;
}

struct DatDag {
    limonp::LocalVector<pair<size_t, const DatMemElem *> > nexts;
    double max_weight;
    int max_next;
};

typedef Darts::DoubleArray JiebaDAT;

struct CacheFileHeader {
    char md5_hex[32] = {};
    double min_weight = 0;
    uint32_t elements_num = 0;
    uint32_t dat_size = 0;
};

static_assert(sizeof(DatMemElem) == 16, "DatMemElem length invalid");
static_assert((sizeof(CacheFileHeader) % sizeof(DatMemElem)) == 0, "DatMemElem CacheFileHeader length equal");

class DatTrie {
   public:
    DatTrie() {}
    ~DatTrie() {
#if defined(_WIN32) || defined(_WIN64)
        BOOL ret = ::UnmapViewOfFile(mmap_addr_);
        assert(ret);

        ret = ::CloseHandle(mmap_fd_);
        assert(ret);

        ret = ::CloseHandle(file_fd_);
        assert(ret);
#else

        ::munmap(mmap_addr_, mmap_length_);
        mmap_addr_ = nullptr;
        mmap_length_ = 0;

        ::close(mmap_fd_);
        mmap_fd_ = -1;
#endif
    }

    const DatMemElem *Find(const string &key) const {
        JiebaDAT::result_pair_type find_result;
        dat_.exactMatchSearch(key.c_str(), find_result);

        if ((0 == find_result.length) || (find_result.value < 0) || (find_result.value >= (int)elements_num_)) {
            return nullptr;
        }

        return &elements_ptr_[find_result.value];
    }

    void Find(RuneStrArray::const_iterator begin, RuneStrArray::const_iterator end, vector<struct DatDag> &res,
              size_t max_word_len) const {
        res.clear();
        res.resize(end - begin);
        const string text_str = EncodeRunesToString(begin, end);

        for (size_t i = 0, begin_pos = 0; i < size_t(end - begin); i++) {
            static const size_t max_num = 128;
            JiebaDAT::result_pair_type result_pairs[max_num] = {};
            std::size_t num_results = dat_.commonPrefixSearch(&text_str[begin_pos], &result_pairs[0], max_num);

            res[i].nexts.push_back(pair<size_t, const DatMemElem *>(i + 1, nullptr));

            for (std::size_t idx = 0; idx < num_results; ++idx) {
                auto &match = result_pairs[idx];

                if ((match.value < 0) || (match.value >= (int)elements_num_)) {
                    continue;
                }

                auto const char_num = Utf8CharNum(&text_str[begin_pos], match.length);

                if (char_num > max_word_len) {
                    continue;
                }

                auto pValue = &elements_ptr_[match.value];

                if (1 == char_num) {
                    res[i].nexts[0].second = pValue;
                    continue;
                }

                res[i].nexts.push_back(pair<size_t, const DatMemElem *>(i + char_num, pValue));
            }

            begin_pos += limonp::UnicodeToUtf8Bytes((begin + i)->rune);
        }
    }

    double GetMinWeight() const { return min_weight_; }

    void SetMinWeight(double d) { min_weight_ = d; }

    bool InitBuildDat(vector<DatElement> &elements, const string &dat_cache_file, const string &md5) {
        BuildDatCache(elements, dat_cache_file, md5);
        return InitAttachDat(dat_cache_file, md5);
    }

    bool InitAttachDat(const string &dat_cache_file, const string &md5) {
#if defined(_WIN32) || defined(_WIN64)
        const static DWORD fileFlags = FILE_ATTRIBUTE_READONLY | FILE_FLAG_RANDOM_ACCESS;
        HANDLE hFile =
            CreateFile(dat_cache_file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL, OPEN_EXISTING, fileFlags, NULL);

        if (INVALID_HANDLE_VALUE == hFile) {
            return false;
        }

        UniqueCloseHandlePtr fileGuard(hFile, CloseHandleFunc);

        uint64_t fileSize;
        uint64_t s = GetFileSize(dat_cache_file, &fileSize);
        if ((s != ERROR_SUCCESS) || (fileSize == 0)) {
            return false;
        }

        HANDLE hMap = CreateFileMapping(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
        if (!hMap) {
            return false;
        }

        UniqueCloseHandlePtr mapGuard(hMap, CloseHandleFunc);
        void *ptr = MapViewOfFileEx(hMap, FILE_MAP_READ, 0, 0, static_cast<SIZE_T>(fileSize), NULL);

        if (!ptr) {
            return false;
        }

        mmap_fd_ = hMap;
        file_fd_ = hFile;
        mapGuard.release();
        fileGuard.release();

        mmap_addr_ = static_cast<char *>(ptr);
        mmap_length_ = fileSize;
#else
        mmap_fd_ = ::open(dat_cache_file.c_str(), O_RDONLY);

        if (mmap_fd_ < 0) {
            return false;
        }

        const auto seek_off = ::lseek(mmap_fd_, 0, SEEK_END);
        assert(seek_off >= 0);
        mmap_length_ = seek_off;

        mmap_addr_ = reinterpret_cast<char *>(mmap(NULL, mmap_length_, PROT_READ, MAP_SHARED, mmap_fd_, 0));
        assert(MAP_FAILED != mmap_addr_);

#endif
        assert(mmap_length_ >= sizeof(CacheFileHeader));
        CacheFileHeader &header = *reinterpret_cast<CacheFileHeader *>(mmap_addr_);
        elements_num_ = header.elements_num;
        min_weight_ = header.min_weight;
        assert(sizeof(header.md5_hex) == md5.size());

        if (0 != memcmp(&header.md5_hex[0], md5.c_str(), md5.size())) {
            return false;
        }

        assert(mmap_length_ ==
               sizeof(header) + header.elements_num * sizeof(DatMemElem) + header.dat_size * dat_.unit_size());
        elements_ptr_ = (const DatMemElem *)(mmap_addr_ + sizeof(header));
        const char *dat_ptr = mmap_addr_ + sizeof(header) + sizeof(DatMemElem) * elements_num_;
        dat_.set_array(dat_ptr, header.dat_size);
        return true;
    }

   private:
    void BuildDatCache(vector<DatElement> &elements, const string &dat_cache_file, const string &md5) {
        std::sort(elements.begin(), elements.end());

        vector<const char *> keys_ptr_vec;
        vector<int> values_vec;
        vector<DatMemElem> mem_elem_vec;

        keys_ptr_vec.reserve(elements.size());
        values_vec.reserve(elements.size());
        mem_elem_vec.reserve(elements.size());

        CacheFileHeader header;
        header.min_weight = min_weight_;
        assert(sizeof(header.md5_hex) == md5.size());
        memcpy(&header.md5_hex[0], md5.c_str(), md5.size());

        for (size_t i = 0; i < elements.size(); ++i) {
            keys_ptr_vec.push_back(elements[i].word.data());
            values_vec.push_back(i);
            mem_elem_vec.push_back(DatMemElem());
            auto &mem_elem = mem_elem_vec.back();
            mem_elem.weight = elements[i].weight;
            mem_elem.SetTag(elements[i].tag);
        }

        XLOG(DEBUG) << "Building DAT for " << elements.size() << " elements."; // 添加日志
        auto const ret = dat_.build(keys_ptr_vec.size(), &keys_ptr_vec[0], NULL, &values_vec[0]);
        if (0 != ret) {
            XLOG(ERROR) << "Darts::DoubleArray::build failed with error code: " << ret;
            throw std::runtime_error("Failed to build Double-Array Trie."); // 抛出异常
        }
        XLOG(DEBUG) << "DAT build successful. DAT size: " << dat_.size();

        header.elements_num = mem_elem_vec.size();
        header.dat_size = dat_.size();

#if defined(_WIN32) || defined(_WIN64)
        {
            string sys_tmp_dir(MAX_PATH, '\0'), tmp_file(MAX_PATH, '\0');

            //  Gets the temp path env string
            auto dwRetVal = GetTempPath(MAX_PATH, &sys_tmp_dir[0]);
            assert(dwRetVal <= MAX_PATH && (dwRetVal > 0));

            //  Generates a temporary file name.
            auto uRetVal = GetTempFileName(sys_tmp_dir.c_str(), TEXT("dat_temp"), 0, &tmp_file[0]);
            tmp_file.erase(std::find(tmp_file.begin(), tmp_file.end(), '\0'), tmp_file.end());
            if (uRetVal == 0) { // 检查 GetTempFileName 是否成功
                 XLOG(ERROR) << "GetTempFileName failed. Error code: " << GetLastError();
                 throw std::runtime_error("Failed to get temporary file name.");
            }

            HANDLE hTempFile = CreateFile(tmp_file.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (hTempFile == INVALID_HANDLE_VALUE) { // 检查 CreateFile 是否成功
                XLOG(ERROR) << "CreateFile for temporary DAT cache failed: " << tmp_file << " Error code: " << GetLastError();
                throw std::runtime_error("Failed to create temporary DAT cache file.");
            }

            {
                UniqueCloseHandlePtr fileGuard(hTempFile, CloseHandleFunc);
                DWORD total_bytes = 0;

                auto append_write = [&total_bytes, &hTempFile](const char *buff, size_t len) {
                    DWORD dwBytesWritten = 0;
                    bool succ = WriteFile(hTempFile, buff, len, &dwBytesWritten, NULL);
                    assert(succ);
                    total_bytes += dwBytesWritten;
                };

                append_write((const char *)&header, sizeof(header));
                append_write((const char *)&mem_elem_vec[0], sizeof(mem_elem_vec[0]) * mem_elem_vec.size());
                append_write((const char *)dat_.array(), dat_.total_size());

                assert(total_bytes ==
                       (DWORD)(sizeof(header) + mem_elem_vec.size() * sizeof(mem_elem_vec[0]) + dat_.total_size()));
            }

            XLOG(DEBUG) << "Attempting to move temporary file [" << tmp_file << "] to target [" << dat_cache_file << "]";
            bool succ = MoveFileEx(tmp_file.c_str(), dat_cache_file.c_str(),
                                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH);
            if (!succ) { // 检查 MoveFileEx 是否成功
                DWORD lastError = GetLastError(); // 获取错误码
                XLOG(ERROR) << "MoveFileEx failed to rename temporary DAT cache from [" << tmp_file << "] to [" << dat_cache_file << "]. Error code: " << lastError;
                // 可以根据 lastError 给出更具体的错误信息提示
                std::string errorMsg = "Failed to rename temporary DAT cache file.";
                if (lastError == ERROR_ACCESS_DENIED) { // Error code 5
                    errorMsg += " (Access Denied)";
                } else if (lastError == ERROR_SHARING_VIOLATION) { // 例如文件被占用
                     errorMsg += " (Sharing Violation - file might be in use)";
                } else if (lastError == ERROR_PATH_NOT_FOUND) { // 目标路径不存在？
                     errorMsg += " (Target path not found)";
                } // 可以根据需要添加更多错误码判断
                throw std::runtime_error(errorMsg);
            }
            XLOG(DEBUG) << "Successfully moved temporary file to: " << dat_cache_file; // 确认成功日志
        }
#else
        {
            string tmp_filepath = string(dat_cache_file) + "_XXXXXX";
            ::umask(S_IWGRP | S_IWOTH);
            const int fd = ::mkstemp(&tmp_filepath[0]);
            if (fd < 0) { // 替换 assert
                XLOG(ERROR) << "mkstemp failed for temporary DAT cache. errno: " << errno;
                throw std::runtime_error("Failed to create temporary DAT cache file.");
            }
            ::fchmod(fd, 0644);

            ssize_t write_bytes = ::write(fd, (const char *)&header, sizeof(header));
            write_bytes += ::write(fd, (const char *)&mem_elem_vec[0], sizeof(mem_elem_vec[0]) * mem_elem_vec.size());
            write_bytes += ::write(fd, dat_.array(), dat_.total_size());

            assert(write_bytes ==
                   (ssize_t)(sizeof(header) + mem_elem_vec.size() * sizeof(mem_elem_vec[0]) + dat_.total_size()));
            ::close(fd);

            XLOG(DEBUG) << "Attempting to rename temporary file [" << tmp_filepath << "] to target [" << dat_cache_file << "]";
            const auto rename_ret = ::rename(tmp_filepath.c_str(), dat_cache_file.c_str());
            if (0 != rename_ret) {
                int err_no = errno; // 获取 errno
                XLOG(ERROR) << "rename failed for temporary DAT cache from [" << tmp_filepath << "] to [" << dat_cache_file << "]. errno: " << err_no << " (" << strerror(err_no) << ")";
                throw std::runtime_error("Failed to rename temporary DAT cache file.");
            }
            XLOG(DEBUG) << "Successfully renamed temporary file to: " << dat_cache_file;
        }
#endif
    XLOG(DEBUG) << "DAT cache file successfully written to: " << dat_cache_file;
    }

    DatTrie(const DatTrie &);
    DatTrie &operator=(const DatTrie &);

   private:
    JiebaDAT dat_;
    const DatMemElem *elements_ptr_ = nullptr;
    size_t elements_num_ = 0;
    double min_weight_ = 0;

#if defined(_WIN32) || defined(_WIN64)
    HANDLE mmap_fd_;
    HANDLE file_fd_;
#else
    int mmap_fd_ = -1;
#endif
    size_t mmap_length_ = 0;
    char *mmap_addr_ = nullptr;
};

inline string CalcFileListMD5(const string &files_list, size_t &file_size_sum) {
    limonp::MD5 md5;

    const auto files = limonp::Split(files_list, "|;");
    file_size_sum = 0;

    for (auto const &local_path : files) {
#if defined(_WIN32) || defined(_WIN64)
        const static DWORD fileFlags = FILE_ATTRIBUTE_READONLY | FILE_FLAG_SEQUENTIAL_SCAN;
        HANDLE hFile =
            CreateFile(local_path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                       OPEN_EXISTING, fileFlags, NULL);
        if (INVALID_HANDLE_VALUE == hFile) {
            XLOG(ERROR) << "Failed to open dictionary file (Win): " << local_path << " Error code: " << GetLastError();
            continue; // 继续处理下一个文件，但记录错误
        }
        UniqueCloseHandlePtr fileGuard(hFile, CloseHandleFunc);

        uint64_t len = 0;
        uint64_t s = GetFileSize(local_path, &len);
        if ((s != ERROR_SUCCESS) || (len == 0)) {
            continue;
        }

        HANDLE hMap = CreateFileMapping(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
        if (!hMap) {
            continue;
        }
        UniqueCloseHandlePtr mapGuard(hMap, CloseHandleFunc);

        void *addr = MapViewOfFileEx(hMap, FILE_MAP_READ, 0, 0, static_cast<SIZE_T>(len), NULL);
        if (!addr) {
            continue;
        }

        md5.Update((unsigned char *)addr, len);
        file_size_sum += len;

        BOOL ret = ::UnmapViewOfFile(addr);
        assert(ret);
#else
        const int fd = ::open(local_path.c_str(), O_RDONLY);
        if (fd < 0) {
            XLOG(ERROR) << "Failed to open dictionary file (Unix): " << local_path << " errno: " << errno;
            continue; // 继续处理下一个文件，但记录错误
        }
        auto const len = ::lseek(fd, 0, SEEK_END);
        if (len > 0) {
            void *addr = ::mmap(NULL, len, PROT_READ, MAP_SHARED, fd, 0);
            assert(MAP_FAILED != addr);

            md5.Update((unsigned char *)addr, len);
            file_size_sum += len;

            ::munmap(addr, len);
        }
        ::close(fd);
#endif
    }

    md5.Final();
    return string(md5.digestChars);
}

}  // namespace cppjieba
