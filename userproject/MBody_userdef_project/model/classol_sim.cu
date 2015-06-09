/*--------------------------------------------------------------------------
   Author: Thomas Nowotny
  
   Institute: Institute for Nonlinear Science
              University of California San Diego
              La Jolla, CA 92093-0402
  
   email to:  tnowotny@ucsd.edu
  
   initial version: 2002-09-26
  
--------------------------------------------------------------------------*/

//--------------------------------------------------------------------------
/*! \file classol_sim.cu

\brief Main entry point for the classol (CLASSification in OLfaction) model simulation. Provided as a part of the complete example of simulating the MBody1 mushroom body model. 
*/
//--------------------------------------------------------------------------


#include "classol_sim.h"

//--------------------------------------------------------------------------
/*! \brief This function is the entry point for running the simulation of the MBody1 model network.
*/
//--------------------------------------------------------------------------


int main(int argc, char *argv[])
{
  if (argc != 3)
  {
    fprintf(stderr, "usage: classol_sim <basename> <CPU=0, GPU=1> \n");
    return 1;
  }
  int which = atoi(argv[2]);
  string OutDir = toString(argv[1]) +"_output";
  string name;
  name= OutDir+ "/"+ toString(argv[1]) + toString(".time");
  FILE *timef= fopen(name.c_str(),"a");  

  patSetTime= (int) (PAT_TIME/DT);
  patFireTime= (int) (PATFTIME/DT);
  fprintf(stdout, "# DT %f \n", DT);
  fprintf(stdout, "# T_REPORT_TME %f \n", T_REPORT_TME);
  fprintf(stdout, "# SYN_OUT_TME %f \n",  SYN_OUT_TME);
  fprintf(stdout, "# PATFTIME %f \n", PATFTIME); 
  fprintf(stdout, "# patFireTime %d \n", patFireTime);
  fprintf(stdout, "# PAT_TIME %f \n", PAT_TIME);
  fprintf(stdout, "# patSetTime %d \n", patSetTime);
  fprintf(stdout, "# TOTAL_TME %f \n", TOTAL_TME);
  
  name= OutDir+ "/"+ toString(argv[1]) + toString(".out.Vm"); 
  FILE *osf= fopen(name.c_str(),"w");
  name= OutDir+ "/"+ toString(argv[1]) + toString(".out.st"); 
  FILE *osf2= fopen(name.c_str(),"w");
  
#ifdef TIMING
  name= OutDir+ "/"+ toString(argv[1]) + toString(".timingprofile"); 
  FILE *timeros= fopen(name.c_str(),"w");
  double tme;
#endif


  //-----------------------------------------------------------------
  // build the neuronal circuitery
  classol locust;

#ifdef TIMING
  timer.startTimer();
#endif

  fprintf(stdout, "# reading PN-KC synapses ... \n");
  name= OutDir+ "/"+ toString(argv[1]) + toString(".pnkc");
  FILE *f= fopen(name.c_str(),"rb");
  locust.read_pnkcsyns(f);
  fclose(f);
 
#ifdef TIMING
  timer.stopTimer();
  tme= timer.getElapsedTime();
  fprintf(timeros, "%% Reading PN-KC synapses: %f \n", tme);
  timer.startTimer();
#endif

  fprintf(stdout, "# reading PN-LHI synapses ... \n");
  name= OutDir+ "/"+ toString(argv[1]) + toString(".pnlhi");
  f= fopen(name.c_str(), "rb");
  locust.read_pnlhisyns(f);
  fclose(f);   
  
#ifdef TIMING
  timer.stopTimer();
  tme= timer.getElapsedTime();
  fprintf(timeros, "%% Reading PN-LHI synapses: %f \n", tme);
  timer.startTimer();
#endif

  fprintf(stdout, "# reading KC-DN synapses ... \n");
  name= OutDir+ "/"+ toString(argv[1]) + toString(".kcdn");
  f= fopen(name.c_str(), "rb");
  locust.read_kcdnsyns(f);

#ifdef TIMING
  timer.stopTimer();
  tme= timer.getElapsedTime();
  fprintf(timeros, "%% Reading KC-DN synapses: %f \n", tme);
  timer.startTimer();
#endif


  fprintf(stdout, "# reading input patterns ... \n");
  name= OutDir+ "/"+ toString(argv[1]) + toString(".inpat");
  f= fopen(name.c_str(), "rb");
  locust.read_input_patterns(f);
  fclose(f);

#ifdef TIMING
  timer.stopTimer();
  tme= timer.getElapsedTime();
  fprintf(timeros, "%% Reading input patterns: %f \n", tme);
  timer.startTimer();
#endif

  locust.generate_baserates();
  //set values here createSparseConnectivityFromDense(locust.model.neuronN[0],locust.model.neuronN[1],gpPNKC, &gPNKC, false);
   if (which == GPU) {
    locust.allocate_device_mem_patterns();
  }
  locust.init(which);         // this includes copying g's for the GPU version

#ifdef TIMING
  timer.stopTimer();
  tme= timer.getElapsedTime();
  fprintf(timeros, "%% Initialisation: %f \n", tme);
#endif


  fprintf(stdout, "# neuronal circuitery built, start computation ... \n\n");

  //------------------------------------------------------------------
  // output general parameters to output file and start the simulation

  fprintf(stdout, "# We are running with fixed time step %f \n", DT);
  t= 0.0;
  int done= 0;
  double last_t_report=  t;
  timer.startTimer();
//  locust.output_state(os, which);  
//  locust.output_spikes(os, which);  
if (which == GPU){ 

  locust.runGPU(DT);
 
  //double synwriteT= 0.0f;
  //double lastsynwrite= 0.0f;
 // int synwrite= 0;
//  locust.output_state(os, which);  
//  float synwriteT= 0.0f;
//  int synwrite= 0;
//  unsigned int sum= 0;
  while (!done) 
  {
    locust.getSpikeNumbersFromGPU();
    locust.getSpikesFromGPU();
    locust.runGPU(DT); // run next batch
 
    //pullDNStateFromDevice();
    
    #ifdef TIMING
     	fprintf(timeros, "%f %f %f \n", neuron_tme, synapse_tme, learning_tme);
    #endif 

    locust.sum_spikes();
//    locust.output_spikes(osf, which);
//    locust.output_state(os, which);  // while outputting the current one ...

   
    locust.output_spikes(osf2, which);

/*    fprintf(osf, "%f ", t);
    for (int i= 0; i < 100; i++) {
    fprintf(osf, "%f ", VDN[i]);
   }
    fprintf(osf,"\n");
*/
    // report progress
    if (t - last_t_report >= T_REPORT_TME)
      {
        fprintf(stdout, "time %f \n", t);
        last_t_report= t;
      }
    // output synapses occasionally
    /*if (synwrite) {
      lastsynwrite= synwriteT;
      name= OutDir + "/" + tS(argv[1]) + tS(".") + tS((int) synwriteT) + tS(".syn");
      f= fopen(name.c_str(),"w");
      locust.write_kcdnsyns(f);
      fclose(f);
      synwrite= 0;
    }
  
    if (t - lastsynwrite >= SYN_OUT_TME) {
      locust.get_kcdnsyns();
      synwrite= 1;
      synwriteT= t;
    }*/
    done= (t >= TOTAL_TME);
    //pullDNStateFromDevice();
  }
}

if (which == CPU){ 
  locust.runCPU(DT);
 
 // double synwriteT= 0.0f;
 // double lastsynwrite= 0.0f;
 // int synwrite= 0;

  while (!done) 
  {

    locust.runCPU(DT); // run next batch

    #ifdef TIMING
	    fprintf(timeros, "%f %f %f \n", neuron_tme, synapse_tme, learning_tme);
    #endif 

    locust.sum_spikes();
//    locust.output_spikes(osf, which);
//    locust.output_state(os, which);  // while outputting the current one ...
 
    locust.output_spikes(osf2, which);

/*    fprintf(osf, "%f ", t);
    for (int i= 0; i < 100; i++) {
    fprintf(osf, "%f ", VDN[i]);
   }
    fprintf(osf,"\n");
*/
    // report progress
    if (t - last_t_report >= T_REPORT_TME)
      {
        fprintf(stdout, "time %f \n", t);
        last_t_report= t;
      }

    // output synapses occasionally
    /*if (synwrite) {
      lastsynwrite= synwriteT;
      name= OutDir + "/" + tS(argv[1]) + tS(".") + tS((int) synwriteT) + tS(".syn");
      f= fopen(name.c_str(),"w");
      locust.write_kcdnsyns(f);
      fclose(f);
      synwrite= 0;
    }
  
    if (t - lastsynwrite >= SYN_OUT_TME) {
      locust.get_kcdnsyns();
      synwrite= 1;
      synwriteT= t;
    }*/
    done= (t >= TOTAL_TME);
  }
}
//  locust.output_state(os);
//    if (which == GPU) locust.getSpikesFromGPU();
//    locust.output_spikes(os, which);
  // if (synwrite) {
  //   lastsynwrite= t;
  //   name= toString(argv[1]) + toString(".") + toString((int) t);
  //   name+= toString(".syn");
  //   f= fopen(name.c_str());
  //   locust.write_kcdnsyns(f);
  // fclose(f);
  //   synwrite= 0;
  // }

  timer.stopTimer();
  if (which == GPU) pullDNStateFromDevice();
  cerr << "output files are created under the current directory." << endl;
  fprintf(timef, "%d %u %u %u %u %u %.4f %.2f %.1f %.2f\n",which, locust.model.sumNeuronN[locust.model.neuronGrpN-1], locust.sumPN, locust.sumKC, locust.sumLHI, locust.sumDN, timer.getElapsedTime(),VDN[0], TOTAL_TME, DT);
  fprintf(stdout, "GPU=%d, %u neurons, %u PN spikes, %u KC spikes, %u LHI spikes, %u DN spikes, simulation took %.4f secs, VDN[0]=%.2f DT=%.1f %.2f\n",which, locust.model.sumNeuronN[locust.model.neuronGrpN-1], locust.sumPN, locust.sumKC, locust.sumLHI, locust.sumDN, timer.getElapsedTime(),VDN[0], TOTAL_TME, DT);
  fclose(osf);
  fclose(osf2);
  fclose(timef);

#ifdef TIMING
  fclose(timeros);
#endif

if (which == GPU) {
  locust.free_device_mem();
}
  return 0;
}
