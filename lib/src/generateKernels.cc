/*--------------------------------------------------------------------------
   Author: Thomas Nowotny

   Institute: Center for Computational Neuroscience and Robotics
              University of Sussex
              Falmer, Brighton BN1 9QJ, UK 

   email to:  T.Nowotny@sussex.ac.uk

   initial version: 2010-02-07

--------------------------------------------------------------------------*/

//------------------------------------------------------------------------
/*! \file generateKernels.cc

  \brief Contains functions that generate code for CUDA kernels. Part of the code generation section.

*/
//-------------------------------------------------------------------------

#include "generateKernels.h"
#include "global.h"
#include "utils.h"
#include "stringUtils.h"
#include "CodeHelper.h"

#include <algorithm>


// The CPU_ONLY version does not need any of this
#ifndef CPU_ONLY

short *isGrpVarNeeded;


//-------------------------------------------------------------------------
/*!
  \brief Function for generating the CUDA kernel that simulates all neurons in the model.

  The code generated upon execution of this function is for defining GPU side global variables that will hold model state in the GPU global memory and for the actual kernel function for simulating the neurons for one time step.
*/
//-------------------------------------------------------------------------

void genNeuronKernel(const NNmodel &model, //!< Model description
                     const string &path  //!< Path for code generation
    )
{
    string name, s, localID;
    unsigned int nt;
    ofstream os;

    name = path + "/" + model.name + "_CODE/neuronKrnl.cc";
    os.open(name.c_str());

    // write header content
    writeHeader(os);
    os << ENDL;

    // compiler/include control (include once)
    os << "#ifndef _" << model.name << "_neuronKrnl_cc" << ENDL;
    os << "#define _" << model.name << "_neuronKrnl_cc" << ENDL;
    os << ENDL;

    // write doxygen comment
    os << "//-------------------------------------------------------------------------" << ENDL;
    os << "/*! \\file neuronKrnl.cc" << ENDL << ENDL;
    os << "\\brief File generated from GeNN for the model " << model.name << " containing the neuron kernel function." << ENDL;
    os << "*/" << ENDL;
    os << "//-------------------------------------------------------------------------" << ENDL << ENDL;

    //os << "__device__ __host__ float exp(int i) { return exp((float) i); }" << endl;

    os << "// include the support codes provided by the user for neuron or synaptic models" << ENDL;
    os << "#include \"support_code.h\"" << ENDL << ENDL;

    isGrpVarNeeded = new short[model.neuronGrpN];
    for (int i = 0; i < model.synapseGrpN; i++) {
        if (model.synapseConnType[i] == SPARSE){
            if ((model.synapseSpanType[i] == 0) && (model.neuronN[model.synapseTarget[i]] > synapseBlkSz)) {
                isGrpVarNeeded[model.synapseTarget[i]] = 1; //! Binary flag for the sparse synapses to use atomic operations when the number of connections is bigger than the block size, and shared variables otherwise
            }
            if ((model.synapseSpanType[i] == 1) && (model.neuronN[model.synapseSource[i]] > synapseBlkSz)) {
                isGrpVarNeeded[model.synapseTarget[i]] = 1; //! Binary flag for the sparse synapses to use atomic operations when the number of connections is bigger than the block size, and shared variables otherwise
            }
        }
    }
  
    // kernel header
    os << "extern \"C\" __global__ void calcNeurons(";
    for (int i= 0, l= model.neuronKernelParameters.size(); i < l; i++) {
        os << model.neuronKernelParameterTypes[i] << " " << model.neuronKernelParameters[i] << ", ";
    }
    os << model.ftype << " t)" << ENDL;
    os << OB(5);

    // kernel code
    unsigned int neuronGridSz = model.padSumNeuronN[model.neuronGrpN - 1];
    neuronGridSz = neuronGridSz / neuronBlkSz;
    if (neuronGridSz < deviceProp[theDevice].maxGridSize[1]) {
        os << "unsigned int id = " << neuronBlkSz << " * blockIdx.x + threadIdx.x;" << ENDL;
    }
    else {
        os << "unsigned int id = " << neuronBlkSz << " * (blockIdx.x * " << ceil(sqrt((float) neuronGridSz)) << " + blockIdx.y) + threadIdx.x;" << ENDL;
    }

    // these variables deal with high V "spike type events"
    for (int i = 0; i < model.neuronGrpN; i++) {
        if (model.neuronNeedSpkEvnt[i]) {
            os << "__shared__ volatile unsigned int posSpkEvnt;" << ENDL;
            os << "__shared__ unsigned int shSpkEvnt[" << neuronBlkSz << "];" << ENDL;
            os << "unsigned int spkEvntIdx;" << ENDL;
            os << "__shared__ volatile unsigned int spkEvntCount;" << ENDL;
            break;
        }
    }

    // these variables now deal only with true spikes, not high V "events"
    for (int i = 0; i < model.neuronGrpN; i++) {
        if (nModels[model.neuronType[i]].thresholdConditionCode != tS("")) {
            os << "__shared__ unsigned int shSpk[" << neuronBlkSz << "];" << ENDL;
            os << "__shared__ volatile unsigned int posSpk;" << ENDL;
            os << "unsigned int spkIdx;" << ENDL;
            os << "__shared__ volatile unsigned int spkCount;" << ENDL;
            break;
        }
    }
    os << ENDL;

    // Reset global spike counting vars here if there are no synapses at all
    if (model.resetKernel == GENN_FLAGS::calcNeurons) {
        os << "if (id == 0)" << OB(6);
        for (int j = 0; j < model.neuronGrpN; j++) {
            if (model.neuronDelaySlots[j] > 1) { // WITH DELAY
                os << "dd_spkQuePtr" << model.neuronName[j] << " = (dd_spkQuePtr" << model.neuronName[j] << " + 1) % " << model.neuronDelaySlots[j] << ";" << ENDL;
                if (model.neuronNeedSpkEvnt[j]) {
                    os << "dd_glbSpkCntEvnt" << model.neuronName[j] << "[dd_spkQuePtr" << model.neuronName[j] << "] = 0;" << ENDL;
                }
                if (model.neuronNeedTrueSpk[j]) {
                    os << "dd_glbSpkCnt" << model.neuronName[j] << "[dd_spkQuePtr" << model.neuronName[j] << "] = 0;" << ENDL;
                }
                else {
                    os << "dd_glbSpkCnt" << model.neuronName[j] << "[0] = 0;" << ENDL;
                }
            }
            else { // NO DELAY
                if (model.neuronNeedSpkEvnt[j]) {
                    os << "dd_glbSpkCntEvnt" << model.neuronName[j] << "[0] = 0;" << ENDL;
                }
                os << "dd_glbSpkCnt" << model.neuronName[j] << "[0] = 0;" << ENDL;
            }
        }
        os << CB(6);
        os << "__threadfence();" << ENDL << ENDL;
    }

    // Initialise shared spike count vars
    for (int i = 0; i < model.neuronGrpN; i++) {
        if (!nModels[model.neuronType[i]].thresholdConditionCode.empty()) {
            os << "if (threadIdx.x == 0)" << OB(8);
            os << "spkCount = 0;" << ENDL;
            os << CB(8);
            break;
        }
    }
    for (int i = 0; i < model.neuronGrpN; i++) {
        if (model.neuronNeedSpkEvnt[i]) {
            os << "if (threadIdx.x == 1)" << OB(7);
            os << "spkEvntCount = 0;" << ENDL;
            os << CB(7);
            break;
        }
    }
    os << "__syncthreads();" << ENDL;
    os << ENDL;

    
    for (int i = 0; i < model.neuronGrpN; i++) {
        nt = model.neuronType[i];

        string queueOffset = (model.neuronDelaySlots[i] > 1 ? "(dd_spkQuePtr" + model.neuronName[i] + " * " + tS(model.neuronN[i]) + ") + " : "");
        string queueOffsetTrueSpk = (model.neuronNeedTrueSpk[i] ? queueOffset : "");

        os << "// neuron group " << model.neuronName[i] << ENDL;
        if (i == 0) {
            os << "if (id < " << model.padSumNeuronN[i] << ")" << OB(10);
            localID = string("id");
        }
        else {
            os << "if ((id >= " << model.padSumNeuronN[i - 1] << ") && (id < " << model.padSumNeuronN[i] << "))" << OB(10);
            os << "unsigned int lid = id - " << model.padSumNeuronN[i - 1] << ";" << ENDL;
            localID = string("lid");
        }

        vector<bool> varNeedQueue = model.neuronVarNeedQueue[i];
        if ((find(varNeedQueue.begin(), varNeedQueue.end(), true) != varNeedQueue.end()) && (model.neuronDelaySlots[i] > 1)) {
            os << "unsigned int delaySlot = (dd_spkQuePtr" << model.neuronName[i];
            os << " + " << (model.neuronDelaySlots[i] - 1);
            os << ") % " << model.neuronDelaySlots[i] << ";" << ENDL;
        }
        os << ENDL;

        os << "// only do this for existing neurons" << ENDL;
        os << "if (" << localID << " < " << model.neuronN[i] << ")" << OB(20);

        os << "// pull neuron variables in a coalesced access" << ENDL;
        for (int k = 0; k < nModels[nt].varNames.size(); k++) {
            os << nModels[nt].varTypes[k] << " l" << nModels[nt].varNames[k] << " = dd_";
            os << nModels[nt].varNames[k] << model.neuronName[i] << "[";
            if ((model.neuronVarNeedQueue[i][k]) && (model.neuronDelaySlots[i] > 1)) {
                os << "(delaySlot * " << model.neuronN[i] << ") + ";
            }
            os << localID << "];" << ENDL;
        }
        if ((nModels[nt].simCode.find("$(sT)") != string::npos)
            || (nModels[nt].thresholdConditionCode.find("$(sT)") != string::npos)
            || (nModels[nt].resetCode.find("$(sT)") != string::npos)) { // load sT into local variable
            os << model.ftype << " lsT = dd_sT" <<  model.neuronName[i] << "[";
            if (model.neuronDelaySlots[i] > 1) {
                os << "(delaySlot * " << model.neuronN[i] << ") + ";
            }
            os << localID << "];" << ENDL;
        }
        os << ENDL;

        if ((model.inSyn[i].size() > 0) || (nModels[nt].simCode.find("Isyn") != string::npos)) {
            os << model.ftype << " Isyn = 0;" << ENDL;
        }
        for (int j = 0; j < model.inSyn[i].size(); j++) {
            unsigned int synPopID= model.inSyn[i][j]; // number of (post)synapse group
            postSynModel psm= postSynModels[model.postSynapseType[synPopID]];
            string sName= model.synapseName[synPopID];

            os << "// pull inSyn values in a coalesced access" << ENDL;
            os << model.ftype << " linSyn" << sName << " = dd_inSyn" << sName << "[" << localID << "];" << ENDL;
            if (model.synapseGType[synPopID] == INDIVIDUALG) {
                for (int k = 0, l = psm.varNames.size(); k < l; k++) {
                    os << psm.varTypes[k] << " lps" << psm.varNames[k] << sName;
                    os << " = dd_" <<  psm.varNames[k] << sName << "[" << localID << "];" << ENDL;
                }
            }
            string psCode = psm.postSyntoCurrent;
            substitute(psCode, "$(id)", localID);
            substitute(psCode, "$(t)", "t");
            substitute(psCode, "$(inSyn)", "linSyn" + sName);
            name_substitutions(psCode, "l", nModels[nt].varNames, "");
            value_substitutions(psCode, nModels[nt].pNames, model.neuronPara[i]);
            value_substitutions(psCode, nModels[nt].dpNames, model.dnp[i]);
            if (model.synapseGType[synPopID] == INDIVIDUALG) {
                name_substitutions(psCode, "lps", psm.varNames, sName);
            }
            else {
                value_substitutions(psCode, psm.varNames, model.postSynIni[synPopID]);
            }
            value_substitutions(psCode, psm.pNames, model.postSynapsePara[synPopID]);
            value_substitutions(psCode, psm.dpNames, model.dpsp[synPopID]);
            name_substitutions(psCode, "", nModels[nt].extraGlobalNeuronKernelParameters, model.neuronName[i]);
            psCode= ensureFtype(psCode, model.ftype);
            checkUnreplacedVariables(psCode,"postSyntoCurrent");
            if (!psm.supportCode.empty()) {
                os << OB(29) << " using namespace " << sName << "_postsyn;" << ENDL;
            }
            os << "Isyn += " << psCode << ";" << ENDL;
            if (!psm.supportCode.empty()) {
                os << CB(29) << " // namespace bracket closed" << ENDL;
            }
        }

	if (!nModels[nt].supportCode.empty()) {
	    os << " using namespace " << model.neuronName[i] << "_neuron;" << ENDL;
	}
        string thCode= nModels[nt].thresholdConditionCode;
        if (thCode.empty()) { // no condition provided
            cerr << "Warning: No thresholdConditionCode for neuron type " << model.neuronType[i] << " used for population \"" << model.neuronName[i] << "\" was provided. There will be no spikes detected in this population!" << endl;
        }
        else {
            os << "// test whether spike condition was fulfilled previously" << ENDL;
            substitute(thCode, "$(id)", localID);
            substitute(thCode, "$(t)", "t");
            name_substitutions(thCode, "l", nModels[nt].varNames, "");
            substitute(thCode, "$(Isyn)", "Isyn");
            substitute(thCode, "$(sT)", "lsT");
            value_substitutions(thCode, nModels[nt].pNames, model.neuronPara[i]);
            value_substitutions(thCode, nModels[nt].dpNames, model.dnp[i]);
            name_substitutions(thCode, "", nModels[nt].extraGlobalNeuronKernelParameters, model.neuronName[i]);
            thCode= ensureFtype(thCode, model.ftype);
            checkUnreplacedVariables(thCode,"thresholdConditionCode");
            if (GENN_PREFERENCES::autoRefractory) {
                os << "bool oldSpike= (" << thCode << ");" << ENDL;
            }
        }

        os << "// calculate membrane potential" << ENDL;
        string sCode = nModels[nt].simCode;
        substitute(sCode, "$(id)", localID);
        substitute(sCode, "$(t)", "t");
        name_substitutions(sCode, "l", nModels[nt].varNames, "");
        value_substitutions(sCode, nModels[nt].pNames, model.neuronPara[i]);
        value_substitutions(sCode, nModels[nt].dpNames, model.dnp[i]);
        name_substitutions(sCode, "", nModels[nt].extraGlobalNeuronKernelParameters, model.neuronName[i]);
        substitute(sCode, "$(Isyn)", "Isyn");
        substitute(sCode, "$(sT)", "lsT");
        sCode= ensureFtype(sCode, model.ftype);
        checkUnreplacedVariables(sCode,"neuron simCode");
        os << sCode << ENDL;

        // look for spike type events first.
        if (model.neuronNeedSpkEvnt[i]) {
            // Create local variable
            os << "bool spikeLikeEvent = false;" << ENDL;

            // Loop through outgoing synapse populations that will contribute to event condition code
            for(const auto &spkEventCond : model.neuronSpkEvntCondition[i]) {
                // Replace of parameters, derived parameters and extraglobalsynapse parameters
                string eCode = spkEventCond.first;

                // code substitutions ----
                substitute(eCode, "$(id)", "n");
                substitute(eCode, "$(t)", "t");
                extended_name_substitutions(eCode, "l", nModels[model.neuronType[i]].varNames, "_pre", "");
                name_substitutions(eCode, "", nModels[model.neuronType[i]].extraGlobalNeuronKernelParameters, model.neuronName[i]);
                eCode = ensureFtype(eCode, model.ftype);
                checkUnreplacedVariables(eCode, "neuronSpkEvntCondition");

                // Open scope for spike-like event test
                os << OB(31);

                // Use synapse population support code namespace if required
                if (!spkEventCond.second.empty()) {
                    os << " using namespace " << spkEventCond.second << ";" << ENDL;
                }

                // Combine this event threshold test with
                os << "spikeLikeEvent |= (" << eCode << ");" << ENDL;

                // Close scope for spike-like event test
                os << CB(31);
            }

            os << "// register a spike-like event" << ENDL;
            os << "if (spikeLikeEvent)" << OB(30);
            os << "spkEvntIdx = atomicAdd((unsigned int *) &spkEvntCount, 1);" << ENDL;
            os << "shSpkEvnt[spkEvntIdx] = " << localID << ";" << ENDL;
            os << CB(30);
        }

        // test for true spikes if condition is provided
        if (!thCode.empty()) {
            os << "// test for and register a true spike" << ENDL;
            if (GENN_PREFERENCES::autoRefractory) {
		os << "if ((" << thCode << ") && !(oldSpike)) " << OB(40);
            }
            else {
		os << "if (" << thCode << ") " << OB(40);
            }
            os << "spkIdx = atomicAdd((unsigned int *) &spkCount, 1);" << ENDL;
            os << "shSpk[spkIdx] = " << localID << ";" << ENDL;

            // add after-spike reset if provided
            if (!nModels[nt].resetCode.empty()) {
                string rCode = nModels[nt].resetCode;
                substitute(rCode, "$(id)", localID);
                substitute(rCode, "$(t)", "t");
                name_substitutions(rCode, "l", nModels[nt].varNames, "");
                value_substitutions(rCode, nModels[nt].pNames, model.neuronPara[i]);
                value_substitutions(rCode, nModels[nt].dpNames, model.dnp[i]);
                substitute(rCode, "$(Isyn)", "Isyn");
                substitute(rCode, "$(sT)", "lsT");
                name_substitutions(rCode, "", nModels[nt].extraGlobalNeuronKernelParameters, model.neuronName[i]);
                rCode= ensureFtype(rCode, model.ftype);
                checkUnreplacedVariables(rCode, "resetCode");
                os << "// spike reset code" << ENDL;
                os << rCode << ENDL;
            }
            os << CB(40);
        }

        // store the defined parts of the neuron state into the global state variables dd_V etc
        for (int k = 0, l = nModels[nt].varNames.size(); k < l; k++) {
            if (model.neuronVarNeedQueue[i][k]) {
                os << "dd_" << nModels[nt].varNames[k] << model.neuronName[i] << "[" << queueOffset << localID << "] = l" << nModels[nt].varNames[k] << ";" << ENDL;
            }
            else {
                os << "dd_" << nModels[nt].varNames[k] << model.neuronName[i] << "[" << localID << "] = l" << nModels[nt].varNames[k] << ";" << ENDL;
            }
        }

        if (model.inSyn[i].size() > 0) {
            os << "// the post-synaptic dynamics" << ENDL;
        }
        for (int j = 0; j < model.inSyn[i].size(); j++) {
            postSynModel psModel= postSynModels[model.postSynapseType[model.inSyn[i][j]]];
            string sName= model.synapseName[model.inSyn[i][j]];
            string pdCode = psModel.postSynDecay;
            substitute(pdCode, "$(id)", localID);
            substitute(pdCode, "$(t)", "t");
            substitute(pdCode, "$(inSyn)", "linSyn" + sName);
            name_substitutions(pdCode, "lps", psModel.varNames, sName);
            value_substitutions(pdCode, psModel.pNames, model.postSynapsePara[model.inSyn[i][j]]);
            value_substitutions(pdCode, psModel.dpNames, model.dpsp[model.inSyn[i][j]]);
            name_substitutions(pdCode, "l", nModels[nt].varNames, "");
            value_substitutions(pdCode, nModels[nt].pNames, model.neuronPara[i]);
            value_substitutions(pdCode, nModels[nt].dpNames, model.dnp[i]);
            pdCode= ensureFtype(pdCode, model.ftype);
            checkUnreplacedVariables(pdCode, "postSynDecay");
            if (!psModel.supportCode.empty()) {
                os << OB(29) << " using namespace " << sName << "_postsyn;" << ENDL;
            }
            os << pdCode << ENDL;
            if (!psModel.supportCode.empty()) {
                os << CB(29) << " // namespace bracket closed" << endl;
            }

            os << "dd_inSyn"  << sName << "[" << localID << "] = linSyn" << sName << ";" << ENDL;
            for (int k = 0, l = psModel.varNames.size(); k < l; k++) {
                os << "dd_" <<  psModel.varNames[k] << model.synapseName[model.inSyn[i][j]] << "[" << localID << "] = lps" << psModel.varNames[k] << sName << ";"<< ENDL;
            }
        }

        os << CB(20);
        os << "__syncthreads();" << ENDL;

        if (model.neuronNeedSpkEvnt[i]) {
            os << "if (threadIdx.x == 1)" << OB(50);
            os << "if (spkEvntCount > 0) posSpkEvnt = atomicAdd((unsigned int *) &dd_glbSpkCntEvnt" << model.neuronName[i];
            if (model.neuronDelaySlots[i] > 1) {
                os << "[dd_spkQuePtr" << model.neuronName[i] << "], spkEvntCount);" << ENDL;
            }
            else {
                os << "[0], spkEvntCount);" << ENDL;
            }
            os << CB(50); // end if (threadIdx.x == 0)
            os << "__syncthreads();" << ENDL;
        }

        if (!nModels[model.neuronType[i]].thresholdConditionCode.empty()) {
            os << "if (threadIdx.x == 0)" << OB(51);
            os << "if (spkCount > 0) posSpk = atomicAdd((unsigned int *) &dd_glbSpkCnt" << model.neuronName[i];
            if ((model.neuronDelaySlots[i] > 1) && (model.neuronNeedTrueSpk[i])) {
                os << "[dd_spkQuePtr" << model.neuronName[i] << "], spkCount);" << ENDL;
            }
            else {
                os << "[0], spkCount);" << ENDL;
            }
            os << CB(51); // end if (threadIdx.x == 1)

            os << "__syncthreads();" << ENDL;
        }
        if (model.neuronNeedSpkEvnt[i]) {
            os << "if (threadIdx.x < spkEvntCount)" << OB(60);
            os << "dd_glbSpkEvnt" << model.neuronName[i] << "[" << queueOffset << "posSpkEvnt + threadIdx.x] = shSpkEvnt[threadIdx.x];" << ENDL;
            os << CB(60); // end if (threadIdx.x < spkEvntCount)
        }

        if (!nModels[model.neuronType[i]].thresholdConditionCode.empty()) {
            os << "if (threadIdx.x < spkCount)" << OB(70);
            os << "dd_glbSpk" << model.neuronName[i] << "[" << queueOffsetTrueSpk << "posSpk + threadIdx.x] = shSpk[threadIdx.x];" << ENDL;
            if (model.neuronNeedSt[i]) {
                os << "dd_sT" << model.neuronName[i] << "[" << queueOffset << "shSpk[threadIdx.x]] = t;" << ENDL;
            }
            os << CB(70); // end if (threadIdx.x < spkCount)
        }
        os << CB(10); // end if (id < model.padSumNeuronN[i] )
        os << ENDL;
    }
    os << CB(5) << ENDL; // end of neuron kernel

    os << "#endif" << ENDL;
    os.close();
}


