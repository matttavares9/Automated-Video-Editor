#include "Clip.h"
#include <opencv2/imgcodecs.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/video/tracking.hpp>
#include <iostream>
#include <math.h>
#include <cmath>
#include <opencv2/objdetect.hpp>
#if CV_VERSION_MAJOR >= 5
#include <opencv2/xobjdetect.hpp>
#endif
#include <random>
#include <string>
#include <filesystem>
#include <stdexcept>
#include <algorithm>

using namespace cv;
using namespace std;

//Helper function which determines whether an image is blurry based on the Laplacian variance.
//"image" is the input image to be checked for blurriness.
//"threshold" is the threshold value to classify an image as blurry or not.
// Returns true if the image is blurry, false otherwise.
double blurVariance(const Mat& image) {
	Mat gray_image;
	cvtColor(image, gray_image, COLOR_BGR2GRAY);

	//Laplacian is a 2nd derivative that is used to highlight areas of rapid intensity chages with respect to its cordinates, which indicates blurriness.
	//This method is much faster than alternates such as Optical Flow for detecting motion related blurriness between clips.
	Mat laplacian_image; //Laplacian image
	Laplacian(gray_image, laplacian_image, CV_64F);


	Scalar mean;
	Scalar stddev;
	meanStdDev(laplacian_image, mean, stddev); //Calculates the mean and standa4rd deviation of the pixel values in the image. Informs about the distribution of intensities in the image

	double variance = stddev.val[0] * stddev.val[0]; //Variance measures the spread of the pixel intensities in the image. Squaring std gives this value.
	//Variance is important to detect blurriness. High frequency means theres a large presence of edges, textures, and finer details. Blurry images have LESS variance, looking smooth and supressed.
	return variance;
}

bool isBlurry(Mat image, double threshold = Clip::DEFAULT_BLUR_THRESHOLD) {
	return blurVariance(image) < threshold;
}

//Finds a non-blurry section within the given clip.
//"length" is the length of the clip in seconds.
//The function returns true if a non-blurry section of at least the minimum length is found.
bool Clip::FindNotBlurry(double length) {
	//Ensures the non-blurry section is longer than the minimum length. The non-blurry section must be ATLEAST min_length seconds.
	double min_length = std::min(length, getLength());
	start_timestamp = end_timestamp = 0;
	blur_samples.clear();

	//Length of frames to the minimum length.
	int len_to_frames = min_length * getFPS();
	VideoCapture video(video_path);
	Mat frame;

	int frame_num = 0;
	int start_num = 0;
	int end_num = 0;

	bool min_reached = false;
	int frame_skip = std::max(1, static_cast<int>(getFPS()/2)); //Original half-second sampling.

	while (video.read(frame)) {
		frame_num++;
		if (frame_num % frame_skip == 0)
		{
			int length_between = end_num - start_num;

			if (length_between > len_to_frames) //Length between the non blurry clips is equal to the minimum length of frames.
			{
				min_reached = true;
			}
			const double variance = blurVariance(frame);
			blur_samples.push_back({frame_num - 1, variance, variance < blur_threshold});
			if (variance < blur_threshold) //Configurable Laplacian-variance blur threshold.
			{
				//If number of frames between start and end is less than the minimum number of frames (minimum seconds long).
				if (min_reached != true)//If a proper video length has been established (ATLEAST min seconds).
				{
					start_num = frame_num + 1; //When it's not blurry, start won't be updated. Therefore, that's the proper start frame.
				}
				else
				{
					start_timestamp = start_num / getFPS();
					end_timestamp = end_num / getFPS();
					return true;
				}
			}
			else //If it's not blurry, end frame keeps updating until a new start frame (blurry) is found.
			{
				end_num = frame_num;
			}
		}
	}
	if (min_reached == true)
	{
		start_timestamp = start_num / getFPS();
		end_timestamp = end_num / getFPS();
		return true;
	}
	return false;
}

//Performs facial recognition to find the first frame with a detected faces. This ensures that the preferred part of the clip is shown in the video.
//"start" is the frame index to start processing from. It will be updated in this function and is passed by reference.
void Clip::FacialRecognition(int& start)
{
	// Optional for headless use; the caller supplies the cascade location.
	if (face_cascade_path.empty()) return;
	VideoCapture video(video_path);

	//The algorithm that is pretrained by Opencv to detect faces.
	//Takes an image into an integral image for greater efficiency. There is a binary value associated with the brightness of an image.
	//Uses features to describe characteristics of onjects. Haar-like features take rectangular regions on the integral image to capture light and dark regions.
	//These results weight in positive and negative samples.
	CascadeClassifier facedetect;
	if (!facedetect.load(face_cascade_path))
		throw std::runtime_error("Unable to load face cascade: " + face_cascade_path);


	Mat img;
	int frame = 0;

	int frameSkip = 10; //Process every 10 frames.

	while (video.read(img)) {
		frame++;
		if (frame > end_timestamp * getFPS()) break;
		if (frame > start)
		{
			//Skip frames if not divisible by frameSkip.
			if (frame % frameSkip != 0) {
				continue;
			}

			vector<Rect> faces;

			facedetect.detectMultiScale(img, faces, 1.3, 5); //Stores result in faces.

			//When a face is found, STOP.
			if (!faces.empty()) //Face is detected.
			{
				start = frame;
				break;
			}
		}
	}
}

