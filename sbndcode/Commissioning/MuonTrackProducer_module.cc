// Framework includes 
#include "art/Framework/Core/EDProducer.h"
#include "art/Framework/Core/ModuleMacros.h" 
#include "art/Framework/Principal/Event.h" 
#include "fhiclcpp/ParameterSet.h"
#include "art/Framework/Principal/Handle.h"
#include "canvas/Persistency/Common/Ptr.h"
#include "canvas/Persistency/Common/PtrVector.h"
#include "art/Framework/Services/Registry/ServiceHandle.h"
#include "art_root_io/TFileService.h"
#include "messagefacility/MessageLogger/MessageLogger.h"

// LArSoft includes 
#include "larcore/Geometry/Geometry.h"
#include "larcorealg/Geometry/PlaneGeo.h"
#include "larcorealg/Geometry/WireGeo.h"
#include "lardataobj/RecoBase/Hit.h"
#include "larcoreobj/SimpleTypesAndConstants/geo_types.h"
#include "larcorealg/Geometry/GeometryCore.h"
#include "lardataobj/Simulation/AuxDetSimChannel.h"
#include "larcore/Geometry/AuxDetGeometry.h"
#include "lardataobj/RawData/RawDigit.h"
#include "lardataobj/RawData/raw.h"
#include "lardata/Utilities/AssociationUtil.h"


// SBN/SBND includes
#include "sbnobj/SBND/Commissioning/MuonTrack.hh"

// ROOT includes 
#include "TRandom3.h"

// C++ includes
#include <vector>
#include <algorithm>
#include <stdlib.h>
#include <math.h>
#include <vector>
#include <iostream>
#include <cmath>
#include <bitset>
#include <memory>

#define PI 3.14159265

using std::vector;

class MuonTrackProducer: public art::EDProducer {
public:
    // The destructor generated by the compiler is fine for classes
    // without bare pointers or other resource use.

    // Plugins should not be copied or assigned.
   explicit MuonTrackProducer(fhicl::ParameterSet const & p);
   MuonTrackProducer(MuonTrackProducer const &) = delete;
   MuonTrackProducer(MuonTrackProducer &&) = delete;
   MuonTrackProducer & operator = (MuonTrackProducer const &) = delete;
   MuonTrackProducer & operator = (MuonTrackProducer &&) = delete;

   // Required functions.
   void produce(art::Event & evt) override;
   void reconfigure(fhicl::ParameterSet const & p);

private:
   // Define functions 
   // Resets hit information for collection plane
   void ResetCollectionHitVectors(int n);
   // Resets hit information for induction planes
   void ResetInductionHitVectors(int n);
   // Resets variables for AC Crossing muons 
   void ResetMuonVariables(int n); 
   // Finds distance between two points 
   float Distance(int x1, int y1, int x2, int y2);
   // Performs Hough Transform for collection planes
   void Hough(vector<vector<int>> coords, vector<int> param, bool save_hits, int nentry, 
              vector<vector<int>>& lines, vector<vector<int>>& hit_idx);
   // Finds t0, stores them in a vector<vector<double>>     
   void FindEndpoints(vector<vector<int>>& lines_col, vector<vector<int>>& lines_ind, vector<vector<int>>& hit_idx, 
                      int range, vector<art::Ptr<recob::Hit>> hitlist, 
                      vector<vector<geo::Point_t>>& muon_endpoints, vector<vector<int>>& muon_hitpeakT, vector<vector<int>>& muon_hit_idx); 
   // Fixes endpoints, returns true if conditions are fulfilled 
   bool FixEndpoints(geo::WireID wire_col, geo::WireID wire_ind, geo::Point_t& point); 
   // Sorts endpoints into categories 
   void SortEndpoints(vector<vector<geo::Point_t>>& muon_endpoints, vector<vector<int>> muon_hitpeakT, vector<int>& muon_type);
   // Finds trajectories, stores them in a vector<vector<double>> 
   void Findt0(vector<vector<int>> muon_hitpeakT, vector<int> muon_type, vector<double>& muon_t0); 
   // Finds endpoints, stores them in a vector<vector<geo::Point_t>>
   void FindTrajectories(vector<vector<geo::Point_t>> muon_endpoints, vector<vector<int>> muon_hitpeakT, 
                         vector<vector<double>>& muon_trajectories); 

