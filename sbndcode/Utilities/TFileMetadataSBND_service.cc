////////////////////////////////////////////////////////////////////////
// Name:  TFileMetadataSBND_service.cc.  
//
// Purpose:  generate SBND-specific sam metadata for root Tfiles (histogram or ntuple files).
//
// FCL parameters: GenerateTFileMetadata: This needs to be set to "true" in the fcl file
//				    to generate metadata (default value: false)
//	     	   dataTier: Currrently this needs to be parsed by the user
//		     	     for ntuples, dataTier = root-tuple; 
//		             for histos, dataTier = root-histogram
//		             (default value: root-tuple)
//	           fileFormat: This is currently specified by the user,
//			       the fileFormat for Tfiles is "root" (default value: root)	
//
// Other notes: 1. This service uses the ART's standard file_catalog_metadata service
//		to extract some of the common (common to both ART and TFile outputs)
//	        job-specific metadata parameters, so, it is important to call this
//  		service in your fcl file
//		stick this line in your "services" section of fcl file:
//		FileCatalogMetadata:  @local::art_file_catalog_mc
//	
//              2. When you call FileCatalogMetadata service in your fcl file, and if 
//		you have (art) root Output section in your fcl file, and if you do not  
//		have "dataTier" specified in that section, then this service will throw
//		an exception. To avoid this, either remove the entire root Output section
//		in your fcl file (and remove art stream output from your end_paths) or 
//		include appropriate dataTier information in the section.If you are only
//		running analysis job, best way is to not include any art root Output section.
//
//	        3. This service is exclusively written to work with production (in other
//		words for jobs submitted through grid). Some of the metadata parameters
//		(output TFileName, filesize, Project related details) are captured/updated
//		during and/or after the workflow. 
//	
//
// Created:  21-Feb-2018,  D. Brailsford
//  based on the SBND version by T. Junk which is based on the 
//  based on the MicroBooNE example by S. Gollapinni
//
////////////////////////////////////////////////////////////////////////

#include <string>
#include <sstream>
#include <iomanip>
#include <vector>
#include <iostream>
#include <fstream>
#include <algorithm>
#include "sbndcode/Utilities/TFileMetadataSBND.h"
#include "sbndcode/Utilities/FileCatalogMetadataSBND.h"
#include "art/Framework/Principal/SubRun.h"
#include "art/Framework/Services/Registry/ServiceHandle.h"
#include "art/Framework/Services/System/FileCatalogMetadata.h"
#include "art/Utilities/OutputFileInfo.h"
#include "art/Framework/IO/Root/RootDB/SQLite3Wrapper.h"
#include "art/Framework/IO/Root/RootDB/SQLErrMsg.h"
#include "cetlib/exception.h"
#include "messagefacility/MessageLogger/MessageLogger.h"
#include "TROOT.h"
#include "TFile.h"
#include "TTimeStamp.h"
#include <ctime>
#include <stdio.h>
#include <time.h>

#include "art/Framework/Services/Optional/TFileService.h"

using namespace std;


//--------------------------------------------------------------------

// Constructor.
util::TFileMetadataSBND::TFileMetadataSBND(fhicl::ParameterSet const& pset, 
					   art::ActivityRegistry& reg):
  fGenerateTFileMetadata(false),	
  fFileStats("",art::ServiceHandle<art::TriggerNamesService const>{}->getProcessName())
{
  reconfigure(pset);

  // Register for callbacks.
 
  reg.sPostBeginJob.watch(this, &TFileMetadataSBND::postBeginJob);
  reg.sPostOpenFile.watch(this, &TFileMetadataSBND::postOpenFile);
  reg.sPostCloseFile.watch(this, &TFileMetadataSBND::postCloseFile);
  reg.sPostProcessEvent.watch(this, &TFileMetadataSBND::postEvent);
  reg.sPostBeginSubRun.watch(this, &TFileMetadataSBND::postBeginSubRun);
  reg.sPostCloseOutputFile.watch(this, &TFileMetadataSBND::postCloseOutputFile);
}

//--------------------------------------------------------------------
// Destructor.
util::TFileMetadataSBND::~TFileMetadataSBND()
{
}

//--------------------------------------------------------------------
// Set service paramters
void util::TFileMetadataSBND::reconfigure(fhicl::ParameterSet const& pset)
{    
  // Get parameters.
  fGenerateTFileMetadata = pset.get<bool>("GenerateTFileMetadata", false);
  fJSONFileName = pset.get<std::string>("JSONFileName");
    
  if (!fGenerateTFileMetadata) return;

  md.fdata_tier  	   = pset.get<std::string>("dataTier","root-tuple");	   
  md.ffile_format	   = pset.get<std::string>("fileFormat","root");              
}