//Helper function to output a random value for the clip's length.
//"min" is the minimum length.
//"max" is the maxmimum length.
double RandomLength(double min, double max)
{
	random_device rd; //Random seed for number engine and uses real distribution to generate random double values.
	mt19937 gen(rd());
	uniform_real_distribution<double> num(min, max);

	return num(gen); //Length of the clip.
}

//Constructs a Clip object with the provided video clip information.
//"clip_name" is the name of the video clip file.
//"clip_num" is the current clip ID, representing it's order of creation.
//"path" is the directory path where the video clip is located.
//"max_length" is the maximum duration allowed for the clip.
//"min_length" is the minimum duration required for the clip.
Clip::Clip(string clip_name, int& clip_num, string path, double max_length, double min_length)
{
	clip_num++;
	id = clip_num;
	video_path = (std::filesystem::path(path) / clip_name).string();


	VideoCapture input(video_path);
	if (!input.isOpened()) throw std::runtime_error("Unable to open video: " + video_path);
	width = input.get(CAP_PROP_FRAME_WIDTH);
	height = input.get(CAP_PROP_FRAME_HEIGHT);
	fps = input.get(CAP_PROP_FPS);
	if (fps <= 0) throw std::runtime_error("Video has invalid frame rate");
	video_length = input.get(CAP_PROP_FRAME_COUNT) / fps;
}

//Handles and analyzes the video clip to determine suitable start and end timestamps, based on the calculations of member functions.
//"max_length" is the maximum allowable duration for the clip.
//"min_length" is the minimum required duration for the clip.
void Clip::Create(double max_length, double min_length)
{
	double cut_length;
	VideoCapture input(video_path);
	int total_frames = static_cast<int>(input.get(CAP_PROP_FRAME_COUNT));
	video_length = total_frames / fps;

	if (min_length > video_length)
		min_length = video_length;
	if (max_length > video_length)
		max_length = video_length;

	int start_f = start_timestamp * getFPS(); //Start frame.
	if (FindNotBlurry(min_length)) //Original contiguous-clear-shot selector.
	{
		////Handles facial recognition. Updates staring frame based on calculation.

		start_f = start_timestamp * getFPS();
		FacialRecognition(start_f); //Sets new start.

		int frames_to_min = min_length * getFPS(); //Number of frames of the minimum length.
		if (end_timestamp * getFPS() - start_f > frames_to_min) //From the updated starting frame (when a face is found) is greater than minimum time.
		{
			double seconds_to_startf = start_f / getFPS();
			if (seconds_to_startf >= start_timestamp)
			{
				start_timestamp = seconds_to_startf;
			}
		}
	}

	double current_clip_length = end_timestamp - start_timestamp;
	//If clip length is longer than the maximum, a random segment within this length will be used.
	if (current_clip_length > max_length)
	{
		cut_length = RandomLength(std::min(max_length, std::max(min_length, max_length-2)), max_length);
		double cut = (current_clip_length - cut_length)/2; //cut -> cut_length -> cut.
		double new_start = start_timestamp + cut;
		double new_end = end_timestamp - cut;

		//Updates new start and end times based on the previous data.
		start_timestamp = new_start;
		end_timestamp = new_end;
	}

}

//Set's the starting timestamp (in seconds).
//"start" is a floating point number corresponding to the new start time.
void Clip::setStart(float start)
{
	start_timestamp = start;
}

//Set's the ending timestamp (in seconds).
//"start" is a floating point number corresponding to the new end time.
void Clip::setEnd(float end)
{
	end_timestamp = end;
}


//Writes the selected segment of the video clip to the specified VideoWriter.
//"output" is the VideoWriter to which the clip will be written.
void Clip::WriteTo(VideoWriter output)
{
	Mat img; //Creates image because it needs each frame image in the video
	int curr_frame = 0;
	bool start = false;

	VideoCapture video(video_path);
	int frames_to_start = getFPS() * start_timestamp;
	int frames_to_end = getFPS() * end_timestamp;
	while (video.read(img)) {

		if (curr_frame == frames_to_start) //If the current frame is equal to the updated clip start frame, start the video writing.
			start = true;
		else if (curr_frame == frames_to_end)
			break;
		if (start == true)
		{

			output.write(img);
			int delay = static_cast<int>(1000 / getFPS());

			waitKey(delay);
		}
		curr_frame++;
	}
	//Edited clip releases
	video.release();
}