   void PrintHoughLines(vector<vector<int>>& lines, int plane); 
   
   // Define variables 
   int nhits;

   vector<vector<int>> hit_02, hit_12;
   vector<vector<int>> lines_02, lines_12;
   vector<vector<int>> hit_idx_02, hit_idx_12; 

   vector<vector<int>> hit_00, hit_01, hit_10, hit_11;
   vector<vector<int>> lines_00, lines_01, lines_10, lines_11;

   vector<int> muon_tpc, muon_type; 
   vector<double> muon_t0;
   vector<vector<geo::Point_t>> muon_endpoints;
   vector<vector<int>> muon_hitpeakT, muon_hit_idx; 
   vector<vector<double>> muon_trajectories; 

   // parameters from the fcl file 
   std::string fHitsModuleLabel; 
   int fHoughThreshold;
   int fHoughMaxGap;
   int fHoughRange;
   int fHoughMinLength;
   int fHoughMuonLength;
   int fEndpointRange; 
   std::vector<int> fKeepMuonTypes = {0, 1, 2, 3, 4, 5}; 

   // services 
   art::ServiceHandle<art::TFileService> tfs;
   geo::GeometryCore const* fGeometryService;

}; // class MuonTrackProducer

MuonTrackProducer::MuonTrackProducer(fhicl::ParameterSet const & p)
   : EDProducer(p)
{
   produces< std::vector<sbnd::comm::MuonTrack> >();
   produces< art::Assns<recob::Hit, sbnd::comm::MuonTrack> >();

   fGeometryService = lar::providerFrom<geo::Geometry>();
   this->reconfigure(p);
}

// Constructor
void MuonTrackProducer::reconfigure(fhicl::ParameterSet const & p)
{  
   // Initialize member data here.
   fHitsModuleLabel      = p.get<std::string>("HitsModuleLabel");

   // Hough parameters 
   fHoughThreshold      = p.get<int>("HoughThreshold",10);
   fHoughMaxGap         = p.get<int>("HoughMaxGap",30);
   fHoughRange          = p.get<int>("HoughRange",100);
   fHoughMinLength      = p.get<int>("HoughMinLength",500);
   fHoughMuonLength     = p.get<int>("HoughMuonLength",2500);

   // Muon function parameters 
   fEndpointRange       = p.get<int>("EndpointRange",30);
   fKeepMuonTypes       = p.get<std::vector<int>>("KeepMuonTypes");

} // MuonTrackProducer()

