/*
 * Copyright (C) 2006-2013  Music Technology Group - Universitat Pompeu Fabra
 *
 * This file is part of Essentia
 *
 * Essentia is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Affero General Public License as published by the Free
 * Software Foundation (FSF), either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the Affero GNU General Public License
 * version 3 along with this program.  If not, see http://www.gnu.org/licenses/
 */

#include "sbic.h"
#include <limits>
#include <cassert>

using namespace std;
using namespace TNT;
using namespace essentia;
using namespace standard;

const char* SBic::name = "SBic";
const char* SBic::description = DOC("This descriptor segments the audio file into homogeneous portions using the Bayesian Information Criterion. The algorithm searches segments for which the feature vectors have the same probability distribution based on the implementation in [1]. The input matrix is assumed to have features along dim1 (horizontal) while frames along dim2 (vertical).\n"
"\n"
"The segmentation is done in three phases: coarse segmentation, fine segmentation and segment validation. The first phase uses parameters 'size1' and 'inc1' to perform BIC segmentation. The second phase uses parameters 'size2' and 'inc2' to perform a local search for segmentation around the segmentation done by the first phase. Finally, the validation phase verifies that BIC differentials at segmentation points are positive.\n"
"\n"
"Because this algorithm takes as input feature vectors of frames, all units are in terms of frames. For example, if a 44100Hz audio signal is segmented as [0, 99, 199] with a frame size of 1024 and a hopsize of 512, this means, in the time domain, that the audio signal is segmented at [0s, 99*512/44100s, 199*512/44100s].\n"
"\n"
"An exception is thrown if the input only contains one frame of features (i.e. second dimension is less than 2).\n"
"\n"
"References:\n"
"  [1] Audioseg, http://audioseg.gforge.inria.fr\n\n"
"  [2] G. Gravier, M. Betser, and M. Ben, Audio Segmentation Toolkit,\n"
"  release 1.2, 2010. Available online:\n"
"  https://gforge.inria.fr/frs/download.php/25187/audioseg-1.2.pdf\n");



Array2D<Real> subarray(const Array2D<Real>& matrix, int i0, int i1, int j0, int j1) {
   int dim1 = i1 - i0 + 1, dim2 = j1 - j0 + 1;
   if (dim1<1 || dim2 <1) return Array2D<Real>();
   Array2D<Real> subMatrix = Array2D<Real>(dim1,dim2);

   for (int i=0; i<dim1; ++i) {
     for (int j=0; j<dim2; ++j) {
       subMatrix[i][j] = matrix[i+i0][j+j0];
     }
   }

   return subMatrix;
}

// This function returns the logarithm of the determinant of (the covariance) matrix
// Seems kind of magic that all together can be computed in just few lines...
Real SBic::logDet(const Array2D<Real>& matrix) const {

  // Remember dimensions are swapped: dim1 is the number of features and dim2 is the number of frames!

  // As we are computing the determinant of the covariance matrix and this matrix is known to be symmetric
  // and positive definite, we can apply  the cholesky decomposition: A = LL*.
  // The determinant, in this case, is known to be the product of the squares of the diagonal
  // of the decomposed matrix (L).
  // The diagonal of L (l_ii) is in fact sqrt(a_ii). As the prod(sqr(l_ii)) = prod(sqr(sqrt(a_ii))) = prod(a_ii),
  // the determinant of A, will be the product of the diagonal elements.
  // Due to computing the log_determinant, then log(prod(a_ii])) = sum(log(a_ii))
  // http://en.wikipedia.org/wiki/Cholesky_decomposition

  int dim1 = matrix.dim1();
  int dim2 = matrix.dim2();

  vector<Real> mp(dim1, 0.0);
  vector<Real> vp(dim1, 0.0);
  Real a, logd = 0.0;
  Real z = 1.0 / Real(dim2);
  Real zz = z * z;

  // As for computing the determinant we are only interested in the diagonal of the covariance matrix, which for
  // each feature vector is:
  // 1/n(sum(x_ii - mu_i)^2) = 1/n(sum(x_i^2) - 2*mu_i*sum(x_i) + sum(mu_i)^2) =
  // 1/n(sum(x_i^2) - 2*n*mu_i*mu_i + n*mu_i^2) = 1/n(sum(x_i^2) - n*mu^2) = 1/n*sum(x_i^2)+ mu_i^2
  // where mu_i is the mean of feature i, and n is the number of frames

  for (int i=0; i<dim1; ++i) {
    for (int j=0; j<dim2; ++j) {
      a = matrix[i][j];
      mp[i] += a;
      vp[i] += a * a;
    }
  }

  // this code accumulates rounding errors which causes bad behaviour when input features are constant.
  // A possible soultion would be to check for a higher threshold (1e-6), as constant features should
  // give a covariance of zero, because (x_i - mu)^2 = 0
  Real diag_cov = 0.0; // diagonal values of the covariance matrix
  for (int j=0; j<dim1; ++j) {
    diag_cov = vp[j] * z - mp[j] * mp[j] * zz; // 1/n*sum(x_i^2)+ mu_i^2.
    // although it could be zero when input is constant, this operation can never be negative by definition
    // however due to rounding errors, it does get negative at times with values of order 1e-9, thus we convert
    // them to zero (1e-10), bounding the logarithm to -10
    logd += diag_cov > 1e-5 ? log(diag_cov):-5;
  }

  return logd;

  // another way of computing the same as above, with possibly less rounding errors, but more expensive.
  //vector<Real> cov(dim1, 0.0);
  //for (int i=0; i<dim1; ++i) {
  //  Real mean = mp[i]/dim2;
  //  Real cov = 0.0;
  //  for (int j=0; j<dim2; ++j) {
  //    a = matrix[i][j];
  //    cov += (a-mean)*(a-mean);
  //  }
  //  cov /= dim2;
  //  logd += cov > 0 ? log(cov):-30;
  //}
}

