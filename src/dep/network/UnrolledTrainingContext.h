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

#ifndef TINYRNN_UNROLLEDTRAININGCONTEXT_H_INCLUDED
#define TINYRNN_UNROLLEDTRAININGCONTEXT_H_INCLUDED

#include "Common.h"
#include "Id.h"
#include "Neuron.h"
#include "SerializedObject.h"

#include <random>
#include <iostream>
#include <sstream>
#include <iterator>
#include <numeric>

namespace TinyRNN
{   
    class UnrolledTrainingContext final : public SerializedObject
    {
    public:
        
        using Ptr = std::shared_ptr<UnrolledTrainingContext>;
        using RawData = std::vector<Value>;
        using Indices = std::vector<Index>;
        using Mapping = std::map<std::string, Index>;
        using VariableKey = std::vector<Id>;
        
    public:
        
        UnrolledTrainingContext();
        
        void restoreNeuronState(Neuron::Ptr targetNeuron);

        Value evaluateVariable(const VariableKey &variableKey, Value defaultValue);
        Index allocateOrReuseVariable(Value value, const VariableKey &variableKey);
        
        void registerInputVariable(Index variableIndex);
        void registerOutputVariable(Index variableIndex);
        void registerTargetVariable(Index variableIndex);
        void registerRateVariable(Index variableIndex);
        
        Indices getInputVariables() const;
        Indices getOutputVariables() const;
        Indices getTargetVariables() const;
        Index getRateVariable() const;
        
        RawData &getMemory();
        RawData &getOutputs();
        
        void clear();
        void clearMappings();
        
    public:
        
        virtual void deserialize(SerializationContext::Ptr context) override;
        virtual void serialize(SerializationContext::Ptr context) const override;
        
    private:
        
        RawData memory;                         // the actual data passed to the kernel
        Mapping mapping;                        // variable name connected to its index in memory
        
        Indices inputVariables;                 // indices of input variables
        Indices outputVariables;                // indices of output variables
        Indices targetVariables;                // indices of target variables
        Index rateVariable;
        
    private: // temporary stuff, never serialized:
        
        RawData outputs;                        // holds the most recent output
        
    private:
        
        std::string getKeyForVariable(const VariableKey &variableKey) const;
        
        TINYRNN_DISALLOW_COPY_AND_ASSIGN(UnrolledTrainingContext);
    };
    
    //===------------------------------------------------------------------===//
    // UnrolledTrainingContext implementation
    //===------------------------------------------------------------------===//
    
    inline UnrolledTrainingContext::UnrolledTrainingContext() : rateVariable(0)
    {}
    
    inline Index UnrolledTrainingContext::allocateOrReuseVariable(Value value, const VariableKey &variableKey)
    {
        const std::string &key = this->getKeyForVariable(variableKey);
        const bool variableExists = (this->mapping.find(key) != this->mapping.end());
        
        if (variableExists)
        {
            const Index variableIndex = this->mapping[key];
            this->memory[variableIndex] = value;
            return variableIndex;
        }
        else
        {
            //std::cout << this->memory.size() << " is " << key << std::endl;
            this->memory.push_back(value);
            const Index variableIndex = (this->memory.size() - 1);
            this->mapping[key] = variableIndex;
            return variableIndex;
        }
        
        return 0;
    }
    
    inline Value UnrolledTrainingContext::evaluateVariable(const VariableKey &variableKey, Value defaultValue)
    {
        const std::string &key = this->getKeyForVariable(variableKey);
        const bool variableExists = (this->mapping.find(key) != this->mapping.end());
        
        if (variableExists)
        {
            const Index variableIndex = this->mapping[key];
            //std::cout << "Variable: " << key << " = " << std::to_string(this->memory[variableIndex]) << std::endl;
            return this->memory[variableIndex];
        }
        
        //std::cout << "Variable missing: " << key << ", default to " << std::to_string(defaultValue) << std::endl;
        return defaultValue;
    }
    
    inline std::string UnrolledTrainingContext::getKeyForVariable(const VariableKey &variableKey) const
    {
        std::ostringstream key;
        
        std::copy(variableKey.begin(),
                  variableKey.end() - 1,
                  std::ostream_iterator<Id>(key, "::"));
        
        key << variableKey.back();
        
        return key.str();
    }
    
    inline void UnrolledTrainingContext::registerInputVariable(Index variableIndex)
    {
        this->inputVariables.push_back(variableIndex);
    }
    
    inline void UnrolledTrainingContext::registerOutputVariable(Index variableIndex)
    {
        this->outputVariables.push_back(variableIndex);
        this->outputs.resize(this->outputVariables.size());
    }
    