void MuonTrackProducer::produce(art::Event & evt)
{
   std::unique_ptr<vector<sbnd::comm::MuonTrack>> muon_tracks(new vector<sbnd::comm::MuonTrack>);
   std::unique_ptr<art::Assns<recob::Hit, sbnd::comm::MuonTrack>> muon_tracks_assn(new art::Assns<recob::Hit, sbnd::comm::MuonTrack>); 
   
   int event = evt.id().event();
   std::cout << "Processing event " << event << std::endl;
   // get nhits
   art::Handle<vector<recob::Hit>> hitListHandle;
   vector<art::Ptr<recob::Hit>> hitlist;
   if (evt.getByLabel(fHitsModuleLabel,hitListHandle)) {
      art::fill_ptr_vector(hitlist, hitListHandle);
      nhits = hitlist.size();
   }
   else {
      std::cout << "Failed to get recob::Hit data product." << std::endl;
      nhits = 0;
   }
   
   // obtain collection hits, perform hough transform, obtain tracks, and save col track info 
   ResetCollectionHitVectors(20);

   for (int i = 0; i < nhits; ++i) {
      geo::WireID wireid = hitlist[i]->WireID();
      int hit_wire = int(wireid.Wire), hit_peakT = int(hitlist[i]->PeakTime()), hit_plane = wireid.Plane, hit_tpc = wireid.TPC;
      if (hit_plane==2 && hit_peakT>0){ // if collection plane and only positive peakT 
         vector<int> v{hit_wire,hit_peakT,i};
         if (hit_tpc==0)
            hit_02.push_back(v);  
         else
            hit_12.push_back(v); 
      }
   } // end of nhit loop
   hit_02.shrink_to_fit(); hit_12.shrink_to_fit(); 

   // perform hough transform
   bool save_col_hits = true;
   vector<int> HoughParam{fHoughThreshold, fHoughMaxGap, fHoughRange, fHoughMinLength, fHoughMuonLength};
   Hough(hit_02, HoughParam, save_col_hits, event, lines_02, hit_idx_02);
   Hough(hit_12, HoughParam, save_col_hits, event, lines_12, hit_idx_12);

   bool muon_in_tpc0 = !(lines_02.empty()); // will be true if a muon was detected in tpc0 
   bool muon_in_tpc1 = !(lines_12.empty()); // will be true if a muon was detected in tpc1

   //find induction plane hits, tracks, and fill muon variables 
   ResetInductionHitVectors(20);
   ResetMuonVariables(20);

   if (muon_in_tpc0 == true || muon_in_tpc1 == true){
      for (int i = 0; i < nhits; ++i) {
         geo::WireID wireid = hitlist[i]->WireID();
         int hit_wire = int(wireid.Wire), hit_peakT = int(hitlist[i]->PeakTime()), hit_tpc = wireid.TPC, hit_plane = wireid.Plane;
         vector<int> v{hit_wire,hit_peakT,i};
         if (muon_in_tpc0 == true){ //if ac muon was found in tpc0 
            if (hit_plane==0 && hit_tpc==0 && hit_peakT>0) 
               hit_00.push_back(v);
            else if (hit_plane==1 && hit_tpc==0 && hit_peakT>0)
               hit_01.push_back(v);
         }
         else if (muon_in_tpc1 == true){ // if ac muon was found in tpc 1
            if (hit_plane==0 && hit_tpc==1 && hit_peakT>0)
               hit_10.push_back(v);
            else if (hit_plane==1 && hit_tpc==1 && hit_peakT>0)
               hit_11.push_back(v);
         } 
      }
      bool save_ind_hits = false;
      vector<vector<int>> ind_empty; // placeholder empty vector 
      if (muon_in_tpc0){
         Hough(hit_00, HoughParam, save_ind_hits, event, lines_00, ind_empty);
         Hough(hit_01, HoughParam, save_ind_hits, event, lines_01, ind_empty);

         FindEndpoints(lines_02, lines_00, hit_idx_02, fEndpointRange, hitlist, muon_endpoints, muon_hitpeakT, muon_hit_idx);
         FindEndpoints(lines_02, lines_01, hit_idx_02, fEndpointRange, hitlist, muon_endpoints, muon_hitpeakT, muon_hit_idx);
      }
      if (muon_in_tpc1){
         Hough(hit_10, HoughParam, save_ind_hits, event, lines_10, ind_empty);
         Hough(hit_11, HoughParam, save_ind_hits, event, lines_11, ind_empty);

         FindEndpoints(lines_12, lines_10, hit_idx_12, fEndpointRange, hitlist, muon_endpoints, muon_hitpeakT, muon_hit_idx);
         FindEndpoints(lines_12, lines_11, hit_idx_12, fEndpointRange, hitlist, muon_endpoints, muon_hitpeakT, muon_hit_idx);
      }
      if (muon_endpoints.empty() == false){
         SortEndpoints(muon_endpoints, muon_hitpeakT, muon_type);
         FindTrajectories(muon_endpoints, muon_hitpeakT, muon_trajectories);
         Findt0(muon_hitpeakT, muon_type, muon_t0);

         for (int i=0; i<int(muon_endpoints.size()); i++){
            int track_type = muon_type.at(i);
            vector<int> track_hit_idx = muon_hit_idx.at(i); 
            bool keep_track = false; 

            for (auto t : fKeepMuonTypes){
               if (track_type == t)
                  keep_track = true; 
            }
            if (keep_track){
               geo::Point_t endpoint1 = (muon_endpoints.at(i)).at(0), endpoint2 = (muon_endpoints.at(i)).at(1); 

               sbnd::comm::MuonTrack mytrack;
               mytrack.t0_us = muon_t0.at(i);
               mytrack.x1_pos = float(endpoint1.X());
               mytrack.y1_pos = float(endpoint1.Y());
               mytrack.z1_pos = float(endpoint1.Z());
               mytrack.x2_pos = float(endpoint2.X());
               mytrack.y2_pos = float(endpoint2.Y());
               mytrack.z2_pos = float(endpoint2.Z());

               mytrack.theta_xz = (muon_trajectories.at(i)).at(0);
               mytrack.theta_yz = (muon_trajectories.at(i)).at(1);

               mytrack.tpc = (endpoint1.X() < 0)? 0:1;
               mytrack.type = muon_type.at(i); 

               muon_tracks->push_back(mytrack);
               for (int hit_i=0; hit_i<int(track_hit_idx.size()); hit_i++){
                  util::CreateAssn(*this, evt, *muon_tracks, hitlist[hit_i], *muon_tracks_assn);
               }
            }
         }
      }
   } // end of finding ind hits 
   evt.put(std::move(muon_tracks));
   evt.put(std::move(muon_tracks_assn)); 
} // MuonTrackProducer::produce()