//--------------------------------------------------------------------
// PostBeginJob callback.
// Insert per-job metadata via TFileMetadata service.
void util::TFileMetadataSBND::postBeginJob()
{ 
  // only generate metadata when this is true
  if (!fGenerateTFileMetadata) return;
    
  // get the start time  
  md.fstart_time = time(0); 
  
  // Get art metadata service and extract paramters from there
  art::ServiceHandle<art::FileCatalogMetadata> artmds;
  
  art::FileCatalogMetadata::collection_type artmd;
  artmds->getMetadata(artmd);
  
  for(auto const & d : artmd)
    mdmap[d.first] = d.second;
    
  std::map<std::string,std::string>::iterator it;
  
  // if a certain paramter/key is not found, assign an empty string value to it
  
  if ((it=mdmap.find("applicationFamily"))!=mdmap.end()) std::get<0>(md.fapplication) = it->second;
  else std::get<0>(md.fapplication) = "\" \"";   
   
  if ((it=mdmap.find("process_name"))!=mdmap.end()) std::get<1>(md.fapplication) = it->second;
  else std::get<1>(md.fapplication) = "\" \"";  
  
  if ((it=mdmap.find("applicationVersion"))!=mdmap.end()) std::get<2>(md.fapplication) = it->second;
  else  std::get<2>(md.fapplication) = "\" \"";   
  
  if ((it=mdmap.find("group"))!=mdmap.end()) md.fgroup = it->second;
  else md.fgroup = "\" \"";   
    
  if ((it=mdmap.find("file_type"))!=mdmap.end()) md.ffile_type = it->second;
  else  md.ffile_type = "\" \"";  
    
  if ((it=mdmap.find("run_type"))!=mdmap.end()) frunType = it->second;
  else frunType = "\" \"";         	     	
}


//--------------------------------------------------------------------        
// PostOpenFile callback.
void util::TFileMetadataSBND::postOpenFile(std::string const& fn)
{
  if (!fGenerateTFileMetadata) return;
  
  // save parent input files here
  md.fParents.insert(fn);
  fFileStats.recordInputFile(fn);
}

//--------------------------------------------------------------------        
// PostOpenOutputFile callback.
void util::TFileMetadataSBND::postCloseOutputFile(art::OutputFileInfo const& outputinfo)
{
  if (!fGenerateTFileMetadata) return;
  
  //Set the output name of the json using the standard format
 
}

//--------------------------------------------------------------------  	
// PostEvent callback.
void util::TFileMetadataSBND::postEvent(art::Event const& evt)
{
  if(!fGenerateTFileMetadata) return;	
  
  art::RunNumber_t run = evt.run();
  art::SubRunNumber_t subrun = evt.subRun();
  art::EventNumber_t event = evt.event();
  art::SubRunID srid = evt.id().subRunID();
      
  // save run, subrun and runType information once every subrun    
  if (fSubRunNumbers.count(srid) == 0){
    fSubRunNumbers.insert(srid);
    md.fruns.push_back(make_tuple(run, subrun, frunType));   
  }
  
  // save the first event
  if (md.fevent_count == 0) md.ffirst_event = event;
  md.flast_event = event;
  // event counter
  ++md.fevent_count;
    
}

//--------------------------------------------------------------------  	
// PostSubRun callback.
void util::TFileMetadataSBND::postBeginSubRun(art::SubRun const& sr)
{

  if(!fGenerateTFileMetadata) return;

  art::RunNumber_t run = sr.run();
  art::SubRunNumber_t subrun = sr.subRun();
  art::SubRunID srid = sr.id();

  // save run, subrun and runType information once every subrun
  if (fSubRunNumbers.count(srid) == 0){
    fSubRunNumbers.insert(srid);
    md.fruns.push_back(make_tuple(run, subrun, frunType));
  }
}

