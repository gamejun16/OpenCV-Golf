#include <iostream>
#include "opencv2/opencv.hpp"

using namespace std;
using namespace cv;


void getObjectHistogram(Mat &frame, Rect object_region, Mat &globalHistogram, Mat &objectHistogram);
void backProjection(const Mat &frame, const Mat &objectHistogram, Mat &bp);


int range_count = 0;
int hue = 0;
int low_hue1 = 0, low_hue2 = 0;
int high_hue1 = 0, high_hue2 = 0;


void callback(int range, void*)
{
	cout << "hue = " << hue << endl;


	int low_hue = hue - range;
	int high_hue = hue + range;


	if (low_hue < 10) {
		range_count = 2;

		high_hue1 = 180;
		low_hue1 = low_hue + 180;
		high_hue2 = high_hue;
		low_hue2 = 0;
	}
	else if (high_hue > 170) {
		range_count = 2;

		high_hue1 = low_hue;
		low_hue1 = 180;
		high_hue2 = high_hue - 180;
		low_hue2 = 0;
	}
	else {
		range_count = 1;

		low_hue1 = low_hue;
		high_hue1 = high_hue;
	}


	cout << low_hue1 << "  " << high_hue1 << endl;
	cout << low_hue2 << "  " << high_hue2 << endl;

}


int main()
{

	Scalar red(0, 0, 255);
	Scalar blue(255, 0, 0);
	Scalar yellow(0, 255, 255);
	Scalar magenta(255, 0, 255);


	Mat rgb_color = Mat(1, 1, CV_8UC3, red);
	Mat hsv_color;

	cvtColor(rgb_color, hsv_color, COLOR_BGR2HSV);


	hue = (int)hsv_color.at<Vec3b>(0, 0)[0];


	int rangeH = 10;
	int LowS = 50;
	int LowV = 50;

	//callback(rangeH);


	namedWindow("결과 영상", 1);
	createTrackbar("rangeH(0~90)", "결과 영상", &rangeH, 179, callback); //Hue (0 - 179)
	createTrackbar("LowS(0~255)", "결과 영상", &LowS, 255); //Saturation (0 - 255)
	createTrackbar("LowV(0~255)", "결과 영상", &LowV, 255); //Value (0 - 255)


	VideoCapture cap;
	Mat img_frame, img_hsv;

	cap.open(-1);
	// OR advance usage: select any API backend
	int deviceID = 0;             // 0 = open default camera
	int apiID = cv::CAP_ANY;      // 0 = autodetect default API
								  // open selected camera using selected API
	cap.open(deviceID + apiID);


	if (!cap.isOpened()) {
		cerr << "ERROR! Unable to open camera\n";
		cin.get();
		return -1;
	}



	// 추가 
	Rect trackingWindow(0, 0, 30, 30);
	int frame_index = 0;

	Mat globalHistogram, objectHistogram;
	Rect prev_rect;


	for (;;)
	{
		// wait for a new frame from camera and store it into 'frame'
		cap.read(img_frame);

		// check if we succeeded
		if (img_frame.empty()) {
			cerr << "ERROR! blank frame grabbed\n";
			break;
		}


		//추가
		frame_index++;


		//HSV로 변환
		cvtColor(img_frame, img_hsv, COLOR_BGR2HSV);


		//지정한 HSV 범위를 이용하여 영상을 이진화
		Mat img_mask1, img_mask2;
		inRange(img_hsv, Scalar(low_hue1, LowS, LowV), Scalar(high_hue1, 255, 255), img_mask1);
		if (range_count == 2) {
			inRange(img_hsv, Scalar(low_hue2, LowS, LowV), Scalar(high_hue2, 255, 255), img_mask2);
			img_mask1 |= img_mask2;
		}


		//morphological opening 작은 점들을 제거 
		erode(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));
		dilate(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));


		//morphological closing 영역의 구멍 메우기 
		dilate(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));
		erode(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));



		if (frame_index < 100) {
			//라벨링 
			Mat img_labels, stats, centroids;
			int numOfLables = connectedComponentsWithStats(img_mask1, img_labels,
				stats, centroids, 8, CV_32S);


			//영역박스 그리기
			int max = -1, idx = 0;
			for (int j = 1; j < numOfLables; j++) {
				int area = stats.at<int>(j, CC_STAT_AREA);
				if (max < area)
				{
					max = area;
					idx = j;
				}
			}


			int left = stats.at<int>(idx, CC_STAT_LEFT);
			int top = stats.at<int>(idx, CC_STAT_TOP);
			int width = stats.at<int>(idx, CC_STAT_WIDTH);
			int height = stats.at<int>(idx, CC_STAT_HEIGHT);


			//추가 
			Rect object_region(left, top, width, height);
			getObjectHistogram(img_hsv, object_region, globalHistogram, objectHistogram);
			trackingWindow = object_region;


			rectangle(img_frame, Point(left, top), Point(left + width, top + height),
				Scalar(0, 0, 255), 1);

			prev_rect = object_region;
		}
		else {

			// Tracking
			//meanShift(bp, trackingWindow, TermCriteria(CV_TERMCRIT_EPS | CV_TERMCRIT_ITER, 100, 0.01));
			//rectangle(img_frame, trackingWindow, CV_RGB(0, 0, 255), 2);

			Mat bp;
			backProjection(img_hsv, objectHistogram, bp);
			bitwise_and(bp, img_mask1, bp);

			RotatedRect rect = CamShift(bp, prev_rect, TermCriteria(TermCriteria::EPS | TermCriteria::COUNT, 10, 1));

			ellipse(img_frame, rect, CV_RGB(255, 0, 0), 3, CV_8U);

		}


		Mat img_result;
		cvtColor(img_mask1, img_mask1, COLOR_GRAY2BGR);
		hconcat(img_frame, img_mask1, img_result);

		imshow("결과 영상", img_result);


		if (waitKey(5) >= 0)
			break;


	}


	return 0;

}


void getObjectHistogram(Mat &frame, Rect object_region, Mat &globalHistogram, Mat &objectHistogram)
{
	const int channels[] = { 0, 1 };
	const int histSize[] = { 64, 64 };
	float range[] = { 0, 256 };
	const float *ranges[] = { range, range };

	// Histogram in object region
	Mat objectROI = frame(object_region);
	calcHist(&objectROI, 1, channels, noArray(), objectHistogram, 2, histSize, ranges, true, false);


	// A priori color distribution with cumulative histogram
	calcHist(&frame, 1, channels, noArray(), globalHistogram, 2, histSize, ranges, true, true);


	// Boosting: Divide conditional probabilities in object area by a priori probabilities of colors
	for (int y = 0; y < objectHistogram.rows; y++) {
		for (int x = 0; x < objectHistogram.cols; x++) {
			objectHistogram.at<float>(y, x) /= globalHistogram.at<float>(y, x);
		}
	}

	normalize(objectHistogram, objectHistogram, 0, 255, NORM_MINMAX);
}


void backProjection(const Mat &frame, const Mat &objectHistogram, Mat &bp) {
	const int channels[] = { 0, 1 };
	float range[] = { 0, 256 };
	const float *ranges[] = { range, range };
	calcBackProject(&frame, 1, channels, objectHistogram, bp, ranges);
}