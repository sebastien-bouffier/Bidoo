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

#ifndef SCOPEDMEMORYBLOCK_H_INCLUDED
#define SCOPEDMEMORYBLOCK_H_INCLUDED

namespace TinyRNN
{
    template<typename T>
    class ScopedMemoryBlock final
    {
    public:
        
        ScopedMemoryBlock() noexcept : data(nullptr), size(0) {}
        
        explicit ScopedMemoryBlock(const size_t numElements) :
        data(static_cast<T *>(std::calloc(numElements, sizeof(T)))),
        size(numElements)
        {
        }
        
        ~ScopedMemoryBlock()
        {
            this->clear();
        }
        
        inline T *getData() const noexcept                  { return this->data; }
        inline T& operator[] (size_t index) const noexcept  { return this->data[index]; }
        
        ScopedMemoryBlock &operator =(ScopedMemoryBlock &&other) noexcept
        {
            std::swap(this->data, other.data);
            this->size = other.size;
            return *this;
        }
        
        void clear() noexcept
        {
            std::free(this->data);
            this->data = nullptr;
            this->size = 0;
        }
        
        size_t getSize() const noexcept
        {
            return this->size;
        }
        
    private:
        
        T *data;
        size_t size;
        
        TINYRNN_DISALLOW_COPY_AND_ASSIGN(ScopedMemoryBlock);
    };
}  // namespace TinyRNN

#endif  // SCOPEDMEMORYBLOCK_H_INCLUDED