//-------------------------------------------------------------------------
/*!
  \brief Function for generating the CUDA synapse kernel code that handles presynaptic 
  spikes or spike type events

*/
//-------------------------------------------------------------------------

void generate_process_presynaptic_events_code(
    ostream &os, //!< output stream for code
    const NNmodel &model, //!< the neuronal network model to generate code for
    unsigned int src, //!< the number of the src neuron population
    unsigned int trg, //!< the number of the target neuron population
    int i, //!< the index of the synapse group being processed
    const string &localID, //!< the variable name of the local ID of the thread within the synapse group
    unsigned int inSynNo, //!< the ID number of the current synapse population as the incoming population to the target neuron population
    const string &postfix //!< whether to generate code for true spikes or spike type events
    )
{
    string theAtomicAdd;
    int version;
    cudaRuntimeGetVersion(&version);     
    if (((deviceProp[theDevice].major < 2) && (model.ftype == "float")) 
        || (((deviceProp[theDevice].major < 6) || (version < 8000)) && (model.ftype == "double")))
    {
        theAtomicAdd= "atomicAddSW";
    }
    else {
        theAtomicAdd= "atomicAdd";
    }

    bool evnt = postfix == "Evnt";
    int UIntSz = sizeof(unsigned int) * 8;
    int logUIntSz = (int) (logf((float) UIntSz) / logf(2.0f) + 1e-5f);

    if ((evnt && model.synapseUsesSpikeEvents[i]) || (!evnt && model.synapseUsesTrueSpikes[i])) {
        unsigned int synt = model.synapseType[i];
        bool sparse = model.synapseConnType[i] == SPARSE;
        unsigned int nt_pre = model.neuronType[src];
        bool delayPre = model.neuronDelaySlots[src] > 1;
        string offsetPre = (delayPre ? "(delaySlot * " + tS(model.neuronN[src]) + ") + " : "");

        unsigned int nt_post = model.neuronType[trg];
        bool delayPost = model.neuronDelaySlots[trg] > 1;
        string offsetPost = (delayPost ? "(dd_spkQuePtr" + model.neuronName[trg] + " * " + tS(model.neuronN[trg]) + ") + " : "");

        // Detect spike events or spikes and do the update
        if ( sparse && (model.synapseSpanType[i] == 1)) { // parallelisation along pre-synaptic spikes, looped over post-synaptic neurons
            int maxConnections;
            if ((sparse) && (isGrpVarNeeded[model.synapseTarget[i]])) {
                if (model.maxConn[i] < 1) {
                    fprintf(stderr, "Model Generation warning: for every SPARSE synapse group used you must also supply (in your model)\
 a max possible number of connections via the model.setMaxConn() function.\n");
                    maxConnections = model.neuronN[trg];
                }
                else {
                    maxConnections = model.maxConn[i];
                }
            }
            else {
                maxConnections = model.neuronN[trg];
            }

            //os << "if (" << localID << " < " << maxConnections << ")" << OB(101);

            os << "if (" << localID << " < " ;
            if (delayPre) {
                os << "dd_glbSpkCnt" << postfix << model.neuronName[src] << "[delaySlot])" << OB(102);
            }
            else {
                os << "dd_glbSpkCnt" << postfix << model.neuronName[src] << "[0])" << OB(102);
            }

            if (!weightUpdateModels[synt].simCode_supportCode.empty()) {
                os << " using namespace " << model.synapseName[i] << "_weightupdate_simCode;" << ENDL;
            }

            if (delayPre) {
              os << "int preInd = dd_glbSpk"  << postfix << model.neuronName[src];
              os << "[(delaySlot * " << model.neuronN[src] << ") + " << localID << "];";
            }
            else {
              os << "int preInd = dd_glbSpk"  << postfix << model.neuronName[src];
              os << "[" << localID << "];" << ENDL;
            }
            os << "prePos = dd_indInG" << model.synapseName[i] << "[preInd];" << ENDL;
            os << "npost = dd_indInG" << model.synapseName[i] << "[preInd + 1] - prePos;" << ENDL;

            if (model.synapseGType[i] == INDIVIDUALID) {
                os << "unsigned int gid = (dd_glbSpkCnt" << postfix << "[" << localID << "] * " << model.neuronN[trg] << " + i);" << ENDL;
            }
            if ((evnt) && (model.needEvntThresholdReTest[i])) {
                os << "if ";
                if (model.synapseGType[i] == INDIVIDUALID) {
                    // Note: we will just access global mem. For compute >= 1.2 simultaneous access to same global mem in the (half-)warp will be coalesced - no worries
                    os << "((B(dd_gp" << model.synapseName[i] << "[gid >> " << logUIntSz << "], gid & " << UIntSz - 1 << ")) && ";
                }

                // code substitutions ----
                string eCode = weightUpdateModels[synt].evntThreshold;
                value_substitutions(eCode, weightUpdateModels[synt].pNames, model.synapsePara[i]);
                value_substitutions(eCode, weightUpdateModels[synt].dpNames, model.dsp_w[i]);
                name_substitutions(eCode, "", weightUpdateModels[synt].extraGlobalSynapseKernelParameters, model.synapseName[i]);

//                neuron_substitutions_in_synaptic_code(eCode, model, src, trg, nt_pre, nt_post, offsetPre, offsetPost, "shSpkEvnt" + "[j]", "ipost", "dd_");
                neuron_substitutions_in_synaptic_code(eCode, model, src, trg, nt_pre, nt_post, offsetPre, offsetPost, "preInd", "i", "dd_");
          //  os << "shSpk" << postfix << "[threadIdx.x] = dd_glbSpk" << postfix << model.neuronName[src] << "[" << offsetPre << "(r * BLOCKSZ_SYN) + threadIdx.x];" << ENDL;
                eCode= ensureFtype(eCode, model.ftype);
                checkUnreplacedVariables(eCode, "evntThreshold");
                // end code substitutions ----
                os << "(" << eCode << ")";

                if (model.synapseGType[i] == INDIVIDUALID) {
                    os << ")";
                }
                os << OB(130);
            }
            else if (model.synapseGType[i] == INDIVIDUALID) {
                os << "if (B(dd_gp" << model.synapseName[i] << "[gid >> " << logUIntSz << "], gid & " << UIntSz - 1 << "))" << OB(135);
            }
            os << "for (int i = 0; i < npost; ++i)" << OB(103);
            os << "        ipost = dd_ind" <<  model.synapseName[i] << "[prePos];" << ENDL;

// Code substitutions ----------------------------------------------------------------------------------
            string wCode = (evnt ? weightUpdateModels[synt].simCodeEvnt : weightUpdateModels[synt].simCode);
            substitute(wCode, "$(t)", "t");

                if (isGrpVarNeeded[model.synapseTarget[i]]) { // SPARSE using atomicAdd
                    substitute(wCode, "$(updatelinsyn)", theAtomicAdd + "(&$(inSyn), $(addtoinSyn))");
                    substitute(wCode, "$(inSyn)", "dd_inSyn" + model.synapseName[i] + "[ipost]");
                }
                else { // SPARSE using shared memory
                    substitute(wCode, "$(updatelinsyn)", "$(inSyn) += $(addtoinSyn)");
                    substitute(wCode, "$(inSyn)", "shLg[ipost]");
                }
                if (model.synapseGType[i] == INDIVIDUALG) {
                    name_substitutions(wCode, "dd_", weightUpdateModels[synt].varNames, model.synapseName[i] + "[prePos]");
                }
                else {
                    value_substitutions(wCode, weightUpdateModels[synt].varNames, model.synapseIni[i]);
                }

            value_substitutions(wCode, weightUpdateModels[synt].pNames, model.synapsePara[i]);
            value_substitutions(wCode, weightUpdateModels[synt].dpNames, model.dsp_w[i]);
            name_substitutions(wCode, "dd_", weightUpdateModels[synt].extraGlobalSynapseKernelParameters, model.synapseName[i]);
            substitute(wCode, "$(addtoinSyn)", "addtoinSyn");
            neuron_substitutions_in_synaptic_code(wCode, model, src, trg, nt_pre, nt_post, offsetPre, offsetPost, "preInd", "ipost", "dd_");
            wCode= ensureFtype(wCode, model.ftype);
            checkUnreplacedVariables(wCode, "simCode"+postfix);
            // end code substitutions -------------------------------------------------------------------------

            os << wCode << ENDL;

            os << "prePos += 1;" << ENDL;
            os << CB(103);
            if ((evnt) && (model.needEvntThresholdReTest[i])) {
                os << CB(130);
            }
            else if (model.synapseGType[i] == INDIVIDUALID) {
                os << CB(135);
            }
            os << CB(102);
            //os << CB(101);
        }
        else { // classical parallelisation of post-synaptic neurons in parallel and spikes in a loop
            os << "// process presynaptic events: " << (evnt ? "Spike type events" : "True Spikes") << ENDL;
            os << "for (r = 0; r < numSpikeSubsets" << postfix << "; r++)" << OB(90);
            os << "if (r == numSpikeSubsets" << postfix << " - 1) lmax = ((lscnt" << postfix << "-1) % BLOCKSZ_SYN) +1;" << ENDL;
            os << "else lmax = BLOCKSZ_SYN;" << ENDL;
            os << "__syncthreads();" << ENDL;
            os << "if (threadIdx.x < lmax)" << OB(100);
            os << "shSpk" << postfix << "[threadIdx.x] = dd_glbSpk" << postfix << model.neuronName[src] << "[" << offsetPre << "(r * BLOCKSZ_SYN) + threadIdx.x];" << ENDL;
            os << CB(100);

            if ((sparse) && (!isGrpVarNeeded[model.synapseTarget[i]])) {
                // set shLg to 0 for all postsynaptic neurons; is ok as model.neuronN[model.synapseTarget[i]] <= synapseBlkSz
                os << "if (threadIdx.x < " << model.neuronN[model.synapseTarget[i]] << ") shLg[threadIdx.x] = 0;" << ENDL;
            }
            os << "__syncthreads();" << ENDL;

            int maxConnections;
            if ((sparse) && (isGrpVarNeeded[model.synapseTarget[i]])) {
                if (model.maxConn[i] < 1) {
                    fprintf(stderr, "Model Generation warning: for every SPARSE synapse group used you must also supply (in your model)\
 a max possible number of connections via the model.setMaxConn() function.\n");
                    maxConnections = model.neuronN[trg];
                }
                else {
                    maxConnections = model.maxConn[i];
                }
            }
            else {
                maxConnections = model.neuronN[trg];
            }
            os << "// loop through all incoming spikes" << ENDL;
            os << "for (j = 0; j < lmax; j++)" << OB(110);
            os << "// only work on existing neurons" << ENDL;
            os << "if (" << localID << " < " << maxConnections << ")" << OB(120);
            if (model.synapseGType[i] == INDIVIDUALID) {
                os << "unsigned int gid = (shSpk" << postfix << "[j] * " << model.neuronN[trg] << " + " << localID << ");" << ENDL;
            }

            if (!weightUpdateModels[synt].simCode_supportCode.empty()) {
                os << " using namespace " << model.synapseName[i] << "_weightupdate_simCode;" << ENDL;
            }
            if ((evnt) && (model.needEvntThresholdReTest[i])) {
                os << "if ";
                if (model.synapseGType[i] == INDIVIDUALID) {
                    // Note: we will just access global mem. For compute >= 1.2 simultaneous access to same global mem in the (half-)warp will be coalesced - no worries
                    os << "((B(dd_gp" << model.synapseName[i] << "[gid >> " << logUIntSz << "], gid & " << UIntSz - 1 << ")) && ";
                }

                // code substitutions ----
                string eCode = weightUpdateModels[synt].evntThreshold;
                value_substitutions(eCode, weightUpdateModels[synt].pNames, model.synapsePara[i]);
                value_substitutions(eCode, weightUpdateModels[synt].dpNames, model.dsp_w[i]);
                name_substitutions(eCode, "", weightUpdateModels[synt].extraGlobalSynapseKernelParameters, model.synapseName[i]);
                neuron_substitutions_in_synaptic_code(eCode, model, src, trg, nt_pre, nt_post, offsetPre, offsetPost, "shSpkEvnt[j]", "ipost", "dd_");
                eCode= ensureFtype(eCode, model.ftype);
                checkUnreplacedVariables(eCode, "evntThreshold");
                // end code substitutions ----
                os << "(" << eCode << ")";

                if (model.synapseGType[i] == INDIVIDUALID) {
                    os << ")";
                }
                os << OB(130);
            }
            else if (model.synapseGType[i] == INDIVIDUALID) {
                os << "if (B(dd_gp" << model.synapseName[i] << "[gid >> " << logUIntSz << "], gid & " << UIntSz - 1 << "))" << OB(135);
            }

            if (sparse) { // SPARSE
                os << "prePos = dd_indInG" << model.synapseName[i] << "[shSpk" << postfix << "[j]];" << ENDL;
                os << "npost = dd_indInG" << model.synapseName[i] << "[shSpk" << postfix << "[j] + 1] - prePos;" << ENDL;
                os << "if (" << localID << " < npost)" << OB(140);
                os << "prePos += " << localID << ";" << ENDL;
                os << "ipost = dd_ind" << model.synapseName[i] << "[prePos];" << ENDL;
            }
            else { // DENSE
            os << "ipost = " << localID << ";" << ENDL;
            }

            // Code substitutions ----------------------------------------------------------------------------------
            string wCode = (evnt ? weightUpdateModels[synt].simCodeEvnt : weightUpdateModels[synt].simCode);
            substitute(wCode, "$(t)", "t");
            if (sparse) { // SPARSE
                if (isGrpVarNeeded[model.synapseTarget[i]]) { // SPARSE using atomicAdd
                    substitute(wCode, "$(updatelinsyn)", theAtomicAdd + "(&$(inSyn), $(addtoinSyn))");
                    substitute(wCode, "$(inSyn)", "dd_inSyn" + model.synapseName[i] + "[ipost]");
                }
                else { // SPARSE using shared memory
                    substitute(wCode, "$(updatelinsyn)", "$(inSyn) += $(addtoinSyn)");
                    substitute(wCode, "$(inSyn)", "shLg[ipost]");
                }
                if (model.synapseGType[i] == INDIVIDUALG) {
                    name_substitutions(wCode, "dd_", weightUpdateModels[synt].varNames, model.synapseName[i] + "[prePos]");
                }
                else {
                    value_substitutions(wCode, weightUpdateModels[synt].varNames, model.synapseIni[i]);
                }
        }
            else { // DENSE
                substitute(wCode, "$(updatelinsyn)", "$(inSyn) += $(addtoinSyn)");
                substitute(wCode, "$(inSyn)", "linSyn");
                if (model.synapseGType[i] == INDIVIDUALG) {
                    name_substitutions(wCode, "dd_", weightUpdateModels[synt].varNames, model.synapseName[i] + "[shSpk"
                                       + postfix + "[j] * " + tS(model.neuronN[trg]) + "+ ipost]");
                }
                else {
                    value_substitutions(wCode, weightUpdateModels[synt].varNames, model.synapseIni[i]);
                }
            }
            value_substitutions(wCode, weightUpdateModels[synt].pNames, model.synapsePara[i]);
            value_substitutions(wCode, weightUpdateModels[synt].dpNames, model.dsp_w[i]);
            name_substitutions(wCode, "", weightUpdateModels[synt].extraGlobalSynapseKernelParameters, model.synapseName[i]);
            substitute(wCode, "$(addtoinSyn)", "addtoinSyn");
            neuron_substitutions_in_synaptic_code(wCode, model, src, trg, nt_pre, nt_post, offsetPre, offsetPost, "shSpk" + postfix + "[j]", "ipost", "dd_");
            wCode= ensureFtype(wCode, model.ftype);
            checkUnreplacedVariables(wCode, "simCode"+postfix);
            // end Code substitutions -------------------------------------------------------------------------
            os << wCode << ENDL;

            if (sparse) {
                os << CB(140); // end if (id < npost)
            }

            if ((evnt) && (model.needEvntThresholdReTest[i])) {
                os << CB(130); // end if (eCode)
            }
            else if (model.synapseGType[i] == INDIVIDUALID) {
                os << CB(135); // end if (B(dd_gp" << model.synapseName[i] << "[gid >> " << logUIntSz << "], gid
            }
            os << CB(120) << ENDL;

            if ((sparse) && (!isGrpVarNeeded[model.synapseTarget[i]])) {
                os << "__syncthreads();" << ENDL;
                os << "if (threadIdx.x < " << model.neuronN[model.synapseTarget[i]] << ")" << OB(136); // need to write back results
                os << "linSyn += shLg[" << localID << "];" << ENDL;
                os << "shLg[" << localID << "] = 0;" << ENDL;
                os << CB(136) << ENDL;

                os << "__syncthreads();" << ENDL;
            }
            os << CB(110) << ENDL;
        os << CB(90) << ENDL;
        }
    }
}


