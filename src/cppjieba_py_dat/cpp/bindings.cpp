#include <pybind11/pybind11.h>
#include <pybind11/stl.h>       // For automatic conversions (vector, string, pair)
#include <string>
#include <vector>
#include <utility>              // For std::pair

// Core CppJieba headers needed for bindings
#include "cppjieba/Jieba.hpp"
#include "cppjieba/KeywordExtractor.hpp" // Needed for extractor access and its result type (pair)

// Logging header for setting log level
#include "limonp/Logging.hpp"

namespace py = pybind11;

// Define the Python module 'bindings'
PYBIND11_MODULE(bindings, m) {
    m.doc() = "Python bindings for DAT-optimized CppJieba";
    // --- Bind Jieba class ---
    py::class_<cppjieba::Jieba>(m, "Jieba", "Main Jieba interface for segmentation, tagging, etc.")
        // Constructor binding
        .def(py::init<const std::string&, const std::string&, const std::string&, const std::string&, const std::string&, const std::string&>(),
             py::arg("dict_path"),           // Main dictionary path
             py::arg("model_path"),          // HMM model path
             py::arg("user_dict_path"),      // User dictionary path
             py::arg("idf_path") = "",       // Optional IDF path
             py::arg("stop_word_path") = "", // Optional stop word path
             py::arg("dat_cache_path") = ""  // Optional DAT cache directory (passed from Python __init__)
            )

        // --- Bind Segmentation Methods (returning List[str]) ---
        .def("cut",
             [](const cppjieba::Jieba& self, const std::string& sentence, bool hmm) -> std::vector<std::string> {
                 std::vector<std::string> words;
                 self.Cut(sentence, words, hmm); // Uses MixSegment
                 return words;
             },
             "Cut sentence using MixSegment.",
             py::arg("sentence"),
             py::arg("hmm") = true // Default to HMM enabled
            )

        .def("cut_all",
             [](const cppjieba::Jieba& self, const std::string& sentence) -> std::vector<std::string> {
                 std::vector<std::string> words;
                 self.CutAll(sentence, words); // Uses FullSegment
                 return words;
             },
             "Cut sentence using FullSegment (cuts all possible words).",
             py::arg("sentence")
            )

        .def("cut_for_search",
             [](const cppjieba::Jieba& self, const std::string& sentence, bool hmm) -> std::vector<std::string> {
                 std::vector<std::string> words;
                 self.CutForSearch(sentence, words, hmm); // Uses QuerySegment
                 return words;
             },
             "Cut sentence for search engine using QuerySegment.",
             py::arg("sentence"),
             py::arg("hmm") = true // Default to HMM enabled
            )

        // --- Bind POS Tagging Methods ---
        .def("tag",
             [](const cppjieba::Jieba& self, const std::string& sentence) -> std::vector<std::pair<std::string, std::string>> {
                 std::vector<std::pair<std::string, std::string>> result;
                 self.Tag(sentence, result); // Returns list of (word, tag) pairs
                 return result; // pybind11 automatically converts to List[Tuple[str, str]]
             },
             "Tag words with Part-of-Speech.",
             py::arg("sentence")
            )

        .def("lookup_tag", &cppjieba::Jieba::LookupTag,
             "Lookup the POS tag for a single word in the dictionary.",
             py::arg("word")
            )

        // --- Bind Dictionary Lookup Method ---
        .def("find", &cppjieba::Jieba::Find,
             "Check if a word exists in the dictionary (including user dictionary).",
             py::arg("word")
            )

        // --- Bind Keyword Extraction Method ---
        .def("extract_keywords",
             [](const cppjieba::Jieba& self, const std::string& sentence, int top_k) -> std::vector<std::pair<std::string, double>> {
                 // Directly call the Extract overload returning pairs
                 std::vector<std::pair<std::string, double>> keywords;
                 // Access the public 'extractor' member and call its 'Extract' method
                 self.extractor.Extract(sentence, keywords, top_k);
                 return keywords; // pybind11 automatically converts to List[Tuple[str, float]]
             },
             "Extract keywords from sentence using TF-IDF.",
             py::arg("sentence"),
             py::arg("top_k") = 20 // Default top K value
            );
        // Note: We removed the binding for the intermediate KeywordResultPython struct
        // Note: We removed the commented-out WordPython binding


    // --- Module Version ---
    // This allows getting the version via cppjieba_py_dat.bindings.__version__
#ifdef VERSION_INFO
    m.attr("__version__") = VERSION_INFO; // Set by build system (e.g., CMake or setup.py)
#else
    m.attr("__version__") = "dev"; // Default if not set during build
#endif
} // PYBIND11_MODULE
