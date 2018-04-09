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

#ifndef TINYRNN_NEURON_H_INCLUDED
#define TINYRNN_NEURON_H_INCLUDED

#include "SerializedObject.h"
#include "Id.h"
#include "SerializationKeys.h"

namespace TinyRNN
{
    class TrainingContext;
    
    class Neuron final : public SerializedObject,
                         public std::enable_shared_from_this<Neuron>
    {
    public:
        
        using Ptr = std::shared_ptr<Neuron>;
        using WeakPtr = std::weak_ptr<Neuron>;
        using HashMap = std::unordered_map<Id, Neuron::Ptr>;
        using Vector = std::vector<Neuron::Ptr>;
        using Values = std::vector<Value>;
        
    public:
        
        class Connection final : public SerializedObject,
                                 public std::enable_shared_from_this<Connection>
        {
        public:
            
            using Ptr = std::shared_ptr<Connection>;
            using HashMap = std::unordered_map<Id, Connection::Ptr>;
            using SortedMap = std::map<Id, Connection::Ptr>;
            
        public:
            
            Connection();
            
            Connection(Neuron::WeakPtr input,
                       Neuron::WeakPtr output);
            
            Id getUuid() const noexcept;
            
            Neuron::Ptr getInputNeuron() const;
            Neuron::Ptr getGateNeuron() const;
            Neuron::Ptr getOutputNeuron() const;
            
            bool hasGate() const noexcept;
            void setGate(Neuron::WeakPtr gateNeuron);
            void connect(Neuron::WeakPtr inputNeuron, Neuron::WeakPtr outputNeuron);
            
        public:
            
            virtual void deserialize(SerializationContext::Ptr context) override;
            virtual void serialize(SerializationContext::Ptr context) const override;
            
        private:
            
            Id uuid;
            
            Value weight;
            Value gain;
            
            void setRandomWeight();
            
            Neuron::WeakPtr inputNeuron;
            Neuron::WeakPtr gateNeuron;
            Neuron::WeakPtr outputNeuron;
            
            friend class Neuron;
            friend class UnrolledNeuron;
            friend class UnrolledTrainingContext;
            
        private:
            
            TINYRNN_DISALLOW_COPY_AND_ASSIGN(Connection);
        };
        
    public:
        
        enum ActivationType
        {
            Sigmoid,    // Outputs a value between 0 and 1, useful for gates
            Tanh,
            LeakyReLU   // Computationally less complex, no vanishing gradient
        };
        
        explicit Neuron(ActivationType defaultActivation = Tanh);
        Neuron(Value defaultBias, ActivationType defaultActivation = Sigmoid);
        
        Id getUuid() const noexcept;
        Connection::HashMap getOutgoingConnections() const;
        
        bool isGate() const noexcept;
        bool isSelfConnected() const noexcept;
        bool isConnectedTo(Neuron::Ptr other) const;
        Connection::Ptr getSelfConnection() const noexcept;
        
        Connection::Ptr findConnectionWith(Neuron::Ptr other) const;
        Connection::Ptr findOutgoingConnectionTo(Neuron::Ptr other) const;
        Connection::Ptr findIncomingConnectionFrom(Neuron::Ptr other) const;
        
        Connection::Ptr connectWith(Neuron::Ptr other);
        
        void gate(Connection::Ptr connection);
        
        // Used for the neurons in input layer
        void feed(Value value);
        
        // Used for all layers other that input
        Value process();
        
        // Used for the neurons in output layer
        void train(Value rate, Value target);
        
        // Used for all layers other that input
        void backPropagate(Value rate);
        
    public:
        
        virtual void deserialize(SerializationContext::Ptr context) override;
        virtual void serialize(SerializationContext::Ptr context) const override;
        
    private:
        
        Id uuid;
        
        ActivationType activationType;
        
        Value bias;
        Value activation;
        Value derivative;
        
        Value state;
        Value oldState;
        
        Value errorResponsibility;
        Value projectedActivity;
        Value gatingActivity;
        
        bool isGatingAnyConnection;
        
        void feedWithRandomBias(Value signal);
        void setRandomBias();
        
        static Value activationSigmoid(Value x);
        static Value derivativeSigmoid(Value x);
        static Value activationTanh(Value x);
        static Value derivativeTanh(Value x);
        static Value activationReLU(Value x);
        static Value derivativeReLU(Value x);
        
        Connection::HashMap incomingConnections;
        Connection::HashMap outgoingConnections;
        Connection::HashMap gatedConnections;
        Connection::Ptr selfConnection;
        
        bool isOutput() const;
        void learn(Value rate = 0.1);
        
        friend class Layer;
        friend class UnrolledNeuron;
        friend class UnrolledTrainingContext;
        
    private:
        
        // The cache maps, never serialized
        // Consume A LOT of memory
        using EligibilityMap = std::unordered_map<Id, Value>;
        using ExtendedEligibilityMap = std::unordered_map<Id, EligibilityMap>;
        using Influences = std::unordered_map<Id, Connection::Ptr>;
        using InfluencesMap = std::unordered_map<Id, Influences>;
        
        mutable InfluencesMap influences;
        mutable EligibilityMap eligibility;
        mutable ExtendedEligibilityMap extended;
        
        mutable Neuron::HashMap neighbours;
        
    private:
        
        TINYRNN_DISALLOW_COPY_AND_ASSIGN(Neuron);
    };
    