float MuonTrackProducer::Distance(int x1, int y1, int x2, int y2){
   return sqrt(pow(x2 - x1, 2) + pow(y2 - y1, 2) * 1.0);
}

void MuonTrackProducer::Hough(vector<vector<int>> coords, vector<int> param,  bool save_hits, int nentry,
                              vector<vector<int>>& lines, vector<vector<int>>& hit_idx){
   // set parameters 
   int threshold = param.at(0);
   int max_gap = param.at(1);
   int range = param.at(2);
   int min_length = param.at(3);
   int muon_length = param.at(4);

   // set global variables 
   TRandom3 rndgen;
   const int h = 3500; const int w = 2000; //range of hit_wire
   constexpr int accu_h = h + w + 1 ; const int accu_w = 180; 
   const int x_c = (w/2); const int y_c = (h/2);

   // create accumulator/pointer to accumulator 
   int accu[accu_h][accu_w] = {{0}};
   int (*adata)[accu_w];
   adata = &accu[(accu_h-1)/2]; // have pointer point to the middle of the accumulator array (then we can use negative indices)

   // declare necessary vectors 
   vector<vector<int>> data = coords; // points will not be removed 
   vector<vector<int>> deaccu; // deaccumulator
   vector<vector<int>> outlines; 
   vector<vector<int>> outhit_idx; 

   // loop over points and perform transform 
   int count = coords.size(); 
   for ( ; count>0; count--){ 
      int idx = rndgen.Uniform(count);
      int max_val = threshold-1;
      if ((coords.at(idx)).empty())
         continue; 
      int x = coords[idx][0], y = coords[idx][1], rho = 0, theta = 0;
      vector<int> v{x,y}; 
      deaccu.push_back(v);
      //loop over all angles and fill the accumulator 
      for (int j=0; j<accu_w; j++){ 
         int r = int(round((x-x_c)*cos(j*PI/accu_w) + (y-y_c)*sin(j*PI/accu_w)));
         int val = ++(adata[r][j]);       
         if (max_val < val){
            max_val = val;
            rho = r;
            theta = j*180/accu_w;
         }
      }
      if (max_val < threshold){
         (coords.at(idx)).clear(); 
         continue;
      }
      //start at point and walk the corridor on both sides 
      vector<vector<int>> endpoint(2, vector<int>(4)); 
      vector<int> lines_idx;
      lines_idx.clear(); 
      for (int k=0; k<2;k++){
         int i=0, gap=0;
         while (gap < max_gap){ 
            (k==0)? i++ : i--; 
            if ( (idx+i) == int(data.size()) || (idx+i) <0) // if we reach the edges of the data set 
               break;
            if ((data.at(idx+i)).empty()) // if the point has already been removed 
               continue;
            int x1 = data[idx+i][0], y1 = int(data[idx+i][1]), wire_idx = data[idx+i][2]; 
            int last_x, diffx; 
            if (endpoint[k][0]!= 0){ // ensure we don't jump large x-values 
               last_x = endpoint[k][0];
               diffx = abs(last_x - x1);
               if (diffx > 30){
                  break;
               }
            }
            int y_val = int(round((rho - (x1 - x_c)*cos(theta*PI/180.0))/sin(theta*PI/180.0) + y_c));
            if (abs(y_val-y1) <= range){
               gap = 0;
               endpoint[k] = {x1, y1, wire_idx, idx+i};
               (coords.at(idx+i)).clear();
               (data.at(idx+i)).clear();
               if (save_hits){
                  lines_idx.push_back(wire_idx);
               }
            }
            else
               gap++;
         } // end of while loop 
      } // end of k loop 

      // unvote from the accumulator 
      for (int n = (deaccu.size()-1); n>=0; n--){ 
         int x1 = deaccu[n][0], y1 = int(deaccu[n][1]);
         int y_val = int(round((rho - (x1 - x_c)*cos(theta*PI/180.0))/sin(theta*PI/180.0) + y_c));
         if (y1 >= (y_val-range) && y1 <= (y_val+range)){
            for (int m=0; m<accu_w; m++){
               int r = int(round((x1-x_c)*cos(m*PI/accu_w) + (y1-y_c)*sin(m*PI/accu_w)));
               (adata[r][m])--;
            }
            deaccu.erase(deaccu.begin() + n);
         }
      } // end of deaccumulator loop

      int x0_end = endpoint[0][0], y0_end = endpoint[0][1], x1_end = endpoint[1][0], y1_end = endpoint[1][1];
      int wire0_end = endpoint[0][2], wire1_end = endpoint[1][2]; 
      int idx0_end = endpoint[0][3], idx1_end = endpoint[1][3];
      if ((x0_end==0 && y0_end==0) || (x1_end==0 && y1_end==0)) // don't add the (0,0) points 
         continue;
      vector<int> outline = {x0_end, y0_end, x1_end, y1_end, wire0_end, wire1_end, idx0_end, idx1_end, rho, theta};

      outlines.push_back(outline);
      if (save_hits){
         outhit_idx.push_back(lines_idx);
      }

   } // end of point loop 
   // combine lines that are split 
   for (int i=0; i<int(outlines.size()); i++){
      bool same = false;
      for (int j=i+1; j<int(outlines.size()) && same == false; j++){ 
         int xi_coords[2] = {outlines[i][0], outlines[i][2]}; int xj_coords[2] = {outlines[j][0], outlines[j][2]};
         int yi_coords[2] = {outlines[i][1], outlines[i][3]}; int yj_coords[2] = {outlines[j][1], outlines[j][3]};
         int rhoi = outlines[i][8], rhoj = outlines[j][8];
         int thetai = outlines[i][9], thetaj = outlines[j][9]; 

         int var = 100;
         int rho_var = 30;
         int theta_var = 20; 
         for (int k=0; k<2 && same == false; k++){
            for (int l=0; l<2 && same == false; l++){
               int counter = 0; 
               if ((xi_coords[k] < (xj_coords[l] + var)) && (xi_coords[k] > (xj_coords[l] - var)))
                  counter++;
               if ((yi_coords[k] < (yj_coords[l] + var)) && (yi_coords[k] > (yj_coords[l] - var)))
                  counter++ ;
               if ((rhoi < (rhoj + rho_var)) && (rhoi > (rhoj - rho_var)))
                  counter++; 
               if ((thetai < (thetaj + theta_var)) && (thetai > (thetaj - theta_var)))
                  counter++;
               if (counter >= 3){ // if at least three of the conditions are fulfilled 
                  if(k==0){
                     if(l==0){
                        outlines[j][2] = outlines[i][0];
                        outlines[j][3] = outlines[i][1];
                     }
                     else{
                        outlines[j][0] = outlines[i][0];
                        outlines[j][1] = outlines[i][1];
                     }
                  }
                  else{
                     if(l==0){
                        outlines[j][2] = outlines[i][2]; 
                        outlines[j][3] = outlines[i][3];                        
                     }
                     else{
                        outlines[j][0] = outlines[i][2];
                        outlines[j][1] = outlines[i][3]; 
                     }  
                  }
                  same = true;
                  // remove the extra segment 
                  (outlines.at(i)).clear();
                  if (save_hits){
                     (outhit_idx.at(j)).insert( (outhit_idx.at(j)).end(),  (outhit_idx.at(i)).begin(),  (outhit_idx.at(i)).end()); 
                     (outhit_idx.at(i)).clear();
                  }
               } 
            }
         }
      } // end of j loop 
   } // end of i loop 

   for (int i=0; i<int(outlines.size()); i++){
      if ((outlines.at(i)).empty())
         continue;
      int x0_end = outlines[i][0], y0_end = outlines[i][1], x1_end = outlines[i][2], y1_end = outlines[i][3];
      if (muon_length!=0){
         int y_diff = abs(y0_end-y1_end);
         if (y_diff > muon_length){
            lines.push_back(outlines.at(i));
            if (save_hits)
               hit_idx.push_back(outhit_idx.at(i)); 
         }
      }
      else{
         float length = Distance(x0_end,y0_end,x1_end,y1_end);
         if (length > min_length){
            lines.push_back(outlines.at(i));
            if (save_hits)
               hit_idx.push_back(outhit_idx.at(i)); 
         }
      }
   }
   //free memory 
   data.clear(); deaccu.clear(); outlines.clear(); outhit_idx.clear();
} // end of hough 

