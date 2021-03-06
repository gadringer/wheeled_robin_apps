/** 
 * Node that uses the WheeledRobin robot to perform a guided tour
 * with multiple stops. It provides interfaces to nodes for 
 * navigation, speech synthesis, physical user interfaces and displaying 
 * graphics and uses a state machine to manage the workflow.
 * 
 * JKU Linz
 * Institute for Robotics
 * Alexander Reiter, Armin Steinhauser
 * December 2013
 */

#include <ros/ros.h>
#include <tf/transform_listener.h>
#include <std_msgs/String.h>
#include <std_msgs/Bool.h>
#include <geometry_msgs/PoseStamped.h>
#include <actionlib/client/simple_action_client.h>
#include <actionlib_msgs/GoalStatus.h>
#include <move_base_msgs/MoveBaseAction.h>
#include <stdio.h>
#include <string>
#include <sstream> 

// global variables
ros::Time last_button_msg_time;	// last time a button has been pressed
int current_goal = 0; // currently active goal

// function declarations
void createPoseFromParams(std::string param_name, geometry_msgs::PoseStamped* pose);
void buttonCb(std_msgs::Bool msg);
bool last_button_state = false;

// define state names
enum state {
	START, RETURN_START, WAIT_PERSON, APPROACH_PERSON, ASK_PERSON, WAIT_BUTTON_TOUR, APPROACH_PRESENTATION, PRESENT, ASK_REPETITION, WAIT_BUTTON_REPETITION, TALK_BYE
};

int main(int argc, char** argv) {
	// init ROS
	ros::init(argc, argv, "state_machine");
	ros::NodeHandle nh;
	ros::Rate loop_rate(10);
	
	// set up publishers
	ros::Publisher speech_pub = nh.advertise<std_msgs::String>("/speech", 1);
	
	// set up subscribers
	ros::Subscriber button_sub = nh.subscribe<std_msgs::Bool>("/pushed", 1, buttonCb);
	
	// client for navigation goals
	actionlib::SimpleActionClient<move_base_msgs::MoveBaseAction> client("move_base/goal", true);
	//client.waitForServer();
	
	// init state machine
	state st = START;
	
	// tf listener
	tf::TransformListener listener;
	tf::StampedTransform transform;
	
	// read parameters
	double base_range = 0.; // range around base to trigger question for tour
	ros::param::get("base_range", base_range);
	base_range = base_range*base_range; // cheaper to compare (no sqrt)
	std::string person_frame; // name of frame to person or group of persons
	ros::param::get("person_frame", person_frame);
	double button_duration; // time for user to interact with robot
	ros::param::get("button_duration", button_duration);
	std::string goal_basename; // basename for goal parameter names (within goals namespace)
	ros::param::get("goal_basename", goal_basename);
	std::string video_path; // path to folder with presentation videos
	ros::param::get("video_path", video_path);
	
	// init time
	ros::Time ask_time;
	last_button_msg_time = ros::Time(0);
	
	move_base_msgs::MoveBaseGoal goal;
	
	while(ros::ok()) {
		switch(st) {
			case START: {
				createPoseFromParams("start", &(goal.target_pose));
				client.sendGoal(goal);
				st = RETURN_START;
				ROS_INFO("Switching to state %d", st);
				break;
			}
			case RETURN_START: {
				if (client.getState() == actionlib::SimpleClientGoalState::SUCCEEDED) {
					st = WAIT_PERSON;
					ROS_INFO("Reached start position");
					ROS_INFO("Switching to state %d", st);
				}
				current_goal = 0;
				break;
			}
			case WAIT_PERSON: {
				// tf from robot to base footprint of person at /person
				try {
					listener.lookupTransform("/base_footprint", person_frame, ros::Time(0), transform);
				} catch (tf::TransformException ex) {
					ROS_ERROR("%s", ex.what());
					break;
				}
				tf::Vector3 distance = transform.getOrigin();
				double person_range = distance.length2();
				
				// check if person is within range of base to trigger question for tour
				if(person_range <= base_range) { // person within range
					createPoseFromParams("base", &(goal.target_pose));
					client.sendGoal(goal);
					st = APPROACH_PERSON;
				}
				break;
			}
			case APPROACH_PERSON: {
				if (client.getState() == actionlib::SimpleClientGoalState::SUCCEEDED) { // goal reached
					st = ASK_PERSON;
					ROS_INFO("Reached person");
					ROS_INFO("Switching to state %d", st);
				}
				break;
			}
			case ASK_PERSON: {
				std_msgs::String ask_tour;
				ask_tour.data = "Please touch me if you would like to take a tour.";
				speech_pub.publish(ask_tour);
				ask_time = ros::Time::now();
				st = WAIT_BUTTON_TOUR;
				ROS_INFO("Switching to state %d", st);
				break;
			}
			case WAIT_BUTTON_TOUR: {
				if(ros::Time::now() - ask_time > ros::Duration(button_duration)) {
					if(last_button_msg_time > ask_time && last_button_state) { // tour requested
						st = APPROACH_PRESENTATION;
						std::stringstream ss;
						ss << goal_basename;
						ss << current_goal;
						createPoseFromParams(ss.str().c_str(), &(goal.target_pose));
						client.sendGoal(goal);
						ROS_INFO("Tour requested");
						ROS_INFO("Switching to state %d", st);
					} 
				} else { // no tour requested
					std_msgs::String say_nothanks;
					say_nothanks.data = "Thanks for wasting my time. Good bye.";
					speech_pub.publish(say_nothanks);
					st = RETURN_START;
					ROS_INFO("No tour requested.");
					ROS_INFO("Switching to state %d", st);
				}
				break;
			}
			case APPROACH_PRESENTATION: {
				if (client.getState() == actionlib::SimpleClientGoalState::SUCCEEDED) { // goal reached
					st = PRESENT;
					ROS_INFO("Switching to state %d", st);
				}
				break;
			case PRESENT:
				std_msgs::String video_path;
				std::stringstream ss;
				ss << "/goals/";
				ss << goal_basename;
				ss << current_goal;
				ss << "/folder";
				ros::param::get("goal_basename", video_path.data);
				video_path.data = video_path.data + video_path.data;
				break;
				//TODO check if presentation is finished, switch to ASK_REPETITION
			}
			case ASK_REPETITION: {
				std_msgs::String ask_pres;
				ask_pres.data = "Would you like me to repeat the presentation?";
				speech_pub.publish(ask_pres);
				ask_time = ros::Time::now();
				st = WAIT_BUTTON_REPETITION;
				ROS_INFO("Switching to state %d", st);
				break;
			}
			case WAIT_BUTTON_REPETITION: {
				if(ros::Time::now() - ask_time > ros::Duration(button_duration)) {
					if(last_button_msg_time > ask_time && last_button_state) { // repetition requested
						st = PRESENT;
						ROS_INFO("Button pressed");
						ROS_INFO("Switching to state %d", st);
					} 
				} else { // no repetition requested
					current_goal++;
					std::stringstream ss;
					ss << goal_basename;
					ss << current_goal;
					if(ros::param::has(ss.str().c_str())) { // another goal exists
						std::stringstream ss;
						ss << goal_basename;
						ss << current_goal;
						createPoseFromParams(ss.str().c_str(), &(goal.target_pose));
						client.sendGoal(goal);
						st = APPROACH_PRESENTATION;
						std_msgs::String say_continue;
						say_continue.data = "Okay, lets continue with the next stop.";
						speech_pub.publish(say_continue);
						ROS_INFO("Switching to state %d", st);
					} else {
						st = TALK_BYE;
						ROS_INFO("Switching to state %d", st);
					}
				}
				break;
			}
			case TALK_BYE: {
				std_msgs::String say_bye;
				say_bye.data = "I hope you enjoyed the tour. Thanks for visiting us. Please follow me back to the start.";
				speech_pub.publish(say_bye);
				st = RETURN_START;
				ROS_INFO("Switching to state %d", st);
				break;
			}
			default:
				;
		}
		
		ros::spinOnce();
	}
	
	return 0;
}

