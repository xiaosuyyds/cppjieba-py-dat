# src/cppjieba_py_dat/__init__.py
import os
import sys
import threading
import platform  # For OS specific paths
from typing import List, Tuple, Optional  # For type hints

try:
    # Python 3.9+
    from importlib.resources import files as resources_files

    _HAS_IMPORTLIB_RESOURCES_FILES = True
except ImportError:
    # Fallback for Python < 3.9
    from importlib.resources import path as resources_path
    import contextlib

    _HAS_IMPORTLIB_RESOURCES_FILES = False

# 导入 C++ 扩展模块
from . import bindings as _bindings

# 推荐使用 appdirs 找用户缓存目录
try:
    import appdirs

    _use_appdirs = True
except ImportError:
    _use_appdirs = False

_PACKAGE_NAME = __name__

# --- 全局单例管理 ---
_jieba_instance = None
_instance_lock = threading.Lock()


def _get_resource_path(filename):
    """获取包内资源文件的路径"""
    try:
        if _HAS_IMPORTLIB_RESOURCES_FILES:
            return str(resources_files(_PACKAGE_NAME).joinpath(filename))
        else:
            with contextlib.ExitStack() as stack:
                path_context = resources_path(_PACKAGE_NAME, filename)
                actual_path = stack.enter_context(path_context)
                return str(actual_path)
    except Exception as e:
        print(f"Error finding resource '{filename}' in package '{_PACKAGE_NAME}': {e}", file=sys.stderr)
        raise FileNotFoundError(f"Could not find resource '{filename}' within the package.")


def _get_default_dat_cache_dir():
    """获取默认的 DAT 缓存目录 (跨平台)"""
    app_name = "cppjieba_py_dat"
    if _use_appdirs:
        cache_dir = appdirs.user_cache_dir(app_name)
    else:
        home = os.path.expanduser("~")
        if platform.system() == "Windows":
            local_app_data = os.environ.get("LOCALAPPDATA", os.path.join(home, "AppData", "Local"))
            cache_dir = os.path.join(local_app_data, app_name, "Cache")
        elif platform.system() == "Darwin":
            cache_dir = os.path.join(home, "Library", "Caches", app_name)
        else:
            xdg_cache_home = os.environ.get("XDG_CACHE_HOME", os.path.join(home, ".cache"))
            cache_dir = os.path.join(xdg_cache_home, app_name)
    try:
        os.makedirs(cache_dir, exist_ok=True)
    except OSError as e:
        print(f"Warning: Could not create DAT cache directory '{cache_dir}': {e}", file=sys.stderr)
        return ""
    return cache_dir


def _initialize_global_instance(**kwargs):
    """内部函数：实际初始化全局实例"""
    user_dict_path = kwargs.get('user_dict_path')
    dat_cache_dir = kwargs.get('dat_cache_dir')

    _user_dict_path = os.path.abspath(user_dict_path) if user_dict_path else ""
    if user_dict_path and not os.path.exists(_user_dict_path):
        print(f"Warning: User dictionary path '{_user_dict_path}' does not exist.", file=sys.stderr)
        _user_dict_path = ""

    _dat_cache_dir = dat_cache_dir if dat_cache_dir is not None else _get_default_dat_cache_dir()

    try:
        dict_path = _get_resource_path("dict/jieba.dict.utf8")
        hmm_path = _get_resource_path("dict/hmm_model.utf8")
        # --- 按需加载 IDF 和停用词 ---
        # 如果用户提供了路径，或者你想默认加载，就在这里获取路径
        idf_path_arg = kwargs.get('idf_path', "")  # 允许通过 kwargs 传递
        stop_word_path_arg = kwargs.get('stop_word_path', "")

        idf_path = _get_resource_path("dict/idf.utf8") if idf_path_arg is None else idf_path_arg  # 默认加载？
        stop_word_path = _get_resource_path(
            "dict/stop_words.utf8") if stop_word_path_arg is None else stop_word_path_arg  # 默认加载？
    except FileNotFoundError:
        print("Error: Could not find essential dictionary files.", file=sys.stderr)
        raise

    try:
        # 创建 C++ 核心对象
        return _bindings.Jieba(
            dict_path=dict_path,
            model_path=hmm_path,
            user_dict_path=_user_dict_path,
            idf_path=idf_path,
            stop_word_path=stop_word_path,
            dat_cache_path=_dat_cache_dir
        )
    except Exception as e:
        print(f"Error initializing global Jieba instance: {e}")
        raise


