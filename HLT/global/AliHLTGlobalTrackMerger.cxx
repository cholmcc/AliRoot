//$Id$

/**************************************************************************
 * This file is property of and copyright by the ALICE HLT Project        * 
 * ALICE Experiment at CERN, All rights reserved.                         *
 *                                                                        *
 * Primary Authors: Jacek Otwinowski (Jacek.Otwinowski@gsi.de)            *
 *                  for The ALICE HLT Project.                            *
 *                                                                        *
 * Permission to use, copy, modify and distribute this software and its   *
 * documentation strictly for non-commercial purposes is hereby granted   *
 * without fee, provided that the above copyright notice appears in all   *
 * copies and that both the copyright notice and this permission notice   *
 * appear in the supporting documentation. The authors make no claims     *
 * about the suitability of this software for any purpose. It is          *
 * provided "as is" without express or implied warranty.                  *
 **************************************************************************/

/** @file   AliHLTGlobalTrackMerger.cxx
    @author Jacek Otwinowski
    @date   
    @brief  The HLT global merger base class
*/

#include "AliHLTTPCTransform.h"
#include "AliHLTTPCTrack.h"
#include "AliHLTTPCTrackSegmentData.h"
#include "AliHLTTPCTransform.h"
#include "AliHLTTPCTrackArray.h"

#include "AliTRDtrackV1.h"
#include "AliESDEvent.h"
#include "AliESDVertex.h"
#include "AliTracker.h"

#include <TClonesArray.h>

#include "AliHLTGlobalTrackMerger.h"

#if __GNUC__>= 3
using namespace std;
#endif

ClassImp(AliHLTGlobalTrackMerger)

AliHLTGlobalTrackMerger::AliHLTGlobalTrackMerger()
  :
  fMaxY(0.0),
  fMaxZ(0.0),
  fMaxSnp(0.0),
  fMaxTgl(0.0),
  fMaxSigned1Pt(0.0),
  fVertex(0)
{
  //Default constructor

  // standard vertex settings at the moment
  // V(0.,0.,0.), sigmaVx=sigmaVy=5.e-3 [cm], sigmaVz=5.3 [cm]    
  fVertex = new AliESDVertex;
}

//_____________________________________________________________________________
AliHLTGlobalTrackMerger::~AliHLTGlobalTrackMerger()
{
  //Destructor
  if(fVertex) delete fVertex; fVertex =0;
}

//_____________________________________________________________________________
Bool_t AliHLTGlobalTrackMerger::LoadTracks(TClonesArray *aTRDTracks, AliESDEvent *esdEvent)
{
  // load TRD tracks
  if(!aTRDTracks) return kFALSE;

  Int_t entries = aTRDTracks->GetEntriesFast();
  for(Int_t i=0; i<entries; ++i) {
    AliTRDtrackV1 *track = (AliTRDtrackV1*)aTRDTracks->At(i);
    if(!track) continue;

    FillTRDESD(track,AliESDtrack::kTRDin,esdEvent); 
  }

return kTRUE;
}

//_____________________________________________________________________________
Bool_t AliHLTGlobalTrackMerger::LoadTracks(AliHLTTPCTrackArray *aTPCTracks, AliESDEvent *esdEvent)
{
  // load TPC tracks
  if(!aTPCTracks) return kFALSE;

  for (int i=0; i<aTPCTracks->GetNTracks();++i) {
    AliHLTTPCTrack* track=(*aTPCTracks)[i];
    if(!track) continue;

    // convert to the AliKalmanTrack 
    Int_t local=track->Convert2AliKalmanTrack();
    if(local<0) continue;

    FillTPCESD(track,AliESDtrack::kTPCin,esdEvent); 
  }

return kTRUE;
}

