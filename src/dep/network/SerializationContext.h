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

#ifndef TINYRNN_SERIALIZATIONCONTEXT_H_INCLUDED
#define TINYRNN_SERIALIZATIONCONTEXT_H_INCLUDED

namespace TinyRNN
{
    class SerializedObject;
    
    class SerializationContext
    {
    public:
        
        using Ptr = std::shared_ptr<SerializationContext>;
        
    public:
        
        virtual ~SerializationContext() = default;
        
        virtual void setRealProperty(Value value, const std::string &key) = 0;
        virtual Value getRealProperty(const std::string &key) const = 0;
        
        virtual void setNumberProperty(long long value, const std::string &key) = 0;
        virtual long long getNumberProperty(const std::string &key) const = 0;
        
        virtual void setStringProperty(const std::string &value, const std::string &key) = 0;
        virtual std::string getStringProperty(const std::string &key) const = 0;
        
        virtual size_t getNumChildrenContexts() const = 0;
        virtual SerializationContext::Ptr getChildContext(int index) const = 0;
        
        virtual SerializationContext::Ptr getChildContext(const std::string &key) const = 0;
        virtual SerializationContext::Ptr addChildContext(const std::string &key) = 0;
        virtual SerializationContext::Ptr addChildContextUnordered(const std::string &key) = 0;
        
        static inline std::string encodeBase64(unsigned char const *bytesToEncode, size_t inLen);
        static inline std::string encodeBase64(const std::string &s);
        static inline std::vector<unsigned char> decodeBase64(const std::string &encodedString);
    };
    
    //===------------------------------------------------------------------===//
    // Helpers
    //===------------------------------------------------------------------===//

    static const std::string base64Chars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";
    
    static inline bool is_base64(unsigned char c)
    {
        return (isalnum(c) || (c == '+') || (c == '/'));
    }
    
    inline std::string SerializationContext::encodeBase64(unsigned char const *bytesToEncode, size_t inLen)
    {
        std::string ret;
        int i = 0;
        int j = 0;
        unsigned char char_array_3[3];
        unsigned char char_array_4[4];
        
        while ((inLen--) != 0u)
        {
            char_array_3[i++] = *(bytesToEncode++);
            
            if (i == 3)
            {
                char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
                char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
                char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
                char_array_4[3] = char_array_3[2] & 0x3f;
                
                for (i = 0; (i < 4) ; i++)
                {
                    ret += base64Chars[char_array_4[i]];
                }
                
                i = 0;
            }
        }
        
        if (i != 0)
        {
            for (j = i; j < 3; j++)
            {
                char_array_3[j] = '\0';
            }
            
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;
            
            for (j = 0; (j < i + 1); j++)
            {
                ret += base64Chars[char_array_4[j]];
            }
            
            while ((i++ < 3))
            {
                ret += '=';
            }
        }
        
        return ret;
    }
    
    inline std::string SerializationContext::encodeBase64(const std::string &s)
    {
        return SerializationContext::encodeBase64((const unsigned char *)s.data(), s.length());
    }
    
    inline std::vector<unsigned char> SerializationContext::decodeBase64(const std::string &encodedString)
    {
        size_t in_len = encodedString.size();
        int i = 0;
        int j = 0;
        int in_ = 0;
        unsigned char char_array_4[4], char_array_3[3];
        std::vector<unsigned char> ret;
        
        while (in_len-- && (encodedString[in_] != '=') && is_base64(encodedString[in_]))
        {
            char_array_4[i++] = encodedString[in_];
            in_++;
            
            if (i == 4)
            {
                for (i = 0; i < 4; i++)
                {
                    char_array_4[i] = base64Chars.find(char_array_4[i]);
                }
                
                char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
                char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
                char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];
                
                for (i = 0; (i < 3); i++)
                {
                    ret.push_back(char_array_3[i]);
                }
                
                i = 0;
            }
        }
        
        if (i)
        {
            for (j = i; j < 4; j++)
            {
                char_array_4[j] = 0;
            }
            
            for (j = 0; j < 4; j++)
            {
                char_array_4[j] = base64Chars.find(char_array_4[j]);
            }
            
            char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
            char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
            char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];
            
            for (j = 0; (j < i - 1); j++)
            {
                ret.push_back(char_array_3[j]);
            }
        }
        
        return ret;
    }
} // namespace TinyRNN

#endif // TINYRNN_SERIALIZATIONCONTEXT_H_INCLUDED