void MuonTrackProducer::ResetCollectionHitVectors(int n) {
   hit_02.clear(); 
   hit_12.clear(); 
   lines_02.clear(); 
   lines_12.clear(); 
   hit_idx_02.clear();
   hit_idx_12.clear();

   hit_02.reserve(3000); 
   hit_12.reserve(3000); 
   lines_02.reserve(n); 
   lines_12.reserve(n); 
   hit_idx_02.reserve(3000);
   hit_idx_12.reserve(3000);
}

void MuonTrackProducer::ResetInductionHitVectors(int n){
   hit_00.clear();
   hit_01.clear();
   hit_10.clear();
   hit_11.clear();

   lines_00.clear(); 
   lines_01.clear();
   lines_10.clear(); 
   lines_11.clear();
   hit_00.reserve(5000);
   hit_01.reserve(5000);
   hit_10.reserve(5000);
   hit_11.reserve(5000);

   lines_00.reserve(n); 
   lines_01.reserve(n);
   lines_10.reserve(n); 
   lines_11.reserve(n);
}

void MuonTrackProducer::ResetMuonVariables(int n){
   muon_tpc.clear();
   muon_endpoints.clear();
   muon_hitpeakT.clear();
   muon_hit_idx.clear(); 
   muon_type.clear();
   muon_trajectories.clear(); 
   muon_t0.clear();

   muon_tpc.reserve(n);
   muon_endpoints.reserve(n);
   muon_hitpeakT.reserve(n);
   muon_hit_idx.reserve(n);
   muon_type.reserve(n);
   muon_trajectories.reserve(n);
   muon_t0.reserve(n); 

}

