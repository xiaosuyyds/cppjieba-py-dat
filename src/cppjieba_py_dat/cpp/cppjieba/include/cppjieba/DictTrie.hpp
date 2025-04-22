#pragma once

#include <string>
#include <vector>
#include <fstream> // for std::ifstream check_user_file
#include <stdexcept> // for std::runtime_error, std::invalid_argument
#include "limonp/Logging.hpp"
#include "limonp/StringUtil.hpp" // for Split
#include "DatTrie.hpp" // 包含 DatTrie.hpp

#if defined(_WIN32) || defined(_WIN64)

#include <windows.h>
#include <shlwapi.h> // For PathCombineA, PathFileExistsA, PathIsDirectoryA
#pragma comment(lib, "shlwapi.lib") // Link against shlwapi.lib for Path* functions
#include <direct.h> // For _mkdir
#define MKDIR(path) _mkdir(path)
#define PATH_SEPARATOR '\\'

#else // Linux, macOS, etc.

#include <sys/stat.h> // For mkdir, stat
#include <sys/types.h>
#include <cerrno> // For errno
#include <cstring> // For strerror
#define MKDIR(path) mkdir(path, 0755) // 0755 permissions
#define PATH_SEPARATOR '/'
#endif

#include "limonp/StringUtil.hpp"
#include "limonp/Logging.hpp"
#include "Unicode.hpp"
#include "DatTrie.hpp"

namespace cppjieba {

using namespace limonp;

const double MIN_DOUBLE = -3.14e+100;
const double MAX_DOUBLE = 3.14e+100;
const size_t DICT_COLUMN_NUM = 3;
const char* const UNKNOWN_TAG = "";

class DictTrie {
public:
    enum UserWordWeightOption {
        WordWeightMin,
        WordWeightMedian,
        WordWeightMax,
    }; // enum UserWordWeightOption

    DictTrie(const string& dict_path, const string& user_dict_paths = "", const string & dat_cache_path = "",
             UserWordWeightOption user_word_weight_opt = WordWeightMedian) {
        Init(dict_path, user_dict_paths, dat_cache_path, user_word_weight_opt);
    }

    ~DictTrie() {}

    const DatMemElem* Find(const string & word) const {
        return dat_.Find(word);
    }

    void Find(RuneStrArray::const_iterator begin,
              RuneStrArray::const_iterator end,
              vector<struct DatDag>&res,
              size_t max_word_len = MAX_WORD_LENGTH) const {
        dat_.Find(begin, end, res, max_word_len);
    }

    bool IsUserDictSingleChineseWord(const Rune& word) const {
        return IsIn(user_dict_single_chinese_word_, word);
    }

    double GetMinWeight() const {
        return dat_.GetMinWeight();
    }

    size_t GetTotalDictSize() const {
        return total_dict_size_;
    }

    void InserUserDictNode(const string& line, bool saveNodeInfo = true) {
        vector<string> buf;
        DatElement node_info;
        Split(line, buf, " ");

        if (buf.size() == 0) {
            return;
        }

        node_info.word = buf[0];
        node_info.weight = user_word_default_weight_;
        node_info.tag = UNKNOWN_TAG;

        if (buf.size() == 2) {
            node_info.tag = buf[1];
        } else if (buf.size() == 3) {
            if (freq_sum_ > 0.0) {
                const int freq = atoi(buf[1].c_str());
                node_info.weight = log(1.0 * freq / freq_sum_);
                node_info.tag = buf[2];
            }
        }

        if (saveNodeInfo) {
            static_node_infos_.push_back(node_info);
        }

        if (Utf8CharNum(node_info.word) == 1) {
            RuneArray word;

            if (DecodeRunesInString(node_info.word, word)) {
                user_dict_single_chinese_word_.insert(word[0]);
            } else {
                XLOG(ERROR) << "Decode " << node_info.word << " failed.";
            }
        }
    }