//_____________________________________________________________________________
void AliHLTGlobalTrackMerger::FillTPCESD(AliHLTTPCTrack *tpcTrack, ULong_t flags, AliESDEvent* esdEvent)
{
  // create AliESDtracks from AliHLTTPCTracks 
  // and add them to AliESDEvent

  if(!tpcTrack) return;
  if(!esdEvent) return;

  AliESDtrack iotrack;
  iotrack.UpdateTrackParams(tpcTrack,flags);

  Float_t points[4]={tpcTrack->GetFirstPointX(),tpcTrack->GetFirstPointY(),tpcTrack->GetLastPointX(),tpcTrack->GetLastPointY()};
  if(tpcTrack->GetSector() == -1){ // Set first and last points for global tracks
    Double_t s = TMath::Sin( tpcTrack->GetAlpha() );
    Double_t c = TMath::Cos( tpcTrack->GetAlpha() );
    points[0] =  tpcTrack->GetFirstPointX()*c + tpcTrack->GetFirstPointY()*s;
    points[1] = -tpcTrack->GetFirstPointX()*s + tpcTrack->GetFirstPointY()*c;
    points[2] =  tpcTrack->GetLastPointX() *c + tpcTrack->GetLastPointY() *s;
    points[3] = -tpcTrack->GetLastPointX() *s + tpcTrack->GetLastPointY() *c;
  }
  iotrack.SetTPCPoints(points);

  esdEvent->AddTrack(&iotrack);
}
 
//_____________________________________________________________________________
void AliHLTGlobalTrackMerger::FillTRDESD(AliTRDtrackV1* trdTrack, ULong_t flags, AliESDEvent* esdEvent)
{
  // create AliESDtracks from AliTRDtrackV1
  // and add them to AliESDEvent

  if(!trdTrack) return;
  if(!esdEvent) return;

  AliESDtrack iotrack;
  iotrack.UpdateTrackParams(trdTrack,flags);
  esdEvent->AddTrack(&iotrack);
}

