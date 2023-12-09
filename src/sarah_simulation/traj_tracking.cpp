// Created by weijian on 17/11/23.

// include ROS Libraries
#include <ros/ros.h>
#include "std_msgs/Int32.h"
#include <tf/tf.h>
#include <geometry_msgs/Twist.h>
#include <geometry_msgs/Pose2D.h>
#include <nav_msgs/Odometry.h>
#include <cmath>
#include <iostream>
#include "liom_local_planner/vehicle_model.h"
#include "liom_local_planner/optimizer_interface.h"
#include "yaml-cpp/yaml.h"
#include <vector>
#include "liom_local_planner/math/pose.h"
#include "traj_tracking/fg_test.h"
#include "traj_tracking/matplotlibcpp.h"
#include <ackermann_msgs/AckermannDriveStamped.h>
#include "liom_local_planner/yaml_all.h"
#include "traj_tracking/generate_vertex.h"
GenerateCar car;
TwoWheelModel twowheel;

namespace plt = matplotlibcpp;

using namespace liom_local_planner;
using namespace math;

// function to get robot position and orientation from odom topic
void plot_circle (const double& x, const double& y, int index, int pose_index);
void plot_car (const double& x, const double& y, const double& theta, int index, int pose_index);
void robot_odom1(const nav_msgs::Odometry msg);
std::vector<Pose> generate_path(std::string filename);
// Trajectory generate_traj(std::string filename);
Trajectory generate_ref_traj(Trajectory traj_lead, double offset);
Trajectory generate_ref_traj_diff(Trajectory traj_lead, double offset, double angle);
// function to calculate the euclidean distance between robot and desired traj 
double getDistance(double x1, double y1, double x2, double y2);

// variable declaration
// ackermann_msgs::AckermannDriveStamped robot1_cmd, robot2_cmd;
geometry_msgs::Twist robot1_cmd;
ros::Publisher robot1_cmd_pub; // node publishers
ros::Publisher integer_pub;
tf::Point Odom_pos;    //odometry position (x,y)
geometry_msgs::Pose actual_pose;
double Odom_yaw;    //odometry orientation (yaw)
geometry_msgs::Pose2D qd,robot_pose1, err;
geometry_msgs::Twist vel_msg;
double robot1_vel;
double robot1_omg;
std::vector<double> UpdateControlInputTwoWheel(int timestep, const std::vector<Pose>& path, const geometry_msgs::Pose2D& robot_pose, 
						double ev, double ew, ros::Publisher& robot_vel_pub,geometry_msgs::Twist& vel_msg);
void UpdateControlInputCar(int timestep, const Trajectory& traj, const geometry_msgs::Pose2D& robot_pose, 
						ros::Publisher& robot_vel_pub, geometry_msgs::Twist& vel_msg);
void zero_input(ros::Publisher& robot_vel_pub, geometry_msgs::Twist& vel_msg);

// VehicleModel vehicle;
// double vel_max = vehicle.max_vel_diff;
// double omg_max = vehicle.omg_max_diff;
// double acc_max = vehicle.max_acc_diff;
// double angacc_max = vehicle.omg_acc_diff;

std::vector<std::vector<std::vector<double>>> generate_traj_fg(std::string filename, int numR) {
    // 读取YAML文件
    YAML::Node traj_ = YAML::LoadFile(filename);

    // 检查文件是否成功加载
    if (!traj_.IsDefined()) {
        std::cerr << "Failed to load yaml file.\n";
    }
    int j = 0, k = 0;
    std::vector<std::vector<std::vector<double>>> plan(numR, std::vector<std::vector<double>>(traj_.size() / numR , std::vector<double>(5)));
    // 读取每个轨迹点
    for (const auto& kv : traj_) {
        const std::string& poseName = kv.first.as<std::string>();
        const YAML::Node& poseNode = kv.second;
        plan[j][k][0] = poseNode["x"].as<double>();
        plan[j][k][1] = poseNode["y"].as<double>();
        plan[j][k][2] = poseNode["theta"].as<double>();
        plan[j][k][3] = poseNode["v"].as<double>();
        plan[j][k][4] = poseNode["omega"].as<double>();
        k++;
        if (k > traj_.size() / numR - 1) {
            j++;
            k = 0;
        }
    }
    return plan;
}