// This function finds the next change in matrix
std::pair <int, std::vector<Real> > SBic::bicChangeSearch(const Array2D<Real>& matrix, int inc, int current, Real& dmin) const {
  int nFeatures = matrix.dim1();
  int nFrames = matrix.dim2();

//    E_INFO("inc: " << inc);
//    E_INFO("current: " << current);
//    E_INFO("nFeatures: " << nFeatures);
//    E_INFO("nFrames: " << nFrames);
    
  std::vector<Real> d;
    
  Real penalty;
  Real s, s1, s2;
  Array2D<Real> half;
  int n1, n2, seg = 0, shift = inc-1;

  // according to the paper the penalty coefficient should be the following:
  // penalty = 0.5*(3*nFeatures + nFeatures*nFeatures);

  penalty = _cpw * _cp * log(Real(nFrames));
  dmin = numeric_limits<Real>::max();
  Real dBicValue;
    
  // log-determinant for the entire window
  s = logDet(matrix);

  // loop on all mid positions
  while (shift < nFrames - inc) {
    // first part
    n1 = shift + 1;
    half = subarray(matrix, 0, nFeatures-1, 0, shift);
    s1 = logDet(half);

    // second part
    n2 = nFrames - n1;
    half = subarray(matrix, 0, nFeatures-1, shift+1, nFrames-1);
    s2 = logDet(half);

    dBicValue = 0.5 * (n1*s1 + n2*s2 - nFrames*s + penalty);
    d.push_back(dBicValue);
      
    if (dBicValue < dmin) {
      seg = shift;
      dmin = dBicValue;
    }
    shift += inc;
  }

  std::pair <int, vector<Real> > bicChangeResult;
  bicChangeResult.first = current + seg;
  if (dmin > 0)
  {
    bicChangeResult.first = 0;
  }
  bicChangeResult.second = d;

  return bicChangeResult;
}

// This function computes the delta bic. It is actually used to determine
// whether two consecutive segments have the same probability distribution
// or not. In such case, these segments are joined.
Real SBic::delta_bic(const Array2D<Real>& matrix, Real segPoint) const{

  int nFeatures = matrix.dim1();
  int nFrames = matrix.dim2();
  Array2D<Real> half;
  Real s, s1, s2;

  // entire segment
  s = logDet(matrix);

  // first half
  half = subarray(matrix, 0, nFeatures-1, 0, int(segPoint));
  s1 = logDet(half);

  // second half
  half = subarray(matrix, 0, nFeatures-1, int(segPoint + 1), nFrames-1);
  s2 = logDet(half);

  return 0.5 * ( segPoint*s1 + (nFrames - segPoint)*s2 - nFrames*s + _cpw*_cp*log(Real(nFrames)) );
}


void SBic::configure() {
  _size1 = parameter("size1").toInt();
  _inc1 = parameter("inc1").toInt();
  _size2 = parameter("size2").toInt();
  _inc2 = parameter("inc2").toInt();
  _cpw = parameter("cpw").toReal();
}