def _get_instance(**kwargs):
    """获取或创建全局 Jieba 实例 (线程安全)"""
    global _jieba_instance
    if _jieba_instance is None:
        with _instance_lock:
            if _jieba_instance is None:
                _jieba_instance = _initialize_global_instance(**kwargs)
    elif kwargs:  # 检查是否传入了新的配置参数
        # 如果用户尝试用不同配置获取已初始化的单例，发出警告
        # 注意：这个检查很简单，可能无法覆盖所有情况
        current_config_keys = ['user_dict_path', 'dat_cache_dir', 'idf_path', 'stop_word_path']
        if any(kwargs.get(k) is not None for k in current_config_keys):
            print("Warning: Global Jieba instance already initialized. "
                  "Ignoring new configuration parameters passed to functional API.", file=sys.stderr)
    return _jieba_instance


# --- 函数式接口定义 ---
def cut(sentence: str, cut_all: bool = False, HMM: bool = True) -> List[str]:
    # ... (代码同前) ...
    instance = _get_instance()
    if instance is None: raise RuntimeError("Jieba core failed to initialize.")
    if cut_all:
        return instance.cut_all(sentence)
    else:
        return instance.cut(sentence, hmm=HMM)


def cut_for_search(sentence: str, HMM: bool = True) -> List[str]:
    instance = _get_instance()
    if instance is None:
        raise RuntimeError("Jieba core failed to initialize.")
    return instance.cut_for_search(sentence, hmm=HMM)


def lcut(sentence: str, cut_all: bool = False, HMM: bool = True) -> List[str]:
    # lcut 通常是 cut 的别名，返回列表
    return cut(sentence, cut_all=cut_all, HMM=HMM)


def lcut_for_search(sentence: str, HMM: bool = True) -> List[str]:
    # lcut_for_search 通常是 cut_for_search 的别名
    return cut_for_search(sentence, HMM=HMM)


def tag(sentence: str) -> List[Tuple[str, str]]:
    instance = _get_instance()
    if instance is None:
        raise RuntimeError("Jieba core failed to initialize.")
    return instance.tag(sentence)


def lookup_tag(word: str) -> str:
    instance = _get_instance()
    if instance is None:
        raise RuntimeError("Jieba core failed to initialize.")
    return instance.lookup_tag(word)


def extract_keywords(sentence: str, top_k: int = 20, allow_pos: Tuple[str, ...] = ()) -> List[Tuple[str, float]]:
    instance = _get_instance()
    if instance is None:
        raise RuntimeError("Jieba core failed to initialize.")
    raw_results = instance.extract_keywords(sentence, top_k=top_k)
    if not allow_pos:
        return raw_results
    filtered_results = []
    for keyword, weight in raw_results:
        tag_ = instance.lookup_tag(keyword)  # 使用 instance
        should_keep = False
        for allowed in allow_pos:
            if tag_ == allowed:
                should_keep = True; break
        if should_keep:
            filtered_results.append((keyword, weight))
    return filtered_results


def word_exists(word: str) -> bool:
    instance = _get_instance()
    if instance is None:
        raise RuntimeError("Jieba core failed to initialize.")
    return instance.find(word)