std::vector<std::vector<Vec2d>> generate_obs(std::string filename) {
	std::vector<std::vector<Vec2d>> obstacle;
	Vec2d vert;
	// 读取YAML文件
	YAML::Node obs = YAML::LoadFile(filename);

	// 检查文件是否成功加载
	if (!obs.IsDefined()) {
		std::cerr << "Failed to load yaml file.\n";
	}

	// 读取每个轨迹点
	for (const auto& kv : obs) {
		std::vector<Vec2d> obs_;
		const std::string& poseName = kv.first.as<std::string>();
		const YAML::Node& poseNode = kv.second;
		double x = poseNode["vx1"].as<double>();
		double y = poseNode["vy1"].as<double>();
		vert.set_x(x);
		vert.set_y(y);
		obs_.push_back(vert);
		x = poseNode["vx2"].as<double>();
		y = poseNode["vy2"].as<double>();
		vert.set_x(x);
		vert.set_y(y);
		obs_.push_back(vert);			
		x = poseNode["vx3"].as<double>();
		y = poseNode["vy3"].as<double>();
		vert.set_x(x);
		vert.set_y(y);
		obs_.push_back(vert);			
		x = poseNode["vx4"].as<double>();
		y = poseNode["vy4"].as<double>();
		vert.set_x(x);
		vert.set_y(y);
		obs_.push_back(vert);
		obstacle.push_back(obs_);
	}
	return obstacle;
}

void zero_input(ros::Publisher& robot_vel_pub, geometry_msgs::Twist& vel_msg) {
	vel_msg.linear.x = 0;
	vel_msg.linear.y =0;
	vel_msg.linear.z =0;
	vel_msg.angular.x = 0;
	vel_msg.angular.y = 0;
	vel_msg.angular.z =0;
	robot_vel_pub.publish(vel_msg);
}

void robot_actual_pose1(const geometry_msgs::Pose msg)
{
	actual_pose.position.x = msg.position.x;
	actual_pose.position.y = msg.position.y;
	actual_pose.position.z = msg.position.z;
	actual_pose.orientation.x = msg.orientation.x;
	actual_pose.orientation.y = msg.orientation.y;
	actual_pose.orientation.z = msg.orientation.z;
	actual_pose.orientation.w = msg.orientation.w;
    tf::pointMsgToTF(msg.position, Odom_pos);
    Odom_yaw = tf::getYaw(msg.orientation);
	robot_pose1.x = msg.position.x;
	robot_pose1.y = msg.position.y;
    robot_pose1.theta = Odom_yaw;   
}

void plot_car (const double& x, const double& y, const double& theta, int index, int pose_index) {
    std::map<std::string, std::string> style;      
    double radius = twowheel.radius;
    std::vector<double> x_, y_;
    switch (index)
    {
    case 1:
        style = {{"color", "blue"}, {"label", "Car-like Robot1 Original Trajectory"}};
        break;   
    case 2:
        style = {{"color", "red"}, {"label", "Car-like Robot1 Real Trajectory"}};
        break;  
    case 3:
        style = {{"color", "blue"},{"label", "Car-like Robot2 Original Trajectory"}};
        break;   
    case 4:
        style = {{"color", "red"}, {"label", "Car-like Robot2 Real Trajectory"}};
        break;   
    default:
        break;
    }
    Vec2d center(x, y);
    std::vector<Vec2d> vertex = car.rotatePoint(center, theta);
    vertex.push_back(vertex[0]);
    for (int j = 0; j < vertex.size(); j++) {
        x_.push_back(vertex[j].x());
        y_.push_back(vertex[j].y());
    }
    plt::plot(x_, y_, style);
    // if (pose_index == 0) {
    //     plt::legend();
    // }
}

