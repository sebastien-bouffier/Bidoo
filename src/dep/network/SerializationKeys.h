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

#ifndef TINYRNN_SERIALIZATIONKEYS_H_INCLUDED
#define TINYRNN_SERIALIZATIONKEYS_H_INCLUDED

#include "Common.h"

namespace TinyRNN
{
    namespace Keys
    {
        namespace Core
        {
            static const std::string Network = "Network";
            static const std::string Layer = "Layer";
            static const std::string Layers = "Layers";
            static const std::string InputLayer = "InputLayer";
            static const std::string OutputLayer = "OutputLayer";
            static const std::string HiddenLayers = "HiddenLayers";
            static const std::string Neuron = "Neuron";
            static const std::string Neurons = "Neurons";
            static const std::string Connection = "Connection";
            static const std::string Connections = "Connections";
            static const std::string InputNeuronUuid = "InputNeuronUuid";
            static const std::string OutputNeuronUuid = "OutputNeuronUuid";
            static const std::string GateNeuronUuid = "GateNeuronUuid";
            static const std::string Uuid = "Uuid";
            static const std::string Name = "Name";
            
            static const std::string NeuronUuid = "NeuronUuid";
            static const std::string ConnectionUuid = "ConnectionUuid";
            
            static const std::string Weight = "Weight";
            static const std::string Gain = "Gain";
            
            static const std::string Bias = "Bias";
            static const std::string ActivationType = "ActivationType";
            static const std::string Activation = "Activation";
            static const std::string Derivative = "Derivative";
            static const std::string State = "State";
            static const std::string OldState = "OldState";

            static const std::string ErrorResponsibility = "ErrorResponsibility";
            static const std::string ProjectedActivity = "ProjectedActivity";
            static const std::string GatingActivity = "GatingActivity";
            
            static const std::string Rate = "Rate";
            static const std::string Influence = "Influence";
            static const std::string Eligibility = "Eligibility";
            static const std::string ExtendedTrace = "ExtendedTrace";
            static const std::string Target = "Target";
            
            static const std::string ErrorAccumulator = "ErrorAccumulator";
            static const std::string Gradient = "Gradient";
        } // namespace Core
        
        namespace Mapping
        {
            static const Id Network = 1;
            static const Id Layer = 2;
            static const Id Layers = 3;
            static const Id InputLayer = 4;
            static const Id OutputLayer = 5;
            static const Id HiddenLayers = 6;
            static const Id Neuron = 7;
            static const Id Neurons = 8;
            static const Id Connection = 9;
            static const Id Connections = 10;
            static const Id InputNeuronUuid = 11;
            static const Id OutputNeuronUuid = 12;
            static const Id GateNeuronUuid = 13;
            static const Id Uuid = 14;
            static const Id Name = 15;
            
            static const Id TrainingContext = 16;
            static const Id TrainingNeuronContext = 17;
            static const Id NeuronContexts = 18;
            static const Id TrainingConnectionContext = 19;
            static const Id ConnectionContexts = 20;
            static const Id NeuronUuid = 21;
            static const Id ConnectionUuid = 22;
            
            static const Id Weight = 23;
            static const Id Gain = 24;
            
            static const Id Bias = 25;
            static const Id Activation = 26;
            static const Id Derivative = 27;
            static const Id State = 28;
            static const Id OldState = 29;
            
            static const Id ErrorResponsibility = 30;
            static const Id ProjectedActivity = 31;
            static const Id GatingActivity = 32;
            
            static const Id Rate = 33;
            static const Id Influence = 34;
            static const Id Eligibility = 35;
            static const Id ExtendedTrace = 36;
            static const Id Target = 37;
            
            static const Id ErrorAccumulator = 38;
            static const Id Gradient = 39;
        } // namespace Mapping
        
        namespace Unrolled
        {
            static const std::string TrainingContext = "UnrolledTrainingContext";
            static const std::string Network = "UnrolledNetwork";
            
            static const std::string FeedKernel = "FeedKernel";
            static const std::string TrainKernel = "TrainKernel";
            
            static const std::string Commands = "Commands";
            static const std::string CommandsSize = "CommandsSize";
            static const std::string Indices = "Indices";
            static const std::string IndicesSize = "IndicesSize";
            
            static const std::string VariablesMapping = "VariablesMapping";
            static const std::string InputsMapping = "InputsMapping";
            static const std::string OutputsMapping = "OutputsMapping";
            static const std::string TargetsMapping = "TargetsMapping";
            static const std::string RateMapping = "RateMapping";
            
            static const std::string RawMemory = "RawMemory";
            static const std::string MemorySize = "MemorySize";
            static const std::string Variable = "Variable";
            static const std::string Key = "Key";
            static const std::string Index = "Index";
        } // namespace Unrolled
    } // namespace Keys
}  // namespace TinyRNN

#endif  // TINYRNN_SERIALIZATIONKEYS_H_INCLUDED
