/*
    Copyright (c) 2016 Peter Rudenko

    Permission is hereby granted, free of charge, to any person obtaining
    a copy of this software and associated documentation files (the "Software"),
    to deal in the Software without restriction, including without limitation
    the rights to use, copy, modify, merge, publish, distribute, sublicense,
    and/or sell copies of the Software, and to permit persons to whom the Software
    is furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in
    all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
    EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
    OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
    IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
    CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
    TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
    OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#ifndef TINYRNN_ID_H_INCLUDED
#define TINYRNN_ID_H_INCLUDED

#include "Common.h"
#include <climits>
#include <random>

namespace TinyRNN
{
    namespace Uuid
    {
        static std::string generateIsoUuid()
        {
            static const int uuidLength = 32;
            static std::random_device randomDevice;
            static std::mt19937 mt19937(randomDevice());
            std::uniform_int_distribution<const unsigned char> distribution(0, UCHAR_MAX);
            
            unsigned char uuid[uuidLength];
            
            for (unsigned char & i : uuid)
            {
                const unsigned char a = distribution(mt19937);
                i = a;
            }
            
            // ISO/IEC 9834-8:
            uuid[6] = (uuid[6] & 0x0f) | 0x40;
            uuid[8] = (uuid[8] & 0x3f) | 0x80;
            
            std::string result;
            static const char hexadecimalDigits[] = "0123456789abcdef";
            
            for (int i = 0; i < uuidLength; ++i)
            {
                result.push_back(hexadecimalDigits[uuid[i] >> 4]);
                result.push_back(hexadecimalDigits[uuid[i] & 0xf]);
            }
            
            return result;
        }
        
        static inline Id generateId()
        {
            static Id kRecentId = 0;
            return ++kRecentId;
        }
    } // namespace Uuid;
}  // namespace TinyRNN

#endif // TINYRNN_ID_H_INCLUDED