//Displays the updated segment of the video clip, using OpenCV, in a resizable window.
//This method plays the clip at a higher speed (frame skipping) to enable preview and testing.
void Clip::Display()
{
	Mat img;
	int curr_frame = 0;
	bool start = false;

	VideoCapture video(video_path);

	int frames_to_start = getFPS() * start_timestamp;
	int frames_to_end = getFPS() * end_timestamp;

	int frameskip = 5; //Speeds up the video for display/testing purposes.

	while (video.read(img)) {
		if (curr_frame > frames_to_end)
				break;
		if (curr_frame % frameskip == 0)
		{
			resize(img, img, Size(getWidth() / 4, getHeight() / 4));

			if (curr_frame > frames_to_start)
			{
				start = true;
			}

			if (start == true)
			{
				//Text used to inform user of it's clip number. Helpful when selecting clips to edit.

				Point textPosition(10, 30);
				Scalar textColor(255, 255, 255);
				int fontSize = 1;
				int thickness = 2;

				putText(img, to_string(id), textPosition, FONT_HERSHEY_SIMPLEX, fontSize, textColor, thickness);
				imshow("Preview", img);
				int delay = static_cast<int>(1000 / getFPS());

				waitKey(1);
			}
		}
		curr_frame++;
	}
	destroyWindow("Preview");
}

/*
UNUSED MOTION DETECTION EXPERIMENTATION:

Although interesting, and perhaps useful eventually, optical flow is far too expensive and takes too much time for what it's worth.
It is also has difficulty percieving when an object is TOO blurry, and finetuning the settings seems unneccessary for it's cost.
The purpose was too begin the footage when there is little motion, but it seems there are more efficient methods available.

	- performing calculations to estimate the scope of focus in the video (start and stop times).
	- Create instead of a human facial detector, a horse detector to know when or if there's a horse.
	- training an AI model to know when to start and stop using "bad" or "good" labels if the camera is too blurry (still seems cost intensive but more probable).


IMPORTANT CONCEPTS LEARNED:

OPTICAL FLOW FURTHER DISCUSSED/FOUND IN MUTLIVARIABLE CALCULUS
Computes the optical flow between two frames(previous frame and the current gray frame)
//Uses Farneback algorithm and optical flow (pattern of motion)
//Finds point. Compares the change of that point after each frame iteration. Therefore, calculating the rate of change of the point/object.
//Pixel cooresponds to a binary value to represent brightness
//Uses image derivaties to see changes in grayscale. Takes very small displacement values between the images (derivatives) to form a polynomial approximation.
//I(x,y,t) = I(x+dx, y+dy, t+dt) where I represents the image intensity, and it's parameters represent its pixels (spacial cordinates) over time.
//When taking the taylor series (approximates function based on infinite sum of derivatives) it gives a polynomial formula to APPROXIMATE the motion vector at a specific pixel.


//prev, next, flow, pyr_scale, levels, winsize, iterations, poly_n, poly_sigma, flags
//flow is the image which represents the optical flow

void clip::motionDetect()
{

	vector<double> mag;
	int skip = 0;
	VideoCapture cap(video_path);

	Mat frame;
	while (cap.read(frame)) {
		if (skip > getFPS() / 2)
		{
			Mat grayFrame;
			cvtColor(frame, grayFrame, COLOR_BGR2GRAY); //converts the current frame to grayscale

			Mat stabilizedFrame;
			static Mat prevFrame;
			static bool isFirstFrame = true;

			if (isFirstFrame) { //handles first frame
				grayFrame.copyTo(prevFrame);
				isFirstFrame = false;
			}

			//Creates matrix of vectors based on optical flow. Values coorespond to a pixel and it's motion vector between the two frames (dx,dy).
			//

			Mat flow; //motion estimation for each pixel in that image.
			//Compares pixel intensities from the first frame's neighborhoodd with the corresponding pixel intensities of the second frame. Based on intendity, motion vector is given.

			calcOpticalFlowFarneback(prevFrame, grayFrame, flow, 0.5, 3, 15, 3, 5, 1.2, cv::OPTFLOW_FARNEBACK_GAUSSIAN);
			//Scalar is a template class fro a 4 element vector. Calculates mean value of each channel in the matrix (dx, dy). Takes the average of the components.

			Scalar meanFlow = mean(flow);
			double meanMagnitude = norm(meanFlow); //takes the magnitude of the scalar

			if (meanMagnitude > 15)
			{
				imwrite("frame.jpg", frame);
				break;
			}
		}
		skip++;
	}
}
*/