    //===------------------------------------------------------------------===//
    // Neuron implementation
    //===------------------------------------------------------------------===//
    
    inline Neuron::Neuron(ActivationType defaultActivation) :
    uuid(Uuid::generateId()),
    activationType(defaultActivation),
    bias(0.0),
    activation(0.0),
    derivative(0.0),
    state(0.0),
    oldState(0.0),
    errorResponsibility(0.0),
    projectedActivity(0.0),
    gatingActivity(0.0),
    isGatingAnyConnection(false)
    {
        this->setRandomBias();
    }
    
    inline Neuron::Neuron(Value defaultBias, ActivationType defaultActivation) :
    uuid(Uuid::generateId()),
    activationType(defaultActivation),
    bias(defaultBias),
    activation(0.0),
    derivative(0.0),
    state(0.0),
    oldState(0.0),
    errorResponsibility(0.0),
    projectedActivity(0.0),
    gatingActivity(0.0),
    isGatingAnyConnection(false)
    {
    }
    
    inline void Neuron::feedWithRandomBias(Value signal)
    {
        this->activation = signal;
        this->derivative = 0.0;
        this->setRandomBias();
    }
    
    inline void Neuron::setRandomBias()
    {
        std::random_device randomDevice;
        std::mt19937 mt19937(randomDevice());
        std::uniform_real_distribution<Value> distribution(-0.001, 0.001);
        this->bias = distribution(mt19937);
    }
    
    inline Id Neuron::getUuid() const noexcept
    {
        return this->uuid;
    }
    
    //===------------------------------------------------------------------===//
    // Connections
    //===------------------------------------------------------------------===//
    
    inline Neuron::Connection::HashMap Neuron::getOutgoingConnections() const
    {
        Connection::HashMap outgoing;
        outgoing.insert(this->outgoingConnections.begin(), this->outgoingConnections.end());
        
        if (this->isSelfConnected())
        {
            outgoing[this->selfConnection->getUuid()] = this->selfConnection;
        }
        
        return outgoing;
    }
    
    inline Neuron::Connection::Ptr Neuron::connectWith(Neuron::Ptr other)
    {
        if (other.get() == this)
        {
            this->selfConnection = Connection::Ptr(new Connection(this->shared_from_this(),
                                                                  this->shared_from_this()));
            return this->selfConnection;
        }
        
        if (Connection::Ptr existingOutgoingConnection = this->findOutgoingConnectionTo(other))
        {
            return existingOutgoingConnection;
        }
        
        Connection::Ptr newConnection(new Connection(this->shared_from_this(), other));
        const Id newConnectionId = newConnection->getUuid();
        
        // reference all the connections
        this->outgoingConnections[newConnectionId] = newConnection;
        other->incomingConnections[newConnectionId] = newConnection;
        
        // reference traces
        this->neighbours[other->getUuid()] = other;
        other->eligibility[newConnectionId] = 0.0;
        
        for (auto &extendedTrace : other->extended)
        {
            extendedTrace.second[newConnectionId] = 0.0;
        }
        
        return newConnection;
    }
    