    void LoadUserDict(const string& filePaths, bool saveNodeInfo = true) {
        vector<string> files = limonp::Split(filePaths, "|;");

        for (size_t i = 0; i < files.size(); i++) {
            ifstream ifs(files[i].c_str());
            XCHECK(ifs.is_open()) << "open " << files[i] << " failed";
            string line;

            for (; getline(ifs, line);) {
                if (line.size() == 0) {
                    continue;
                }

                InserUserDictNode(line, saveNodeInfo);
            }
        }
    }


private:
    void Init(const string& dict_path, const string& user_dict_paths, const string& dat_cache_dir,
              UserWordWeightOption user_word_weight_opt) {
        if (dict_path.empty()) {
             XLOG(ERROR) << "Main dictionary path cannot be empty.";
             throw std::invalid_argument("Main dictionary path cannot be empty.");
        }

        string dict_files = dict_path;
        if (!user_dict_paths.empty()) {
            vector<string> user_files = limonp::Split(user_dict_paths, "|;");
            for(const auto& user_file : user_files) {
                std::ifstream check_user_file(user_file);
                if (!check_user_file.good()) {
                    XLOG(WARNING) << "User dictionary file not found or not readable: " << user_file;
                }
            }
            dict_files += ";" + user_dict_paths;
        }

        size_t file_size_sum = 0;
        XLOG(DEBUG) << "Calculating MD5 for dictionary files: " << dict_files;
        const string md5 = CalcFileListMD5(dict_files, file_size_sum);
        if (md5.empty() || file_size_sum == 0) {
            XLOG(ERROR) << "Failed to calculate MD5 or total file size is zero for dictionaries: " << dict_files;
            throw std::runtime_error("Failed to process dictionary files for MD5 calculation.");
        }
        XLOG(DEBUG) << "Calculated MD5: " << md5 << ", Total size: " << file_size_sum;


        // --- 使用平台特定 API 构建完整的目标 DAT 文件路径 ---
        string dat_file_path; // 最终的 .dat 文件路径
        if (!dat_cache_dir.empty()) {
            bool cache_dir_exists = false;
            bool cache_dir_is_dir = false;

            #if defined(_WIN32) || defined(_WIN64)
                DWORD fileAttr = GetFileAttributesA(dat_cache_dir.c_str());
                if (fileAttr != INVALID_FILE_ATTRIBUTES) {
                    cache_dir_exists = true;
                    cache_dir_is_dir = (fileAttr & FILE_ATTRIBUTE_DIRECTORY);
                }
            #else
                struct stat st;
                if (stat(dat_cache_dir.c_str(), &st) == 0) {
                    cache_dir_exists = true;
                    cache_dir_is_dir = S_ISDIR(st.st_mode);
                }
            #endif

            if (!cache_dir_exists) {
                XLOG(DEBUG) << "Cache directory does not exist, attempting to create: " << dat_cache_dir;
                // 尝试创建目录 (Windows 的 _mkdir 需要逐级创建，这里简化处理，假设父目录存在)
                // 更健壮的方式是循环创建父目录
                int ret = MKDIR(dat_cache_dir.c_str());
                if (ret != 0
                #if defined(_WIN32) || defined(_WIN64)
                    // On Windows, mkdir returns 0 on success, EEXIST if exists, ENOENT if path not found
                     && GetLastError() != ERROR_ALREADY_EXISTS // Allow if already exists
                #else
                     && errno != EEXIST // Allow if already exists
                #endif
                   ) {
                    XLOG(ERROR) << "Failed to create cache directory: " << dat_cache_dir << " Error: " << strerror(errno);
                } else {
                    cache_dir_is_dir = true; // Assume success means it's now a directory
                }
            }

            if (cache_dir_is_dir) {
                string file_name = "jieba_" + md5 + "_" + to_string(user_word_weight_opt) + ".dat";
                #if defined(_WIN32) || defined(_WIN64)
                    char full_path[MAX_PATH];
                    if (PathCombineA(full_path, dat_cache_dir.c_str(), file_name.c_str())) {
                        dat_file_path = full_path;
                    } else {
                         XLOG(ERROR) << "PathCombine failed for DAT path.";
                    }
                #else
                    // 确保目录路径以 / 结尾
                    string dir_path_with_sep = dat_cache_dir;
                    if (dir_path_with_sep.empty() || dir_path_with_sep.back() != PATH_SEPARATOR) {
                        dir_path_with_sep += PATH_SEPARATOR;
                    }
                    dat_file_path = dir_path_with_sep + file_name;
                #endif
                if (!dat_file_path.empty()) {
                     XLOG(DEBUG) << "Using DAT cache file path: " << dat_file_path;
                }
            } else {
                 XLOG(ERROR) << "Provided cache path is not a directory or could not be created: " << dat_cache_dir;
            }
        }

        if (dat_file_path.empty()) {
             XLOG(WARNING) << "DAT cache path is invalid or empty, DAT caching disabled.";
             // !!! 重要: 如果你的实现必须依赖 DAT 缓存文件才能工作，这里应该抛出异常 !!!
             // throw std::runtime_error("Failed to determine a valid DAT cache file path.");
             // 如果可以无缓存运行（每次重新解析文本词典），则需要添加相应逻辑。
             // 假设当前版本必须使用 DAT 文件：
             throw std::runtime_error("Valid DAT cache path could not be determined.");
        }
        // --- 路径构建结束 ---


        if (dat_.InitAttachDat(dat_file_path, md5)) {
            XLOG(DEBUG) << "Successfully attached DAT cache file: " << dat_file_path;
            LoadUserDict(user_dict_paths, false);
            total_dict_size_ = file_size_sum;
            return; // 初始化成功
        }

        XLOG(DEBUG) << "DAT cache file not found or invalid, rebuilding: " << dat_file_path;

        static_node_infos_.clear();
        user_dict_single_chinese_word_.clear();

        LoadDefaultDict(dict_path);
        if (static_node_infos_.empty()) {
             XLOG(ERROR) << "Failed to load default dictionary: " << dict_path;
             throw std::runtime_error("Failed to load default dictionary.");
        }

        freq_sum_ = CalcFreqSum(static_node_infos_);
        CalculateWeight(static_node_infos_, freq_sum_);
        double min_weight = 0;
        SetStaticWordWeights(user_word_weight_opt, min_weight);
        dat_.SetMinWeight(min_weight);

        if (!user_dict_paths.empty()) {
            LoadUserDict(user_dict_paths, true);
        }

        bool build_ret = dat_.InitBuildDat(static_node_infos_, dat_file_path, md5);

        if (!build_ret) {
             XLOG(ERROR) << "Failed to build and attach DAT cache after building: " << dat_file_path;
             throw std::runtime_error("Failed to initialize DictTrie with DAT cache after building.");
        }
        XLOG(DEBUG) << "Successfully built and attached DAT cache: " << dat_file_path;

        total_dict_size_ = file_size_sum;
        vector<DatElement>().swap(static_node_infos_);
    }

