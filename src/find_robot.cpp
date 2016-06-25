//Size of the image is 640*360
#include "ros/ros.h"
#include "geometry_msgs/PoseStamped.h"
#include "nav_msgs/Odometry.h"
#include "sensor_msgs/Image.h"
#include <image_transport/image_transport.h>
#include <cv_bridge/cv_bridge.h>
#include <sensor_msgs/image_encodings.h>
#include "ardrone_autonomy/navdata_altitude.h"
#include "image_process.h"

using namespace cv;
using namespace std;

class FindRobot
{
public: 
	FindRobot();
private:
	ros::NodeHandle n;
	ros::Subscriber image_sub;

	IplImage *source_image;
	IplImage *source_image_resized;
	IplImage *ROI_image;
	IplImage temp;
	IplConvKernel * myModel;
	double target_image[10][2];
	double white_target[1][2];
	double black_target[1][2];
	int ROI_width;
	int ROI_height;

	void imageCallback(const sensor_msgs::Image &msg);
};

FindRobot::FindRobot()
{
	image_sub = n.subscribe("/videofile/image_raw", 1, &FindRobot::imageCallback,this);
	source_image_resized = cvCreateImage(cvSize(640,360),IPL_DEPTH_8U, 3);
	myModel = cvCreateStructuringElementEx(3,3,2,2,CV_SHAPE_ELLIPSE);
}

void FindRobot::imageCallback(const sensor_msgs::Image &msg)
{
	float theta;
	cv_bridge::CvImagePtr cv_ptr;
	cv_bridge::CvImage cv_to_ros;
	cv_ptr = cv_bridge::toCvCopy(msg, sensor_msgs::image_encodings::BGR8);

	temp = (IplImage)(cv_ptr->image);
	source_image = &temp;
	cvResize(source_image, source_image_resized);
	
	IplImage *image_threshold = cvCreateImage(cvGetSize(source_image_resized),IPL_DEPTH_8U, 1);

	Color_Detection_Reverse(source_image_resized, image_threshold, 5, 300, 0.5, 1, 40, 255);	
	cvErode(image_threshold, image_threshold, myModel, 1);
	cvDilate(image_threshold, image_threshold, myModel, 1);

	cvShowImage("Threshold Image", image_threshold);
	waitKey(1);

	//find the contours
	CvMemStorage* storage = cvCreateMemStorage(0);  
	CvSeq* contour = 0;  
	cvFindContours(image_threshold, storage, &contour, sizeof(CvContour), CV_RETR_EXTERNAL, CV_CHAIN_APPROX_SIMPLE);
	cvZero(image_threshold);

	//get rid of the contours which don't meet the requirement
	int count = 0; 
	for( ; contour != 0; contour = contour->h_next )
	{  
		double tmparea = fabs(cvContourArea(contour));  
	 	// printf("Area:%f\n", tmparea);
	 	if (tmparea < 1000)  
	 	{  
	 		cvSeqRemove(contour, 0); 
	 		continue; 
	 	} 

		CvRect aRect = cvBoundingRect(contour, 0 );
		if((double)aRect.height/(double)aRect.width<0.9)
		{    
			cvSeqRemove(contour,0); 
			continue;    
		}
		if((double)aRect.width/(double)aRect.height<0.9)
		{    
			cvSeqRemove(contour,0); 
			continue;    
		}
		
		//draw the contours
		cvDrawContours(image_threshold, contour, CV_RGB( 255, 255, 255), CV_RGB( 255, 255, 255), 0, 1, 8 );
		//find the center of the contours
		target_image[count][0] = (aRect.x + aRect.x + aRect.width) / 2;
		target_image[count][1] = (aRect.y + aRect.y + aRect.height) / 2;
		ROI_width = aRect.width;
		ROI_height = aRect.height;
		//draw a rectangle of the contour region
		cvRectangle(image_threshold, cvPoint(aRect.x, aRect.y), cvPoint(aRect.x + aRect.width, aRect.y + aRect.height),CV_RGB(255,255, 255), 1, 8, 0);
		cvRectangle(source_image_resized, cvPoint(aRect.x, aRect.y), cvPoint(aRect.x + aRect.width, aRect.y + aRect.height),CV_RGB(0,255, 0), 3, 8, 0);
		
		count++;		
	} 

	if(count > 0){
		CvRect rect;
		rect.x = target_image[0][0] - ROI_width/2;
		rect.y = target_image[0][1] - ROI_height/2;
		rect.width = ROI_width;
		rect.height = ROI_height;
		ROI_image = cvCreateImage(cvSize(ROI_width, ROI_height), IPL_DEPTH_8U, 3);
		//get the ROI region
		cvSetImageROI(source_image_resized,rect);
		cvCopy(source_image_resized,ROI_image);  
		cvResetImageROI(source_image_resized); 

		IplImage* gray_ROI = cvCreateImage(cvGetSize(ROI_image), IPL_DEPTH_8U, 1); 
		IplImage* white_ROI = cvCreateImage(cvGetSize(ROI_image), IPL_DEPTH_8U, 1);   
		IplImage* black_ROI = cvCreateImage(cvGetSize(ROI_image), IPL_DEPTH_8U, 1); 
     	cvCvtColor(ROI_image,gray_ROI,CV_BGR2GRAY);
     	cvSmooth(gray_ROI,gray_ROI,CV_MEDIAN,5,5);

     	cvThreshold(gray_ROI,white_ROI,190,255,CV_THRESH_BINARY);
     	cvThreshold(gray_ROI,black_ROI,3,255,CV_THRESH_BINARY_INV);

     	bool is_white = false;
     	bool is_black = false;
     	double white_area, black_area;

     	is_white = find_circle_center(ROI_image, white_ROI, white_target[0][0], white_target[0][1], white_area);
     	is_black = find_circle_center(ROI_image, black_ROI, black_target[0][0], black_target[0][1], black_area);

     	if(is_white && is_black){
     		ROS_INFO("Area white:%f",white_area);
     		ROS_INFO("Area black:%f",black_area);
     		ROS_INFO("x:%f, y:%f",white_target[0][0]-black_target[0][0], white_target[0][1]-black_target[0][1]);
     	}

     	cvShowImage("ROI Image", ROI_image);
		waitKey(1);

		cvReleaseImage(&gray_ROI);
		cvReleaseImage(&white_ROI);
		cvReleaseImage(&black_ROI);
	}
	
	cvShowImage("Origin Image", source_image_resized);
	waitKey(1);
	
	cvReleaseImage(&image_threshold);
	cvReleaseMemStorage(&storage);
}


int main(int argc, char **argv)
{
	ros::init(argc, argv, "find_robot");
	FindRobot Find_robot;
	ros::spin();
}