# CppJieba-Py-Dat

[![PyPI version](https://badge.fury.io/py/cppjieba-py-dat.svg)](https://badge.fury.io/py/cppjieba-py-dat)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

基于 [byronhe/cppjieba](https://github.com/byronhe/cppjieba)（使用 Double-Array Trie 进行了内存优化）的 Python (PyBind11) 绑定。旨在提供与原版 [CppJieba](https://github.com/yanyiwu/cppjieba) 相似的功能，但具有显著降低的内存占用和快速的加载速度（通过 mmap）。

**请注意:** 由于本项目主要基于 DAT 优化版本的 CppJieba，其主要优势在于**大幅降低了相对于原版 CppJieba 的内存消耗**([来源](https://byronhe.com/post/2019/11/25/cppjieba-darts-DAT-memory_optimize/))，并利用 mmap 实现了快速加载和潜在的多进程内存共享。与纯 Python 的 `jieba` 库相比，虽然仍有内存和速度优势，但内存降低幅度可能不如对比原版 CppJieba 那样显著。

**特别说明:** 本项目的 Python 绑定代码很大程度上是在 AI 辅助下完成的。如果您在使用中发现任何错误或有改进建议，欢迎提交 Issue 和 PR！

~~本项目主要是作者自己写着用的，所以代码很垃，勿喷~~

## 主要特性

*   **较低内存占用:** 使用 Double-Array Trie (DAT) 替换原版 CppJieba 的内存 Trie，大幅降低词典加载内存（尤其对比原版 CppJieba）。
*   **快速加载:** 利用 `mmap` 加载预先构建的 DAT 缓存文件，实现近乎瞬时的初始化（非首次运行）。
*   **高性能:** 基于 C++ 实现，分词速度通常远快于纯 Python 实现。
*   **兼容 CppJieba 主要功能:** 支持精确模式、全模式、搜索引擎模式分词，以及词性标注和关键词提取。

## 与原版 `jieba` (Python) 和 `cppjieba-py` 的区别

*   **内存:** 通常比纯 Python `jieba` 和基于原版 CppJieba 的绑定占用更少内存。
*   **速度:** 分词速度通常快于纯 Python `jieba`，与基于原版 CppJieba 的绑定性能相当或取决于具体实现。
*   **初始化:**
    *   **首次运行/词典更新:** 需要构建 DAT 缓存文件，可能耗时几秒钟。
    *   **后续运行:** 加载速度极快。
*   **动态词典:** **不支持** 运行时动态添加用户词 (`add_word` / `InsertUserWord` 功能被移除)。如需更新用户词典，需要修改用户词典文件并**重新运行程序**（会自动检测到变更并重建 DAT 缓存）。
*   **依赖:** 需要 C++ 编译环境（如果从源码安装），但提供了预编译的 Wheel 包方便安装。

## 安装

**通过 pip 安装 (推荐):**

```bash
pip install cppjieba-py-dat
```

**从源码安装:**

如果想从源码安装，或者没有提供适合你平台的预编译 Wheel 包，你需要：

1.  一个支持 C++14 的 C++ 编译器 (例如 GCC, Clang, MSVC)。
2.  安装 Python 开发头文件 (例如 `python3-dev` on Debian/Ubuntu, `python3-devel` on Fedora/CentOS, 或 Visual Studio Build Tools C++ workload on Windows)。
3.  安装 `pybind11`: `pip install pybind11>=2.6`
4.  安装 `setuptools` 和 `wheel`: `pip install setuptools wheel build`
5.  克隆本仓库并构建：
    ```bash
    git clone https://github.com/xiaosuyydds/cppjieba-py-dat.git
    cd cppjieba-py-dat
    python -m build
    pip install dist/cppjieba_py_dat-*.whl
    ```

## 使用示例

```python
import cppjieba_py_dat as jieba
import os

# --- 初始化 (自动处理 DAT 缓存) ---
# 默认使用包内词典
j = jieba.Jieba()
print("Jieba initialized.")

# # 使用自定义用户词典
# user_dict = 'path/to/my_user.dict'
# if os.path.exists(user_dict):
#     j_user = jieba.Jieba(user_dict_path=user_dict)
# else:
#     print(f"User dict not found: {user_dict}")

# --- 分词 ---
sentence = "我来到北京清华大学"
print("Default cut:", "/ ".join(j.cut(sentence)))
# Output: 我/ 来到/ 北京/ 清华大学

print("Full mode cut:", "/ ".join(j.cut(sentence, cut_all=True)))
# Output: 我/ 来到/ 北京/ 清华/ 清华大学/ 华大/ 大学

print("Search mode cut:", "/ ".join(j.cut_for_search(sentence)))
# Output: 我/ 来到/ 北京/ 清华/ 华大/ 大学/ 清华大学

# --- 词性标注 ---
sentence_pos = "他来到了网易杭研大厦"
tags = j.tag(sentence_pos)
print("POS Tagging:", tags)
# Output: [('他', 'r'), ('来到', 'v'), ('了', 'ul'), ('网易', 'nz'), ('杭研', 'x'), ('大厦', 'n')] (示例)

print("Tag for '清华大学':", j.lookup_tag('清华大学'))
# Output: nt (示例)

# --- 关键词提取 ---
long_sentence = "..." # (省略长文本)
keywords = j.extract_keywords(long_sentence, top_k=5)
print("Keywords:", keywords)
# Output: [('关键词1', 权重1), ('关键词2', 权重2), ...]

# 带词性过滤
keywords_filtered = j.extract_keywords(long_sentence, top_k=5, allow_pos=('ns', 'n', 'vn', 'v'))
print("Keywords (filtered):", keywords_filtered)

# --- 检查词语是否存在 ---
print("'清华大学' exists:", j.word_exists('清华大学')) # True
print("'不存在的词' exists:", j.word_exists('不存在的词')) # False
```

## DAT 缓存

*   为了实现快速加载，本库会在首次运行时根据当前词典（主词典+用户词典）内容生成一个 `.dat` 缓存文件。
*   缓存文件默认存放在用户缓存目录下（例如 Linux 的 `~/.cache/cppjieba_py_dat/`, Windows 的 `C:\Users\<用户>\AppData\Local\cppjieba_py_dat\cppjieba_py_dat\Cache\`）。可以通过 `Jieba` 构造函数的 `dat_cache_dir` 参数指定位置。
*   文件名包含词典内容的 MD5 值，当词典文件发生变化时，程序会自动检测并重新生成缓存。
*   生成缓存可能需要几秒钟时间。

## 注意事项

*   **不支持动态添加词语:** 由于 DAT 的特性，无法在运行时添加用户词。
*   **修改用户词典:** 修改用户词典文件后，需要重新运行程序才能生效（会自动重建缓存）。
*   **依赖库许可证:** 本项目使用了 CppJieba, limonp, darts-clone 等库，请遵守它们各自的开源许可证（详情见 `LICENSE` 文件）。

## 致谢

*   感谢 [Yanyi Wu](https://github.com/yanyiwu) 创建了优秀的 CppJieba 项目。
*   感谢 [byronhe](https://github.com/byronhe) 提供了基于 DAT 优化的 CppJieba 版本。
*   感谢 [s-yata](https://github.com/s-yata) 开发了 darts-clone 库。
*   感谢 `pybind11` 提供了方便的 C++/Python 绑定工具。
*   感谢 AI 在本项目开发过程中提供的巨大帮助。

## 许可证

本项目使用 [MIT License](LICENSE)。
