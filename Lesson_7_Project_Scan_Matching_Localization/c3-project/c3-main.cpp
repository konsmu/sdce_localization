// student: Konstantin Mueller

#include <carla/client/Client.h>
#include <carla/client/ActorBlueprint.h>
#include <carla/client/BlueprintLibrary.h>
#include <carla/client/Map.h>
#include <carla/geom/Location.h>
#include <carla/geom/Transform.h>
#include <carla/client/Sensor.h>
#include <carla/sensor/data/LidarMeasurement.h>
#include <thread>

#include <carla/client/Vehicle.h>

//pcl code
//#include "render/render.h"

namespace cc = carla::client;
namespace cg = carla::geom;
namespace csd = carla::sensor::data;

using namespace std::chrono_literals;
using namespace std::string_literals;

using namespace std;

#include <string>
#include <pcl/io/pcd_io.h>
#include <pcl/visualization/pcl_visualizer.h>
#include <pcl/filters/voxel_grid.h>
#include "helper.h"
#include <sstream>
#include <chrono> 
#include <ctime> 
#include <pcl/registration/icp.h>
#include <pcl/registration/ndt.h>
#include <pcl/console/time.h>   // TicToc

PointCloudT pclCloud;
cc::Vehicle::Control control;
std::chrono::time_point<std::chrono::system_clock> currentTime;
vector<ControlState> cs;

bool refresh_view = false;
void keyboardEventOccurred(const pcl::visualization::KeyboardEvent &event, void* viewer)
{

  	//boost::shared_ptr<pcl::visualization::PCLVisualizer> viewer = *static_cast<boost::shared_ptr<pcl::visualization::PCLVisualizer> *>(viewer_void);
	if (event.getKeySym() == "Right" && event.keyDown()){
		cs.push_back(ControlState(0, -0.02, 0));
  	}
	else if (event.getKeySym() == "Left" && event.keyDown()){
		cs.push_back(ControlState(0, 0.02, 0)); 
  	}
  	if (event.getKeySym() == "Up" && event.keyDown()){
		cs.push_back(ControlState(0.1, 0, 0));
  	}
	else if (event.getKeySym() == "Down" && event.keyDown()){
		cs.push_back(ControlState(-0.1, 0, 0)); 
  	}
	if(event.getKeySym() == "a" && event.keyDown()){
		refresh_view = true;
	}
}

void Accuate(ControlState response, cc::Vehicle::Control& state){

	if(response.t > 0){
		if(!state.reverse){
			state.throttle = min(state.throttle+response.t, 1.0f);
		}
		else{
			state.reverse = false;
			state.throttle = min(response.t, 1.0f);
		}
	}
	else if(response.t < 0){
		response.t = -response.t;
		if(state.reverse){
			state.throttle = min(state.throttle+response.t, 1.0f);
		}
		else{
			state.reverse = true;
			state.throttle = min(response.t, 1.0f);

		}
	}
	state.steer = min( max(state.steer+response.s, -1.0f), 1.0f);
	state.brake = response.b;
}

void drawCar(Pose pose, int num, Color color, double alpha, pcl::visualization::PCLVisualizer::Ptr& viewer){

	BoxQ box;
	box.bboxTransform = Eigen::Vector3f(pose.position.x, pose.position.y, 0);
    box.bboxQuaternion = getQuaternion(pose.rotation.yaw);
    box.cube_length = 4;
    box.cube_width = 2;
    box.cube_height = 2;
	renderBox(viewer, box, num, color, alpha);
}

// This code was reused from Lesson 6 (Exercise NDT Alignment)
// Thic function uses NDT to align the scanned point cloud with the map, returning the transformation matrix to get the pose if the ego vehicle. 
Eigen::Matrix4d NDT(pcl::NormalDistributionsTransform<pcl::PointXYZ, pcl::PointXYZ> ndt, PointCloudT::Ptr source, Pose startingPose, int iterations){

	//1. Transform the source to the startingPose: align source with starting pose
	Eigen::Matrix4f init_guess = transform3D(startingPose.rotation.yaw, startingPose.rotation.pitch, startingPose.rotation.roll, startingPose.position.x, startingPose.position.y, startingPose.position.z).cast<float>();

	//2. Set the ndt object's values
  	// Setting max number of registration iterations.
  	ndt.setMaximumIterations(iterations);
	ndt.setInputSource(source);
  	
	//3. Call align on the ndt object	
	pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_ndt(new pcl::PointCloud<pcl::PointXYZ>);
  	ndt.align(*cloud_ndt, init_guess);

	//cout << "Normal Distributions Transform has converged:" << ndt.hasConverged () << " score: " << ndt.getFitnessScore () <<  " time: " << time.toc() <<  " ms" << endl;

	//4. If ndt converged get the ndt objects output transform, return the transform
	Eigen::Matrix4d transformation_matrix = ndt.getFinalTransformation().cast<double>();

	return transformation_matrix;
}