void MuonTrackProducer::FindEndpoints(vector<vector<int>>& lines_col, vector<vector<int>>& lines_ind, vector<vector<int>>& hit_idx, 
                                      int range, vector<art::Ptr<recob::Hit>> hitlist, 
                                      vector<vector<geo::Point_t>>& muon_endpoints, vector<vector<int>>& muon_hitpeakT, vector<vector<int>>& muon_hit_idx){
   if (lines_ind.empty() == false){
      for (int i=0; i<int(lines_col.size()); i++){
         bool match = false;
         if ((lines_col.at(i)).empty() == true)
            continue;
         for (int j=0; j<int(lines_ind.size()) && match == false; j++){
            int peakT0_col, peakT1_col, peakT0_ind, peakT1_ind; 
            int wire0_col, wire1_col, wire0_ind, wire1_ind;

            bool order_col = (lines_col.at(i)).at(1) < (lines_col.at(i)).at(3); 
            peakT0_col = order_col? (lines_col.at(i)).at(1) : (lines_col.at(i)).at(3);
            peakT1_col = order_col? (lines_col.at(i)).at(3) : (lines_col.at(i)).at(1);
            wire0_col  = order_col? (lines_col.at(i)).at(4) : (lines_col.at(i)).at(5); 
            wire1_col  = order_col? (lines_col.at(i)).at(5) : (lines_col.at(i)).at(4); 

            bool order_ind = (lines_ind.at(j)).at(1) < (lines_ind.at(j)).at(3); 
            peakT0_ind = order_ind? (lines_ind.at(j)).at(1) : (lines_ind.at(j)).at(3);
            peakT1_ind = order_ind? (lines_ind.at(j)).at(3) : (lines_ind.at(j)).at(1);
            wire0_ind  = order_ind? (lines_ind.at(j)).at(4) : (lines_ind.at(j)).at(5); 
            wire1_ind  = order_ind? (lines_ind.at(j)).at(5) : (lines_ind.at(j)).at(4); 

            int peakT_range = range; 
            if ( (abs(peakT0_col - peakT0_ind) < peakT_range) && (abs(peakT1_col - peakT1_ind) < peakT_range)){
               geo::WireID awire_col = hitlist[wire0_col]->WireID(); 
               geo::WireID awire_ind = hitlist[wire0_ind]->WireID();
               geo::WireID cwire_col = hitlist[wire1_col]->WireID();
               geo::WireID cwire_ind = hitlist[wire1_ind]->WireID();
               geo::Point_t apoint, cpoint, endpoint1, endpoint2; 

               bool aintersect = fGeometryService->WireIDsIntersect(awire_col, awire_ind, apoint);
               bool cintersect = fGeometryService->WireIDsIntersect(cwire_col, cwire_ind, cpoint); 

               if (aintersect)
                  endpoint1 = apoint;
               else{ // NOTE: hardcoded fix
                  bool afix = FixEndpoints(awire_col, awire_ind, apoint); 
                  if (afix == true)
                     endpoint1 = apoint;
               }
               if (cintersect)
                  endpoint2 = cpoint;
               else{ // NOTE:: hardcoded fix 
                  bool cfix = FixEndpoints(cwire_col, cwire_ind, cpoint); 
                  if ( cfix == true)
                     endpoint2 = cpoint;
               }  
               if (endpoint1.Mag2() != 0 && endpoint2.Mag2() != 0 ){
                  vector<geo::Point_t> pair{endpoint1,endpoint2};
                  muon_endpoints.push_back(pair);

                  vector<int> v{peakT0_col, peakT1_col};
                  vector<int> indices = hit_idx.at(i);

                  muon_hitpeakT.push_back(v);
                  muon_hit_idx.push_back(indices); 
                  match = true;
                  lines_col.at(i).clear();
               } 
            }
         } // end of j loop 
      } // end of i loop 
   }
} 