//_____________________________________________________________________________
Bool_t AliHLTGlobalTrackMerger::Merge(AliESDEvent* esdEvent)
{
  // merge TPC and TRD tracks
  // 1. propagate TPC track to the radius between TPC and TRD
  // 2. propagate TRD track to the same radius between TPC and TRD
  // 3. matches TPC and TRD tracks at the radius
  // 4. propagate matched TPC and TRD track to the merging radius (first measured TPC point - x coordinate)
  // 5. merge TPC and TRD track parameters at the merging radius 
  // 6. create AliESDtrack from merged tracks
  // 7. add AliESDtrack to AliESDEvent

  if(!esdEvent) return kFALSE;

  const Double_t kMaxStep     = 10.0;    // [cm] track propagation step
  const Double_t kMatchRadius = 285.0;   // [cm] matching at radius between TPC and TRD
  Double_t kMergeRadius = 0.0;
  Bool_t isOk = kFALSE;
  Bool_t isMatched = kFALSE;

  Int_t nTracks = esdEvent->GetNumberOfTracks(); 
  //HLTDebug("nTracks %d",nTracks);
  HLTWarning("nTracks %d",nTracks);

  for(Int_t iTrack = 0; iTrack<nTracks; ++iTrack) 
  {
    AliESDtrack *track = esdEvent->GetTrack(iTrack); 
    if(!track) continue;

    // TPC tracks
    if((track->GetStatus()&AliESDtrack::kTPCin)==0) continue;
    AliESDtrack *tpcTrack = track;

    kMergeRadius = tpcTrack->GetTPCPoints(0); // [cm] merging at first measured TPC point

    //HLTWarning("-------------------------------------------------------------------------------------");
    //HLTWarning("-------------------------------------------------------------------------------------");

    //HLTWarning("1-----tpc before matching: alpha %f, x %f, y, %f, z %f, snp %f, tgl %f, 1pt %f",tpcTrack->GetAlpha(),tpcTrack->GetX(),tpcTrack->GetY(),tpcTrack->GetZ(),tpcTrack->GetSnp(),tpcTrack->GetTgl(),tpcTrack->GetSigned1Pt());

    // propagate tracks to the matching radius 
    isOk = AliTracker::PropagateTrackTo(tpcTrack,kMatchRadius,tpcTrack->GetMass(),kMaxStep,kFALSE);
    if(!isOk) continue;

    //HLTWarning("2-----tpc before matching: alpha %f, x %f, y, %f, z %f, snp %f, tgl %f, 1pt %f",tpcTrack->GetAlpha(),tpcTrack->GetX(),tpcTrack->GetY(),tpcTrack->GetZ(),tpcTrack->GetSnp(),tpcTrack->GetTgl(),tpcTrack->GetSigned1Pt());

    for(Int_t jTrack = 0; jTrack<nTracks; ++jTrack) 
    {
      AliESDtrack *track = esdEvent->GetTrack(jTrack); 
      if(!track) continue;

      // TRD tracks
      if((track->GetStatus()&AliESDtrack::kTRDin)==0) continue;
      AliESDtrack *trdTrack = track;

      //HLTWarning("1-----trd before matching: alpha %f, x %f, y, %f, z %f, snp %f, tgl %f, 1pt %f",trdTrack->GetAlpha(),trdTrack->GetX(),trdTrack->GetY(),trdTrack->GetZ(),trdTrack->GetSnp(),trdTrack->GetTgl(),trdTrack->GetSigned1Pt());

      isOk = AliTracker::PropagateTrackTo(trdTrack,kMatchRadius,trdTrack->GetMass(),kMaxStep,kFALSE);
      if(!isOk) continue;

      //HLTWarning("2-----trd before matching: alpha %f, x %f, y, %f, z %f, snp %f, tgl %f, 1pt %f",trdTrack->GetAlpha(),trdTrack->GetX(),trdTrack->GetY(),trdTrack->GetZ(),trdTrack->GetSnp(),trdTrack->GetTgl(),trdTrack->GetSigned1Pt());

      // match TPC and TRD tracks
      isMatched = MatchTracks(tpcTrack,trdTrack);
      if(!isMatched) continue;

      //HLTWarning("2-----tpc after matching: alpha %f, x %f, y, %f, z %f, snp %f, tgl %f, 1pt %f",tpcTrack->GetAlpha(),tpcTrack->GetX(),tpcTrack->GetY(),tpcTrack->GetZ(),tpcTrack->GetSnp(),tpcTrack->GetTgl(),tpcTrack->GetSigned1Pt());

      //HLTWarning("2-----trd after matching: alpha %f, x %f, y, %f, z %f, snp %f, tgl %f, 1pt %f",trdTrack->GetAlpha(),trdTrack->GetX(),trdTrack->GetY(),trdTrack->GetZ(),trdTrack->GetSnp(),trdTrack->GetTgl(),trdTrack->GetSigned1Pt());

      // propagate tracks to the merging radius 
      isOk = AliTracker::PropagateTrackTo(tpcTrack,kMergeRadius,tpcTrack->GetMass(),kMaxStep,kFALSE);
      if(!isOk) continue;

      isOk = AliTracker::PropagateTrackTo(trdTrack,kMergeRadius,trdTrack->GetMass(),kMaxStep,kFALSE);
      if(!isOk) continue;

      //HLTWarning("3-----tpc before merging: alpha %f, x %f, y, %f, z %f, snp %f, tgl %f, 1pt %f",tpcTrack->GetAlpha(),tpcTrack->GetX(),tpcTrack->GetY(),tpcTrack->GetZ(),tpcTrack->GetSnp(),tpcTrack->GetTgl(),tpcTrack->GetSigned1Pt());

      //HLTWarning("3-----trd before merging: alpha %f, x %f, y, %f, z %f, snp %f, tgl %f, 1pt %f",trdTrack->GetAlpha(),trdTrack->GetX(),trdTrack->GetY(),trdTrack->GetZ(),trdTrack->GetSnp(),trdTrack->GetTgl(),trdTrack->GetSigned1Pt());

      // merge TPC and TRD tracks
      // create AliESDtrack and add it to AliESDevent
      Bool_t isMerged = MergeTracks(tpcTrack,trdTrack,esdEvent);
      if(!isMerged) HLTWarning("No merged tracks");

    }
  }

return kTRUE;
}