# --- 添加面向对象的 Jieba 类 ---
class Jieba:
    """
    Object-oriented interface for CppJieba-Py-Dat. Allows creating multiple
    instances with potentially different configurations (though DAT caching
    might be shared based on dictionary paths).
    """

    def __init__(self,
                 user_dict_path: Optional[str] = None,
                 dat_cache_dir: Optional[str] = None,
                 idf_path: Optional[str] = "",  # 允许指定 IDF 路径
                 stop_word_path: Optional[str] = ""  # 允许指定停用词路径
                 ):
        """
        Initializes a new Jieba instance.

        Args:
            user_dict_path (Optional[str]): Path to user dictionary.
            dat_cache_dir (Optional[str]): Directory for DAT cache files. Uses default if None.
            idf_path (Optional[str]): Path to IDF dictionary. Defaults to "" (uses internal if available, or none).
            stop_word_path (Optional[str]): Path to stop word file. Defaults to "" (uses internal if available, or none).
        """
        print("Initializing new Jieba object instance...")  # Log 区分
        _user_dict_path = os.path.abspath(user_dict_path) if user_dict_path else ""
        if user_dict_path and not os.path.exists(_user_dict_path):
            print(f"Warning: User dictionary path '{_user_dict_path}' does not exist.", file=sys.stderr)
            _user_dict_path = ""

        _dat_cache_dir = dat_cache_dir if dat_cache_dir is not None else _get_default_dat_cache_dir()

        try:
            dict_path = _get_resource_path("dict/jieba.dict.utf8")
            hmm_path = _get_resource_path("dict/hmm_model.utf8")

            # 处理 IDF 和停用词路径：如果用户没指定，则尝试加载包内默认的
            _idf_path = _get_resource_path("dict/idf.utf8") if idf_path is None else idf_path
            _stop_word_path = _get_resource_path("dict/stop_words.utf8") if stop_word_path is None else stop_word_path
            # 如果不希望默认加载，可以这样：
            # _idf_path = idf_path if idf_path is not None else ""
            # _stop_word_path = stop_word_path if stop_word_path is not None else ""

        except FileNotFoundError:
            print("Error: Could not find essential dictionary files.", file=sys.stderr)
            raise

        # --- 关键：创建独立的 C++ Jieba 对象实例 ---
        try:
            self._jieba_cpp = _bindings.Jieba(
                dict_path=dict_path,
                model_path=hmm_path,
                user_dict_path=_user_dict_path,
                idf_path=_idf_path,
                stop_word_path=_stop_word_path,
                dat_cache_path=_dat_cache_dir
            )
            print("New Jieba object instance initialized successfully!")
        except Exception as e:
            print(f"Error initializing new Jieba object instance: {e}")
            raise

    # --- 类方法：调用 self._jieba_cpp ---
    def cut(self, sentence: str, cut_all: bool = False, HMM: bool = True) -> List[str]:
        """Cut sentence using this Jieba instance."""
        if cut_all:
            return self._jieba_cpp.cut_all(sentence)
        else:
            return self._jieba_cpp.cut(sentence, hmm=HMM)

    def cut_for_search(self, sentence: str, HMM: bool = True) -> List[str]:
        """Cut sentence for search engine using this Jieba instance."""
        return self._jieba_cpp.cut_for_search(sentence, hmm=HMM)

    def lcut(self, sentence: str, cut_all: bool = False, HMM: bool = True) -> List[str]:
        """Alias for cut."""
        return self.cut(sentence, cut_all=cut_all, HMM=HMM)

    def lcut_for_search(self, sentence: str, HMM: bool = True) -> List[str]:
        """Alias for cut_for_search."""
        return self.cut_for_search(sentence, HMM=HMM)

    def tag(self, sentence: str) -> List[Tuple[str, str]]:
        """Perform POS tagging using this Jieba instance."""
        return self._jieba_cpp.tag(sentence)

    def lookup_tag(self, word: str) -> str:
        """Lookup POS tag using this Jieba instance."""
        return self._jieba_cpp.lookup_tag(word)

    def extract_keywords(self, sentence: str, top_k: int = 20, allow_pos: Tuple[str, ...] = ()) -> List[
        Tuple[str, float]]:
        """Extract keywords using this Jieba instance."""
        # 调用 C++ 绑定方法
        raw_results: list[tuple[str, float]] = self._jieba_cpp.extract_keywords(sentence, top_k=top_k)

        # Python POS 过滤逻辑
        if not allow_pos:
            return raw_results
        filtered_results = []
        for keyword, weight in raw_results:
            tag_ = self.lookup_tag(keyword)  # 注意是 self.lookup_tag
            should_keep = False
            for allowed in allow_pos:
                if tag_ == allowed:
                    should_keep = True
                    break
            if should_keep:
                filtered_results.append((keyword, weight))
        return filtered_results

    def word_exists(self, word: str) -> bool:
        """Check word existence using this Jieba instance."""
        return self._jieba_cpp.find(word)


# --- 暴露公共接口 ---
# 同时暴露函数式接口和面向对象接口
__all__ = [
    # 函数式接口
    'cut', 'cut_for_search', 'lcut', 'lcut_for_search', 'tag', 'lookup_tag',
    'extract_keywords', 'word_exists',
    # 面向对象接口
    'Jieba',
]