    inline void Neuron::gate(Connection::Ptr connection)
    {
        const Id connectionId = connection->getUuid();
        
        // add connection to gated list
        this->gatedConnections[connectionId] = connection;
        
        Neuron::Ptr targetNeuron = connection->getOutputNeuron();
        
        // update traces
        const bool targetNeuronNotFoundInExtendedTrace = (this->extended.find(targetNeuron->getUuid()) == this->extended.end());
        if (targetNeuronNotFoundInExtendedTrace)
        {
            // extended trace
            this->neighbours[targetNeuron->getUuid()] = targetNeuron;
            
            EligibilityMap &xtrace = this->extended[targetNeuron->getUuid()];
            xtrace.clear();
            
            for (auto &i : this->incomingConnections)
            {
                Connection::Ptr input = i.second;
                xtrace[input->getUuid()] = 0.0;
            }
        }
        
        this->influences[targetNeuron->getUuid()][connection->getUuid()] = connection;
        
        this->isGatingAnyConnection = true;
        
        // set gater
        connection->setGate(this->shared_from_this());
    }
    
    //===------------------------------------------------------------------===//
    // Core
    //===------------------------------------------------------------------===//
    
    inline void Neuron::feed(Value signalValue)
    {
        const bool noInputConnections = this->incomingConnections.empty();
        const bool hasOutputConnections = !this->outgoingConnections.empty();
        const bool isInputNeuron = (noInputConnections && hasOutputConnections);
        
        if (isInputNeuron)
        {
            this->feedWithRandomBias(signalValue);
        }
    }
    
    inline Value Neuron::process()
    {
        this->oldState = this->state;
        
        // eq. 15
        if (this->isSelfConnected())
        {
            this->state = this->selfConnection->gain * this->selfConnection->weight * this->state + this->bias;
        }
        else
        {
            this->state = this->bias;
        }
        
        for (auto &i : this->incomingConnections)
        {
            const Connection::Ptr inputConnection = i.second;
            this->state += inputConnection->getInputNeuron()->activation * inputConnection->weight * inputConnection->gain;
        }
        
        switch (this->activationType)
        {
            case Sigmoid:
                this->activation = Neuron::activationSigmoid(this->state);
                this->derivative = Neuron::derivativeSigmoid(this->state);
                break;
            case Tanh:
                this->activation = Neuron::activationTanh(this->state);
                this->derivative = Neuron::derivativeTanh(this->state);
                break;
            case LeakyReLU:
                this->activation = Neuron::activationReLU(this->state);
                this->derivative = Neuron::derivativeReLU(this->state);
                break;
        }
        
        // update traces
        EligibilityMap influences;
        for (auto &id : this->extended)
        {
            // extended elegibility trace
            Neuron::Ptr neighbour = this->neighbours[id.first];
            
            Value influence = 0.0;
            
            // if gated neuron's selfconnection is gated by this unit, the influence keeps track of the neuron's old state
            if (Connection::Ptr neighbourSelfconnection = neighbour->getSelfConnection())
            {
                if (neighbourSelfconnection->getGateNeuron().get() == this)
                {
                    influence = neighbour->oldState;
                }
            }
            
            // index runs over all the incoming connections to the gated neuron that are gated by this unit
            for (auto &incoming : this->influences[neighbour->getUuid()])
            { // captures the effect that has an input connection to this unit, on a neuron that is gated by this unit
                const Connection::Ptr inputConnection = incoming.second;
                influence += inputConnection->weight * inputConnection->getInputNeuron()->activation;
            }
            
            influences[neighbour->getUuid()] = influence;
        }
        
        for (auto &i : this->incomingConnections)
        {
            const Connection::Ptr inputConnection = i.second;
            
            // elegibility trace - Eq. 17
            const Value oldElegibility = this->eligibility[inputConnection->getUuid()];
            this->eligibility[inputConnection->getUuid()] = inputConnection->gain * inputConnection->getInputNeuron()->activation;
            
            if (this->isSelfConnected())
            {
                this->eligibility[inputConnection->getUuid()] += this->selfConnection->gain * this->selfConnection->weight * oldElegibility;
            }
            
            for (auto &i : this->extended)
            {
                // extended elegibility trace
                const Id neuronId = i.first;
                const Value influence = influences[neuronId];
                EligibilityMap &xtrace = i.second;
                Neuron::Ptr neighbour = this->neighbours[neuronId];
                
                // eq. 18
                const Value oldXTrace = xtrace[inputConnection->getUuid()];
                xtrace[inputConnection->getUuid()] = this->derivative * this->eligibility[inputConnection->getUuid()] * influence;
                
                if (Connection::Ptr neighbourSelfConnection = neighbour->getSelfConnection())
                {
                    xtrace[inputConnection->getUuid()] += neighbourSelfConnection->gain * neighbourSelfConnection->weight * oldXTrace;
                }
            }
        }
        
        // update gated connection's gains
        for (auto &i : this->gatedConnections)
        {
            const Connection::Ptr connection = i.second;
            connection->gain = this->activation;
        }
        
        return this->activation;
    }
    