//--------------------------------------------------------------------
// PostCloseFile callback.
void util::TFileMetadataSBND::postCloseFile()
{

  
 
  // Do nothing if generating TFile metadata is disabled.	
  if(!fGenerateTFileMetadata) return;	
  
  // get metadata from the FileCatalogMetadataSBND service, which is filled on its construction
        
  art::ServiceHandle<util::FileCatalogMetadataSBND> paramhandle; 
  md.fFCLName = paramhandle->GetFCLName();
  md.fProjectName = paramhandle->GetProjectName();
  md.fProjectStage = paramhandle->GetProjectStage();
  md.fProjectVersion = paramhandle->GetProjectVersion();
  md.fProjectSoftware = paramhandle->GetProjectSoftware();
  md.fProductionName = paramhandle->GetProductionName();
  md.fProductionType = paramhandle->GetProductionType();

  //update end time
  md.fend_time = time(0);

  // convert start and end times into time format: Year-Month-DayTHours:Minutes:Seconds
  char endbuf[80], startbuf[80];
  struct tm tstruct;
  tstruct = *localtime(&md.fend_time);
  strftime(endbuf,sizeof(endbuf),"%Y-%m-%dT%H:%M:%S",&tstruct);
  tstruct = *localtime(&md.fstart_time);
  strftime(startbuf,sizeof(startbuf),"%Y-%m-%dT%H:%M:%S",&tstruct);
  
  // open a json file and write everything from the struct md complying to the 
  // samweb json format. This json file holds the below information temporarily. 
  // If you submitted a grid job invoking this service, the information from 
  // this file is appended to a final json file and this file will be removed
  
  std::ofstream jsonfile;
  jsonfile.open(fJSONFileName);
  jsonfile<<"{\n  \"application\": {\n    \"family\": "<<std::get<0>(md.fapplication)<<",\n    \"name\": ";
  jsonfile<<std::get<1>(md.fapplication)<<",\n    \"version\": "<<std::get<2>(md.fapplication)<<"\n  },\n  ";
  jsonfile<<"\"data_tier\": \""<<md.fdata_tier<<"\",\n  ";
  jsonfile<<"\"event_count\": "<<md.fevent_count<<",\n  ";
  jsonfile<<"\"file_format\": \""<<md.ffile_format<<"\",\n  ";
  jsonfile<<"\"file_type\": "<<md.ffile_type<<",\n  ";
  jsonfile<<"\"first_event\": "<<md.ffirst_event<<",\n  ";
  jsonfile<<"\"group\": "<<md.fgroup<<",\n  ";
  jsonfile<<"\"last_event\": "<<md.flast_event<<",\n  ";
  //if (md.fdataTier != "generated"){
  unsigned int c=0;
  jsonfile<<"\"parents\": [\n";
  for(auto parent : md.fParents) {
    c++;
    size_t n = parent.find_last_of('/');
    size_t f1 = (n == std::string::npos ? 0 : n+1);
    jsonfile<<"    {\n     \"file_name\": \""<<parent.substr(f1)<<"\"\n    }";
    if (md.fParents.size()==1 || c==md.fParents.size()) jsonfile<<"\n";
    else jsonfile<<",\n"; 
  }      
  jsonfile<<"  ],\n  "; 
  //}   
  c=0;
  jsonfile<<"\"runs\": [\n";
  for(auto &t : md.fruns){
    c++;
    jsonfile<<"    [\n     "<<std::get<0>(t)<<",\n     "<<std::get<1>(t)<<",\n     "<<std::get<2>(t)<<"\n    ]";
    if (md.fruns.size()==1 || c==md.fruns.size()) jsonfile<<"\n";
    else jsonfile<<",\n"; 
  }
  jsonfile<<"  ],\n";          

  if (md.fFCLName!="") jsonfile << "\"fcl.name\": \"" << md.fFCLName << "\",\n";
  if (md.fProjectName!="") jsonfile << "\"sbnd_project.name\": \"" << md.fProjectName << "\",\n";
  if (md.fProjectStage!="") jsonfile << "\"sbnd_project.stage\": \"" << md.fProjectStage << "\",\n";
  if (md.fProjectVersion!="") jsonfile << "\"sbnd_project.version\": \"" << md.fProjectVersion << "\",\n";
  if (md.fProjectSoftware!="") jsonfile << "\"sbnd_project.software\": \"" << md.fProjectSoftware << "\",\n";
  if (md.fProductionName!="") jsonfile << "\"production.name\": \"" << md.fProductionName << "\",\n";
  if (md.fProductionType!="") jsonfile << "\"production.type\": \"" << md.fProductionType << "\",\n";

  // put these at the end because we know they'll be there and the last one needs to not have a comma
  jsonfile<<"\"start_time\": \""<<startbuf<<"\",\n";
  jsonfile<<"\"end_time\": \""<<endbuf<<"\"\n";
  
  
  jsonfile<<"}\n";
  jsonfile.close();  

  fFileStats.recordFileClose();
  //TODO figure out how to make the name identical to the TFile
  //std::string new_name = fRenamer.maybeRenameFile("myjson.json",fJSONFileName);
}



//namespace util{

  DEFINE_ART_SERVICE(util::TFileMetadataSBND)
//}//namespace util