//_____________________________________________________________________________
Bool_t AliHLTGlobalTrackMerger::MergeTracks(AliESDtrack *tpcTrack, AliESDtrack* trdTrack, AliESDEvent *esdEvent)
{
  // merge TPC and TRD track parameters
  // create new AliESDtrack with TPC+TRD merged track parameters
  // add AliESDtrack to AliESDEvent

  if(!tpcTrack) return kFALSE;
  if(!trdTrack) return kFALSE;

  /*
  Double_t tpcParam[5] = {tpcTrack->GetY(),tpcTrack->GetZ(),tpcTrack->GetSnp(),tpcTrack->GetTgl(),tpcTrack->GetSigned1Pt()};
  Double_t trdParam[5] = {trdTrack->GetY(),trdTrack->GetZ(),trdTrack->GetSnp(),trdTrack->GetTgl(),trdTrack->GetSigned1Pt()};

  const Double_t* tpcCv = tpcTrack->GetCovariance();
  const Double_t* trdCv = trdTrack->GetCovariance();

  Double_t tpcCovar[15] = {tpcCv[0],tpcCv[1],tpcCv[2],tpcCv[3],tpcCv[4],tpcCv[5],tpcCv[6],tpcCv[7],tpcCv[8],tpcCv[9],tpcCv[10],tpcCv[11],tpcCv[12],tpcCv[13], tpcCv[14] } ;

  Double_t trdCovar[15] = {trdCv[0],trdCv[1],trdCv[2],trdCv[3],trdCv[4],trdCv[5],trdCv[6],trdCv[7],trdCv[8],trdCv[9],trdCv[10],trdCv[11],trdCv[12],trdCv[13], trdCv[14] } ;


  Bool_t isNotOK = SmoothTracks(tpcParam, tpcCovar, tpcTrack->GetChi2(), 5,
                                trdParam, trdCovar, trdTrack->GetChi2(), 5,
                                trackParam, trackCovar, trackChi2, trackNDF,5);
  */

  Double_t trackParam[5], trackCovar[15]; 
  Double_t trackChi2;
  Int_t trackNDF;

  // calculate merged track parameters
  Bool_t isNotOK = SmoothTracks(tpcTrack->GetParameter(), tpcTrack->GetCovariance(), tpcTrack->GetTPCchi2(), 5,
                                trdTrack->GetParameter(), trdTrack->GetCovariance(), trdTrack->GetTRDchi2(), 5,
                                trackParam, trackCovar, trackChi2, trackNDF,5);

  if(isNotOK) 
      return kFALSE;

  //
  // create AliESDtrack
  // merged TPC+TRD information
  // 

  AliESDtrack track;
  //track.UpdateTrackParams(tpcTrack, AliESDtrack::kTPCrefit);
  track.SetStatus(AliESDtrack::kGlobalMerge);
  track.SetLabel(tpcTrack->GetLabel());
  track.Set(tpcTrack->GetX(),tpcTrack->GetAlpha(),trackParam,trackCovar);
  track.SetGlobalChi2(trackChi2);

  //track.SetTPCLabel(tpcTrack->GetLabel());
  Double32_t tpcPID[AliPID::kSPECIES];
  tpcTrack->GetTPCpid(tpcPID);
  track.SetTPCpid(tpcPID);
  //fTPCncls=t->GetNumberOfClusters();  // no cluster on HLT
  //fTPCchi2=t->GetChi2();

  //track.SetTRDLabel(trdTrack->GetLabel());
  Double32_t trdPID[AliPID::kSPECIES];
  trdTrack->GetTRDpid(trdPID);
  track.SetTRDpid(trdPID);
  //fTRDchi2  = t->GetChi2();
  //fTRDncls  = t->GetNumberOfClusters();
  //for (Int_t i=0;i<6;i++) index[i]=t->GetTrackletIndex(i);

  // add track to AliESDEvent
  esdEvent->AddTrack(&track);

return kTRUE;
}

//_____________________________________________________________________________
void AliHLTGlobalTrackMerger::SetParameter(Double_t maxy, Double_t maxz, Double_t maxsnp, Double_t maxtgl, Double_t signed1Pt)
{ 
  //set parameters for merger
  fMaxY = maxy;
  fMaxZ = maxz;
  fMaxSnp = maxsnp;
  fMaxTgl = maxtgl;
  fMaxSigned1Pt = signed1Pt;
}

//_____________________________________________________________________________
Bool_t AliHLTGlobalTrackMerger::MatchTracks(AliESDtrack *trackTPC, AliESDtrack *trackTRD)
{ 
  // match TPC and TRD tracks 
  // return kTRUE in case of matching
 
  if(!trackTPC) return kFALSE;
  if(!trackTRD) return kFALSE;

  if (TMath::Abs(trackTPC->GetY()-trackTRD->GetY()) > fMaxY) return kFALSE;
  if (TMath::Abs(trackTPC->GetZ()-trackTRD->GetZ()) > fMaxZ) return kFALSE;
  if (TMath::Abs(trackTPC->GetSnp()-trackTRD->GetSnp()) > fMaxSnp) return kFALSE;
  if (TMath::Abs(trackTPC->GetTgl()-trackTRD->GetTgl()) > fMaxTgl) return kFALSE;
  if (TMath::Abs(trackTPC->GetSigned1Pt()-trackTRD->GetSigned1Pt()) > fMaxSigned1Pt) return kFALSE;

return kTRUE;
}

