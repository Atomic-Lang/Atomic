// text.cpp
// Text manipulation library for Atomic — cross-platform (Windows/Linux), dynamic linking (.dla)
//
// Build (.dla):
//   Windows: g++ -std=c++17 -shared -o libs/text/text.dla libs/text/text.cpp
//   Linux:   g++ -std=c++17 -shared -fPIC -o libs/text/text.dla libs/text/text.cpp

#include <string>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <cstdint>

// =============================================================================
// EXPORT MACRO
// =============================================================================

#ifdef _WIN32
    #define AT_EXPORT extern "C" __declspec(dllexport)
#else
    #define AT_EXPORT extern "C" __attribute__((visibility("default")))
#endif

// =============================================================================
// ROTATING BUFFER FOR STRING RETURNS
// =============================================================================

static const int NUM_BUFFERS = 8;
static const int BUF_SIZE    = 4096;
static char str_buffers[NUM_BUFFERS][BUF_SIZE];
static int  buf_index = 0;

static const char* return_str(const std::string& s)
{
    char* buf = str_buffers[buf_index];
    buf_index = (buf_index + 1) % NUM_BUFFERS;

    size_t len = s.size();
    if (len >= static_cast<size_t>(BUF_SIZE))
        len = BUF_SIZE - 1;

    memcpy(buf, s.c_str(), len);
    buf[len] = '\0';

    return buf;
}

// =============================================================================
// EXPORTED FUNCTIONS
// =============================================================================

AT_EXPORT const char* txt_upper(const char* text)
{
    if (!text) return return_str("");
    std::string str(text);
    std::transform(str.begin(), str.end(), str.begin(), ::toupper);
    return return_str(str);
}

AT_EXPORT const char* txt_lower(const char* text)
{
    if (!text) return return_str("");
    std::string str(text);
    std::transform(str.begin(), str.end(), str.begin(), ::tolower);
    return return_str(str);
}

AT_EXPORT int64_t txt_length(const char* text)
{
    if (!text) return 0;
    return static_cast<int64_t>(strlen(text));
}

AT_EXPORT int64_t txt_contains(const char* text, const char* search)
{
    if (!text || !search) return 0;
    return strstr(text, search) != nullptr ? 1 : 0;
}

AT_EXPORT const char* txt_trim(const char* text)
{
    if (!text) return return_str("");
    std::string str(text);
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return return_str("");
    size_t end = str.find_last_not_of(" \t\r\n");
    return return_str(str.substr(start, end - start + 1));
}

AT_EXPORT const char* txt_replace(const char* text,
                                  const char* old_str,
                                  const char* new_str)
{
    if (!text || !old_str || !new_str) return return_str("");
    std::string str(text);
    std::string old_s(old_str);
    std::string new_s(new_str);
    if (old_s.empty()) return return_str(str);

    size_t pos = 0;
    while ((pos = str.find(old_s, pos)) != std::string::npos) {
        str.replace(pos, old_s.length(), new_s);
        pos += new_s.length();
    }
    return return_str(str);
}

AT_EXPORT const char* txt_repeat(const char* text, int64_t times)
{
    int n = static_cast<int>(times);
    if (!text || n <= 0) return return_str("");
    std::string str(text);
    std::string res;
    res.reserve(str.length() * n);
    for (int i = 0; i < n; ++i) res += str;
    return return_str(res);
}

AT_EXPORT const char* txt_reverse(const char* text)
{
    if (!text) return return_str("");
    std::string str(text);
    std::reverse(str.begin(), str.end());
    return return_str(str);
}

AT_EXPORT const char* txt_substr(const char* text,
                                 int64_t start_pos,
                                 int64_t length)
{
    if (!text) return return_str("");
    size_t slen = strlen(text);

    size_t start = (start_pos < 0) ? 0 : static_cast<size_t>(start_pos);
    if (start >= slen) return return_str("");

    size_t len = static_cast<size_t>(length);
    if (len > slen - start) len = slen - start;

    return return_str(std::string(text).substr(start, len));
}

AT_EXPORT int64_t txt_starts_with(const char* text, const char* prefix)
{
    if (!text || !prefix) return 0;
    size_t plen = strlen(prefix);
    return strncmp(text, prefix, plen) == 0 ? 1 : 0;
}

AT_EXPORT int64_t txt_ends_with(const char* text, const char* suffix)
{
    if (!text || !suffix) return 0;
    size_t slen = strlen(text);
    size_t suflen = strlen(suffix);
    if (suflen > slen) return 0;
    return strcmp(text + slen - suflen, suffix) == 0 ? 1 : 0;
}

AT_EXPORT int64_t txt_index_of(const char* text, const char* search)
{
    if (!text || !search) return -1;
    const char* found = strstr(text, search);
    if (!found) return -1;
    return static_cast<int64_t>(found - text);
}

AT_EXPORT const char* txt_char_at(const char* text, int64_t position)
{
    if (!text) return return_str("");
    int idx = static_cast<int>(position);
    int slen = static_cast<int>(strlen(text));
    if (idx < 0 || idx >= slen) return return_str("");
    char buf[2] = { text[idx], '\0' };
    return return_str(std::string(buf));
}

AT_EXPORT int64_t txt_count(const char* text, const char* search)
{
    if (!text || !search) return 0;
    size_t sublen = strlen(search);
    if (sublen == 0) return 0;
    int count = 0;
    const char* p = text;
    while ((p = strstr(p, search)) != nullptr) {
        ++count;
        p += sublen;
    }
    return static_cast<int64_t>(count);
}

AT_EXPORT const char* txt_split(const char* text,
                                const char* delimiter,
                                int64_t index)
{
    if (!text || !delimiter) return return_str("");
    std::string str(text);
    std::string del(delimiter);
    if (del.empty()) return return_str(str);

    size_t pos = 0;
    int current = 0;
    size_t find_pos;
    while ((find_pos = str.find(del, pos)) != std::string::npos) {
        if (current == index) {
            return return_str(str.substr(pos, find_pos - pos));
        }
        pos = find_pos + del.length();
        ++current;
    }
    if (current == index) return return_str(str.substr(pos));
    return return_str("");
}

AT_EXPORT int64_t txt_split_count(const char* text,
                                  const char* delimiter)
{
    if (!text || !delimiter) return 0;
    std::string str(text);
    std::string del(delimiter);
    if (del.empty()) return 1;
    if (str.empty()) return 0;
    int count = 1;
    size_t pos = 0;
    while ((pos = str.find(del, pos)) != std::string::npos) {
        ++count;
        pos += del.length();
    }
    return static_cast<int64_t>(count);
}

AT_EXPORT const char* txt_capitalize(const char* text)
{
    if (!text || *text == '\0') return return_str("");

    std::string str(text);

    if (str[0] >= 'a' && str[0] <= 'z') {
        str[0] = static_cast<char>(toupper(static_cast<unsigned char>(str[0])));
    }

    return return_str(str);
}

AT_EXPORT const char* txt_title(const char* text)
{
    if (!text || *text == '\0') return return_str("");

    std::string str(text);

    for (size_t i = 0; i < str.length(); ++i) {
        unsigned char c = static_cast<unsigned char>(str[i]);

        if (isalpha(c)) {
            bool is_word_start = (i == 0);

            if (!is_word_start) {
                unsigned char prev = static_cast<unsigned char>(str[i - 1]);
                is_word_start = (isspace(prev) || ispunct(prev));
            }

            if (is_word_start) {
                str[i] = toupper(c);
            } else {
                str[i] = tolower(c);
            }
        }
    }

    return return_str(str);
}