// This code was reused from Lesson 6 (Exercise ICP Alignment)
// Thic function uses ICP to align the scanned point cloud with the map, returning the transformation matrix to get the pose if the ego vehicle. 
Eigen::Matrix4d ICP(PointCloudT::Ptr target, PointCloudT::Ptr source, Pose startingPose, int iterations){

  	// 1. Create the transform of the starting position and align source with starting pose
  	Eigen::Matrix4d initTransform = transform3D(startingPose.rotation.yaw, startingPose.rotation.pitch, startingPose.rotation.roll, startingPose.position.x, startingPose.position.y, startingPose.position.z);
  	PointCloudT::Ptr transformSource(new PointCloudT); 
  	pcl::transformPointCloud(*source, *transformSource, initTransform);

	// 2. Create ICP object and set parameters as in Lesson 6
  	pcl::IterativeClosestPoint<PointT, PointT> icp;
  	icp.setMaximumIterations(iterations);
  	icp.setInputSource(transformSource);
  	icp.setInputTarget(target);
	//icp.setMaxCorrespondenceDistance (2);
	//icp.setTransformationEpsilon(0.001);
	//icp.setEuclideanFitnessEpsilon(0.05);
	//icp.setRANSACOutlierRejectionThreshold(10);

	// 3. Create ICP output point cloud and align it using the ICP algorithm
  	PointCloudT::Ptr cloud_icp (new PointCloudT); 
  	icp.align(*cloud_icp);

	/*
	// We do not check for convergence
  	if (icp.hasConverged ())
  	{
  		//std::cout << "\nICP has converged, score is " << icp.getFitnessScore () << std::endl;
  		transformation_matrix = icp.getFinalTransformation ().cast<double>();
  		transformation_matrix =  transformation_matrix * initTransform;
  		//print4x4Matrix(transformation_matrix);
  		
  		PointCloudT::Ptr corrected_scan (new PointCloudT);
  		pcl::transformPointCloud (*source, *corrected_scan, transformation_matrix);
  		if( count == 1)
  			renderPointCloud(viewer, corrected_scan, "corrected_scan_"+to_string(count), Color(0,1,1)); // render corrected scan
		
  		return transformation_matrix;
  	}
	else
  		cout << "WARNING: ICP did not converge" << endl;
	*/

	// 4. Get the final transformation and calculate the localized pose using matrix multiplication of transformation matrices
	Eigen::Matrix4d transformation_matrix = icp.getFinalTransformation().cast<double>();
  	transformation_matrix =  transformation_matrix * initTransform;
  	return transformation_matrix;
}