    inline bool Neuron::isOutput() const
    {
        const bool noProjections = this->outgoingConnections.empty();
        const bool noGates = this->gatedConnections.empty();
        const bool isOutput = (noProjections && noGates);
        return isOutput;
    }
    
    inline void Neuron::train(Value rate, Value target)
    {
        // output neurons get their error from the enviroment
        if (this->isOutput())
        {
            this->errorResponsibility = this->projectedActivity = target - this->activation; // Eq. 10
            this->learn(rate);
        }
    }
    
    inline void Neuron::backPropagate(Value rate)
    {
        Value errorAccumulator = 0.0;
        
        // the rest of the neuron compute their error responsibilities by backpropagation
        if (! this->isOutput())
        {
            // error responsibilities from all the connections projected from this neuron
            for (auto &i : this->outgoingConnections)
            {
                const Connection::Ptr outputConnection = i.second;
                // Eq. 21
                errorAccumulator += outputConnection->getOutputNeuron()->errorResponsibility * outputConnection->gain * outputConnection->weight;
            }
            
            // projected error responsibility
            this->projectedActivity = this->derivative * errorAccumulator;
            
            errorAccumulator = 0.0;
            
            // error responsibilities from all the connections gated by this neuron
            for (auto &i : this->extended)
            {
                const Id gatedNeuronId = i.first;
                const Neuron::Ptr gatedNeuron = this->neighbours[gatedNeuronId];
                
                Value influence = 0.0;
                
                // if gated neuron's selfconnection is gated by this neuron
                if (Connection::Ptr gatedNeuronSelfConnection = gatedNeuron->getSelfConnection())
                {
                    if (gatedNeuronSelfConnection->getGateNeuron().get() == this)
                    {
                        influence = gatedNeuron->oldState;
                    }
                }
                
                // index runs over all the connections to the gated neuron that are gated by this neuron
                for (auto &i : this->influences[gatedNeuronId])
                { // captures the effect that the input connection of this neuron have, on a neuron which its input/s is/are gated by this neuron
                    const Connection::Ptr inputConnection = i.second;
                    influence += inputConnection->weight * inputConnection->getInputNeuron()->activation;
                }
                
                // eq. 22
                errorAccumulator += gatedNeuron->errorResponsibility * influence;
            }
            
            // gated error responsibility
            this->gatingActivity = this->derivative * errorAccumulator;
            
            // error responsibility - Eq. 23
            this->errorResponsibility = this->projectedActivity + this->gatingActivity;
            
            this->learn(rate);
        }
    }
    
    static float clip(float n, float lower, float upper)
    {
        return std::max(lower, std::min(n, upper));
    }
    
    inline void Neuron::learn(Value rate)
    {
        // adjust all the neuron's incoming connections
        for (auto &i : this->incomingConnections)
        {
            const Id inputConnectionUuid = i.first;
            const Connection::Ptr inputConnection = i.second;
            
            // Eq. 24
            Value gradient = this->projectedActivity * this->eligibility[inputConnectionUuid];
            for (auto &ext : this->extended)
            {
                const Id neighbourUuid = ext.first;
                const Neuron::Ptr neighbour = this->neighbours[neighbourUuid];
                gradient += neighbour->errorResponsibility * this->extended[neighbourUuid][inputConnectionUuid];
            }
            
            const auto clippedGradient = clip(gradient, -TINYRNN_GRADIENT_CLIPPING_THRESHOLD, TINYRNN_GRADIENT_CLIPPING_THRESHOLD);
            inputConnection->weight += rate * clippedGradient; // adjust weights - aka learn
        }
        
        // adjust bias
        this->bias += rate * this->errorResponsibility;
    }
    
    inline Value Neuron::activationSigmoid(Value x)
    {
        return 1.0 / (1.0 + exp(-x));
    }
    
    inline Value Neuron::derivativeSigmoid(Value x)
    {
        const Value fx = Neuron::activationSigmoid(x);
        return fx * (1.0 - fx);
    }
    
    inline Value Neuron::activationTanh(Value x)
    {
        const Value eP = exp(x);
        const Value eN = 1.0 / eP;
        return (eP - eN) / (eP + eN);
    }
    