void plot_traj(std::vector<std::vector<double>> traj, std::vector<double> x_plot, std::vector<double>  y_plot, std::vector<double> theta_plot, std::vector<std::vector<Vec2d>> obstacle, const std::string& title) {
	std::vector<double> x_orig_plot, y_orig_plot, theta_orig_plot;
	std::map<std::string, std::string> keywords;
	keywords["color"] = "black";
	for (int i = 0; i < traj.size(); i++) {
		x_orig_plot.push_back(traj[i][0]);
		y_orig_plot.push_back(traj[i][1]);
		theta_orig_plot.push_back(traj[i][2]);
    }
	for (int i = 0; i < x_orig_plot.size(); i++) {
		plot_car(x_orig_plot[i], y_orig_plot[i], theta_orig_plot[i], 1, i);
	}
	for (int i = 0; i < x_plot.size(); i++) {
		plot_car(x_plot[i], y_plot[i], theta_plot[i], 2, i);
	}
	for (const auto& obs : obstacle) {
		std::vector<double> obs_x, obs_y;
		for (const auto& vert : obs) {
			obs_x.push_back(vert.x() / 10);
			obs_y.push_back(vert.y() / 10);
		}
		plt::fill(obs_x, obs_y, keywords);
	}
	plt::title(title); 
}

std::vector<double> cartesian_controller(const geometry_msgs::Pose& actual_pose, const double target_pose_x, const double target_pose_y, const double target_pose_theta,
										 const double target_vel, const double target_angvel) {
	std::vector<double> control_cmd;
	control_cmd.resize(2);
	double theta_act, e_x, e_y, e_local_x, e_local_y, u_w, u_v;
	theta_act = tf::getYaw(actual_pose.orientation);
	e_x = target_pose_x - actual_pose.position.x;
	e_y = target_pose_y - actual_pose.position.y;
	e_local_x = cos(theta_act) * e_x + sin(theta_act) * e_y;
	e_local_y = -sin(theta_act) * e_x + cos(theta_act) * e_y;
	u_w = target_angvel + target_vel * (Ky * e_local_y + Ktheta * sin(target_pose_theta - theta_act));
	u_v = target_vel * cos(target_pose_theta - theta_act) + Kx * e_local_x;
	control_cmd[0] = u_v;
	control_cmd[1] = u_w;
	return control_cmd;
}

std::vector<double> compute_tarj_length(const std::vector<std::vector<double>> path) {
	std::vector<double> path_lengths = {0};
	for (int i = 1; i < path.size(); i++) {
		path_lengths.push_back(path_lengths[i-1] + hypot(path[i][0] - path[i-1][0], path[i][1] - path[i-1][1]));
	}
	return path_lengths;
}

void readtime_count(std::vector<int> &time_set, std::string filename) {
  try {
    // 从文件中加载 YAML 文档
    YAML::Node config = YAML::LoadFile(filename);

    // 确保文档是一个序列
    if (config.IsSequence()) {
        // 读取序列中的每个元素并存储到 std::vector<int> 中
        time_set = config.as<std::vector<int>>();
    } else {
        std::cerr << "Error: YAML document is not a sequence.\n";
    }
  } catch (const YAML::Exception& e) {
      std::cerr << "YAML Exception: " << e.what() << "\n";
  }
}

