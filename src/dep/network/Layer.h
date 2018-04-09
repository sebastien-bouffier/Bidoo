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

#ifndef TINYRNN_LAYER_H_INCLUDED
#define TINYRNN_LAYER_H_INCLUDED

#include "Neuron.h"
#include "SerializedObject.h"
#include "UnrolledTrainingContext.h"
#include "Id.h"
#include "SerializationKeys.h"
#include "UnrolledNeuron.h"

namespace TinyRNN
{
    class Layer final : public SerializedObject
    {
    public:
        
        using Ptr = std::shared_ptr<Layer>;
        using HashMap = std::unordered_map<Id, Layer::Ptr>;
        using Vector = std::vector<Layer::Ptr>;
        
    public:
        
        Layer(int numNeurons, Neuron::ActivationType activation = Neuron::Sigmoid);
        Layer(int numNeurons, Value bias, Neuron::ActivationType activation);
        
        std::string getName() const noexcept;
        Id getUuid() const noexcept;
        size_t getSize() const noexcept;
        
        Neuron::Ptr getNeuron(size_t index) const;
        Neuron::Ptr getNeuronWithId(const Id &uuid) const;
        Neuron::Connection::HashMap findAllOutgoingConnections() const;
        
        bool isSelfConnected() const;
        Neuron::Connection::HashMap getSelfConnections() const;
        
        Neuron::Connection::HashMap connectAllToAll(Layer::Ptr other);
        Neuron::Connection::HashMap connectOneToOne(Layer::Ptr other);
        
        bool gateAllIncomingConnections(Layer::Ptr toLayer, const Neuron::Connection::HashMap &connections);
        bool gateAllOutgoingConnections(Layer::Ptr fromLayer, const Neuron::Connection::HashMap &connections);
        bool gateOneToOne(Layer::Ptr fromLayer, Layer::Ptr toLayer, const Neuron::Connection::HashMap &connections);
        
        // Used for the input layer
        bool feed(const Neuron::Values &values);
        
        // Used for all layers other that input
        Neuron::Values process();
        
        // Used for the output layer
        bool train(Value rate, const Neuron::Values &target);
        
        // Back-propagation magic
        void backPropagate(Value rate);
        
    public:
        
        virtual void deserialize(SerializationContext::Ptr context) override;
        virtual void serialize(SerializationContext::Ptr context) const override;
        
        UnrolledNeuron::Vector toVM(UnrolledTrainingContext::Ptr context,
                              bool asInput, bool asOutput,
                              bool asConst) const;

        void restore(UnrolledTrainingContext::Ptr context);

    private:
        
        Id uuid;
        std::string name;
        
        Neuron::Vector neurons;
        
    private:
        
        TINYRNN_DISALLOW_COPY_AND_ASSIGN(Layer);
    };
    
    //===------------------------------------------------------------------===//
    // Layer implementation
    //===------------------------------------------------------------------===//
    
    inline Layer::Layer(int numNeurons, Neuron::ActivationType activation) :
    uuid(Uuid::generateId())
    {
        this->neurons.reserve(numNeurons);
        for (int i = 0; i < numNeurons; ++i)
        {
            Neuron::Ptr neuron(new Neuron(activation));
            this->neurons.push_back(neuron);
        }
    }
    
    inline Layer::Layer(int numNeurons, Value bias, Neuron::ActivationType activation) :
    uuid(Uuid::generateId())
    {
        this->neurons.reserve(numNeurons);
        for (int i = 0; i < numNeurons; ++i)
        {
            Neuron::Ptr neuron(new Neuron(bias, activation));
            this->neurons.push_back(neuron);
        }
    }
    
    inline std::string Layer::getName() const noexcept
    {
        return this->name;
    }
    
    inline Id Layer::getUuid() const noexcept
    {
        return this->uuid;
    }
    
    inline size_t Layer::getSize() const noexcept
    {
        return this->neurons.size();
    }
    
    //===------------------------------------------------------------------===//
    // Batch connections
    //===------------------------------------------------------------------===//
    
    inline bool Layer::isSelfConnected() const
    {
        for (const auto &neuron : this->neurons)
        {
            if (! neuron->isSelfConnected())
            {
                return false;
            }
        }
        
        return true;
    }
    