    void LoadDefaultDict(const string& filePath) {
        ifstream ifs(filePath.c_str());
        XCHECK(ifs.is_open()) << "open " << filePath << " failed.";
        string line;
        vector<string> buf;

        for (; getline(ifs, line);) {
            Split(line, buf, " ");
            XCHECK(buf.size() == DICT_COLUMN_NUM) << "split result illegal, line:" << line;
            DatElement node_info;
            node_info.word = buf[0];
            node_info.weight = atof(buf[1].c_str());
            node_info.tag = buf[2];
            static_node_infos_.push_back(node_info);
        }
    }

    static bool WeightCompare(const DatElement& lhs, const DatElement& rhs) {
        return lhs.weight < rhs.weight;
    }

    void SetStaticWordWeights(UserWordWeightOption option, double & min_weight) {
        XCHECK(!static_node_infos_.empty());
        vector<DatElement> x = static_node_infos_;
        sort(x.begin(), x.end(), WeightCompare);
        if(x.empty()){
            return;
        }
        min_weight = x[0].weight;
        const double max_weight_ = x[x.size() - 1].weight;
        const double median_weight_ = x[x.size() / 2].weight;

        switch (option) {
            case WordWeightMin:
                user_word_default_weight_ = min_weight;
                break;

            case WordWeightMedian:
                user_word_default_weight_ = median_weight_;
                break;

            default:
                user_word_default_weight_ = max_weight_;
                break;
        }
    }

    double CalcFreqSum(const vector<DatElement>& node_infos) const {
        double sum = 0.0;

        for (size_t i = 0; i < node_infos.size(); i++) {
            sum += node_infos[i].weight;
        }

        return sum;
    }

    void CalculateWeight(vector<DatElement>& node_infos, double sum) const {
        for (size_t i = 0; i < node_infos.size(); i++) {
            DatElement& node_info = node_infos[i];
            assert(node_info.weight > 0.0);
            node_info.weight = log(double(node_info.weight) / sum);
        }
    }

private:
    vector<DatElement> static_node_infos_;
    size_t total_dict_size_ = 0;
    DatTrie dat_;

    double freq_sum_;
    double user_word_default_weight_;
    unordered_set<Rune> user_dict_single_chinese_word_;
};
}