bool MuonTrackProducer::FixEndpoints(geo::WireID wire_col, geo::WireID wire_ind, geo::Point_t& point){
   bool pass = false; 
   double col_end1[3], col_end2[3], ind_end1[3], ind_end2[3];
   fGeometryService->WireEndPoints(wire_col, col_end1, col_end2);
   fGeometryService->WireEndPoints(wire_ind, ind_end1, ind_end2);
   if (abs(ind_end1[1]) > 198 || abs(ind_end2[1]) > 198){ // if it's located on the top or bottom 
      double ind_y = (abs(ind_end1[1]) == 199.792) ? ind_end1[1] : ind_end2[1];
      double ind_z = (abs(ind_end1[1]) == 199.792) ? ind_end1[2] : ind_end2[2]; 
      if (abs(col_end1[2] - ind_z) < 8){
         point.SetX(col_end1[0]);
         point.SetY(ind_y);
         point.SetZ(ind_z);
         pass = true;
      }
   }
   return pass; 
}

void MuonTrackProducer::SortEndpoints(vector<vector<geo::Point_t>>& muon_endpoints, vector<vector<int>> muon_hitpeakT, vector<int>& muon_type){
   for (int i=0; i<int(muon_endpoints.size()); i++){
      geo::Point_t endpoint1 = (muon_endpoints.at(i)).at(0), endpoint2 = (muon_endpoints.at(i)).at(1);
      int dt = (muon_hitpeakT.at(i)).at(1) - (muon_hitpeakT.at(i)).at(0);

      if (dt > 2400){
         muon_type.push_back(0); // anode-cathode crosser 
         endpoint2.SetX(0); 
         continue;
      }

      bool end1_edge = (endpoint1.Y() > 198 || endpoint1.Y() < -198 || endpoint1.Z() > 503 || endpoint1.Z() < 6);
      bool end2_edge = (endpoint2.Y() > 198 || endpoint2.Y() < -198 || endpoint2.Z() > 503 || endpoint2.Z() < 6);

      if (end1_edge == false && end2_edge == true ){ //  if the later one was on an edge, the earlier one must be on anode 
         muon_type.push_back(1); // anode crosser 
         double dx = dt*0.5*0.16; // conversion to distance (cm)
         double true_x = (endpoint2.X() > 0)? (202.05-dx):(-202.05+dx);
         endpoint2.SetX(true_x);
      }
      else if (end1_edge == true  && end2_edge == false){ // if the earlier one was on an edge, the later one must be on cathode 
         muon_type.push_back(2); // cathode crosser
         double dx = dt*0.5*0.16; // conversion to distance (cm)
         double true_x = (endpoint1.X() > 0)? (202.05-dx):(-202.05+dx);
         endpoint1.SetX(true_x); 
         endpoint2.SetX(0.0); // change to location of cathode 
      }
      else if ( (endpoint1.Y() > 198 && endpoint2.Y() < -198) || (endpoint1.Y() < -198 && endpoint2.Y() > 198) ){
         muon_type.push_back(3); // top-bottom crosser 
      }
      else if ( (endpoint1.Z() > 503 && endpoint2.Z() < 6) || (endpoint1.Z() < 6 && endpoint2.Z() > 503)){
         muon_type.push_back(4); // upstream/downstream crosser 
      }
      else{
         muon_type.push_back(5); // uncategorized
      }
   }
}

