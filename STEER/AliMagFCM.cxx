/**************************************************************************
 * Copyright(c) 1998-1999, ALICE Experiment at CERN, All rights reserved. *
 *                                                                        *
 * Author: The ALICE Off-line Project.                                    *
 * Contributors are mentioned in the code where appropriate.              *
 *                                                                        *
 * Permission to use, copy, modify and distribute this software and its   *
 * documentation strictly for non-commercial purposes is hereby granted   *
 * without fee, provided that the above copyright notice appears in all   *
 * copies and that both the copyright notice and this permission notice   *
 * appear in the supporting documentation. The authors make no claims     *
 * about the suitability of this software for any purpose. It is          *
 * provided "as is" without express or implied warranty.                  *
 **************************************************************************/

/*
$Log$
*/

#include "AliMagFCM.h"
#include "TSystem.h"

ClassImp(AliMagFCM)

//________________________________________
AliMagFCM::AliMagFCM(const char *name, const char *title, const Int_t integ, 
		     const Int_t map, const Float_t factor, const Float_t fmax)
  : AliMagF(name,title,integ,map,factor,fmax)
{
  //
  // Standard constructor
  //
  fType = kConMesh;
  printf("Constant Mesh Field %s created: map= %d, factor= %f, file= %s\n",
	 fName.Data(),map,factor,fTitle.Data());
}

//________________________________________
AliMagFCM::AliMagFCM(const AliMagFCM &magf)
{
  //
  // Copy constructor
  //
  magf.Copy(*this);
}

//________________________________________
void AliMagFCM::Field(Float_t *x, Float_t *b)
{
  //
  // Method to calculate the magnetic field
  //
  Double_t ratx, raty, ratz, hix, hiy, hiz, ratx1, raty1, ratz1, 
    bhyhz, bhylz, blyhz, blylz, bhz, blz, xl[3];
  const Double_t kone=1;
  Int_t ix, iy, iz;
  
  // --- find the position in the grid ---
  
  b[0]=b[1]=b[2]=0;
  if(-700<x[2] && x[2]<fZbeg && x[0]*x[0]+(x[1]+30)*(x[1]+30) < 560*560) {
    b[2]=2;
  } else  {
    Bool_t infield=(fZbeg<=x[2] && x[2]<fZbeg+fZdel*(fZn-1)
		    &&  ( fXbeg <= TMath::Abs(x[0]) && TMath::Abs(x[0]) < fXbeg+fXdel*(fXn-1) )
		    &&  ( fYbeg <= TMath::Abs(x[1]) && TMath::Abs(x[1]) < fYbeg+fYdel*(fYn-1) ));
    if(infield) {
      xl[0]=TMath::Abs(x[0])-fXbeg;
      xl[1]=TMath::Abs(x[1])-fYbeg;
      xl[2]=x[2]-fZbeg;
      
      // --- start with x
      
      hix=xl[0]*fXdeli;
      ratx=hix-int(hix);
      ix=int(hix);
      
      hiy=xl[1]*fYdeli;
      raty=hiy-int(hiy);
      iy=int(hiy);
      
      hiz=xl[2]*fZdeli;
      ratz=hiz-int(hiz);
      iz=int(hiz);
      
      if(fMap==2) {
	// ... simple interpolation
	ratx1=kone-ratx;
	raty1=kone-raty;
	ratz1=kone-ratz;
	bhyhz = Bx(ix  ,iy+1,iz+1)*ratx1+Bx(ix+1,iy+1,iz+1)*ratx;
	bhylz = Bx(ix  ,iy+1,iz  )*ratx1+Bx(ix+1,iy+1,iz  )*ratx;
	blyhz = Bx(ix  ,iy  ,iz+1)*ratx1+Bx(ix+1,iy  ,iz+1)*ratx;
	blylz = Bx(ix  ,iy  ,iz  )*ratx1+Bx(ix+1,iy  ,iz  )*ratx;
	bhz   = blyhz             *raty1+bhyhz             *raty;
	blz   = blylz             *raty1+bhylz             *raty;
	b[0]  = blz               *ratz1+bhz               *ratz;
	//
	bhyhz = By(ix  ,iy+1,iz+1)*ratx1+By(ix+1,iy+1,iz+1)*ratx;
	bhylz = By(ix  ,iy+1,iz  )*ratx1+By(ix+1,iy+1,iz  )*ratx;
	blyhz = By(ix  ,iy  ,iz+1)*ratx1+By(ix+1,iy  ,iz+1)*ratx;
	blylz = By(ix  ,iy  ,iz  )*ratx1+By(ix+1,iy  ,iz  )*ratx;
	bhz   = blyhz             *raty1+bhyhz             *raty;
	blz   = blylz             *raty1+bhylz             *raty;
	b[1]  = blz               *ratz1+bhz               *ratz;
	//
	bhyhz = Bz(ix  ,iy+1,iz+1)*ratx1+Bz(ix+1,iy+1,iz+1)*ratx;
	bhylz = Bz(ix  ,iy+1,iz  )*ratx1+Bz(ix+1,iy+1,iz  )*ratx;
	blyhz = Bz(ix  ,iy  ,iz+1)*ratx1+Bz(ix+1,iy  ,iz+1)*ratx;
	blylz = Bz(ix  ,iy  ,iz  )*ratx1+Bz(ix+1,iy  ,iz  )*ratx;
	bhz   = blyhz             *raty1+bhyhz             *raty;
	blz   = blylz             *raty1+bhylz             *raty;
	b[2]  = blz               *ratz1+bhz               *ratz;
	//printf("ratx,raty,ratz,b[0],b[1],b[2] %f %f %f %f %f %f\n",
	//ratx,raty,ratz,b[0],b[1],b[2]);
	//
	// ... use the dipole symmetry
	if (x[0]*x[1] < 0) b[1]=-b[1];
	if (x[0]<0) b[2]=-b[2];
      } else {
	printf("Invalid field map for constant mesh %d\n",fMap);
      }
    } else {
      //This is the ZDC part
      Float_t rad2=x[0]*x[0]+x[1]*x[1];
      if(rad2<kD2RA2) {
	if(x[2]>kD2BEG) {
	  
	  //    Separator Dipole D2
	  if(x[2]<kD2END) b[1]=kFDIP;
	} else if(x[2]>kD1BEG) {
	  
	  //    Separator Dipole D1
	  if(x[2]<kD1END) b[1]=-kFDIP;
	}
	if(rad2<kCORRA2) {
	  
	  //    First quadrupole of inner triplet de-focussing in x-direction
	  //    Inner triplet
	  if(x[2]>kZ4BEG) {
	    if(x[2]<kZ4END) {
	      
	      //    2430 <-> 3060
	      b[0]=-kG1*x[1];
	      b[1]=-kG1*x[0];
	    }
	  } else if(x[2]>kZ3BEG) {
	    if(x[2]<kZ3END) {
	      
	      //    1530 <-> 2080
	      b[0]=kG1*x[1];
	      b[1]=kG1*x[0];
	    }
	  } else if(x[2]>kZ2BEG) {
	    if(x[2]<kZ2END) {
	      
	      //    890 <-> 1430
	      b[0]=kG1*x[1];
	      b[1]=kG1*x[0];
	    }
	  } else if(x[2]>kZ1BEG) {
	    if(x[2]<kZ1END) {
	      
	      //    0 <->  630
	      b[0]=-kG1*x[1];
	      b[1]=-kG1*x[0];
	    }
	  } else if(x[2]>kCORBEG) {
	    if(x[2]<kCOREND) {
	      //    Corrector dipole (because of dimuon arm)
	      b[0]=kFCORN;
	    }
	  }
	}
      }
    }
  }
  if(fFactor!=1) {
    b[0]*=fFactor;
    b[1]*=fFactor;
    b[2]*=fFactor;
  }
}

