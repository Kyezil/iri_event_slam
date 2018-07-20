#pragma once
#include <ros/ros.h>

#include <image_transport/image_transport.h>
#include <cv_bridge/cv_bridge.h>
#include <std_msgs/Bool.h>
#include <sensor_msgs/Image.h>
#include <sensor_msgs/image_encodings.h>
#include <sensor_msgs/CameraInfo.h>
#include <geometry_msgs/PoseStamped.h>
#include <dvs_msgs/Event.h>
#include <dvs_msgs/EventArray.h>

#include <Eigen/Dense>
#include <opencv2/opencv.hpp>

#include "efk.h"
#include "tracker_map.h"

using Vec3 = Eigen::Vector3d;
using Vec4 = Eigen::Vector4d;
using Quaternion = Eigen::Quaterniond;
using AngleAxis = Eigen::AngleAxisd;

namespace track {

class Tracker {
public:
    Tracker(ros::NodeHandle & nh);
    virtual ~Tracker();
    
    // uncertainty in movement per second
    const Vec3 sigma_v = (Eigen::Vector3d() << 2, 2, 2).finished();
    const Vec3 sigma_w = (Eigen::Vector3d() << 0.5, 0.5, 0.5).finished();
    // uncertainty in measurement of pixel-segment distance
    const double sigma_d    = 1;
    // maximum distance to match event to line
    const double MATCHING_DIST_THRESHOLD = 5;
    // minimum margin between 1st and 2nd distance
    const double MATCHING_DIST_MIN_MARGIN = 10;

private:
    ros::NodeHandle nh_;
    EFK efk_;
    TrackerMap map_;

    void cameraInfoCallback(const sensor_msgs::CameraInfo::ConstPtr& msg);
    void cameraPoseCallback(const geometry_msgs::PoseStamped::ConstPtr& msg);
    void resetCallback(const std_msgs::Bool::ConstPtr& msg);
    void eventsCallback(const dvs_msgs::EventArray::ConstPtr& msg);

    void publishTrackedPose(const EFK::State& S);
    
    // camera info parameters
    bool got_camera_info_;
    Vec4 camera_matrix_;
    //Mat camera_matrix_, dist_coeffs_;
    ros::Subscriber camera_info_sub_;

    // last camera pose
    bool got_camera_pose_;
    Vec3 camera_position_; // x,y,z
    Quaternion camera_orientation_; // quaternion x,y,z,w

    // is running ?
    bool is_tracking_running_;

    // pose msg as initial pose
    ros::Subscriber starting_pose_sub_;
    // reset EFK tracking
    ros::Subscriber reset_sub_;
    // get events
    ros::Subscriber event_sub_;
    
    // VISUALIZATION
    // publish pose
    ros::Publisher pose_pub_;
    // debug event association
    image_transport::Publisher event_map_pub_;
    // image of map and events
    cv::Mat map_events_;
    // number of events to acumulate before publishing a map image
    const uint PUBLISH_MAP_EVENTS_RATE = 500;
    uint event_counter_;
    void updateMapEvents(const dvs_msgs::Event &e, bool used = false);

    // TRACKING VARIABLES
    ros::Time last_event_ts;
    void handleEvent(const dvs_msgs::Event &e);
};

}
