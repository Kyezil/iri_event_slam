#include "track_init/track_init.h"

namespace track {
TrackInit::TrackInit(ros::NodeHandle & nh) : nh_(nh) {
  got_camera_info_ = false;

  // setup subscribers and publishers
  camera_info_sub_ = nh_.subscribe("camera_info", 1, &TrackInit::cameraInfoCallback, this);

  image_transport::ImageTransport it_(nh_);
  image_sub_ = it_.subscribe("image", 1, &TrackInit::imageCallback, this);
  image_pub_ = it_.advertise("rendering", 1);
  poseStampedPub = nh_.advertise<geometry_msgs::PoseStamped>("pose", 2, true);
}

TrackInit::~TrackInit() {
  image_pub_.shutdown();
  poseStampedPub.shutdown();
}

void TrackInit::cameraInfoCallback(const sensor_msgs::CameraInfo::ConstPtr& msg) {
  ROS_DEBUG("got camera info");
  got_camera_info_ = true;

  camera_matrix_ = cv::Mat(3, 3, CV_64F);
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++)
      camera_matrix_.at<double>(cv::Point(i, j)) = msg->K[i+j*3];

  dist_coeffs_ = cv::Mat(msg->D.size(), 1, CV_64F);
  for (int i = 0; i < msg->D.size(); i++)
    dist_coeffs_.at<double>(i) = msg->D[i];
}

void TrackInit::imageCallback(const sensor_msgs::Image::ConstPtr& msg) {
  cv_bridge::CvImageConstPtr cv_ptr;
  try {
    cv_ptr = cv_bridge::toCvShare(msg, sensor_msgs::image_encodings::MONO8);
  }
  catch (cv_bridge::Exception& e) {
    ROS_ERROR("cv_bridge exception: %s", e.what());
    return;
  }

  ROS_DEBUG("got an image");
  // undistort image
  cv_bridge::CvImage cv_img;
  cv_img.encoding = cv_ptr->encoding;
  cv::undistort(cv_ptr->image, cv_img.image, camera_matrix_, dist_coeffs_);

  // find square in image
  std::vector<cv::Point> square = findSquare(cv_img);

  // draw square and publish
  if (square.size() > 0) {
  
    ROS_DEBUG_STREAM("got a square: " <<  square);
    // grayscale to color
    cv::cvtColor(cv_img.image, cv_img.image, CV_GRAY2BGR);
    cv_img.encoding = "bgr8";
    // add square
    cv::polylines(cv_img.image, square, true, cv::Scalar(0,0,255), 3, cv::LINE_AA);

    // TEMPORAL code here to get camera position
    // assuming square to be 85x85mm centered in Z = 0 plane
    // that is corners are at [+-85/2, +-85/2, 0]

    // sort point clockwise
    sortPointsCW(square);
    ROS_DEBUG_STREAM("sorted points:" << square);

    for (int i = 0; i < 4; ++i) {
        cv::Point &p = square[i];
        cv::circle(cv_img.image, p, 1, CV_RGB(0,0,255),5);
        cv::putText(cv_img.image, std::to_string(i+1), p, cv::FONT_HERSHEY_DUPLEX, 1, cv::Scalar(0,143,143), 2);
    }

    float hw = 85.0f/2; // half width of square
    // now estimate camera position
    std::vector<cv::Point3d> model_points {
      cv::Point3f(-hw, -hw, 0.0f),
      cv::Point3f( hw, -hw, 0.0f),
      cv::Point3f( hw,  hw, 0.0f),
      cv::Point3f(-hw,  hw, 0.0f)
    };

    std::vector<cv::Point2d> image_points;
    cv::Mat(square).convertTo(image_points, cv::Mat(image_points).type());  

    cv::Mat rotation_vector; // Rotation in axis-angle form
    cv::Mat translation_vector;
    cv::solvePnP(model_points, image_points, camera_matrix_, dist_coeffs_,
      rotation_vector, translation_vector, false, cv::SOLVEPNP_ITERATIVE);

    ROS_DEBUG_STREAM("model points" << cv::Mat(model_points) << "\nimage points:" << cv::Mat(image_points));
    ROS_DEBUG_STREAM("Rotation Vector\n" << rotation_vector);
    ROS_DEBUG_STREAM("Translation Vector\n" << translation_vector);
    
    // reproject points
    std::vector<cv::Point2d> proj_image_points;
    cv::projectPoints(model_points, rotation_vector, translation_vector, camera_matrix_, dist_coeffs_, proj_image_points);
    ROS_INFO_STREAM("Points reprojected");
    for (int i = 0; i < 4; ++i) {
        cv::Point2d &p = proj_image_points[i];
        ROS_INFO_STREAM("P" << i << ": " << p);
        cv::circle(cv_img.image, p, 1, CV_RGB(0,255,0),3);
    }


    // OPENCV assumes frameCoordinates = R * worldCoordinates + t 
    // camera position = - R' * t
    // camera orientation = R'
    cv::Mat R;
    cv::Rodrigues(rotation_vector, R);
    R = R.t();
    translation_vector = -R * translation_vector;
    cv::Rodrigues(R, rotation_vector);

    cv::Point3d pos(translation_vector);
    cv::Point3d rot(rotation_vector);

    geometry_msgs::PoseStamped poseStamped;

    poseStamped.header.frame_id="map";
    poseStamped.header.stamp = ros::Time::now();

    poseStamped.pose.position.x = pos.x;
    poseStamped.pose.position.y = pos.y;
    poseStamped.pose.position.z = pos.z;

    double angle = sqrt(rot.x*rot.x + rot.y*rot.y + rot.z*rot.z);

    if (angle > 0.0) {
        poseStamped.pose.orientation.x = rot.x * sin(angle/2)/angle;
        poseStamped.pose.orientation.y = rot.y * sin(angle/2)/angle;
        poseStamped.pose.orientation.z = rot.z * sin(angle/2)/angle;
        poseStamped.pose.orientation.w = cos(angle/2);
    } else {
        poseStamped.pose.orientation.x = 0;
        poseStamped.pose.orientation.y = 0;
        poseStamped.pose.orientation.z = 0;
        poseStamped.pose.orientation.w = 1;
    }
    poseStampedPub.publish(poseStamped); 
  }

  image_pub_.publish(cv_img.toImageMsg());
}

void TrackInit::sortPointsCW(std::vector<cv::Point> &points) {
  // compute centroid
  cv::Point C(0,0);
  for (cv::Point& p : points) C += p;
  C *= 1.0/points.size();
  // compute the angle of the centroid-vertex segment
  std::vector<std::pair<float, cv::Point> > angles(points.size());
  for (int i = 0; i < points.size(); ++i) {
    angles[i].first = atan2(points[i].y - C.y, points[i].x - C.x);
    angles[i].second = points[i];
  }
  std::sort(angles.begin(), angles.end(), [](const std::pair<float, cv::Point>& c1, const std::pair<float, cv::Point>& c2) {
      return c1.first < c2.first;
  });

  for (int i = 0; i < angles.size(); ++i) {
    points[i] = angles[i].second;
  }
}

std::vector<cv::Point> TrackInit::findSquare(const cv_bridge::CvImage img) {
  std::vector<cv::Point> square;
  std::vector<std::vector<cv::Point> > contours;
  // threshold to low value (square should be black)
  cv::Mat im;
  cv::threshold(img.image, im, 40, 255, cv::THRESH_BINARY_INV);
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