void MuonTrackProducer::FindTrajectories(vector<vector<geo::Point_t>> muon_endpoints, vector<vector<int>> muon_hitpeakT, 
                                         vector<vector<double>>& muon_trajectories){
   for (int i=0; i<int(muon_endpoints.size()); i++){
      vector<geo::Point_t> pair = muon_endpoints[i];

      int dt = (muon_hitpeakT.at(i)).at(1) - (muon_hitpeakT.at(i)).at(0);
      
      double dx = dt*0.5*0.16;
      double dy = float(pair[1].Y())-float(pair[0].Y());
      double dz = float(pair[1].Z())-float(pair[0].Z());
      
      double theta_xz = atan2(dx,dz) * 180/PI;
      double theta_yz = atan2(dy,dz) * 180/PI;

      // std::cout << "theta_xz: " << theta_xz << std::endl;
      // std::cout << "theta_yz: " << theta_yz << std::endl;
      vector<double> trajectories{theta_xz, theta_yz};
      muon_trajectories.push_back(trajectories);
   }
}

void MuonTrackProducer::Findt0(vector<vector<int>> muon_hitpeakT, vector<int> muon_type, 
                        vector<double>& muon_t0){
   for (int i=0; i<int(muon_hitpeakT.size()); i++){
      double t0; 
      if (muon_type.at(i) == 0 || muon_type.at(i) == 1) // anode-cathode crosser or anode crosser 
         t0 = ((muon_hitpeakT.at(i)).at(0) - 500)*0.5; 
      else if (muon_type.at(i) == 2) // cathode crosser
         t0 = ((muon_hitpeakT.at(i)).at(1) - 3000)*0.5; 
      else
         t0 = -500; 
      muon_t0.push_back(t0);
   }           
}

void MuonTrackProducer::PrintHoughLines(vector<vector<int>>& lines, int plane){
   if (lines.empty()==false){
      std::cout << "plane = " << plane << std::endl;
      for (int i=0; i<int(lines.size()); i++){
         vector<int> line = lines.at(i);
         int wire0 = line.at(0), wire1 = line.at(2); 
         int peakT0 = line.at(1), peakT1 = line.at(3);
         std::cout << "wire0, peakT0: (" << wire0 << ", " << peakT0 << ")" << std::endl;
         std::cout << "wire1, peakT1: (" << wire1 << ", " << peakT1 << ")" << std::endl;
      }
   }
   else
      std::cout << "no lines found for this plane" << std::endl;
}

// A macro required for a JobControl module.
DEFINE_ART_MODULE(MuonTrackProducer)