#pragma once

using CLASS_ID = u64; // CLASS ID type

constexpr CLASS_ID make_clsid(const char (&str)[9])
{
    return static_cast<CLASS_ID>(str[0]) << static_cast<CLASS_ID>(56) | static_cast<CLASS_ID>(str[1]) << static_cast<CLASS_ID>(48) |
        static_cast<CLASS_ID>(str[2]) << static_cast<CLASS_ID>(40) | static_cast<CLASS_ID>(str[3]) << static_cast<CLASS_ID>(32) |
        static_cast<CLASS_ID>(str[4]) << static_cast<CLASS_ID>(24) | static_cast<CLASS_ID>(str[5]) << static_cast<CLASS_ID>(16) |
        static_cast<CLASS_ID>(str[6]) << static_cast<CLASS_ID>(8) | static_cast<CLASS_ID>(str[7]);
}

constexpr CLASS_ID make_clsid(const char a, const char b, const char c, const char d, const char e, const char f, const char g, const char h)
{
    return static_cast<CLASS_ID>(a) << static_cast<CLASS_ID>(56) | static_cast<CLASS_ID>(b) << static_cast<CLASS_ID>(48) | static_cast<CLASS_ID>(c) << static_cast<CLASS_ID>(40) |
        static_cast<CLASS_ID>(d) << static_cast<CLASS_ID>(32) | static_cast<CLASS_ID>(e) << static_cast<CLASS_ID>(24) | static_cast<CLASS_ID>(f) << static_cast<CLASS_ID>(16) |
        static_cast<CLASS_ID>(g) << static_cast<CLASS_ID>(8) | static_cast<CLASS_ID>(h);
}

#define MK_CLSID(a, b, c, d, e, f, g, h) make_clsid(a, b, c, d, e, f, g, h)

extern XRCORE_API void CLSID2TEXT(CLASS_ID id, LPSTR text);
extern XRCORE_API CLASS_ID TEXT2CLSID(LPCSTR text);