    inline void UnrolledTrainingContext::registerTargetVariable(Index variableIndex)
    {
        this->targetVariables.push_back(variableIndex);
    }
    
    inline void UnrolledTrainingContext::registerRateVariable(Index variableIndex)
    {
        this->rateVariable = variableIndex;
    }
    
    inline UnrolledTrainingContext::Indices 
    UnrolledTrainingContext::getInputVariables() const
    {
        return this->inputVariables;
    }
    
    inline UnrolledTrainingContext::Indices
    UnrolledTrainingContext::getOutputVariables() const
    {
        return this->outputVariables;
    }
    
    inline UnrolledTrainingContext::Indices
    UnrolledTrainingContext::getTargetVariables() const
    {
        return this->targetVariables;
    }
    
    inline Index UnrolledTrainingContext::getRateVariable() const
    {
        return this->rateVariable;
    }
    
    inline UnrolledTrainingContext::RawData &UnrolledTrainingContext::getMemory()
    {
        return this->memory;
    }
    
    inline UnrolledTrainingContext::RawData &UnrolledTrainingContext::getOutputs()
    {
        return this->outputs;
    }
    
    inline void UnrolledTrainingContext::clear()
    {
        this->memory.clear();
        this->outputs.clear();
        this->mapping.clear();
        this->inputVariables.clear();
        this->outputVariables.clear();
        this->targetVariables.clear();
        this->rateVariable = 0;
    }
    
    inline void UnrolledTrainingContext::clearMappings()
    {
        this->mapping.clear();
    }
    
    //===------------------------------------------------------------------===//
    // Restore neuron state
    //===------------------------------------------------------------------===//
    
    inline void UnrolledTrainingContext::restoreNeuronState(Neuron::Ptr target)
    {
        const Value bias = this->evaluateVariable({target->getUuid(), Keys::Mapping::Bias}, target->bias);
        const Value state = this->evaluateVariable({target->getUuid(), Keys::Mapping::State}, target->state);
        const Value oldState = this->evaluateVariable({target->getUuid(), Keys::Mapping::OldState}, target->oldState);
        const Value activation = this->evaluateVariable({target->getUuid(), Keys::Mapping::Activation}, target->activation);
        
        target->bias = bias;
        target->state = state;
        target->oldState = oldState;
        target->activation = activation;
        
        for (auto &i : target->eligibility)
        {
            const Id &inputConnectionUuid = i.first;
            target->eligibility[inputConnectionUuid] =
            this->evaluateVariable({target->getUuid(), inputConnectionUuid, Keys::Mapping::Eligibility},
                                   target->eligibility[inputConnectionUuid]);
        }
        
        for (auto &i : target->extended)
        {
            const Id &neighbourNeuronUuid = i.first;
            Neuron::EligibilityMap &map = i.second;
            
            for (auto &j : map)
            {
                const Id &inputConnectionUuid = j.first;
                
                const Value extendedTrace =
                this->evaluateVariable({target->getUuid(), neighbourNeuronUuid, inputConnectionUuid, Keys::Mapping::ExtendedTrace},
                                       target->extended[neighbourNeuronUuid][inputConnectionUuid]);
                
                target->extended[neighbourNeuronUuid][inputConnectionUuid] = extendedTrace;
            }
        }
        
        for (auto &i : target->outgoingConnections)
        {
            auto outgoingConnection = i.second;
            auto outgoingConnectionUuid = i.first;
            
            outgoingConnection->weight = this->evaluateVariable({outgoingConnectionUuid, Keys::Mapping::Weight},
                                                                outgoingConnection->weight);
            
            outgoingConnection->gain = this->evaluateVariable({outgoingConnectionUuid, Keys::Mapping::Gain},
                                                              outgoingConnection->gain);
        }
        
        if (target->isSelfConnected())
        {
            auto selfConnection = target->getSelfConnection();
            
            selfConnection->weight = this->evaluateVariable({selfConnection->getUuid(), Keys::Mapping::Weight},
                                                            selfConnection->weight);
            
            selfConnection->gain = this->evaluateVariable({selfConnection->getUuid(), Keys::Mapping::Gain},
                                                          selfConnection->gain);
        }
    }
    
    //===------------------------------------------------------------------===//
    // Serialization
    //===------------------------------------------------------------------===//
    
