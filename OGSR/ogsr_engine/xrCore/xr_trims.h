#pragma once

// refs
struct xr_token;

XRCORE_API int _GetItemCount(LPCSTR, char separator = ',');

XRCORE_API LPSTR __GetItem(LPCSTR, int, LPSTR, const size_t dst_size, char separator = ',', LPCSTR = "", bool trim = true);

template <size_t count>
inline LPSTR _GetItem(LPCSTR src, int index, char (&dst)[count], char separator = ',', LPCSTR def = "", bool trim = true)
{
    return __GetItem(src, index, dst, count, separator, def, trim);
}

XRCORE_API LPCSTR _GetItem(LPCSTR, int, xr_string& p, char separator = ',', LPCSTR = "", bool trim = true);

XRCORE_API LPSTR _GetItems(LPCSTR, int, int, LPSTR, char separator = ',');

XRCORE_API u32 _ParseItem(LPCSTR src, xr_token* token_list);
XRCORE_API void _SequenceToList(SStringVec& lst, LPCSTR in, char separator = ',');

XRCORE_API LPSTR _Trim(LPSTR str);
XRCORE_API LPSTR _TrimLeft(LPSTR str);
XRCORE_API LPSTR _TrimRight(LPSTR str);

XRCORE_API xr_string& _Trim(xr_string& src);
XRCORE_API xr_string& _TrimLeft(xr_string& src);
XRCORE_API xr_string& _TrimRight(xr_string& src);
