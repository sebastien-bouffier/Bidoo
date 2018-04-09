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

#ifndef TINYRNN_CONFIG_H_INCLUDED
#define TINYRNN_CONFIG_H_INCLUDED

#include <list>
#include <vector>
#include <memory>
#include <string>
#include <cstring>
#include <sstream>
#include <map>
#include <unordered_map>
#include <math.h>

#define TINYRNN_GRADIENT_CLIPPING_THRESHOLD 1.0

namespace TinyRNN
{
    using Id = uint32_t;
    using Index = uint32_t;
    using Value = float;
} // namespace TinyRNN

#define TINYRNN_DISALLOW_COPY_AND_ASSIGN(TypeName) \
    TypeName(const TypeName &) = delete;\
    TypeName &operator =(const TypeName &) = delete;

#endif // TINYRNN_CONFIG_H_INCLUDED
