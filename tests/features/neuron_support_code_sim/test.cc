// Google test includes
#include "gtest/gtest.h"

// Autogenerated simulation code includess
#include DEFINITIONS_HEADER

// **NOTE** base-class for simulation tests must be
// included after auto-generated globals are includes
#include "../../utils/simulation_test_vars.h"
#include "../../utils/simulation_neuron_policy_pre_var.h"
#include "../../utils/simulation_synapse_policy_dense.h"

// Combine neuron and synapse policies together to build variable-testing fixture
typedef SimulationTestVars<SimulationNeuronPolicyPreVar, SimulationSynapsePolicyDense> SimulationTestNeuronSupportCode;

TEST_P(SimulationTestNeuronSupportCode, AcceptableError)
{
  float err = Simulate(
    [](unsigned int d, unsigned int j, unsigned int k, float t, float &newX)
    {
        if ((t > 1.1001) && (fmod(t-DT-(d+1)*DT+5e-5,1.0f) < 1e-4))
        {
            newX = t-DT-(d+1)*DT+10*j;
            return true;
        }
        else
        {
          return false;
        }
    });

  // Check total error is less than some tolerance
  EXPECT_LT(err, 5e-2);
}

#ifndef CPU_ONLY
auto simulatorBackends = ::testing::Values(true, false);
#else
auto simulatorBackends = ::testing::Values(false);
#endif

INSTANTIATE_TEST_CASE_P(Sim,
                        SimulationTestNeuronSupportCode,
                        simulatorBackends);