#include "SLAMMath.hpp"
#include <cassert>
#include <climits>
#include <limits>
int main() {
  cv::Mat left(48,64,CV_8UC1), right(48,64,CV_8UC1,cv::Scalar(0));
  for (int y=0;y<left.rows;++y) for(int x=0;x<left.cols;++x)
    left.at<uint8_t>(y,x)=(x*17+y*23+x*y*3)%251;
  for (int y=0;y<left.rows-2;++y) for(int x=0;x<left.cols-4;++x)
    right.at<uint8_t>(y+2,x+4)=left.at<uint8_t>(y,x);
  assert(std::abs(OpenVINS::ncc_match(left,right,30,25,34,27,15)-1.0)<1e-12);
  for (int invalid : {INT_MIN,INT_MAX,-1,0,6,1000000}) {
    assert(OpenVINS::ncc_match(left,right,invalid,25,34,27,15)==-1.0);
    assert(OpenVINS::ncc_match(left,right,30,invalid,34,27,15)==-1.0);
    assert(OpenVINS::ncc_match(left,right,30,25,invalid,27,15)==-1.0);
    assert(OpenVINS::ncc_match(left,right,30,25,34,invalid,15)==-1.0);
  }
  for (int size : {INT_MIN,-1,0,49,INT_MAX})
    assert(OpenVINS::ncc_match(left,right,30,25,34,27,size)==-1.0);
  assert(std::abs(OpenVINS::ncc_match(left,left,7,7,7,7,15)-1.0)<1e-12);
  assert(std::abs(OpenVINS::ncc_match(left,left,56,40,56,40,15)-1.0)<1e-12);
  assert(OpenVINS::ncc_match(left,left,57,40,57,40,15)==-1.0);
  cv::Mat flat(48,64,CV_8UC1,cv::Scalar(1));
  assert(OpenVINS::ncc_match(flat,flat,30,25,30,25,15)==-1.0);
  Eigen::Matrix3d F=Eigen::Matrix3d::Zero();
  F(1,2)=1;F(2,2)=-27;
  cv::Point2f result;
  assert(OpenVINS::track_stereo_epipolar(left,right,{30,25},result,F,15,10,.99));
  assert(result.x==34 && result.y==27);
  for (double b : {0.0,1e-300,-1e-300}) {
    F.setZero();F(0,2)=1;F(1,2)=b;
    assert(!OpenVINS::track_stereo_epipolar(left,right,{30,25},result,F,15,10,.99));
  }
  F.setZero();F(1,2)=1;F(2,2)=std::numeric_limits<double>::quiet_NaN();
  assert(!OpenVINS::track_stereo_epipolar(left,right,{30,25},result,F,15,10,.99));
  for (float bad : {std::numeric_limits<float>::quiet_NaN(),
                    std::numeric_limits<float>::infinity(),-1.0f})
    assert(!OpenVINS::track_stereo_epipolar(left,right,{bad,25},result,F,15,10,.99));
}
