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

#ifndef EVENTEMITTER_H_INCLUDED
#define EVENTEMITTER_H_INCLUDED

#include <functional>

namespace TinyRNN
{
    template <typename... Args>
    class EventEmitter
    {
    public:
        
        using Callback = std::function<void(Args...)>;
        using CallbacksList = std::list<Callback>;
        using CallbacksListPtr = std::shared_ptr<CallbacksList>;
        using CallbacksListWeakPtr = std::weak_ptr<CallbacksList>;
        
    public:
        
        class ScopedSubscription
        {
        public:
            
            ScopedSubscription() {}
            
            ScopedSubscription(EventEmitter &emitter, Callback callback)
            {
                this->subscribe(emitter, callback);
            }
            
            ScopedSubscription(ScopedSubscription &&other)
            {
                this->callbacksList = other.callbacksList;
                this->targetCallback = other.targetCallback;
                other.callbacksList.reset();
            }
            
            virtual ~ScopedSubscription()
            {
                this->unsubscribeAndReset();
            }
            
            ScopedSubscription &operator=(ScopedSubscription &&other)
            {
                this->unsubscribeAndReset();
                this->callbacksList = other.callbacksList;
                this->targetCallback = other.targetCallback;
                other.callbacksList.reset();
                return *this;
            }
            
        private:
            
            void subscribe(EventEmitter &emitter, Callback callback)
            {
                this->unsubscribeAndReset();
                this->callbacksList = emitter.callbacksList;
                this->targetCallback = emitter.callbacksList->insert(emitter.callbacksList->end(), callback);
            }
            
            void unsubscribeAndReset()
            {
                if (! this->callbacksList.expired())
                {
                    this->callbacksList.lock()->erase(this->targetCallback);
                }
                
                this->callbacksList.reset();
            }
            
            CallbacksListWeakPtr callbacksList;
            typename CallbacksList::iterator targetCallback;

            TINYRNN_DISALLOW_COPY_AND_ASSIGN(ScopedSubscription);
        };
        
        EventEmitter() : callbacksList(std::make_shared<CallbacksList>()) {}
        
        virtual ~EventEmitter() {}
        
        void emit(Args... args)
        {
            for (auto &callback : *this->callbacksList)
            {
                callback(args...);
            }
        }
        
        ScopedSubscription subscribe(Callback callback)
        {
            return ScopedSubscription(*this, callback);
        }
        
    private:
        
        CallbacksListPtr callbacksList;
        
        TINYRNN_DISALLOW_COPY_AND_ASSIGN(EventEmitter);
    };
}

#endif // EVENTEMITTER_H_INCLUDED