    inline Value Neuron::derivativeTanh(Value x)
    {
        const Value activation = Neuron::activationTanh(x);
        return 1.0 - (activation * activation);
    }
    
    inline Value Neuron::activationReLU(Value x)
    {
        return x > 0.0 ? x : (0.01 * x);
    }
    
    inline Value Neuron::derivativeReLU(Value x)
    {
        return x > 0.0 ? 1.0 : 0.01;
    }
    

    
    //===------------------------------------------------------------------===//
    // Const stuff
    //===------------------------------------------------------------------===//
    
    inline bool Neuron::isGate() const noexcept
    {
        return this->isGatingAnyConnection;
    }
    
    inline bool Neuron::isSelfConnected() const noexcept
    {
        return (this->selfConnection != nullptr);
    }
    
    inline bool Neuron::isConnectedTo(Neuron::Ptr other) const
    {
        return (this->findConnectionWith(other) != nullptr);
    }
    
    inline Neuron::Connection::Ptr Neuron::getSelfConnection() const noexcept
    {
        return this->selfConnection;
    }
    
    inline Neuron::Connection::Ptr Neuron::findConnectionWith(Neuron::Ptr other) const
    {
        if (other.get() == this)
        {
            return this->selfConnection;
        }
        
        for (const auto &i : this->incomingConnections)
        {
            const Connection::Ptr connection = i.second;
            
            if (connection->getInputNeuron() == other)
            {
                return connection;
            }
        }
        
        for (const auto &i : this->outgoingConnections)
        {
            const Connection::Ptr connection = i.second;
            
            if (connection->getOutputNeuron() == other)
            {
                return connection;
            }
        }
        
        for (const auto &i : this->gatedConnections)
        {
            const Connection::Ptr connection = i.second;
            
            if (connection->getInputNeuron() == other ||
                connection->getOutputNeuron() == other)
            {
                return connection;
            }
        }
        
        return nullptr;
    }
    
    inline Neuron::Connection::Ptr Neuron::findOutgoingConnectionTo(Neuron::Ptr other) const
    {
        for (const auto &i : this->outgoingConnections)
        {
            const Neuron::Connection::Ptr connection = i.second;
            
            if (connection->getOutputNeuron() == other)
            {
                return connection;
            }
        }
        
        return nullptr;
    }
    
    inline Neuron::Connection::Ptr Neuron::findIncomingConnectionFrom(Neuron::Ptr other) const
    {
        for (const auto &i : this->incomingConnections)
        {
            const Neuron::Connection::Ptr connection = i.second;
            
            if (connection->getInputNeuron() == other)
            {
                return connection;
            }
        }
        
        return nullptr;
    }
    
    //===------------------------------------------------------------------===//
    // Serialization
    //===------------------------------------------------------------------===//
    
    inline void Neuron::deserialize(SerializationContext::Ptr context)
    {
        this->uuid = context->getNumberProperty(Keys::Core::Uuid);
        // selfconnection will be restored in network deserialization
        this->activationType = ActivationType(context->getNumberProperty(Keys::Core::ActivationType));
        this->bias = context->getRealProperty(Keys::Core::Bias);
        this->activation = context->getRealProperty(Keys::Core::Activation);
        this->derivative = context->getRealProperty(Keys::Core::Derivative);
        this->state = context->getRealProperty(Keys::Core::State);
        this->oldState = context->getRealProperty(Keys::Core::OldState);
        this->errorResponsibility = context->getRealProperty(Keys::Core::ErrorResponsibility);
        this->projectedActivity = context->getRealProperty(Keys::Core::ProjectedActivity);
        this->gatingActivity = context->getRealProperty(Keys::Core::GatingActivity);
    }
    
    inline void Neuron::serialize(SerializationContext::Ptr context) const
    {
        context->setNumberProperty(this->uuid, Keys::Core::Uuid);
        context->setNumberProperty(this->activationType, Keys::Core::ActivationType);
        context->setRealProperty(this->bias, Keys::Core::Bias);
        context->setRealProperty(this->activation, Keys::Core::Activation);
        context->setRealProperty(this->derivative, Keys::Core::Derivative);
        context->setRealProperty(this->state, Keys::Core::State);
        context->setRealProperty(this->oldState, Keys::Core::OldState);
        context->setRealProperty(this->errorResponsibility, Keys::Core::ErrorResponsibility);
        context->setRealProperty(this->projectedActivity, Keys::Core::ProjectedActivity);
        context->setRealProperty(this->gatingActivity, Keys::Core::GatingActivity);
    }
    