int main(){

	auto client = cc::Client("localhost", 2000);
	client.SetTimeout(2s);
	auto world = client.GetWorld();

	auto blueprint_library = world.GetBlueprintLibrary();
	auto vehicles = blueprint_library->Filter("vehicle");

	auto map = world.GetMap();
	auto transform = map->GetRecommendedSpawnPoints()[1];
	auto ego_actor = world.SpawnActor((*vehicles)[12], transform);

	//Create lidar
	auto lidar_bp = *(blueprint_library->Find("sensor.lidar.ray_cast"));
	// CANDO: Can modify lidar values to get different scan resolutions
	lidar_bp.SetAttribute("upper_fov", "15");
    lidar_bp.SetAttribute("lower_fov", "-25");
    lidar_bp.SetAttribute("channels", "32");
    lidar_bp.SetAttribute("range", "30");
	lidar_bp.SetAttribute("rotation_frequency", "60");
	lidar_bp.SetAttribute("points_per_second", "500000");

	auto user_offset = cg::Location(0, 0, 0);
	auto lidar_transform = cg::Transform(cg::Location(-0.5, 0, 1.8) + user_offset);
	auto lidar_actor = world.SpawnActor(lidar_bp, lidar_transform, ego_actor.get());
	auto lidar = boost::static_pointer_cast<cc::Sensor>(lidar_actor);
	bool new_scan = true;
	std::chrono::time_point<std::chrono::system_clock> lastScanTime, startTime;

	pcl::visualization::PCLVisualizer::Ptr viewer (new pcl::visualization::PCLVisualizer ("3D Viewer"));
  	viewer->setBackgroundColor (0, 0, 0);
	viewer->registerKeyboardCallback(keyboardEventOccurred, (void*)&viewer);

	auto vehicle = boost::static_pointer_cast<cc::Vehicle>(ego_actor);
	Pose pose(Point(0,0,0), Rotate(0,0,0));

	// Load map
	PointCloudT::Ptr mapCloud(new PointCloudT);
  	pcl::io::loadPCDFile("map.pcd", *mapCloud);
  	cout << "Loaded " << mapCloud->points.size() << " data points from map.pcd" << endl;
	renderPointCloud(viewer, mapCloud, "map", Color(0,0,1)); 

	typename pcl::PointCloud<PointT>::Ptr cloudFiltered (new pcl::PointCloud<PointT>);
	typename pcl::PointCloud<PointT>::Ptr scanCloud (new pcl::PointCloud<PointT>);

	lidar->Listen([&new_scan, &lastScanTime, &scanCloud](auto data){

		if(new_scan){
			auto scan = boost::static_pointer_cast<csd::LidarMeasurement>(data);
			for (auto detection : *scan){
				if((detection.x*detection.x + detection.y*detection.y + detection.z*detection.z) > 8.0){
					//pclCloud.points.push_back(PointT(detection.x, detection.y, detection.z));
					// Transformation because of the changed conditions in the CARLA map (see Udacity mentor question)
					pclCloud.points.push_back(PointT(-detection.y, detection.x, detection.z));
				}
			}
			if(pclCloud.points.size() > 5000){ // CANDO: Can modify this value to get different scan resolutions
				lastScanTime = std::chrono::system_clock::now();
				*scanCloud = pclCloud;
				new_scan = false;
			}
		}
	});


	/// This code was reused from Lesson 6 (Exercise NDT Alignment)

	// 1. Create an NDT object and set object attributes
	pcl::NormalDistributionsTransform<pcl::PointXYZ, pcl::PointXYZ> ndt;
  	// Setting maximum step size for More-Thuente line search.
  	ndt.setStepSize(1);
  	// Setting Resolution of NDT grid structure (VoxelGridCovariance).
  	ndt.setResolution(1);
	// Setting minimum transformation difference for termination condition.
  	ndt.setTransformationEpsilon(0.0001);
	// Setting input point cloud
  	ndt.setInputTarget(mapCloud);

	///
	
	Pose poseRef(Point(vehicle->GetTransform().location.x, vehicle->GetTransform().location.y, vehicle->GetTransform().location.z), Rotate(vehicle->GetTransform().rotation.yaw * pi/180, vehicle->GetTransform().rotation.pitch * pi/180, vehicle->GetTransform().rotation.roll * pi/180));
	double maxError = 0;

	//int num_scan = 0;

	while (!viewer->wasStopped())
  	{
		while(new_scan){
			std::this_thread::sleep_for(0.1s);
			world.Tick(1s);
		}
		if(refresh_view){
			viewer->setCameraPosition(pose.position.x, pose.position.y, 60, pose.position.x+1, pose.position.y+1, 0, 0, 0, 1);
			refresh_view = false;
		}
		
		viewer->removeShape("box0");
		viewer->removeShape("boxFill0");
		Pose truePose = Pose(Point(vehicle->GetTransform().location.x, vehicle->GetTransform().location.y, vehicle->GetTransform().location.z), Rotate(vehicle->GetTransform().rotation.yaw * pi/180, vehicle->GetTransform().rotation.pitch * pi/180, vehicle->GetTransform().rotation.roll * pi/180)) - poseRef;
		drawCar(truePose, 0,  Color(1,0,0), 0.7, viewer);
		double theta = truePose.rotation.yaw;
		double stheta = control.steer * pi/4 + theta;
		viewer->removeShape("steer");
		renderRay(viewer, Point(truePose.position.x+2*cos(theta), truePose.position.y+2*sin(theta),truePose.position.z),  Point(truePose.position.x+4*cos(stheta), truePose.position.y+4*sin(stheta),truePose.position.z), "steer", Color(0,1,0));


		ControlState accuate(0, 0, 1);
		if(cs.size() > 0){
			accuate = cs.back();
			cs.clear();

			Accuate(accuate, control);
			vehicle->ApplyControl(control);
		}

  		viewer->spinOnce ();
		
		if(!new_scan){
			
			new_scan = true;

			/*
			// According to the task description, the ground truth is used as the initial pose.
			//num_scan += 1;
			if(num_scan == 1){
				pose.rotation = truePose.rotation;
				pose.position = truePose.position;
			}
			*/

			// TODO: (Filter scan using voxel filter)
			// Create new voxel grid filter object and set input point cloud and resolution
			pcl::VoxelGrid<PointT> vg;
			typename pcl::PointCloud<PointT>::Ptr cloudFiltered (new pcl::PointCloud<PointT>);
			vg.setInputCloud(scanCloud);
			double res = 0.5;
			vg.setLeafSize(res, res, res);
			vg.filter(*cloudFiltered);

			// Render the filtered point cloud
			renderPointCloud(viewer, cloudFiltered, "filter", Color(0,1,0) );
			
			//Eigen::Matrix4d initTransform = transform3D(pose.rotation.yaw, pose.rotation.pitch, pose.rotation.roll, pose.position.x, pose.position.y, pose.position.z);
			//PointCloudT::Ptr transformedSource (new PointCloudT); 
			//pcl::transformPointCloud (*cloudFiltered, *transformedSource, initTransform);
			//renderPointCloud(viewer, transformedSource, "transformedSource", Color(0,1,1) );

			//cloudFiltered = scanCloud;

			// TODO: Find pose transform by using ICP or NDT matching
			// pose = ....
			
			// I experemented with both NDT and ICP, in the following I use NDT
			Eigen::Matrix4d align_transform = NDT(ndt, cloudFiltered, pose, 100);
			//Eigen::Matrix4d align_transform = ICP(mapCloud, cloudFiltered, pose, 4);

  			pose = getPose(align_transform);

			// TODO: Transform scan so it aligns with ego's actual pose and render that scan
			
			// Create a new object to hold the corrected point cloud after transformation
			PointCloudT::Ptr transformed_scan(new PointCloudT);
			pcl::transformPointCloud(*cloudFiltered, *transformed_scan, align_transform);

			// TODO: Change `scanCloud` below to your transformed scan
			viewer->removePointCloud("scan");
			renderPointCloud(viewer, transformed_scan, "scan", Color(1,0,0) );

			viewer->removeAllShapes();
			drawCar(pose, 1, Color(0,1,0), 0.35, viewer);
          
          	double poseError = sqrt( (truePose.position.x - pose.position.x) * (truePose.position.x - pose.position.x) + (truePose.position.y - pose.position.y) * (truePose.position.y - pose.position.y) );
			if(poseError > maxError)
				maxError = poseError;
			double distDriven = sqrt( (truePose.position.x) * (truePose.position.x) + (truePose.position.y) * (truePose.position.y) );
			viewer->removeShape("maxE");
			viewer->addText("Max Error: "+to_string(maxError)+" m", 200, 100, 32, 1.0, 1.0, 1.0, "maxE",0);
			viewer->removeShape("derror");
			viewer->addText("Pose error: "+to_string(poseError)+" m", 200, 150, 32, 1.0, 1.0, 1.0, "derror",0);
			viewer->removeShape("dist");
			viewer->addText("Distance: "+to_string(distDriven)+" m", 200, 200, 32, 1.0, 1.0, 1.0, "dist",0);

			if(maxError > 1.2 || distDriven >= 170.0 ){
				viewer->removeShape("eval");
			if(maxError > 1.2){
				viewer->addText("Try Again", 200, 50, 32, 1.0, 0.0, 0.0, "eval",0);
			}
			else{
				viewer->addText("Passed!", 200, 50, 32, 0.0, 1.0, 0.0, "eval",0);
			}
		}

			pclCloud.points.clear();
		}
  	}
	return 0;
}