//________________________________________
void AliMagFCM::ReadField()
{
  // 
  // Method to read the magnetic field map from file
  //
  FILE *magfile;
  Int_t ix, iy, iz, ipx, ipy, ipz;
  Float_t bx, by, bz;
  char *fname;
  printf("Reading Magnetic Field %s from file %s\n",fName.Data(),fTitle.Data());
  fname = gSystem->ExpandPathName(fTitle.Data());
  magfile=fopen(fname,"r");
  delete [] fname;
  if (magfile) {
    fscanf(magfile,"%d %d %d %f %f %f %f %f %f",
    	   &fXn, &fYn, &fZn, &fXdel, &fYdel, &fZdel, &fXbeg, &fYbeg, &fZbeg);
    printf("fXn %d, fYn %d, fZn %d, fXdel %f, fYdel %f, fZdel %f, fXbeg %f, fYbeg %f, fZbeg %f\n",
    	   fXn, fYn, fZn, fXdel, fYdel, fZdel, fXbeg, fYbeg, fZbeg);
    fXdeli=1./fXdel;
    fYdeli=1./fYdel;
    fZdeli=1./fZdel;
    fB = new TVector(3*fXn*fYn*fZn);
    for (iz=0; iz<fZn; iz++) {
      ipz=iz*3*(fXn*fYn);
      for (iy=0; iy<fYn; iy++) {
	ipy=ipz+iy*3*fXn;
	for (ix=0; ix<fXn; ix++) {
	  ipx=ipy+ix*3;
	  fscanf(magfile,"%f %f %f",&bz,&by,&bx);
	  (*fB)(ipx+2)=bz;
	  (*fB)(ipx+1)=by;
	  (*fB)(ipx  )=bx;
	}
      }
    }
  } else { 
    printf("File %s not found !\n",fTitle.Data());
    exit(1);
  }
}

//________________________________________
void AliMagFCM::Copy(AliMagFCM &magf) const
{
  //
  // Copy *this onto magf
  //
  Fatal("Copy","Not implemented!\n");
}

//________________________________________
AliMagFCM & AliMagFCM::operator =(const AliMagFCM &magf)
{
  magf.Copy(*this);
  return *this;
}
