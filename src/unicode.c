// Echo Bridge — High-quality convolution reverb for Daisy Seed
// Copyright (C) 2025  Daniel Ramirez / LUFS Audio
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.


/**
 * Unicode handling functions for FatFs
 * 
 * This file provides the Unicode handling functions required by FatFs
 * when using Unicode file names.
 */

#include "/home/ubuntu/DaisyExamples/libDaisy/Middlewares/Third_Party/FatFs/src/ff.h"

#if FF_USE_LFN != 0

/**
 * Convert OEM code to Unicode
 * 
 * @param wc Pointer to Unicode code
 * @param cp OEM code
 * @return Number of bytes in the next character
 */
WCHAR ff_oem2uni(WCHAR oem, WORD cp)
{
    // Simple ASCII to Unicode conversion
    return (WCHAR)oem;
}

/**
 * Convert Unicode to OEM code
 * 
 * @param uni Unicode code
 * @param cp Code page
 * @return OEM code
 */
WCHAR ff_uni2oem(DWORD uni, WORD cp)
{
    // Simple Unicode to ASCII conversion
    // Only handle ASCII range
    if (uni < 0x80) {
        return (WCHAR)uni;
    }
    
    // Return '?' for non-ASCII characters
    return (WCHAR)'?';
}

/**
 * Convert character code to upper case
 * 
 * @param uni Unicode character
 * @return Upper case character
 */
DWORD ff_wtoupper(DWORD uni)
{
    // Simple lowercase to uppercase conversion for ASCII
    if (uni >= 'a' && uni <= 'z') {
        uni = uni - 'a' + 'A';
    }
    
    return uni;
}

/**
 * Convert UTF-16 string to UTF-8 string
 * 
 * @param dst Pointer to destination UTF-8 string buffer
 * @param src Pointer to source UTF-16 string
 * @param len Size of destination buffer in unit of byte
 * @return Number of bytes written to the buffer
 */
WCHAR ff_convert(WCHAR src, UINT dir)
{
    // Simple conversion for ASCII range
    if (src < 0x80) {
        return src;
    }
    
    // Return '?' for non-ASCII characters
    return (WCHAR)'?';
}

#endif /* FF_USE_LFN != 0 */