//-------------------------------------------------------------------------
/*!
  \brief Function for generating a CUDA kernel for simulating all synapses.

  This functions generates code for global variables on the GPU side that are 
  synapse-related and the actual CUDA kernel for simulating one time step of 
  the synapses.
*/
//-------------------------------------------------------------------------

void genSynapseKernel(const NNmodel &model, //!< Model description
                      const string &path //!< Path for code generation
    )
{
    string name, s;
    string localID; //!< "id" if first synapse group, else "lid". lid =(thread index- last thread of the last synapse group)
    unsigned int k, src, trg, synt, inSynNo;
    ofstream os;

    // count how many neuron blocks to use: one thread for each synapse target
    // targets of several input groups are counted multiply
    unsigned int numOfBlocks = model.padSumSynapseKrnl[model.synapseGrpN - 1] / synapseBlkSz;

//    cout << "entering genSynapseKernel" << endl;
    name = path + "/" + model.name + "_CODE/synapseKrnl.cc";
    os.open(name.c_str());

    // write header content
    writeHeader(os);
    os << ENDL;

    // compiler/include control (include once)
    os << "#ifndef _" << model.name << "_synapseKrnl_cc" << ENDL;
    os << "#define _" << model.name << "_synapseKrnl_cc" << ENDL;
    os << "#define BLOCKSZ_SYN " << synapseBlkSz << ENDL;
    os << ENDL;
 
    // write doxygen comment
    os << "//-------------------------------------------------------------------------" << ENDL;
    os << "/*! \\file synapseKrnl.cc" << ENDL << ENDL;
    os << "\\brief File generated from GeNN for the model " << model.name;
    os << " containing the synapse kernel and learning kernel functions." << ENDL;
    os << "*/" << ENDL;
    os << "//-------------------------------------------------------------------------" << ENDL << ENDL;


    if (model.synDynGroups > 0) {
        os << "#define BLOCKSZ_SYNDYN " << synDynBlkSz << endl;
	
        // SynapseDynamics kernel header
        os << "extern \"C\" __global__ void calcSynapseDynamics(";
        for (int i= 0, l= model.synapseDynamicsKernelParameters.size(); i < l; i++) {
            os << model.synapseDynamicsKernelParameterTypes[i] << " " << model.synapseDynamicsKernelParameters[i] << ", ";
        }
        os << model.ftype << " t)" << ENDL; // end of synapse kernel header

        // synapse dynamics kernel code
        os << OB(75);

        // common variables for all cases
        os << "unsigned int id = BLOCKSZ_SYNDYN * blockIdx.x + threadIdx.x;" << ENDL;

        os << "// execute internal synapse dynamics if any" << ENDL;
        os << ENDL;

        for (int i = 0; i < model.synDynGroups; i++) {
            k= model.synDynGrp[i];
            src= model.synapseSource[k];
            trg= model.synapseTarget[k];
            synt= model.synapseType[k];
            string synapseName= model.synapseName[k];
            unsigned int srcno= model.neuronN[src];
            unsigned int trgno= model.neuronN[trg];
            int nt_pre= model.neuronType[src];
            int nt_post= model.neuronType[trg];
            bool delayPre = model.neuronDelaySlots[src] > 1;
            bool delayPost = model.neuronDelaySlots[trg] > 1;
            string offsetPre = (delayPre ? "(delaySlot * " + tS(model.neuronN[src]) + ") + " : "");
            string offsetPost = (delayPost ? "(dd_spkQuePtr" + model.neuronName[trg] +" * " + tS(model.neuronN[trg]) + ") + " : "");

            // if there is some internal synapse dynamics
            if (!weightUpdateModels[synt].synapseDynamics.empty()) {

                os << "// synapse group " << synapseName << ENDL;
                if (i == 0) {
                    os << "if (id < " << model.padSumSynDynN[i] << ")" << OB(77);
                    localID = "id";
                }
                else {
                    os << "if ((id >= " << model.padSumSynDynN[i - 1] << ") && (id < " << model.padSumSynDynN[i] << "))" << OB(77);
                    os << "unsigned int lid = id - " << model.padSumSynDynN[i - 1] << ";" << ENDL;
                    localID = "lid";
                }

                if (model.neuronDelaySlots[src] > 1) {
                    os << "unsigned int delaySlot = (dd_spkQuePtr" << model.neuronName[src];
                    os << " + " << (model.neuronDelaySlots[src] - model.synapseDelay[k]);
                    os << ") % " << model.neuronDelaySlots[src] << ";" << ENDL;
                }

                weightUpdateModel wu= weightUpdateModels[synt];
                string SDcode= wu.synapseDynamics;
                substitute(SDcode, "$(t)", "t");

                if (model.synapseConnType[k] == SPARSE) { // SPARSE
                    os << "if (" << localID << " < dd_indInG" << synapseName << "[" << srcno << "])" << OB(25);
                    os << "// all threads participate that can work on an existing synapse" << ENDL;
		    if (!wu.synapseDynamics_supportCode.empty()) {
			            os << " using namespace " << synapseName << "_weightupdate_synapseDynamics;" << ENDL;
		    }
                    if (model.synapseGType[k] == INDIVIDUALG) {
                        // name substitute synapse var names in synapseDynamics code
                        name_substitutions(SDcode, "dd_", wu.varNames, synapseName + "[" + localID +"]");
                    }
                    else {
                        // substitute initial values as constants for synapse var names in synapseDynamics code
                        value_substitutions(SDcode, wu.varNames, model.synapseIni[k]);
                    }
                    // substitute parameter values for parameters in synapseDynamics code
                    value_substitutions(SDcode, wu.pNames, model.synapsePara[k]);
                    // substitute values for derived parameters in synapseDynamics code
                    value_substitutions(SDcode, wu.dpNames, model.dsp_w[k]);
                    neuron_substitutions_in_synaptic_code(SDcode, model, src, trg, nt_pre, nt_post, offsetPre, offsetPost, "dd_preInd"+synapseName+"[" + localID + "]", "dd_ind"+synapseName+"[" + localID + "]", "dd_");
                    SDcode= ensureFtype(SDcode, model.ftype);
                    checkUnreplacedVariables(SDcode, "synapseDynamics");
                    os << SDcode << ENDL;
                }
                else { // DENSE
                    os << "if (" << localID << " < " << srcno*trgno << ")" << OB(25);
                    os << "// all threads participate that can work on an existing synapse" << ENDL;
 		    if (!wu.synapseDynamics_supportCode.empty()) {
			os << " using namespace " << model.synapseName[i] << "_weightupdate_synapseDynamics;" << ENDL;
		    }
		    if (model.synapseGType[k] == INDIVIDUALG) {
                        // name substitute synapse var names in synapseDynamics code
                        name_substitutions(SDcode, "dd_", wu.varNames, synapseName + "[" + localID + "]");
                    }
                    else {
                        // substitute initial values as constants for synapse var names in synapseDynamics code
                        value_substitutions(SDcode, wu.varNames, model.synapseIni[k]);
                    }
                    // substitute parameter values for parameters in synapseDynamics code
                    value_substitutions(SDcode, wu.pNames, model.synapsePara[k]);
                    // substitute values for derived parameters in synapseDynamics code
                    value_substitutions(SDcode, wu.dpNames, model.dsp_w[k]);
                    neuron_substitutions_in_synaptic_code(SDcode, model, src, trg, nt_pre, nt_post, offsetPre, offsetPost, localID +"/" + tS(model.neuronN[trg]), localID +"%" + tS(model.neuronN[trg]), "dd_");
                    SDcode= ensureFtype(SDcode, model.ftype);
                    checkUnreplacedVariables(SDcode, "synapseDynamics");
                    os << SDcode << ENDL;
                }
                os << CB(25);
                os << CB(77);
            }
        }
        os << CB(75);
    }
    
    // synapse kernel header
    os << "extern \"C\" __global__ void calcSynapses(";
    for (int i= 0, l= model.synapseKernelParameters.size(); i < l; i++) {
	os << model.synapseKernelParameterTypes[i] << " " << model.synapseKernelParameters[i] << ", ";
    }
    os << model.ftype << " t)" << ENDL; // end of synapse kernel header

    // synapse kernel code
    os << OB(75);

    // common variables for all cases
    os << "unsigned int id = BLOCKSZ_SYN * blockIdx.x + threadIdx.x;" << ENDL;
    os << "unsigned int lmax, j, r;" << ENDL;
    os << model.ftype << " addtoinSyn;" << ENDL;  
    os << "volatile __shared__ " << model.ftype << " shLg[BLOCKSZ_SYN];" << ENDL;

    // case-dependent variables
    for (int i = 0; i < model.synapseGrpN; i++) { 
        if ((model.synapseConnType[i] != SPARSE) || (!isGrpVarNeeded[model.synapseTarget[i]])){
            os << model.ftype << " linSyn;" << ENDL;
            break;
        }
    }
    // we need ipost in any case, and we need npost if there are any SPARSE connections
    os << "unsigned int ipost;" << ENDL;
    for (int i = 0; i < model.synapseGrpN; i++) {  
        if (model.synapseConnType[i] == SPARSE) {
            os << "unsigned int prePos; " << ENDL;
            os << "unsigned int npost; " << ENDL;
            break;
        }
    }  
    for (int i = 0; i < model.synapseGrpN; i++) {
        if (model.synapseUsesTrueSpikes[i] || model.synapseUsesPostLearning[i]) {
            os << "__shared__ unsigned int shSpk[BLOCKSZ_SYN];" << ENDL;
            //os << "__shared__ " << model.ftype << " shSpkV[BLOCKSZ_SYN];" << ENDL;
            os << "unsigned int lscnt, numSpikeSubsets;" << ENDL;
            break;
        }
    }
    for (int i = 0; i < model.synapseGrpN; i++) {
        if (model.synapseUsesSpikeEvents[i]) {
            os << "__shared__ unsigned int shSpkEvnt[BLOCKSZ_SYN];" << ENDL;
            os << "unsigned int lscntEvnt, numSpikeSubsetsEvnt;" << ENDL;
            break;
        }
    }
    os << ENDL;

    for (int i = 0; i < model.synapseGrpN; i++) {
        src = model.synapseSource[i];
        trg = model.synapseTarget[i];
        synt = model.synapseType[i];
        inSynNo = model.synapseInSynNo[i];

        os << "// synapse group " << model.synapseName[i] << ENDL;
        if (i == 0) {
            os << "if (id < " << model.padSumSynapseKrnl[i] << ")" << OB(77);
            localID = "id";
        }
        else {
            os << "if ((id >= " << model.padSumSynapseKrnl[i - 1] << ") && (id < " << model.padSumSynapseKrnl[i] << "))" << OB(77);
            os << "unsigned int lid = id - " << model.padSumSynapseKrnl[i - 1] << ";" << ENDL;
            localID = "lid";
        }

        if (model.neuronDelaySlots[src] > 1) {
            os << "unsigned int delaySlot = (dd_spkQuePtr" << model.neuronName[src];
            os << " + " << (model.neuronDelaySlots[src] - model.synapseDelay[i]);
            os << ") % " << model.neuronDelaySlots[src] << ";" << ENDL;
        }

        if ((model.synapseConnType[i] != SPARSE) || (!isGrpVarNeeded[model.synapseTarget[i]])){
            os << "// only do this for existing neurons" << ENDL;
            os << "if (" << localID << " < " << model.neuronN[trg] << ")" << OB(80);
            os << "linSyn = dd_inSyn" << model.synapseName[i] << "[" << localID << "];" << ENDL;
            os << CB(80);
        }

        if (model.synapseUsesSpikeEvents[i]) {
            os << "lscntEvnt = dd_glbSpkCntEvnt" << model.neuronName[src];
            if (model.neuronDelaySlots[src] > 1) {
                os << "[delaySlot];" << ENDL;
            }
            else {
                os << "[0];" << ENDL;
            }
            os << "numSpikeSubsetsEvnt = (lscntEvnt+BLOCKSZ_SYN-1) / BLOCKSZ_SYN;" << ENDL;
        }
  
        if ((model.synapseUsesTrueSpikes[i]) || (model.synapseUsesPostLearning[i])) {
            os << "lscnt = dd_glbSpkCnt" << model.neuronName[src];
            if (model.neuronDelaySlots[src] > 1) {
                os << "[delaySlot];" << ENDL;
            }
            else {
                os << "[0];" << ENDL;
            }
            os << "numSpikeSubsets = (lscnt+BLOCKSZ_SYN-1) / BLOCKSZ_SYN;" << ENDL;
        }

        // generate the code for processing spike-like events
        if (model.synapseUsesSpikeEvents[i]) {
            generate_process_presynaptic_events_code(os, model, src, trg, i, localID, inSynNo, "Evnt");
        }

        // generate the code for processing true spike events
        if (model.synapseUsesTrueSpikes[i]) {
            generate_process_presynaptic_events_code(os, model, src, trg, i, localID, inSynNo, "");
        }
        os << ENDL;

        if ((model.synapseConnType[i] != SPARSE) || (!isGrpVarNeeded[model.synapseTarget[i]])) {
            os << "// only do this for existing neurons" << ENDL;
            os << "if (" << localID << " < " << model.neuronN[trg] << ")" << OB(190);
            os << "dd_inSyn" << model.synapseName[i] << "[" << localID << "] = linSyn;" << ENDL;
            os << CB(190);
        }
        // need to do reset operations in this kernel (no learning kernel)
        if (model.resetKernel == GENN_FLAGS::calcSynapses) {
            os << "__syncthreads();" << ENDL;
            os << "if (threadIdx.x == 0)" << OB(200);
            os << "j = atomicAdd((unsigned int *) &d_done, 1);" << ENDL;
            os << "if (j == " << numOfBlocks - 1 << ")" << OB(210);

            for (int j = 0; j < model.neuronGrpN; j++) {
                if (model.neuronDelaySlots[j] > 1) { // WITH DELAY
                    os << "dd_spkQuePtr" << model.neuronName[j] << " = (dd_spkQuePtr" << model.neuronName[j] << " + 1) % " << model.neuronDelaySlots[j] << ";" << ENDL;
                    if (model.neuronNeedSpkEvnt[j]) {
                        os << "dd_glbSpkCntEvnt" << model.neuronName[j] << "[dd_spkQuePtr" << model.neuronName[j] << "] = 0;" << ENDL;
                    }
                    if (model.neuronNeedTrueSpk[j]) {
                        os << "dd_glbSpkCnt" << model.neuronName[j] << "[dd_spkQuePtr" << model.neuronName[j] << "] = 0;" << ENDL;
                    }
                    else {
                        os << "dd_glbSpkCnt" << model.neuronName[j] << "[0] = 0;" << ENDL;
                    }
                }
                else { // NO DELAY
                    if (model.neuronNeedSpkEvnt[j]) {
                        os << "dd_glbSpkCntEvnt" << model.neuronName[j] << "[0] = 0;" << ENDL;
                    }
                    os << "dd_glbSpkCnt" << model.neuronName[j] << "[0] = 0;" << ENDL;
                }
            }
            os << "d_done = 0;" << ENDL;

            os << CB(210); // end "if (j == " << numOfBlocks - 1 << ")"
            os << CB(200); // end "if (threadIdx.x == 0)"
        }

        os << CB(77);
        os << ENDL;
    }
    os << CB(75);
    os << ENDL;


    ///////////////////////////////////////////////////////////////
    // Kernel for learning synapses, post-synaptic spikes

    if (model.lrnGroups > 0) {

        // count how many learn blocks to use: one thread for each synapse source
        // sources of several output groups are counted multiply
        numOfBlocks = model.padSumLearnN[model.lrnGroups - 1] / learnBlkSz;
  
        // Kernel header
        os << "extern \"C\" __global__ void learnSynapsesPost(";
        for (int i= 0, l= model.simLearnPostKernelParameters.size(); i < l; i++) {
            os << model.simLearnPostKernelParameterTypes[i] << " " << model.simLearnPostKernelParameters[i] << ", ";
        }
        os << model.ftype << " t)";
        os << ENDL;

        // kernel code
        os << OB(215);
        os << "unsigned int id = " << learnBlkSz << " * blockIdx.x + threadIdx.x;" << ENDL;
        os << "__shared__ unsigned int shSpk[" << learnBlkSz << "];" << ENDL;
        os << "unsigned int lscnt, numSpikeSubsets, lmax, j, r;" << ENDL;
        os << ENDL;

        for (int i = 0; i < model.lrnGroups; i++) {
            k = model.lrnSynGrp[i];
            src = model.synapseSource[k];
            trg = model.synapseTarget[k];
            synt = model.synapseType[k];
            inSynNo = model.synapseInSynNo[k];
            unsigned int nN = model.neuronN[src];
            bool sparse = model.synapseConnType[k] == SPARSE;

            unsigned int nt_pre = model.neuronType[src];
            bool delayPre = model.neuronDelaySlots[src] > 1;
            string offsetPre = (delayPre ? "(delaySlot * " + tS(model.neuronN[src]) + ") + " : "");
            string offsetTrueSpkPre = (model.neuronNeedTrueSpk[src] ? offsetPre : "");

            unsigned int nt_post = model.neuronType[trg];
            bool delayPost = model.neuronDelaySlots[trg] > 1;
            string offsetPost = (delayPost ? "(dd_spkQuePtr" + model.neuronName[trg] + " * " + tS(model.neuronN[trg]) + ") + " : "");
            string offsetTrueSpkPost = (model.neuronNeedTrueSpk[trg] ? offsetPost : "");

// NOTE: WE DO NOT USE THE AXONAL DELAY FOR BACKWARDS PROPAGATION - WE CAN TALK ABOUT BACKWARDS DELAYS IF WE WANT THEM
            os << "// synapse group " << model.synapseName[k] << ENDL;
            if (i == 0) {
                os << "if (id < " << model.padSumLearnN[i] << ")" << OB(220);
                localID = "id";
            }
            else {
                os << "if ((id >= " << model.padSumLearnN[i - 1] << ") && (id < " << model.padSumLearnN[i] << "))" << OB(220);
                os << "unsigned int lid = id - " << model.padSumLearnN[i - 1] << ";" << ENDL;
                localID = "lid";
            }

            if (delayPre) {
                os << "unsigned int delaySlot = (dd_spkQuePtr" << model.neuronName[src];
                os << " + " << (model.neuronDelaySlots[src] - model.synapseDelay[k]);
                os << ") % " << model.neuronDelaySlots[src] << ";" << ENDL;
            }

            if (delayPost && model.neuronNeedTrueSpk[trg]) {
                os << "lscnt = dd_glbSpkCnt" << model.neuronName[trg] << "[dd_spkQuePtr" << model.neuronName[trg] << "];" << ENDL;
            }
            else {
                os << "lscnt = dd_glbSpkCnt" << model.neuronName[trg] << "[0];" << ENDL;
            }

            os << "numSpikeSubsets = (lscnt+" << learnBlkSz-1 << ") / " << learnBlkSz << ";" << ENDL;
            os << "for (r = 0; r < numSpikeSubsets; r++)" << OB(230);
            os << "if (r == numSpikeSubsets - 1) lmax = ((lscnt-1) % " << learnBlkSz << ")+1;" << ENDL;
            os << "else lmax = " << learnBlkSz << ";" << ENDL;

            os << "if (threadIdx.x < lmax)" << OB(240);
            os << "shSpk[threadIdx.x] = dd_glbSpk" << model.neuronName[trg] << "[" << offsetTrueSpkPost << "(r * " << learnBlkSz << ") + threadIdx.x];" << ENDL;
            os << CB(240);

            os << "__syncthreads();" << ENDL;
            os << "// only work on existing neurons" << ENDL;
            os << "if (" << localID << " < " << model.neuronN[src] << ")" << OB(250);
            os << "// loop through all incoming spikes for learning" << ENDL;
            os << "for (j = 0; j < lmax; j++)" << OB(260) << ENDL;

            if (sparse) {
        os << "unsigned int iprePos = dd_revIndInG" <<  model.synapseName[k] << "[shSpk[j]];" << ENDL;
        os << "unsigned int npre = dd_revIndInG" << model.synapseName[k] << "[shSpk[j] + 1] - iprePos;" << ENDL;
        os << "if (" << localID << " < npre)" << OB(1540);
              os << "iprePos += " << localID << ";" << ENDL;
        //Commenting out the next line as it is not used rather than deleting as I'm not sure if it may be used by different learning models 
              //os << "unsigned int ipre = dd_revInd" << model.synapseName[i] << "[iprePos];" << ENDL;
            }
            if (!weightUpdateModels[synt].simLearnPost_supportCode.empty()) {
                os << " using namespace " << model.synapseName[k] << "_weightupdate_simLearnPost;" << ENDL;
            }
            string code = weightUpdateModels[synt].simLearnPost;
            substitute(code, "$(t)", "t");
            // Code substitutions ----------------------------------------------------------------------------------
            if (sparse) { // SPARSE
                name_substitutions(code, "dd_", weightUpdateModels[synt].varNames, model.synapseName[k] + "[dd_remap" + model.synapseName[k] + "[iprePos]]");
            }
            else { // DENSE
                name_substitutions(code, "dd_", weightUpdateModels[synt].varNames, model.synapseName[k] + "[" + localID + " * " + to_string(model.neuronN[trg]) + " + shSpk[j]]");
            }
            value_substitutions(code, weightUpdateModels[synt].pNames, model.synapsePara[k]);
            value_substitutions(code, weightUpdateModels[synt].dpNames, model.dsp_w[k]);
            name_substitutions(code, "", weightUpdateModels[synt].extraGlobalSynapseKernelParameters, model.synapseName[k]);

            // presynaptic neuron variables and parameters
            if (sparse) { // SPARSE
                neuron_substitutions_in_synaptic_code(code, model, src, trg, nt_pre, nt_post, offsetPre, offsetPost, "dd_revInd" + model.synapseName[k] + "[iprePos]", "shSpk[j]", "dd_");
            }
            else { // DENSE
                neuron_substitutions_in_synaptic_code(code, model, src, trg, nt_pre, nt_post, offsetPre, offsetPost, localID, "shSpk[j]", "dd_");
            }
            code= ensureFtype(code, model.ftype);
            checkUnreplacedVariables(code, "simLearnPost");
            // end Code substitutions -------------------------------------------------------------------------
            os << code << ENDL;
            if (sparse) {
                os << CB(1540);
            }
            os << CB(260);
            os << CB(250);
            os << CB(230);
            if (model.resetKernel == GENN_FLAGS::learnSynapsesPost) {
                os << "__syncthreads();" << ENDL;
                os << "if (threadIdx.x == 0)" << OB(320);
                os << "j = atomicAdd((unsigned int *) &d_done, 1);" << ENDL;
                os << "if (j == " << numOfBlocks - 1 << ")" << OB(330);

                for (int j = 0; j < model.neuronGrpN; j++) {
                    if (model.neuronDelaySlots[j] > 1) { // WITH DELAY
                        os << "dd_spkQuePtr" << model.neuronName[j] << " = (dd_spkQuePtr" << model.neuronName[j] << " + 1) % " << model.neuronDelaySlots[j] << ";" << ENDL;
                        if (model.neuronNeedSpkEvnt[j]) {
                            os << "dd_glbSpkCntEvnt" << model.neuronName[j] << "[dd_spkQuePtr" << model.neuronName[j] << "] = 0;" << ENDL;
                        }
                        if (model.neuronNeedTrueSpk[j]) {
                            os << "dd_glbSpkCnt" << model.neuronName[j] << "[dd_spkQuePtr" << model.neuronName[j] << "] = 0;" << ENDL;
                        }
                        else {
                            os << "dd_glbSpkCnt" << model.neuronName[j] << "[0] = 0;" << ENDL;
                        }
                    }
                    else { // NO DELAY
                        if (model.neuronNeedSpkEvnt[j]) {
                            os << "dd_glbSpkCntEvnt" << model.neuronName[j] << "[0] = 0;" << ENDL;
                        }
                        os << "dd_glbSpkCnt" << model.neuronName[j] << "[0] = 0;" << ENDL;
                    }
                }
                os << "d_done = 0;" << ENDL;

                os << CB(330); // end "if (j == " << numOfBlocks - 1 << ")"
                os << CB(320); // end "if (threadIdx.x == 0)"
            }
            os << CB(220);
        }

        os << CB(215);
    }
    os << ENDL;
    
    os << "#endif" << ENDL;
    os.close();

//    cout << "exiting genSynapseKernel" << endl;
}

#endif