/** 
 * Function that extracts a PoseStamped representation from the
 * parameter group param_name of the goals namespace and stores it in 
 * the provided.
 * 
 * INPUT:	std::string param_name: name of the PoseStamped parameter
 * 			geometry_msgs::PoseStamped* pose: reference to the pose that
 * 										    	to be changed
 * OUTPUT:	none
 */
void createPoseFromParams(std::string param_name, geometry_msgs::PoseStamped* pose) {
	pose->header.stamp = ros::Time::now();
	ros::param::get("/goals/"+param_name+"/x", pose->pose.position.x);
	ros::param::get("/goals/"+param_name+"/y", pose->pose.position.y);
	ros::param::get("/goals/"+param_name+"/z", pose->pose.position.z);
	ros::param::get("/goals/"+param_name+"/q1", pose->pose.orientation.x);
	ros::param::get("/goals/"+param_name+"/q2", pose->pose.orientation.y);
	ros::param::get("/goals/"+param_name+"/q3", pose->pose.orientation.z);
	ros::param::get("/goals/"+param_name+"/q4", pose->pose.orientation.w);
}

/**
 * Callback function that received a boolean ROS message an stores
 * the content and the receive time in global variables as the bool 
 * message does not contain a header structure.
 * 
 * INPUT:	std_msgs::bool msg: message
 * 
 * OUTPUT:	none
 */
 void buttonCb(std_msgs::Bool msg) {
	 last_button_msg_time = ros::Time::now();
	 last_button_state = msg.data;
 }
