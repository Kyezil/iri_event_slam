#include "track_init/track_init.h"

namespace track {
TrackInit::TrackInit(ros::NodeHandle & nh) : nh_(nh) {
  //got_camera_info_ = false;

  // setup subscribers and publishers
  //camera_info_sub_ = nh_.subscribe("camera_info", 1, &TrackInit::cameraInfoCallback, this);

  image_transport::ImageTransport it_(nh_);
  image_sub_ = it_.subscribe("image", 1, &TrackInit::imageCallback, this);
  image_pub_ = it_.advertise("rendering", 1);
  //undistorted_image_pub_ = it_.advertise("dvs_undistorted", 1);
}

TrackInit::~TrackInit() {
  image_pub_.shutdown();
  //undistorted_image_pub_.shutdown();
}
/*
void TrackInit::cameraInfoCallback(const sensor_msgs::CameraInfo::ConstPtr& msg)
{
  got_camera_info_ = true;

  camera_matrix_ = cv::Mat(3, 3, CV_64F);
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++)
      camera_matrix_.at<double>(cv::Point(i, j)) = msg->K[i+j*3];

  dist_coeffs_ = cv::Mat(msg->D.size(), 1, CV_64F);
  for (int i = 0; i < msg->D.size(); i++)
    dist_coeffs_.at<double>(i) = msg->D[i];
}
*/

void TrackInit::imageCallback(const sensor_msgs::Image::ConstPtr& msg) {
  cv_bridge::CvImageConstPtr cv_ptr;
  try {
    cv_ptr = cv_bridge::toCvShare(msg, sensor_msgs::image_encodings::MONO8);
  }
  catch (cv_bridge::Exception& e) {
    ROS_ERROR("cv_bridge exception: %s", e.what());
    return;
  }

  ROS_INFO("got an image");

  // find square in image
  std::vector<cv::Point> square = findSquare(cv_ptr);
  // draw square and publish
  if (square.size() > 0) {
    std::cout << "found a square at: \n";
    
    for (int i = 0; i < 4; ++i) {
      std::cout << "P" << i+1 << " [" << square[i].x << "," << square[i].y << "]\n";
    }
    std::cout << std::endl;
    // grayscale to color
    cv::Mat img;
    cv::cvtColor(cv_ptr->image, img, CV_GRAY2BGR);
    // add square
    cv::polylines(img, square, true, cv::Scalar(0,0,255), 3, cv::LINE_AA);
    // and publish result
    cv_bridge::CvImage cv_image;
    img.copyTo(cv_image.image);
    cv_image.encoding = "bgr8";
    image_pub_.publish(cv_image.toImageMsg());
  } else {
    image_pub_.publish(msg);
  }
}

std::vector<cv::Point> TrackInit::findSquare(const cv_bridge::CvImageConstPtr img) {
  std::vector<cv::Point> square;
  std::vector<std::vector<cv::Point> > contours;
  // threshold to low value (square should be black)
  cv::Mat im;
  cv::threshold(img->image, im, 40, 255, cv::THRESH_BINARY_INV);
  // find contours
  cv::findContours(im, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);
    
  // sort by decreasing area
  std::sort(contours.begin(), contours.end(), [](const std::vector<cv::Point>& c1, const std::vector<cv::Point>& c2) {
      return cv::contourArea(c1, false) > cv::contourArea(c2, false);
  });
  
  // final square result
  for (int i = 0; i < contours.size(); ++i) {
      if (cv::contourArea(contours[i]) > 0.01*im.total()) { // at least 1% of image area
          // check if it's a square, approximate with low tolerance 1% of length
          std::vector<cv::Point> approx;
          cv::approxPolyDP(contours[i], approx, 0.01*cv::arcLength(contours[i], true), true);
          
          if (approx.size() == 4) { // quadrilateral approximation
              square = approx;
              break;
          }
      }
  }
  return square;
}

} // namespace