int main(int argc, char **argv)
{
	int robot_index = 0;
	std::vector<Trajectory_temp> traj_all_set;
    std::vector<int> time_set;
	std::vector<double> x1_orig_plot, y1_orig_plot, theta1_orig_plot, x2_orig_plot, y2_orig_plot, theta2_orig_plot, x3_orig_plot, y3_orig_plot, theta3_orig_plot, x4_orig_plot, y4_orig_plot, theta4_orig_plot, x5_orig_plot, y5_orig_plot, theta5_orig_plot,
	x6_orig_plot, y6_orig_plot, theta6_orig_plot, x7_orig_plot, y7_orig_plot, theta7_orig_plot, x8_orig_plot, y8_orig_plot, theta8_orig_plot, x9_orig_plot, y9_orig_plot, theta9_orig_plot, x10_orig_plot, y10_orig_plot, theta10_orig_plot;
	std::vector<double> x1_plot, y1_plot, theta1_plot, x2_plot, y2_plot, theta2_plot, x3_plot, y3_plot, theta3_plot, x4_plot, y4_plot, theta4_plot, x5_plot, y5_plot, theta5_plot,
	x6_plot, y6_plot, theta6_plot, x7_plot, y7_plot, theta7_plot, x8_plot, y8_plot, theta8_plot, x9_plot, y9_plot, theta9_plot, x10_plot, y10_plot, theta10_plot;
    double distances = 0.0, current_distances = 0.0, distances_old = 0.0, current_vels = 0.0, current_omegas = 0.0, current_thetas = 0.0;
	double target_pose_x, target_pose_y, target_pose_theta, target_vels, target_angvels;
	ros::Time timestamp_old;
	ros::Duration dt;
	auto traj_all = generate_traj("/home/weijian/Heterogeneous_formation/src/liom_local_planner/traj_result/sarah_traj.yaml");
    readtime_count(time_set, "/home/weijian/Heterogeneous_formation/src/liom_local_planner/traj_result/sarah_time.yaml");
	traj_all_set.resize(time_set.size());
    std::vector<FullStates> solution_all;
    solution_all.resize(time_set.size());
    for (int i = 0; i < solution_all.size(); i++) {
      int time_ind_start = 0;
      int time_ind_end = 0;
      for (int j = 0; j < i; j++) {
        time_ind_start += time_set[j];
      }
      for (int j = 0; j < i+1; j++) {
        time_ind_end += time_set[j];
      }
      for (int k = time_ind_start; k < time_ind_end; k++) {
        traj_all_set[i].push_back(traj_all[k]);
      }
    }
	std::vector<std::vector<std::vector<double>>> plan;
	plan.resize(1, std::vector<std::vector<double>>(traj_all.size(), std::vector<double>(5, 0.0)));
	for (int i = 0; i < traj_all.size(); i++) {
		plan[0][i][0] = traj_all[i].x;
		plan[0][i][1] = traj_all[i].y;
		plan[0][i][2] = traj_all[i].theta;
		plan[0][i][3] = traj_all[i].v;
		plan[0][i][4] = traj_all[i].omega;
	}
	target_pose_x = plan[robot_index][0][0];
	target_pose_y = plan[robot_index][0][1];
	target_pose_theta = plan[robot_index][0][2];
	current_thetas = plan[robot_index][0][2];
	// std::vector<double> x, y;
    // int j = 0;
	// for (int i = 0; i < traj_all_set[1].size(); i++) {
	// 	x.push_back(i * 0.2);
	// 	y.push_back(traj_all_set[1][i].v);
	// }
	// plt::plot(x, y);
	// plt::show();
	auto path_lengths = compute_tarj_length(plan[robot_index]);
	auto obstacle = generate_obs("/home/weijian/traj_tracking/src/obstacle.yaml");
	// initialization of ROS node
    ros::init(argc, argv, "robot_control_1");
    ros::NodeHandle n;
	integer_pub = n.advertise<std_msgs::Int32>("integer_topic", 10);
    ros::Subscriber sub_odometry_robot1 = n.subscribe("/mir_pose_simple", 1000 , robot_actual_pose1);
	ros::Rate loop_rate(20);
	robot1_cmd_pub = n.advertise<geometry_msgs::Twist>("/mobile_base_controller/cmd_vel", 1000);
	int path_index = 1;
	int path_index_old = 0;
	bool can_start = false;
	while (ros::ok()) {
		if (actual_pose.position.x == 0 && actual_pose.position.y == 0 && path_index == 1) {
			robot1_cmd.linear.x = 0.0;
			robot1_cmd.angular.z = 0.0;			
		}
		else {
			can_start = true;
			break;
		}
		ros::spinOnce();
		loop_rate.sleep();
	}
	timestamp_old = ros::Time::now(); 
	// main loop
	while(path_index < plan[robot_index].size() - 2 && can_start && ros::ok()){
		// compute distance to next point
		distances = path_lengths[path_index] - current_distances;
		// compute target velocity
		target_vels = plan[robot_index][path_index][3];
		// target_vels = KP_vel * distances * control_rate;
		// check if next point is reached
		if (distances <= fabs(target_vels) * loop_rate.expectedCycleTime().toSec()) {
			path_index += 1;
			distances_old  = distances;
		}
		if (distances > distances_old && path_index != path_index_old) {
			path_index += 1;
		}
		// if (hypot(robot_pose1.x - target_pose_x, robot_pose1.y - target_pose_y) < 0.1 && pow(sin(target_pose_theta) - sin(robot_pose1.theta), 2) +
		// pow(cos(target_pose_theta) - cos(robot_pose1.theta), 2) < 0.05) {
		// 	path_index += 1;
		// }
		// if (hypot(robot_pose1.x - plan[robot_index].back()[0], robot_pose1.y - plan[robot_index].back()[1]) < 0.5 && fabs(robot_pose1.theta - plan[robot_index].back()[2]) < 0.1) {
		// 	break;
		// }
		distances_old = distances;
		path_index_old = path_index;
		// compute next target point
		target_pose_x = plan[robot_index][path_index][0];
		target_pose_y = plan[robot_index][path_index][1];
		target_pose_theta = plan[robot_index][path_index][2];
		if (target_pose_theta > M_PI) {
			target_pose_theta -= 2 * M_PI;
		}
		else if (target_pose_theta < -M_PI) {
			target_pose_theta += 2 * M_PI;
		}
		target_angvels = plan[robot_index][path_index][4];
		dt = ros::Time::now() - timestamp_old;
		current_vels = target_vels;
		current_omegas = target_angvels;
		current_thetas += target_angvels * dt.toSec();
		current_distances += fabs(target_vels) * dt.toSec();
		timestamp_old = ros::Time::now();
		// if (actual_pose.position.x == 0 && actuaxl_pose.position.y == 0 && path_index == 1) {
		// 	robot1_cmd.linear.x = 0.0;
		// 	robot1_cmd.angular.z = 0.0;			
		// }
		// else {
			robot1_cmd.linear.x = target_vels;
			robot1_cmd.angular.z = target_angvels;
			auto cmd = cartesian_controller(actual_pose, target_pose_x, target_pose_y, target_pose_theta, target_vels, target_angvels);
			robot1_cmd.linear.x = cmd[0];
			robot1_cmd.angular.z = cmd[1];
		// }
		
		if (robot_pose1.x != 0 && robot_pose1.y != 0 && robot_pose1.theta != 0) {
			x1_plot.push_back(robot_pose1.x);
			y1_plot.push_back(robot_pose1.y);
			theta1_plot.push_back(robot_pose1.theta);
		}
        std_msgs::Int32 msg;
        msg.data = path_index;
        integer_pub.publish(msg);
		robot1_cmd_pub.publish(robot1_cmd);
		if (fabs(robot1_cmd.linear.x) < 1e-4 && path_index > 1) {
			path_index += 1;
		}
		ros::spinOnce();
		loop_rate.sleep();
	}
	plt::clf();
    plot_traj(plan[robot_index], x1_plot, y1_plot, theta1_plot, obstacle, "Diff1 trajectory");
	plt::show();
    return 0;
}