    //===------------------------------------------------------------------===//
    // Neuron::Connection
    //===------------------------------------------------------------------===//
    
    inline Neuron::Connection::Connection() :
    uuid(Uuid::generateId()),
    weight(0.0),
    gain(1.0)
    {
        this->setRandomWeight();
    }
    
    inline Neuron::Connection::Connection(std::weak_ptr<Neuron> input,
                                          std::weak_ptr<Neuron> output) :
    uuid(Uuid::generateId()),
    weight(0.0),
    gain(1.0),
    inputNeuron(input),
    outputNeuron(output)
    {
        this->setRandomWeight();
    }
    
    inline void Neuron::Connection::setRandomWeight()
    {
        std::random_device randomDevice;
        std::mt19937 mt19937(randomDevice());
        std::uniform_real_distribution<Value> distribution(-0.001, 0.001);
        this->weight = distribution(mt19937);
    }
    
    inline Id Neuron::Connection::getUuid() const noexcept
    {
        return this->uuid;
    }
    
    inline Neuron::Ptr Neuron::Connection::getInputNeuron() const
    {
        return this->inputNeuron.lock();
    }
    
    inline Neuron::Ptr Neuron::Connection::getGateNeuron() const
    {
        return this->gateNeuron.lock();
    }
    
    inline Neuron::Ptr Neuron::Connection::getOutputNeuron() const
    {
        return this->outputNeuron.lock();
    }
    
    inline bool Neuron::Connection::hasGate() const noexcept
    {
        return (this->getGateNeuron() != nullptr);
    }
    
    inline void Neuron::Connection::setGate(Neuron::WeakPtr gateNeuron)
    {
        this->gateNeuron = gateNeuron;
    }
    
    inline void Neuron::Connection::connect(Neuron::WeakPtr weakInput, Neuron::WeakPtr weakOutput)
    {
        Neuron::Ptr strongInput = weakInput.lock();
        Neuron::Ptr strongOutput = weakOutput.lock();
        
        this->inputNeuron = strongInput;
        this->outputNeuron = strongOutput;

        if (strongInput == strongOutput)
        {
            strongInput->selfConnection = this->shared_from_this();
            return;
        }

        // reference all the connections
        strongInput->outgoingConnections[this->getUuid()] = this->shared_from_this();
        strongOutput->incomingConnections[this->getUuid()] = this->shared_from_this();

        // reference all the traces
        strongInput->neighbours[strongOutput->getUuid()] = strongOutput;
        strongOutput->eligibility[this->getUuid()] = 0.0;
        
        for (auto &extendedTrace : strongOutput->extended)
        {
            extendedTrace.second[this->getUuid()] = 0.0;
        }
    }
    
    //===------------------------------------------------------------------===//
    // Serialization
    //===------------------------------------------------------------------===//
    
    inline void Neuron::Connection::deserialize(SerializationContext::Ptr context)
    {
        this->uuid = context->getNumberProperty(Keys::Core::Uuid);
        this->weight = context->getRealProperty(Keys::Core::Weight);
        this->gain = context->getRealProperty(Keys::Core::Gain);
        // optimization hack: deserialized in the network
        //this->inputNeuronUuid = context->getNumberProperty(Keys::Core::InputNeuronUuid);
        //this->gateNeuronUuid = context->getNumberProperty(Keys::Core::GateNeuronUuid);
        //this->outputNeuronUuid = context->getNumberProperty(Keys::Core::OutputNeuronUuid);
    }
    
    inline void Neuron::Connection::serialize(SerializationContext::Ptr context) const
    {
        context->setNumberProperty(this->uuid, Keys::Core::Uuid);
        context->setRealProperty(this->weight, Keys::Core::Weight);
        context->setRealProperty(this->gain, Keys::Core::Gain);
        context->setNumberProperty(this->getInputNeuron()->getUuid(), Keys::Core::InputNeuronUuid);
        context->setNumberProperty(this->getGateNeuron() ? this->getGateNeuron()->getUuid() : 0, Keys::Core::GateNeuronUuid);
        context->setNumberProperty(this->getOutputNeuron()->getUuid(), Keys::Core::OutputNeuronUuid);
    }
} // namespace TinyRNN

#endif  // TINYRNN_NEURON_H_INCLUDED