    inline Neuron::Connection::HashMap Layer::getSelfConnections() const
    {
        Neuron::Connection::HashMap selfConnections;
        
        for (const auto &neuron : this->neurons)
        {
            if (neuron->isSelfConnected())
            {
                selfConnections[neuron->getUuid()] = neuron->getSelfConnection();
            }
        }
        
        return selfConnections;
    }
    
    inline Neuron::Connection::HashMap Layer::connectAllToAll(Layer::Ptr other)
    {
        Neuron::Connection::HashMap connections;
        
        for (Neuron::Ptr &neuronFrom : this->neurons)
        {
            for (Neuron::Ptr &neuronTo : other->neurons)
            {
                if (neuronFrom == neuronTo)
                {
                    continue;
                }
                
                Neuron::Connection::Ptr connection = neuronFrom->connectWith(neuronTo);
                connections[connection->getUuid()] = connection;
            }
        }
        
        return connections;
    }
    
    inline Neuron::Connection::HashMap Layer::connectOneToOne(Layer::Ptr other)
    {
        Neuron::Connection::HashMap connections;
        
        if (this->getSize() != other->getSize())
        {
            // Error
            return connections;
        }
        
        for (size_t i = this->neurons.size(); i --> 0 ;)
        {
            auto neuronFrom = this->neurons[i];
            auto neuronTo = other->neurons[i];
            auto connection = neuronFrom->connectWith(neuronTo);
            connections[connection->getUuid()] = connection;
        }
        
        return connections;
    }
    
    inline bool Layer::gateAllIncomingConnections(Layer::Ptr toLayer, const Neuron::Connection::HashMap &connections)
    {
        if (toLayer->getSize() != this->getSize())
        {
            return false;
        }
        
        for (size_t i = 0; i < toLayer->neurons.size(); ++i)
        {
            Neuron::Ptr targetNeuron = toLayer->neurons[i];
            Neuron::Ptr gaterNeuron = this->neurons[i];
            
            for (auto &i : targetNeuron->incomingConnections)
            {
                Neuron::Connection::Ptr gatedIncomingConnection = i.second;
                const bool shouldExcludeFromGating = (connections.find(gatedIncomingConnection->getUuid()) == connections.end());
                
                if (! shouldExcludeFromGating)
                {
                    gaterNeuron->gate(gatedIncomingConnection);
                }
            }
        }
        
        return true;
    }
    
    inline bool Layer::gateAllOutgoingConnections(Layer::Ptr fromLayer, const Neuron::Connection::HashMap &connections)
    {
        if (fromLayer->getSize() != this->getSize())
        {
            return false;
        }
        
        for (size_t i = 0; i < fromLayer->getSize(); ++i)
        {
            Neuron::Ptr targetNeuron = fromLayer->neurons[i];
            Neuron::Ptr gaterNeuron = this->neurons[i];
            
            for (auto &i : targetNeuron->outgoingConnections)
            {
                Neuron::Connection::Ptr gatedOutgoingConnection = i.second;
                const bool shouldExcludeFromGating = (connections.find(gatedOutgoingConnection->getUuid()) == connections.end());
                
                if (! shouldExcludeFromGating)
                {
                    gaterNeuron->gate(gatedOutgoingConnection);
                }
            }
        }
        
        return true;
    }
    
    inline bool Layer::gateOneToOne(Layer::Ptr fromLayer, Layer::Ptr toLayer, const Neuron::Connection::HashMap &connections)
    {
        if (connections.size() != this->getSize() ||
            fromLayer->getSize() != this->getSize() ||
            toLayer->getSize() != this->getSize())
        {
            return false;
        }
        
        for (size_t i = 0; i < fromLayer->getSize(); ++i)
        {
            Neuron::Ptr sourceNeuron = fromLayer->neurons[i];
            Neuron::Ptr gaterNeuron = this->neurons[i];
            
            for (auto &i : sourceNeuron->outgoingConnections)
            {
                Neuron::Connection::Ptr gatedConnection = i.second;
                const bool isTargetConnection = (connections.find(gatedConnection->getUuid()) != connections.end());
                
                if (isTargetConnection)
                {
                    gaterNeuron->gate(gatedConnection);
                    break;
                }
            }
        }
        
        return true;
    }
    
    //===------------------------------------------------------------------===//
    // Batch processing
    //===------------------------------------------------------------------===//
    