//_____________________________________________________________________________
Bool_t AliHLTGlobalTrackMerger::SmoothTracks( const Double_t T1[], const Double_t C1[], Double_t Chi21, Int_t NDF1,
				    const  Double_t T2[], const Double_t C2[], Double_t Chi22, Int_t NDF2,
				     Double_t T [], Double_t C [], Double_t &Chi2, Int_t &NDF,
				     Int_t N  )
{
  //* Smooth two tracks with parameter vectors of size N
  //*
  //* Input:
  //*
  //* T1[N], T2[N] - tracks
  //* C1[N*(N+1)/2], C2[N*(N+1)/2] - covariance matrices in low-diagonal form:
  //* C = { c00, 
  //*       c10, c11, 
  //*       c20, c21, c22, 
  //*       ...             };
  //* Chi2{1,2}, NDF{1,2} - \Chi^2 and "Number of Degrees of Freedom" values for both tracks
  //* Output: 
  //*
  //* T[N], C[N] ( can be aqual to {T1,C1}, or {T2,C2} )
  //* Chi2, NDF
  //*
  //* returns error flag (0 means OK, 1 not OK )
    
  Int_t M = N*(N+1)/2;
  
  Double_t A[M];
  Double_t K[N*N];
  
  for(Int_t k=0; k<M; k++) A[k] = C1[k] + C2[k]; 
  Bool_t err = InvertS(A,N);
  if( err ) return 1;

  Chi2 = Chi21 + Chi22;
  NDF = NDF1 + NDF2;
  
  MultSSQ( C1, A, K, N);	
  Double_t r[N];
  for( Int_t k=0; k<N;k++) r[k] = T1[k] - T2[k]; 
  for( Int_t k=0; k<N;k++ )
    for( Int_t l=0;l<N;l++) T[k] = T1[k] - K[k*N+l]*r[l];

  for( Int_t ind=0,i=0; i<N; i++ ){
    for( Int_t j=0; j<i; j++ ) Chi2+= 2*r[i]*r[j]*A[ind++];
    Chi2+= r[i]*r[i]*A[ind++];
  }
  NDF+=N;

  for( Int_t l=0; l<N; l++ ) K[ (N+1)*l ] -= 1;
  
  for( Int_t ind = 0, l=0; l<N; ++l ){
    for( Int_t j=0; j<=l; ++j, ind++ ){
      A[ind] = 0;
      for( Int_t k=0; k<N; ++k ) A[ind] -= K[l*N+k] * C1[IndexS(j,k)];
    }
  }
  for( Int_t l=0; l<N; l++ ) C[l] = A[l];
  return 0;
}

//_____________________________________________________________________________
void AliHLTGlobalTrackMerger::MultSSQ( const Double_t *A, const Double_t *B, Double_t *C, Int_t N )
{
  for( Int_t ind=0, i=0; i<N; ++i ){
    for( Int_t j=0; j<N; ++j, ++ind ){
      C[ind] = 0;
      for( Int_t k=0; k<N; ++k ) C[ind] += A[IndexS(i,k)] * B[IndexS(k,j)];
    }
  }
}

