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

#ifndef SCOPEDTIMER_H_INCLUDED
#define SCOPEDTIMER_H_INCLUDED

#include <chrono>
#include <iostream>

namespace TinyRNN
{
    class ScopedTimer final
    {
    public:
        
        explicit ScopedTimer(const std::string &targetName) :
            startTime(std::chrono::high_resolution_clock::now())
        {
            std::cout << targetName << std::endl;
        }
        
        ~ScopedTimer()
        {
            const auto endTime = std::chrono::high_resolution_clock::now();
            const auto milliSeconds = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - this->startTime).count();
            std::cout << "Done (" << std::to_string(milliSeconds) << " ms)" << std::endl;
        }
        
    private:
        
        std::chrono::high_resolution_clock::time_point startTime;
        
        TINYRNN_DISALLOW_COPY_AND_ASSIGN(ScopedTimer);
    };
}  // namespace TinyRNN

#endif  // SCOPEDTIMER_H_INCLUDED