    inline bool Layer::feed(const Neuron::Values &values)
    {
        if (values.size() != this->neurons.size())
        {
            // INPUT size and LAYER size must be the same size to activate!
            return false;
        }
        
        for (size_t i = 0; i < this->neurons.size(); ++i)
        {
            this->neurons[i]->feed(values[i]);
        }
        
        return true;
    }
    
    inline Neuron::Values Layer::process()
    {
        Neuron::Values result;
        
        for (auto &neuron : this->neurons)
        {
            const Value activation = neuron->process();
            result.push_back(activation);
        }
        
        return result;
    }
    
    inline bool Layer::train(Value rate, const Neuron::Values &target)
    {
        if (target.size() != this->neurons.size())
        {
            return false;
        }
        
        // Unsigned backwards iteration from (this->neurons.size() - 1) to 0
        for (size_t i = this->neurons.size(); i --> 0 ;)
        {
            this->neurons[i]->train(rate, target[i]);
        }
        
        return true;
    }
    
    inline void Layer::backPropagate(Value rate)
    {
        for (size_t i = this->neurons.size(); i --> 0 ;)
        {
            this->neurons[i]->backPropagate(rate);
        }
    }
    
    //===------------------------------------------------------------------===//
    // Collecting data
    //===------------------------------------------------------------------===//
    
    inline Neuron::Ptr Layer::getNeuron(size_t index) const
    {
        return this->neurons[index];
    }
    
    // todo optimize?
    // currently O(n), but used only in network deserialization
    // also use a hashmap?
    inline Neuron::Ptr Layer::getNeuronWithId(const Id &uuid) const
    {
        for (const auto &neuron : this->neurons)
        {
            if (neuron->getUuid() == uuid)
            {
                return neuron;
            }
        }
        
        return nullptr;
    }
    
    inline Neuron::Connection::HashMap Layer::findAllOutgoingConnections() const
    {
        Neuron::Connection::HashMap result;
        
        for (const auto &neuron : this->neurons)
        {
            const Neuron::Connection::HashMap neuronOutgoingConnections = neuron->getOutgoingConnections();
            result.insert(neuronOutgoingConnections.begin(), neuronOutgoingConnections.end());
        }
        
        return result;
    }
    
    //===------------------------------------------------------------------===//
    // Serialization
    //===------------------------------------------------------------------===//
    
    inline void Layer::deserialize(SerializationContext::Ptr context)
    {
        this->uuid = context->getNumberProperty(Keys::Core::Uuid);
        this->name = context->getStringProperty(Keys::Core::Name);
        
        this->neurons.clear();
        SerializationContext::Ptr neuronsNode(context->getChildContext(Keys::Core::Neurons));
        
        for (size_t i = 0; i < neuronsNode->getNumChildrenContexts(); ++i)
        {
            SerializationContext::Ptr neuronNode(neuronsNode->getChildContext(i));
            Neuron::Ptr neuron(new Neuron());
            neuron->deserialize(neuronNode);
            this->neurons.push_back(neuron);
        }
    }
    
    inline void Layer::serialize(SerializationContext::Ptr context) const
    {
        context->setNumberProperty(this->uuid, Keys::Core::Uuid);
        context->setStringProperty(this->name, Keys::Core::Name);
        
        SerializationContext::Ptr allNeuronsNode(context->addChildContext(Keys::Core::Neurons));
        for (const auto &neuron : this->neurons)
        {
            SerializationContext::Ptr neuronNode(allNeuronsNode->addChildContext(Keys::Core::Neuron));
            neuron->serialize(neuronNode);
        }
    }
    
    //===------------------------------------------------------------------===//
    // Batch hardcoding stuff
    //===------------------------------------------------------------------===//
    
    inline UnrolledNeuron::Vector Layer::toVM(UnrolledTrainingContext::Ptr context,
                                        bool asInput, bool asOutput,
                                        bool asConst) const
    {
        UnrolledNeuron::Vector result;
        
        for (auto &neuron : this->neurons)
        {
            result.push_back(UnrolledNeuron::buildFrom(context, neuron, asInput, asOutput, asConst));
        }
        
        return result;
    }
        
    inline void Layer::restore(UnrolledTrainingContext::Ptr context)
    {
        for (auto &neuron : this->neurons)
        {
            context->restoreNeuronState(neuron);
        }
    }
    
} // namespace TinyRNN

#endif // TINYRNN_LAYER_H_INCLUDED