    inline void UnrolledTrainingContext::deserialize(SerializationContext::Ptr context)
    {
        this->clear();
        
        const std::string &memoryEncoded = context->getStringProperty(Keys::Unrolled::RawMemory);
        const size_t memorySize = context->getNumberProperty(Keys::Unrolled::MemorySize);
        
        this->memory.resize(memorySize);
        const std::vector<unsigned char> &memoryDecoded = context->decodeBase64(memoryEncoded);
        std::memcpy(this->memory.data(), memoryDecoded.data(), sizeof(Value) * memorySize);
        
        if (auto mappingNode = context->getChildContext(Keys::Unrolled::VariablesMapping))
        {
            for (size_t i = 0; i < mappingNode->getNumChildrenContexts(); ++i)
            {
                SerializationContext::Ptr variableNode(mappingNode->getChildContext(i));
                const std::string &key = variableNode->getStringProperty(Keys::Unrolled::Key);
                const size_t index = variableNode->getNumberProperty(Keys::Unrolled::Index);
                this->mapping[key] = index;
            }
        }
        
        if (auto inputsNode = context->getChildContext(Keys::Unrolled::InputsMapping))
        {
            for (size_t i = 0; i < inputsNode->getNumChildrenContexts(); ++i)
            {
                SerializationContext::Ptr variableNode(inputsNode->getChildContext(i));
                const size_t index = variableNode->getNumberProperty(Keys::Unrolled::Index);
                this->inputVariables.push_back(index);
            }
        }
        
        if (auto outputsNode = context->getChildContext(Keys::Unrolled::OutputsMapping))
        {
            for (size_t i = 0; i < outputsNode->getNumChildrenContexts(); ++i)
            {
                SerializationContext::Ptr variableNode(outputsNode->getChildContext(i));
                const size_t index = variableNode->getNumberProperty(Keys::Unrolled::Index);
                this->outputVariables.push_back(index);
            }
            
            this->outputs.resize(this->outputVariables.size());
        }
        
        if (auto targetsNode = context->getChildContext(Keys::Unrolled::TargetsMapping))
        {
            for (size_t i = 0; i < targetsNode->getNumChildrenContexts(); ++i)
            {
                SerializationContext::Ptr variableNode(targetsNode->getChildContext(i));
                const size_t index = variableNode->getNumberProperty(Keys::Unrolled::Index);
                this->targetVariables.push_back(index);
            }
        }
        
        if (auto rateNode = context->getChildContext(Keys::Unrolled::RateMapping))
        {
            this->rateVariable = rateNode->getNumberProperty(Keys::Unrolled::Index);
        }
    }
    
    inline void UnrolledTrainingContext::serialize(SerializationContext::Ptr context) const
    {
        const std::string memoryEncoded =
        context->encodeBase64((const unsigned char *)this->memory.data(),
                              sizeof(Value) * this->memory.size());
        
        context->setStringProperty(memoryEncoded, Keys::Unrolled::RawMemory);
        context->setNumberProperty(this->memory.size(), Keys::Unrolled::MemorySize);
        
        SerializationContext::Ptr mappingNode(context->addChildContext(Keys::Unrolled::VariablesMapping));
        for (const auto &i : this->mapping)
        {
            SerializationContext::Ptr variableNode(mappingNode->addChildContextUnordered(Keys::Unrolled::Variable));
            variableNode->setStringProperty(i.first, Keys::Unrolled::Key);
            variableNode->setNumberProperty(i.second, Keys::Unrolled::Index);
        }
        
        SerializationContext::Ptr inputsNode(context->addChildContext(Keys::Unrolled::InputsMapping));
        for (const auto &i : this->inputVariables)
        {
            SerializationContext::Ptr variableNode(inputsNode->addChildContext(Keys::Unrolled::Variable));
            variableNode->setNumberProperty(i, Keys::Unrolled::Index);
        }
        
        SerializationContext::Ptr outputsNode(context->addChildContext(Keys::Unrolled::OutputsMapping));
        for (const auto &i : this->outputVariables)
        {
            SerializationContext::Ptr variableNode(outputsNode->addChildContext(Keys::Unrolled::Variable));
            variableNode->setNumberProperty(i, Keys::Unrolled::Index);
        }
        
        SerializationContext::Ptr targetsNode(context->addChildContext(Keys::Unrolled::TargetsMapping));
        for (const auto &i : this->targetVariables)
        {
            SerializationContext::Ptr variableNode(targetsNode->addChildContext(Keys::Unrolled::Variable));
            variableNode->setNumberProperty(i, Keys::Unrolled::Index);
        }
        
        SerializationContext::Ptr rateNode(context->addChildContext(Keys::Unrolled::RateMapping));
        rateNode->setNumberProperty(this->rateVariable, Keys::Unrolled::Index);
    }
}  // namespace TinyRNN

#endif  // TINYRNN_UNROLLEDTRAININGCONTEXT_H_INCLUDED