void SBic::compute()
{
    const Array2D<Real>& features = _features.get();
    vector<Real>& segmentation = _segmentation.get();
    vector<Real>& segValues = _segValues.get();
    vector<Real>& bicValues = _bicValues.get();
    Array2D<Real> window;

    int currSeg = 0, endSeg = 0, currIdx, prevSeg, nextSeg, i;

    // I assume matrix's dim1 as the number of features and dim2 as the number of frames
    int nFeatures = features.dim1();
    int nFrames = features.dim2();

    if (nFrames < 2) {
        throw EssentiaException("SBic: second dimension of features matrix is less than 2, unable to perform segmentation with less than 2 frames");
    }

    _cp = 2 * nFeatures;

    ///////////////////////////////////
    // first pass - coarse segmentation
    
    E_INFO("First pass");
    
    endSeg = -1; // so the very first pass becomes _size1 - 1

    Real dmin;
    int offset;
    int prevCurrSeg;
    while (endSeg < nFrames-1)
    {
        endSeg += _size1;
        if (endSeg >= nFrames)
        {
            endSeg = nFrames-1;
        }

        E_INFO(" ");
        E_INFO("currSeg : " << currSeg << " / endSeg: " <<  endSeg);
        window = subarray(features, 0, nFeatures-1, currSeg, endSeg);

        // A change has been found
        std::pair<int, std::vector<Real> > bicChangeResult;
        bicChangeResult = bicChangeSearch(window, _inc1, currSeg, dmin);
        i = bicChangeResult.first;
        std::vector<Real> tmpBicValues(bicChangeResult.second);
        E_INFO("tmpBicValues.size() : " << tmpBicValues.size());
        
        
        if (i)
        {
            E_INFO("Found peak at : " << i);
            prevCurrSeg = currSeg;
            segmentation.push_back(i);
            segValues.push_back(dmin);
            currSeg = (i + _inc1);
            endSeg = currSeg - 1;
            
            int nToAdd = i - prevCurrSeg + 1;
            
            // Store bic values up to (and including) the peak
            for (int j=0; j < nToAdd; ++j)
            {
                bicValues.push_back(tmpBicValues[j]);
            }
            E_INFO("size of bicValues: " << bicValues.size());
            E_INFO("number of values added: " << nToAdd);
        }
        
        if (endSeg == nFrames-1)
        {
            E_INFO("Storing end of bic values");
            for (int j=0; j < tmpBicValues.size(); ++j)
            {
                bicValues.push_back(tmpBicValues[j+currSeg]);
            }
        }
    }
    
    E_INFO("nPeaks: " << segmentation.size());
    E_INFO("nFrames: " << nFrames);
    E_INFO("size of bicValues: " << bicValues.size());
    
    //////////////////////////////////
    // second pass - fine segmentation
    
    E_INFO("Second pass");
    
    currSeg = currIdx = prevSeg = nextSeg = 0;
    int halfSize = _size2 / 2;

    for (currIdx=0; currIdx < int(segmentation.size()); ++currIdx)
    {
        currSeg = int(segmentation[currIdx] - halfSize);
        if (currSeg < 0)
        {
            currSeg = 0;
        }

        endSeg = currSeg + _size2 - 1;

        if (endSeg >= nFrames)
        {
            endSeg = nFrames-1;
        }

        window = subarray(features, 0, nFeatures-1, currSeg, endSeg);

        // A change has been found
        std::pair<int, std::vector<Real> > bicChangeResult;
        bicChangeResult = bicChangeSearch(window, _inc1, currSeg, dmin);
        if (i)
        {
            prevSeg = (currIdx == 0) ? 0 : int(segmentation[currIdx-1]);
            nextSeg = (currIdx + 1 >= int(segmentation.size())) ? nFrames - 1 : int(segmentation[currIdx + 1]);

            if (prevSeg <= i  &&  i <= nextSeg)
            {
                if (i != int(segmentation[currIdx]))
                {
                    E_INFO("Refined peak");
                    
                    // We move (refine) the segmentation
                    segmentation[currIdx] = i;
                    segValues[currIdx] = dmin;
                }
            }
            else
            {
                // We remove the segmentation
                if (currIdx < int(segmentation.size()))
                {
                    segmentation.erase(segmentation.begin() + currIdx);
                    segValues.erase(segValues.begin() + currIdx);
                    --currIdx;
                }
            }
        }
    }

    E_INFO("nPeaks: " << segmentation.size());
    
    //////////////////////////////////
    // third pass - segment validation

    E_INFO("Segment validation");
    
    // the whole signal was interpretted as one segment, just return
    if (segmentation.size() == 0) {
        return;
    }

    currSeg = 0;

    // verify delta_bic is negative between consecutive segments
    for (i=1; i<int(segmentation.size())-1; ++i)
    {
        endSeg = int(segmentation[i+1]);
        window = subarray(features, 0, nFeatures-1, currSeg, endSeg);
        if (delta_bic(window, segmentation[i] - segmentation[i - 1]) > 0)
        {
            segmentation.erase(segmentation.begin() + i);
            segValues.erase(segValues.begin() + i);
            --i;
            continue;
        }
        currSeg = int(segmentation[i] + 1);
    }
    E_INFO("nPeaks: " << segmentation.size());
}
