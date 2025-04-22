import sys
import os
from setuptools import setup, Extension
import pybind11

_ROOT_DIR = os.path.abspath(os.path.dirname(__file__))

# --- C++ 扩展模块定义 ---

cpp_sources = [
    'src/cppjieba_py_dat/cpp/bindings.cpp',
    'src/cppjieba_py_dat/cpp/cppjieba/limonp/Md5.cpp',
]

# 头文件包含目录 (保持之前的设置)
include_dirs = [
    pybind11.get_include(),
    # For #include "cppjieba/..."
    os.path.join(_ROOT_DIR, 'src/cppjieba_py_dat/cpp/cppjieba/include/'),
    # For #include "limonp/..."
    os.path.join(_ROOT_DIR, 'src/cppjieba_py_dat/cpp/cppjieba/'),
    # For #include "darts.h"
    os.path.join(_ROOT_DIR, 'src/cppjieba_py_dat/cpp/cppjieba/darts-clone/'),
    # For #include "Logging.hpp" (if used directly)
    os.path.join(_ROOT_DIR, 'src/cppjieba_py_dat/cpp/cppjieba/limonp/'),
]

# 编译器参数 (保持之前的设置)
extra_compile_args = ['-O3', '-Wall']
compile_time_log_level = 2
if sys.platform == 'darwin':  # macOS
    extra_compile_args += [
        '-std=c++14',
        '-mmacosx-version-min=10.9',
        f'-DLOGGING_LEVEL={compile_time_log_level}'
    ]
    extra_link_args = ['-stdlib=libc++', '-mmacosx-version-min=10.9']
elif sys.platform == 'win32':  # Windows (MSVC)
    extra_compile_args = [
        '/std:c++14',
        '/O2',
        '/EHsc',
        '/utf-8',
        f'/DLOGGING_LEVEL={compile_time_log_level}'
    ]
    extra_link_args = []
else:  # Linux & 其他
    extra_compile_args += [
        '-std=c++14',
        '-pthread',
        '-fvisibility=hidden',
        f'-DLOGGING_LEVEL={compile_time_log_level}'
    ]
    extra_link_args = ['-pthread']

# 定义扩展模块 (使用更新后的 cpp_sources)
ext_modules = [
    Extension(
        'cppjieba_py_dat.bindings',
        sorted(cpp_sources),  # 使用包含两个 .cpp 文件的列表
        include_dirs=include_dirs,
        language='c++',
        extra_compile_args=extra_compile_args,
        extra_link_args=extra_link_args,
    )
]

# --- Setup 函数 ---
setup(
    ext_modules=ext_modules,
    zip_safe=False,
)