//_____________________________________________________________________________
Bool_t AliHLTGlobalTrackMerger::InvertS( Double_t A[], Int_t N )
{
  //* input: simmetric > 0 NxN matrix A = {a11,a21,a22,a31..a33,..}  
  //* output: inverse A, in case of problems fill zero and return 1
  //*  
  //* A->low triangular Anew : A = Anew x Anew^T
  //* method:
  //* for(j=1,N) for(i=j,N) Aij=(Aii-sum_{k=1}^{j-1}Aik*Ajk )/Ajj
  //*   

  Bool_t ret = 0;
  
  const Double_t ZERO = 1.E-20;
    
  {
    Double_t *j1 = A, *jj = A;
    for( Int_t j=1; j<=N; j1+=j++, jj+=j ){
      Double_t *ik = j1, x = 0;
      while( ik!=jj ){
	x -= (*ik) * (*ik);
	ik++;
      }
      x += *ik;
      if( x > ZERO ){
	x = sqrt(x);
	*ik = x;
	ik++;
	x = 1 / x;
	for( Int_t step=1; step<=N-j; ik+=++step ){ // ik==Ai1
	  Double_t sum = 0;
	  for( Double_t *jk=j1; jk!=jj; sum += *(jk++) * *(ik++) );
	  *ik = (*ik - sum) * x; // ik == Aij
	}
      }else{
	Double_t *ji=jj;
	for( Int_t i=j; i<N; i++ ) *(ji+=i) = 0.;
	ret = 1;
      }   
    }
  }
  
  //* A -> Ainv
  //* method : 
  //* for(i=1,N){ 
  //*   Aii = 1/Aii; 
  //*   for(j=1,i-1) Aij=-(sum_{k=j}^{i-1} Aik * Akj) / Aii ;
  //* }
  
  {
    Double_t *ii=A,*ij=A;
    for( Int_t i = 1; i<=N; ij=ii+1, ii+=++i ){
      if( *ii > ZERO ){
	Double_t x = -(*ii = 1./ *ii);
	{ 
	  Double_t *jj = A;
	  for( Int_t j=1; j<i; jj+=++j, ij++ ){
	    Double_t *ik = ij, *kj = jj, sum = 0.;
	    for( Int_t k=j; ik!=ii; kj+=k++, ik++ ){
	      sum += *ik * *kj;
	    }
	    *kj = sum * x;
	  }
	}
      }else{      
	for( Double_t *ik = ij; ik!=ii+1; ik++ ){
	  *ik = 0.;
	}
	ret = 1;
      }
    }
  }
  
  //* A -> A^T x A
  //* method: 
  //* Aij = sum_{k=i}^N Aki * Akj
  
  {
    Double_t *ii=A, *ij=A;
    for( Int_t i=1; i<=N; ii+=++i ){
      do{ 
	Double_t *ki = ii, *kj = ij, sum = 0.;
	for( Int_t k=i; k<=N; ki+=k, kj+=k++ ) sum += (*ki) * (*kj);
	*ij = sum;
      }while( (ij++)!=ii );
    }    
  }
  return ret;    
}

//_____________________________________________________________________________
void  AliHLTGlobalTrackMerger::PropagateTracksToDCA(AliESDEvent *esdEvent)
{
  // try to propagate all tracks to DCA to primary vertex
  if(!esdEvent) return;

  const Double_t kBz = esdEvent->GetMagneticField();
  const Double_t kSmallRadius  = 2.8; // [cm] something less than the beam pipe radius
  const Double_t kMaxStep  = 10.0;    // [cm] track propagation step
  Bool_t isOK = kFALSE;

  Int_t nTracks = esdEvent->GetNumberOfTracks(); 
  for(Int_t iTrack = 0; iTrack<nTracks; ++iTrack) {
    AliESDtrack *track = esdEvent->GetTrack(iTrack); 
    if(!track) continue;

    //HLTWarning("1-------: alpha %f, x %f, y, %f, z %f, snp %f, tgl %f, 1pt %f",track->GetAlpha(),track->GetX(),track->GetY(),track->GetZ(),track->GetSnp(),track->GetTgl(),track->GetSigned1Pt());

    // propagate to small radius (material budget included)
    isOK = AliTracker::PropagateTrackTo(track,kSmallRadius,track->GetMass(),kMaxStep,kFALSE);

    //HLTWarning("2-------: alpha %f, x %f, y, %f, z %f, snp %f, tgl %f, 1pt %f",track->GetAlpha(),track->GetX(),track->GetY(),track->GetZ(),track->GetSnp(),track->GetTgl(),track->GetSigned1Pt());
     
    // relate tracks to DCA to primary vertex
    if(isOK) 
    {
      track->RelateToVertex(fVertex, kBz, kVeryBig);
     // HLTWarning("3-------: alpha %f, x %f, y, %f, z %f, snp %f, tgl %f, 1pt %f",track->GetAlpha(),track->GetX(),track->GetY(),track->GetZ(),track->GetSnp(),track->GetTgl(),track->GetSigned1Pt());
    }
  }
}
