

// OpenCVDLL.cpp : DLL 응용 프로그램을 위해 내보낸 함수를 정의합니다.
//


#include "stdafx.h"
#include "opencv2/opencv.hpp"
#include "opencv2/core.hpp"
#include "opencv2/imgproc.hpp"
#include "opencv2\core\ocl.hpp"

using namespace cv;
using namespace std;


enum TARGET_COLOR {
	TARGET_RED, TARGET_BLUE, TARGET_YELLOW, TARGET_MAGENTA, TARGET_LIME,
};

struct Color32
{
	uchar red;
	uchar green;
	uchar blue;
	uchar alpha;
};

Mat saved_frame_top, saved_frame_mask;

//static cv::VideoCapture cap;
extern "C" {
	VideoCapture cap; // front cam
	VideoCapture cap2; // top cam

	// (이전 프레임의) 기존 중심 좌표
	Point centerPos; Point centerPos_front;
	// center_[POS] <Top cam>
	int c_width, c_left, c_top;
	// center_[POS] <Front cam>
	int c_width_front, c_left_front, c_top_front;

	// 인식 범위를 지정하기위한 좌표
	// 640*480 기준 ready 상태(화면의 중앙), hitted 상태(공이 진행할 수 있는 범위 전체)
	int READY_TOP_LEFT = 420, READY_TOP_RIGHT = 520, READY_LEFT_TOP = 190, READY_LEFT_BOTTOM = 290;
	int HITTED_TOP_LEFT = 0, HITTED_TOP_RIGHT = 520, HITTED_LEFT_TOP = 0, HITTED_LEFT_BOTTOM = 480;

	// 매개변수로 전달되는 플레그(isOnReady)에 따라  READY_ or HITTED_ 로 값을 바꿔가며 사용
	int TOP_LEFT, TOP_RIGHT, LEFT_TOP, LEFT_BOTTOM;

	int setCamPropCount;

	void initParameters() {
		centerPos = Point(0, 0);
		c_width = 0;c_left = 0;c_width_front = 0;c_left_front = 0;c_top_front = 0;
	}

	__declspec(dllexport) bool cv_TrackingOn() {

		cv::ocl::setUseOpenCL(true); // OpenCL 사용 준비

		initParameters();

		// 두 카메라를 모두 활성화하며 프로세스 진행
		if (!cap.isOpened() && !cap2.isOpened()) {

			cap.set(cv::CAP_PROP_SETTINGS, 0);
			cap2.set(cv::CAP_PROP_SETTINGS, 0);

			cap.open(0); // 2
			cap2.open(1); // 0

			cap.set(CAP_PROP_EXPOSURE, -4);

			setCamPropCount = 0;

			return true;
		}
		return false;
	}

	__declspec(dllexport) bool cv_TrackingOff() {

		destroyAllWindows();

		cap.release();

		cap2.release();


		return true;
	}

	__declspec(dllexport) bool cv_Tracking(int* _left, int* _width, int* _top, bool* _callFlag, int* _clock, int* _count, bool isReadyDone=false)
	{
		*_count = *_count + 1;

		setCamPropCount = setCamPropCount + 1;

		// 아직 준비되지 않았다면
		if (!isReadyDone) {
			TOP_LEFT = READY_TOP_LEFT;
			TOP_RIGHT = READY_TOP_RIGHT;
			LEFT_TOP = READY_LEFT_TOP;
			LEFT_BOTTOM = READY_LEFT_BOTTOM;
		}

		// 준비가 완료되어 HIT 판별을 대기하는 중이라면
		else {
			TOP_LEFT = HITTED_TOP_LEFT;
			TOP_RIGHT = HITTED_TOP_RIGHT;
			LEFT_TOP = HITTED_LEFT_TOP;
			LEFT_BOTTOM = HITTED_LEFT_BOTTOM;
		}

		Scalar red(0, 0, 255);
		Scalar blue(255, 0, 0);
		Scalar yellow(0, 255, 255);
		Scalar magenta(255, 0, 255);
		Scalar lime(0, 238, 179); // lime color
		
		Scalar greenLower(29, 86, 6), greenUpper(64, 255, 255);
		
		

		/**************************		[ 상단 캠 ] 공의 좌표(xPos, yPos) 검출		***************************/
		Mat img_frame;
		UMat u_img_frame, u_img_hsv, u_img_gray, u_img_cut;
		try {
			// 프레임 읽어오기. 캠 화면을 img_frame에 저장
			cap2.read(img_frame);
			
			*_clock = clock();

			// 카메라에 빈 영상이 담겼을 경우
			if (img_frame.empty()) {
				return false;
			}
			u_img_frame = img_frame.getUMat(AccessFlag::ACCESS_READ);
			u_img_cut = u_img_frame(Range(LEFT_TOP, LEFT_BOTTOM), Range(TOP_LEFT, TOP_RIGHT));
			GaussianBlur(u_img_cut, u_img_cut, Size(5, 5), 0);

			cvtColor(u_img_cut, u_img_hsv, COLOR_BGR2HSV); // color-based detection

			/********************** 색 기반 트래킹 전처리 **********************/

			//지정한 HSV 범위를 이용하여 영상을 이진화
			Mat img_mask1;
			inRange(u_img_hsv, greenLower, greenUpper, img_mask1);

			// 정확도를 높이기 위한 구문. 성능에 영향을 준다.
			//morphological opening 작은 점들을 제거
			erode(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));
			dilate(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));
			//morphological closing 영역의 구멍 메우기
			dilate(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));
			erode(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));

			//라벨링
			Mat img_labels, stats, centroids;
			int numOfLables = connectedComponentsWithStats(img_mask1, img_labels, stats, centroids, 8, CV_32S);

			/********** 색 기반 트래킹 전처리 종료 ***********/


			/**************** 색 및 모양 추적에 대해 동시에 검출된 대상 지정  *****************/
				// 1. '색'으로 인식된 물체의 중심(colorCenter)을 저장
				// 2. 그 중 가장 크게 인식된 영역을 '공'으로 인식
				// 3. 공의 중심 좌표를 반환

			// 색을 기반으로 인식된 물체들을 순회
			int max = -1, idx = 0;

			for (int j = 1; j < numOfLables; j++) {
				int area = stats.at<int>(j, CC_STAT_AREA);
				if (max < area)
				{
					// 가장 큰 색 범위의 물체를 저장	
					max = area;
					idx = j;
				}
			}

			// 물체의 인식 정보를 유니티로 넘겨서 그 정보를 이용해 좌표(x, z축) 결정
			int left = stats.at<int>(idx, CC_STAT_LEFT);
			int top = stats.at<int>(idx, CC_STAT_TOP);
			int width = stats.at <int>(idx, CC_STAT_WIDTH);
			int height = stats.at<int>(idx, CC_STAT_HEIGHT);

			// 공이 인식되지 않으면 인식 범위 내 최대 크기의 width가 검출됨을 이용
			// 인식된 공이 없을 때
			if (width >= 50 || width == 0) {
				*_left = 0; // 이 값이 0이면 유니티에서 공이 '없음'으로 판단하게 된다
				rectangle(u_img_frame, Rect(Point(TOP_LEFT, LEFT_TOP), Point(TOP_RIGHT, LEFT_BOTTOM)), Scalar(255, 255, 255), 4);

			}
			// 인식된 대상이 있을 때 
			else{
				// 색 기반으로 인식된 물체의 중심 좌표.
				Point colorCenter = Point(left + (width / 2) + TOP_LEFT, top + (height / 2) + LEFT_TOP);

				// 흔들림 허용 범위(pixel)
				int errorRange = 10;

				// 이전 프레임과 비교했을 때 중심점의 흔들림이 '거의' 없었다면
				// 이전 프레임에서 검출했던 공의 정보를 그대로 사용
				// 공의 흔들림을 최소화하는 구문
				if (centerPos.x + errorRange > colorCenter.x && centerPos.x - errorRange < colorCenter.x && centerPos.y + errorRange > colorCenter.y && centerPos.y - errorRange < colorCenter.y) {
					colorCenter = centerPos;
					width = c_width;
				}
				
				//	 변화가 있었다면, 그 변화에 대한 공의 위치정보를 전달 및 중심 좌표 갱신?
				else {
					*_callFlag = true; // 함수가 호출(값 갱신)되는 순간마다 true -> Unity 내에서 갱신된 값을 읽을 때마다 false로 바꾸어 준다.

					// renew center position
					centerPos = colorCenter;

					// renew values
					c_left = left;
					c_top = top;
					c_width = width;

					// pass to unity
					*_left = c_left;
					*_top = c_top;
					*_width = c_width;
				}

				circle(u_img_frame, centerPos, width / 2, Scalar(0, 255, 0), 2);
				circle(u_img_frame, centerPos, 2, Scalar(255, 0, 0), 3);

				// 인식 범위 표시
				rectangle(u_img_frame, Rect(Point(TOP_LEFT, LEFT_TOP), Point(TOP_RIGHT,LEFT_BOTTOM)), Scalar(0, 0, 255), 4);
			}

			//imshow("top(shape)", u_img_gray);
			//imshow("top(hsv)", u_img_hsv);
			imshow("top(color)", img_mask1); 
			imshow("top(x, y)", u_img_frame); 

			waitKey(1);
		}
		catch (Exception e) {
			return false;
		}
		/************ 이상 상단 캠 ************/

		return true;
	}

	__declspec(dllexport) bool cv_Tracking_2(int* _height, bool* _callFlag_2, int* _count, bool isReadyDone = false)
	{
		*_callFlag_2 = true; // 함수가 호출(값 갱신)되는 순간마다 true -> Unity 내에서 갱신된 값을 읽을 때마다 false로 바꾸어 준다.

		// 아직 준비되지 않았다면
		if (!isReadyDone) {
			TOP_LEFT = READY_TOP_LEFT;
			TOP_RIGHT = READY_TOP_RIGHT;
			LEFT_TOP = READY_LEFT_TOP;
			LEFT_BOTTOM = READY_LEFT_BOTTOM;
		}
		// 준비가 완료되어 HIT 판별을 대기하는 중이라면
		else {
			TOP_LEFT = HITTED_TOP_LEFT;
			TOP_RIGHT = HITTED_TOP_RIGHT;
			LEFT_TOP = HITTED_LEFT_TOP;
			LEFT_BOTTOM = HITTED_LEFT_BOTTOM;
		}

		Scalar red(0, 0, 255);
		Scalar blue(255, 0, 0);
		Scalar yellow(0, 255, 255);
		Scalar magenta(255, 0, 255);
		Scalar lime(0, 238, 179); // lime color
		Scalar lime2(185, 238, 0);
		
		Scalar greenLower(29, 86, 6), greenUpper(64, 255, 255);
		

		/**************************		[ 전면 캠 ] 공의 좌표(zPos) 검출		***************************/

		Mat img_frame_front;
		UMat u_img_frame_front, u_img_hsv, u_img_cut;

		try {
			// 프레임 읽어오기. 캠 화면을 img_frame에 저장
			cap.read(img_frame_front);

			// 카메라에 빈 영상이 담겼을 경우
			if (img_frame_front.empty()) {
				return false;
			}

			u_img_frame_front = img_frame_front.getUMat(AccessFlag::ACCESS_READ);
			u_img_cut = u_img_frame_front(Range(LEFT_TOP, LEFT_BOTTOM), Range(TOP_LEFT, TOP_RIGHT));
			GaussianBlur(u_img_cut, u_img_cut, Size(5, 5), 0);

			//cvtColor(u_img_cut, u_img_gray, COLOR_BGR2GRAY); // shape_based detection
			cvtColor(u_img_cut, u_img_hsv, COLOR_BGR2HSV); // color_based detection

			/********************** 색 기반 트래킹 전처리 **********************/

			//	//지정한 HSV 범위를 이용하여 영상을 이진화
			Mat img_mask1;
			inRange(u_img_hsv, greenLower, greenUpper, img_mask1);

			// 정확도를 높이기 위한 구문. 성능에 영향을 준다.
			//morphological opening 작은 점들을 제거
			erode(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));
			dilate(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));
			//morphological closing 영역의 구멍 메우기
			dilate(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));
			erode(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));

			//라벨링
			Mat img_labels, stats, centroids;
			int numOfLables = connectedComponentsWithStats(img_mask1, img_labels, stats, centroids, 8, CV_32S);

			/********** 색 기반 트래킹 전처리 종료 ***********/

			/**************** 색 및 모양 추적에 대해 동시에 검출된 대상 지정  *****************/
				// 1. '색'으로 인식된 물체의 중심(colorCenter)을 저장
				// 2. '모양'으로 인식된 물체들을 순회하며 '색'인식의 결과와 중심 좌표를 비교
				// 3. 일치되는 물체(녹색이면서 공의 모양인 물체)를 공으로 결정내리며 정보를 출력 및 반환
				// 4. 없다면?

				// 색으로 인식된 물체들을 순회
			int max = -1, idx = 0;
			for (int j = 1; j < numOfLables; j++) {
				int area = stats.at<int>(j, CC_STAT_AREA);
				if (max < area)
				{
					max = area;
					idx = j;
				}
			}
			// 물체의 인식 정보를 유니티로 넘겨서 그 정보를 이용해 좌표(x, z축) 탐색
			int left = stats.at<int>(idx, CC_STAT_LEFT);
			int top = stats.at<int>(idx, CC_STAT_TOP);
			int width = stats.at <int>(idx, CC_STAT_WIDTH);
			int height = stats.at<int>(idx, CC_STAT_HEIGHT);

			// 공이 인식되지 않으면 인식 범위 내 최대 크기의 width가 검출됨을 이용
			// 인식된 공이 없을 때
			if (width >= TOP_RIGHT - TOP_LEFT) {
				*_height = 0; // 이 값이 0이면 유니티에서 공이 '없음'으로 판단하게 된다?
			}

			// 인식된 공이 있을 때 
			// 인식된 것이 공이라고 확정지을 수 있나? 그냥 유사색의 무언가일 가능성 있음
			else if (width < TOP_RIGHT - TOP_LEFT) {

				// 색 기반으로 인식된 물체의 중심 좌표.
				Point colorCenter = Point(left + (width / 2) + TOP_LEFT, top + (height / 2) + LEFT_TOP);


				int errorRange = 10;

				// 이전 프레임과 비교했을 때 중심점의 흔들림이 '거의' 없었다면
				// 이전 프레임에서 검출했던 공의 정보를 그대로 사용
				// 공의 흔들림을 최소화하는 구문
				if (centerPos_front.x + errorRange > colorCenter.x && centerPos_front.x - errorRange < colorCenter.x) {
					if (centerPos_front.y + errorRange > colorCenter.y && centerPos_front.y - errorRange < colorCenter.y) {
						colorCenter = centerPos_front;
						width = c_width_front;
					}
				}
				// 변화가 있었다면, 그 변화에 대한 공의 위치정보를 전달 및 중심 좌표 갱신?
				else {

					// renew center position
					centerPos_front = colorCenter;

					// renew values
					c_left_front = left;
					c_width_front = width;
					c_top_front = top;

					// pass to unity
					//*_left = c_left;
					//*_top = c_top;
					//*_width = c_width;
					*_height = c_top_front;


				}

				circle(u_img_frame_front, centerPos_front, width / 2, Scalar(0, 255, 0), 2);
				circle(u_img_frame_front, centerPos_front, 2, Scalar(255, 0, 0), 3);
			}
			// 인식 범위 표시
			rectangle(u_img_frame_front, Rect(Point(TOP_LEFT, LEFT_TOP), Point(TOP_RIGHT, LEFT_BOTTOM)), Scalar(0, 0, 255), 4);
	

			//imshow("front(shape)", u_img_gray);
			//imshow("front(hsv)", u_img_hsv);
			imshow("front(color)", img_mask1); 
			imshow("front(z)", u_img_frame_front);

			waitKey(1);

			//if (*_left != left) *_callFlag = true; // 함수가 호출(값 갱신)되는 순간마다 true -> Unity 내에서 갱신된 값을 읽을 때마다 false로 바꾸어 준다.
		

		}
		catch (Exception e) {
			//return false;
		}
		return true;
	}
	

#pragma region history

	// 02-14 09:56 // Tracking Func Save. high quality color based detcting. only apply on top-cam
	/*
		__declspec(dllexport) bool cv_Tracking(int* _left, int* _width, int* _top, bool* _callFlag, int* _count, bool isReadyDone=false)
	{
		//*_count = cap.get(CAP_PROP_FPS);
		//count++;
		//*_count = count;
		//*_count = *_count + 1;
		setCamPropCount = setCamPropCount + 1;

		if (setCamPropCount > 30) {
			cap.set(CAP_PROP_EXPOSURE, -7);
			cap2.set(CAP_PROP_EXPOSURE, -7);

			setCamPropCount = 0;
		}

		// 아직 준비되지 않았다면
		if (!isReadyDone) {
			TOP_LEFT = READY_TOP_LEFT;
			TOP_RIGHT = READY_TOP_RIGHT;
			LEFT_TOP = READY_LEFT_TOP;
			LEFT_BOTTOM = READY_LEFT_BOTTOM;
		}
		// 준비가 완료되어 HIT 판별을 대기하는 중이라면
		else {
			TOP_LEFT = HITTED_TOP_LEFT;
			TOP_RIGHT = HITTED_TOP_RIGHT;
			LEFT_TOP = HITTED_LEFT_TOP;
			LEFT_BOTTOM = HITTED_LEFT_BOTTOM;
		}

		Scalar red(0, 0, 255);
		Scalar blue(255, 0, 0);
		Scalar yellow(0, 255, 255);
		Scalar magenta(255, 0, 255);
		Scalar lime(0, 238, 179); // lime color
		//Scalar greenLower(29, 86, 6), greenUpper(64, 255, 255);
		Scalar greenLower(29, 86, 6), greenUpper(64, 255, 255);

		/**************************		[ 상단 캠 ] 공의 좌표(xPos, yPos) 검출		***************************
	Mat img_frame;
	UMat u_img_frame, u_img_hsv, u_img_gray, u_img_cut;
	try {
		//// 프레임 읽어오기. 캠 화면을 img_frame에 저장
		cap.read(img_frame);

		// 카메라에 빈 영상이 담겼을 경우
		if (img_frame.empty()) {
			return false;
		}
		u_img_frame = img_frame.getUMat(AccessFlag::ACCESS_READ);
		u_img_cut = u_img_frame(Range(LEFT_TOP, LEFT_BOTTOM), Range(TOP_LEFT, TOP_RIGHT));
		GaussianBlur(u_img_cut, u_img_cut, Size(5, 5), 0);

		//cvtColor(u_img_cut, u_img_gray, COLOR_BGR2GRAY); // shape-based detection
		cvtColor(u_img_cut, u_img_hsv, COLOR_BGR2HSV); // color-based detection

		/********************** 색 기반 트래킹 전처리 **********************

		//	//지정한 HSV 범위를 이용하여 영상을 이진화
		Mat img_mask1;
		inRange(u_img_hsv, greenLower, greenUpper, img_mask1);

		// 정확도를 높이기 위한 구문. 성능에 영향을 준다.
		//morphological opening 작은 점들을 제거
		erode(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));
		dilate(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));
		//morphological closing 영역의 구멍 메우기
		dilate(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(3, 3)));
		erode(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(3, 3)));

		//라벨링
		Mat img_labels, stats, centroids;
		int numOfLables = connectedComponentsWithStats(img_mask1, img_labels, stats, centroids, 8, CV_32S);

		/********** 색 기반 트래킹 전처리 종료 ***********
		/**************** 색 및 모양 추적에 대해 동시에 검출된 대상 지정  *****************
			// 1. '색'으로 인식된 물체의 중심(colorCenter)을 저장
			// 2. '모양'으로 인식된 물체들을 순회하며 '색'인식의 결과와 중심 좌표를 비교
			// 3. 일치되는 물체(녹색이면서 공의 모양인 물체)를 공으로 결정내리며 정보를 출력 및 반환
			// 4. 없다면?

			// 색으로 인식된 물체들을 순회
		int max = -1, idx = 0;

		for (int j = 1; j < numOfLables; j++) {
			int area = stats.at<int>(j, CC_STAT_AREA);
			if (max < area)
			{
				/// 너무 작거나 크지 않은 물체에 대해서 선별
				// 가장 큰 색 범위의 물체를 저장
				// 너무 큰 경우에 대해서는 이후 걸러내는 과정을 거침
				//if (stats.at <int>(j, CC_STAT_WIDTH) > 10 && stats.at <int>(j, CC_STAT_WIDTH) < 100) {
				max = area;
				idx = j;
				//}
			}
		}
		// 물체의 인식 정보를 유니티로 넘겨서 그 정보를 이용해 좌표(x, z축) 탐색
		int left = stats.at<int>(idx, CC_STAT_LEFT);
		int top = stats.at<int>(idx, CC_STAT_TOP);
		int width = stats.at <int>(idx, CC_STAT_WIDTH);
		int height = stats.at<int>(idx, CC_STAT_HEIGHT);

		// 공이 인식되지 않으면 인식 범위 내 최대 크기의 width가 검출됨을 이용
		// 인식된 공이 없을 때
		if (width >= TOP_RIGHT - TOP_LEFT) {
			*_left = 0; // 이 값이 0이면 유니티에서 공이 '없음'으로 판단하게 된다
		}
		// 인식된 공이 있을 때 
		// 인식된 것이 공이라고 확정지을 수 있나? 그냥 유사색의 무언가일 가능성 있음
		else if (width < TOP_RIGHT - TOP_LEFT) {

			// 색 기반으로 인식된 물체의 중심 좌표.
			Point colorCenter = Point(left + (width / 2) + TOP_LEFT, top + (height / 2) + LEFT_TOP);



			int errorRange = 4;

			// 이전 프레임과 비교했을 때 중심점의 흔들림이 '거의' 없었다면
			// 이전 프레임에서 검출했던 공의 정보를 그대로 사용
			// 공의 흔들림을 최소화하는 구문
			if (centerPos.x + errorRange > colorCenter.x && centerPos.x - errorRange < colorCenter.x) {
				if (centerPos.y + errorRange > colorCenter.y && centerPos.y - errorRange < colorCenter.y) {
					colorCenter = centerPos;
					width = c_width;
				}
			}
			// 변화가 있었다면, 그 변화에 대한 공의 위치정보를 전달 및 중심 좌표 갱신?
			else {
				*_callFlag = true; // 함수가 호출(값 갱신)되는 순간마다 true -> Unity 내에서 갱신된 값을 읽을 때마다 false로 바꾸어 준다.

				// renew center position
				centerPos = colorCenter;

				// renew values
				c_left = left;
				c_top = top;
				c_width = width;

				// pass to unity
				*_left = c_left;
				*_top = c_top;
				*_width = c_width;
				//*_height = height;


			}

			circle(u_img_frame, centerPos, width / 2, Scalar(0, 255, 0), 2);
			circle(u_img_frame, centerPos, 2, Scalar(255, 0, 0), 3);
		}

		// 인식 범위 표시
		rectangle(u_img_frame, Rect(Point(TOP_LEFT, LEFT_TOP), Point(TOP_RIGHT, LEFT_BOTTOM)), Scalar(0, 0, 255), 4);

		//imshow("top(shape)", u_img_gray);
		//imshow("top(hsv)", u_img_hsv);
		imshow("top(color)", img_mask1);
		imshow("top(x, y)", u_img_frame);


		waitKey(1);

	}
	catch (Exception e) {
		return false;
	}
	/************ 이상 상단 캠 ************

	return true;
	}
	*/

	// 02-13 09:22 // Tracking Func Save. color and shape based detecting at same time
	/*
		// 02-12 09:29 // Tracking Func Save. apply shape-based ball detection
	
	__declspec(dllexport) bool cv_Tracking(int* _left, int* _width, int* _top, bool* _callFlag, int* _count)
	{

		//*_callFlag = true; // 함수가 호출(값 갱신)되는 순간마다 true -> Unity 내에서 갱신된 값을 읽을 때마다 false로 바꾸어 준다.
		//*_count = cap.get(CAP_PROP_FPS);
		//count++;
		//*_count = count;
		*_count = *_count + 1;
		setCamPropCount = setCamPropCount + 1;

		if (setCamPropCount > 30) {
			cap.set(CAP_PROP_EXPOSURE, -7);
			cap2.set(CAP_PROP_EXPOSURE, -7);

			setCamPropCount = 0;
		}

		Scalar red(0, 0, 255);
		Scalar blue(255, 0, 0);
		Scalar yellow(0, 255, 255);
		Scalar magenta(255, 0, 255);
		Scalar lime(0, 238, 179); // lime color
		//Scalar greenLower(29, 86, 6), greenUpper(64, 255, 255);
		Scalar greenLower(29, 86, 6), greenUpper(64, 255, 255);

		/**************************		[ 상단 캠 ] 공의 좌표(xPos, yPos) 검출		***************************
	Mat img_frame;
	UMat u_img_frame, u_img_hsv, u_img_gray, u_img_cut;
	try {
		//// 프레임 읽어오기. 캠 화면을 img_frame에 저장
		cap.read(img_frame);

		// 카메라에 빈 영상이 담겼을 경우
		if (img_frame.empty()) {
			return false;
		}
		u_img_frame = img_frame.getUMat(AccessFlag::ACCESS_READ);
		u_img_cut = u_img_frame(Range(190, 290), Range(270, 370));
		GaussianBlur(u_img_cut, u_img_cut, Size(5, 5), 0);

		cvtColor(u_img_cut, u_img_gray, COLOR_BGR2GRAY); // shape-based detection
		cvtColor(u_img_cut, u_img_hsv, COLOR_BGR2HSV); // color-based detection

		/********************** 색 기반 트래킹 전처리 **********************

		//	//지정한 HSV 범위를 이용하여 영상을 이진화
		Mat img_mask1;
		inRange(u_img_hsv, greenLower, greenUpper, img_mask1);

		// 정확도를 높이기 위한 구문. 성능에 영향을 준다.
		//morphological opening 작은 점들을 제거
		erode(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));
		dilate(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));
		////morphological closing 영역의 구멍 메우기
		dilate(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));
		erode(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));

		//라벨링
		Mat img_labels, stats, centroids;
		int numOfLables = connectedComponentsWithStats(img_mask1, img_labels, stats, centroids, 8, CV_32S);

		/********** 색 기반 트래킹 전처리 종료 ***********
		/**************** 색 및 모양 추적에 대해 동시에 검출된 대상 지정  *****************
			// 1. '색'으로 인식된 물체의 중심(colorCenter)을 저장
			// 2. '모양'으로 인식된 물체들을 순회하며 '색'인식의 결과와 중심 좌표를 비교
			// 3. 일치되는 물체(녹색이면서 공의 모양인 물체)를 공으로 결정내리며 정보를 출력 및 반환
			// 4. 없다면?

			// 색으로 인식된 물체들을 순회
		int max = -1, idx = 0;
		for (int j = 1; j < numOfLables; j++) {
			int area = stats.at<int>(j, CC_STAT_AREA);
			if (max < area)
			{
				// 너무 작거나 크지 않은 물체에 대해서 선별
				if (stats.at <int>(j, CC_STAT_WIDTH) > 10) {
					max = area;
					idx = j;
				}
			}
		}
		// 물체의 인식 정보를 유니티로 넘겨서 그 정보를 이용해 좌표(x, z축) 탐색
		int left = stats.at<int>(idx, CC_STAT_LEFT);
		int top = stats.at<int>(idx, CC_STAT_TOP);
		int width = stats.at <int>(idx, CC_STAT_WIDTH);
		int height = stats.at<int>(idx, CC_STAT_HEIGHT);

		// 색 기반으로 인식된 물체의 중심 좌표. 모양을 기반으로 한 차례 더 검사를 거쳐 공인지 판단한다.
		Point colorCenter = Point(left + (width / 2), top + (height / 2));

		// pass to unity
		*_left = left;
		*_top = top;
		*_width = width;
		//*_height = height;

		//// 색으로 추출한 물체를 화면에 표시
		//if (left != 0) {
		//	circle(u_img_frame, Point(left + (width / 2) + 270, top + (height / 2) + 190), width / 2, Scalar(0, 0, 255), 2);
		//	//circle(u_img_frame, Point(left + (width / 2) , top + (height / 2) ), width / 2, Scalar(0, 0, 255), 2);
		//}


		Canny(u_img_gray, u_img_gray, 100, 30, 3);

		vector<Vec3f> circles;
		HoughCircles(u_img_gray, circles, HOUGH_GRADIENT, 1, 100, 50, 20, 5, 40);

		for (size_t i = 0; i < circles.size(); i++)
		{
			Vec3i c = circles[i];

			// 모양으로 인식한 물체가 색으로 인식한 물체와 중심점이 비슷(errorRange: 오차범위)한 경우
			// 동그란 + 녹색 물체를 화면에 표시 >> 공을 추출할 수 있게 됨
			int errorRange = 5;
			if (colorCenter.x + errorRange > c[0] && colorCenter.x - errorRange < c[0]) {
				if (colorCenter.y + errorRange > c[1] && colorCenter.y - errorRange < c[1]) {

					// 녹색 + 원. 공의 중심을 추출
					Point shapeCenter(c[0] + 270, c[1] + 190);
					int radius = c[2];

					// 이전 프레임과 비교했을 때 중심점의 흔들림이 '거의' 없었다면
					// 이전 프레임에서 검출했던 공의 정보를 그대로 사용
					// 공의 흔들림을 최소화하는 구문
					if (centerPos.x + errorRange > shapeCenter.x && centerPos.x - errorRange < shapeCenter.x) {
						if (centerPos.y + errorRange > shapeCenter.y && centerPos.y - errorRange < shapeCenter.y) {
							shapeCenter = centerPos;
							radius = centerRadius;
						}
					}


					circle(u_img_frame, shapeCenter, radius, Scalar(0, 255, 0), 2);
					circle(u_img_frame, shapeCenter, 2, Scalar(255, 0, 0), 3);

					centerPos = shapeCenter;
					centerRadius = radius;

					// 인식 범위 표시
					rectangle(u_img_frame, Rect(Point(270, 190), Point(370, 290)), Scalar(0, 0, 255), 4);
				}
			}
		}
		//imshow("top(shape)", u_img_gray);
		//imshow("top(hsv)", u_img_hsv);
		imshow("top(color)", img_mask1);
		imshow("top(x, y)", u_img_frame);

		waitKey(1);
		*_callFlag = true;

	}
	catch (Exception e) {
		return false;
	}
	/************ 이상 상단 캠 ************



	// 1 ms 동안 ESC의 키 입력을 대기
	// 해당 키 입력이 확인되면 캠 종료
	//if (waitKey(1) == 27) cv_TrackingOff();

	return true;
	}

	//__declspec(dllexport) bool cv_Tracking_2(int* _left, int* _width, int* _height, bool* _callFlag_2, int* _count)
	__declspec(dllexport) bool cv_Tracking_2(int* _height, bool* _callFlag_2, int* _count)
	{
		//*_callFlag = true; // 함수가 호출(값 갱신)되는 순간마다 true -> Unity 내에서 갱신된 값을 읽을 때마다 false로 바꾸어 준다.
	//*_count = cap.get(CAP_PROP_FPS);
	//count++;
	//*_count = count;


		Scalar red(0, 0, 255);
		Scalar blue(255, 0, 0);
		Scalar yellow(0, 255, 255);
		Scalar magenta(255, 0, 255);
		Scalar lime(0, 238, 179); // lime color
		Scalar greenLower(29, 86, 6), greenUpper(64, 255, 255);

		/**************************		[ 전면 캠 ] 공의 좌표(zPos) 검출		***************************

		Mat img_frame_top;
		UMat u_img_frame_top, u_img_hsv, u_img_gray, u_img_cut;

		try {
			//	//*_count = *_count + 1;


			// 프레임 읽어오기. 캠 화면을 img_frame에 저장
			cap2.read(img_frame_top);

			// 카메라에 빈 영상이 담겼을 경우
			if (img_frame_top.empty()) {
				return false;
			}
			u_img_frame_top = img_frame_top.getUMat(AccessFlag::ACCESS_READ);
			u_img_cut = u_img_frame_top(Range(190, 290), Range(270, 370));
			GaussianBlur(u_img_cut, u_img_cut, Size(5, 5), 0);

			cvtColor(u_img_cut, u_img_gray, COLOR_BGR2GRAY); // shape_based detection
			cvtColor(u_img_cut, u_img_hsv, COLOR_BGR2HSV); // color_based detection

			/********************** 색 기반 트래킹 전처리 **********************

			//	//지정한 HSV 범위를 이용하여 영상을 이진화
			Mat img_mask1;
			inRange(u_img_hsv, greenLower, greenUpper, img_mask1);

			// 정확도를 높이기 위한 구문. 성능에 영향을 준다.
			//morphological opening 작은 점들을 제거
			erode(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));
			dilate(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));
			////morphological closing 영역의 구멍 메우기
			dilate(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));
			erode(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));

			//라벨링
			Mat img_labels, stats, centroids;
			int numOfLables = connectedComponentsWithStats(img_mask1, img_labels, stats, centroids, 8, CV_32S);

			/********** 색 기반 트래킹 전처리 종료 ***********

			/**************** 색 및 모양 추적에 대해 동시에 검출된 대상 지정  *****************
				// 1. '색'으로 인식된 물체의 중심(colorCenter)을 저장
				// 2. '모양'으로 인식된 물체들을 순회하며 '색'인식의 결과와 중심 좌표를 비교
				// 3. 일치되는 물체(녹색이면서 공의 모양인 물체)를 공으로 결정내리며 정보를 출력 및 반환
				// 4. 없다면?

				// 색으로 인식된 물체들을 순회
			int max = -1, idx = 0;
			for (int j = 1; j < numOfLables; j++) {
				int area = stats.at<int>(j, CC_STAT_AREA);
				if (max < area)
				{
					// 너무 작거나 크지 않은 물체에 대해서 선별
					if (stats.at <int>(j, CC_STAT_WIDTH) > 10) {
						max = area;
						idx = j;
					}
				}
			}
			// 물체의 인식 정보를 유니티로 넘겨서 그 정보를 이용해 좌표(x, z축) 탐색
			int left = stats.at<int>(idx, CC_STAT_LEFT);
			int top = stats.at<int>(idx, CC_STAT_TOP);
			int width = stats.at <int>(idx, CC_STAT_WIDTH);
			int height = stats.at<int>(idx, CC_STAT_HEIGHT);


			// 색 기반으로 인식된 물체의 중심 좌표. 모양을 기반으로 한 차례 더 검사를 거쳐 공인지 판단한다.
			Point colorCenter = Point(left + (width / 2), top + (height / 2));

			// pass to unity
			*_height = top;


			Canny(u_img_gray, u_img_gray, 100, 30, 3);

			vector<Vec3f> circles;
			HoughCircles(u_img_gray, circles, HOUGH_GRADIENT, 1, 100, 20, 10, 10, 40);
			//HoughCircles(u_img_gray, circles, HOUGH_GRADIENT, 1, 100, 20, 50, 20, 40);
			//

			for (size_t i = 0; i < circles.size(); i++)
			{
				Vec3i c = circles[i];



				Point center(c[0] + 270, c[1] + 190);
				// 모양으로 인식한 물체가 색으로 인식한 물체와 중심점이 비슷(errorRange: 오차범위)한 경우
				// 동그란 + 녹색 물체를 화면에 표시 >> 공을 추출할 수 있게 됨
				int errorRange = 5;
				if (colorCenter.x + errorRange > c[0] && colorCenter.x - errorRange < c[0]) {
					if (colorCenter.y + errorRange > c[1] && colorCenter.y - errorRange < c[1]) {

						// 녹색 + 원. 공의 중심을 추출
						Point shapeCenter(c[0] + 270, c[1] + 190);
						int radius = c[2];

						// 이전 프레임과 비교했을 때 중심점의 흔들림이 '거의' 없었다면
						// 이전 프레임에서 검출했던 공의 정보를 그대로 사용
						// 공의 흔들림을 최소화하는 구문
						if (centerPos_top.x + errorRange > shapeCenter.x && centerPos_top.x - errorRange < shapeCenter.x) {
							if (centerPos_top.y + errorRange > shapeCenter.y && centerPos_top.y - errorRange < shapeCenter.y) {
								shapeCenter = centerPos_top;
								radius = centerRadius;
							}
						}


						circle(u_img_frame_top, shapeCenter, radius, Scalar(0, 255, 0), 2);
						circle(u_img_frame_top, shapeCenter, 2, Scalar(255, 0, 0), 3);

						centerPos_top = shapeCenter;
						centerRadius = radius;

						// 인식 범위 표시
						rectangle(u_img_frame_top, Rect(Point(270, 190), Point(370, 290)), Scalar(0, 0, 255), 4);
					}
				}
			}

			imshow("front(shape)", u_img_gray);
			imshow("front(hsv)", u_img_hsv);
			imshow("front(color)", img_mask1);
			imshow("front(z)", u_img_frame_top);

			waitKey(1);

			//if (*_left != left) *_callFlag = true; // 함수가 호출(값 갱신)되는 순간마다 true -> Unity 내에서 갱신된 값을 읽을 때마다 false로 바꾸어 준다.
			*_callFlag_2 = true;

		}
		catch (Exception e) {
			return false;
		}
		return true;
	}
	*/

	// 02-07 17:53 // Tracking Func Save. before apply gpu
	/*
	__declspec(dllexport) bool cv_Tracking(int* _left, int* _width, int* _top, bool* _callFlag, int* _count)
	{
		//*_callFlag = true; // 함수가 호출(값 갱신)되는 순간마다 true -> Unity 내에서 갱신된 값을 읽을 때마다 false로 바꾸어 준다.
		//*_count = cap.get(CAP_PROP_FPS);
		//count++;
		//*_count = count;
		*_count = *_count + 1;

		int range_count = 0;

		Scalar red(0, 0, 255);
		Scalar blue(255, 0, 0);
		Scalar yellow(0, 255, 255);
		Scalar magenta(255, 0, 255);
		Scalar lime(0, 238, 179); // lime color

		// Set tracking target(ball)'s color
		Scalar target = lime;

		Mat rgb_color = Mat(1, 1, CV_8UC3, target);
		//Mat rgb_color = Mat(1, 1, CV_8UC3, red);
		Mat hsv_color;

		cvtColor(rgb_color, hsv_color, COLOR_BGR2HSV); // 녹색의 hsv color 추출 어떻게?

		int hue = (int)hsv_color.at<Vec3b>(0, 0)[0];
		int saturation = (int)hsv_color.at<Vec3b>(0, 0)[1];
		int value = (int)hsv_color.at<Vec3b>(0, 0)[2];

		int low_hue = hue - 5;
		int high_hue = hue + 5;

		int low_hue1 = 0, low_hue2 = 0;
		int high_hue1 = 0, high_hue2 = 0;

		if (low_hue < 5) {
			range_count = 2;

			high_hue1 = 180;
			low_hue1 = low_hue + 180;
			high_hue2 = high_hue;
			low_hue2 = 0;
		}
		else if (high_hue > 175) {
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

		/**************************		[ 상단 캠 ] 공의 좌표(xPos, yPos) 검출		***************************

	Mat img_frame, img_hsv;
	try {
		*_count = *_count + 1;


		//// 프레임 읽어오기. 캠 화면을 img_frame에 저장
		cap.read(img_frame);

		// 카메라에 빈 영상이 담겼을 경우
		if (img_frame.empty()) {
			return false;
		}

		//HSV로 변환
		resize(img_frame, img_frame, Size(600, 480));
		GaussianBlur(img_frame, img_frame, Size(7, 7), 0);
		cvtColor(img_frame, img_hsv, COLOR_BGR2HSV);

		//지정한 HSV 범위를 이용하여 영상을 이진화
		Mat img_mask1, img_mask2;
		inRange(img_hsv, Scalar(low_hue1, 50, 50), Scalar(high_hue1, 255, 255), img_mask1);

		if (range_count == 2) {
			inRange(img_hsv, Scalar(low_hue2, 50, 50), Scalar(high_hue2, 255, 255), img_mask2);
			img_mask1 |= img_mask2;
		}


		// 정확도를 높이기 위한 구문. 성능에 영향을 준다.
		// 가장 크게 인식된 것(공)만 사용하면 되기 때문에 굳이 그 외 작은 점들을 지우는 과정은 불필요하므로 생략한다?

		//morphological opening 작은 점들을 제거
		erode(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));
		dilate(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));

		////morphological closing 영역의 구멍 메우기
		//dilate(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));
		//erode(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));

		//라벨링
		Mat img_labels, stats, centroids;
		int numOfLables = connectedComponentsWithStats(img_mask1, img_labels, stats, centroids, 8, CV_32S);

		//영역박스 그리기
		int max = -1, idx = 0;
		for (int j = 1; j < numOfLables; j++) {
			int area = stats.at<int>(j, CC_STAT_AREA);
			if (max < area)
			{
				//if (stats.at <int>(j, CC_STAT_WIDTH) < stats.at <int>(j, CC_STAT_HEIGHT) + 10 && stats.at <int>(j, CC_STAT_WIDTH) > stats.at <int>(j, CC_STAT_HEIGHT) - 10)
				//{
				max = area;
				idx = j;
				//}
			}
		}

		// 물체의 인식 정보를 유니티로 넘겨서 그 정보를 이용해 좌표(x, z축) 탐색
		int left = stats.at<int>(idx, CC_STAT_LEFT);
		int top = stats.at<int>(idx, CC_STAT_TOP);
		int width = stats.at <int>(idx, CC_STAT_WIDTH);
		int height = stats.at<int>(idx, CC_STAT_HEIGHT);



		// pass to unity
		*_left = left;
		*_top = top;
		*_width = width;
		//*_height = height;

		//rectangle(img_frame, Point(left, top), Point(left + width, top + height), Scalar(0, 0, 255), 1);
		circle(img_frame, Point(left + (width / 2), top + (height / 2)), width / 2, Scalar(0, 0, 255));

		hconcat(img_frame, saved_frame_top, img_frame);
		//hconcat(img_mask1, saved_frame_mask, img_mask1);

		//vconcat(img_frame, img_mask1, img_frame);

		//imshow("이진화 영상", img_mask1);
		imshow("top(x, y)", img_frame); // 빨간 박스로 영역 표시
		//imshow("masks", img_mask1);

		waitKey(1);

		//if (*_left != left) *_callFlag = true; // 함수가 호출(값 갱신)되는 순간마다 true -> Unity 내에서 갱신된 값을 읽을 때마다 false로 바꾸어 준다.
		*_callFlag = true;

	}
	catch (Exception e) {
		return false;
	}
	/************ 이상 상단 캠 ************



	// 1 ms 동안 ESC의 키 입력을 대기
	// 해당 키 입력이 확인되면 캠 종료
	//if (waitKey(1) == 27) cv_TrackingOff();

	return true;
	}

	//__declspec(dllexport) bool cv_Tracking_2(int* _left, int* _width, int* _height, bool* _callFlag_2, int* _count)
	__declspec(dllexport) bool cv_Tracking_2(int* _height, bool* _callFlag_2, int* _count)
	{
		//*_callFlag = true; // 함수가 호출(값 갱신)되는 순간마다 true -> Unity 내에서 갱신된 값을 읽을 때마다 false로 바꾸어 준다.
	//*_count = cap.get(CAP_PROP_FPS);
	//count++;
	//*_count = count;

		int range_count = 0;

		Scalar red(0, 0, 255);
		Scalar blue(255, 0, 0);
		Scalar yellow(0, 255, 255);
		Scalar magenta(255, 0, 255);
		Scalar lime(0, 238, 179); // lime color

		// Set tracking target(ball)'s color
		Scalar target = lime;

		Mat rgb_color = Mat(1, 1, CV_8UC3, target);
		//Mat rgb_color = Mat(1, 1, CV_8UC3, red);
		Mat hsv_color;



		cvtColor(rgb_color, hsv_color, COLOR_BGR2HSV); // 녹색의 hsv color 추출 어떻게?

		int hue = (int)hsv_color.at<Vec3b>(0, 0)[0];
		int saturation = (int)hsv_color.at<Vec3b>(0, 0)[1];
		int value = (int)hsv_color.at<Vec3b>(0, 0)[2];

		int low_hue = hue - 5;
		int high_hue = hue + 5;

		int low_hue1 = 0, low_hue2 = 0;
		int high_hue1 = 0, high_hue2 = 0;

		if (low_hue < 5) {
			range_count = 2;

			high_hue1 = 180;
			low_hue1 = low_hue + 180;
			high_hue2 = high_hue;
			low_hue2 = 0;
		}
		else if (high_hue > 175) {
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

		///**************************		[ 전면 캠 ] 공의 좌표(zPos) 검출		***************************

		Mat img_frame_top, img_hsv;
		try {
			//	//*_count = *_count + 1;


				// 프레임 읽어오기. 캠 화면을 img_frame에 저장
			cap2.read(img_frame_top);

			// 카메라에 빈 영상이 담겼을 경우
			if (img_frame_top.empty()) {
				return false;
			}

			//HSV로 변환
			//resize(img_frame_top, img_frame_top, Size(600, 480));
			GaussianBlur(img_frame_top, img_frame_top, Size(7, 7), 0);
			cvtColor(img_frame_top, img_hsv, COLOR_BGR2HSV);

			//지정한 HSV 범위를 이용하여 영상을 이진화
			Mat img_mask1, img_mask2;
			inRange(img_hsv, Scalar(low_hue1, 50, 50), Scalar(high_hue1, 255, 255), img_mask1);

			if (range_count == 2) {
				inRange(img_hsv, Scalar(low_hue2, 50, 50), Scalar(high_hue2, 255, 255), img_mask2);
				img_mask1 |= img_mask2;
			}


			// 정확도를 높이기 위한 구문. 성능에 영향을 준다.
			// 가장 크게 인식된 것(공)만 사용하면 되기 때문에 굳이 그 외 작은 점들을 지우는 과정은 불필요하므로 생략한다?


			//morphological opening 작은 점들을 제거
			erode(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));
			dilate(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));

			////morphological closing 영역의 구멍 메우기
			//dilate(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));
			//erode(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));

			//라벨링
			Mat img_labels, stats, centroids;
			int numOfLables = connectedComponentsWithStats(img_mask1, img_labels, stats, centroids, 8, CV_32S);

			//영역박스 그리기
			int max = -1, idx = 0;
			for (int j = 1; j < numOfLables; j++) {
				int area = stats.at<int>(j, CC_STAT_AREA);
				if (max < area)
				{
					//if (stats.at <int>(j, CC_STAT_WIDTH) < stats.at <int>(j, CC_STAT_HEIGHT) + 10 && stats.at <int>(j, CC_STAT_WIDTH) > stats.at <int>(j, CC_STAT_HEIGHT) - 10)
					//{
					max = area;
					idx = j;
					//}
				}
			}

			// 물체의 인식 정보를 유니티로 넘겨서 그 정보를 이용해 좌표(x, z축) 탐색
			int left = stats.at<int>(idx, CC_STAT_LEFT);
			int top = stats.at<int>(idx, CC_STAT_TOP);
			int width = stats.at <int>(idx, CC_STAT_WIDTH);
			int height = stats.at<int>(idx, CC_STAT_HEIGHT);

			// pass to unity
			*_height = top;

			//rectangle(img_frame, Point(left, top), Point(left + width, top + height), Scalar(0, 0, 255), 1);
			circle(img_frame_top, Point(left + (width / 2), top + (height / 2)), width / 2, Scalar(0, 0, 255));

			//hconcat(img_frame, img_frame, img_frame);
			saved_frame_top = img_frame_top;
			//saved_frame_mask = img_mask1;

			//imshow("이진화 영상", img_mask1);
			//imshow("top(z)", img_frame_top); // 빨간 박스로 영역 표시
			waitKey(1);

			//if (*_left != left) *_callFlag = true; // 함수가 호출(값 갱신)되는 순간마다 true -> Unity 내에서 갱신된 값을 읽을 때마다 false로 바꾸어 준다.
			*_callFlag_2 = true;

		}
		catch (Exception e) {
			return false;
		}
		return true;
	}
	*/

	// 02-06 11:11 // Tracking Func Save. Color Tracking. right before apply shape-based tracking
	/*
	__declspec(dllexport) bool cv_Tracking(int* _left, int* _width, int* _top, bool* _callFlag, int* _count)
	{
		//*_callFlag = true; // 함수가 호출(값 갱신)되는 순간마다 true -> Unity 내에서 갱신된 값을 읽을 때마다 false로 바꾸어 준다.
		//*_count = cap.get(CAP_PROP_FPS);
		//count++;
		//*_count = count;

		int range_count = 0;

		Scalar red(0, 0, 255);
		Scalar blue(255, 0, 0);
		Scalar yellow(0, 255, 255);
		Scalar magenta(255, 0, 255);
		Scalar lime(0, 238, 179); // lime color

		// Set tracking target(ball)'s color
		Scalar target = lime;

		Mat rgb_color = Mat(1, 1, CV_8UC3, target);
		//Mat rgb_color = Mat(1, 1, CV_8UC3, red);
		Mat hsv_color;

		cvtColor(rgb_color, hsv_color, COLOR_BGR2HSV); // 녹색의 hsv color 추출 어떻게?

		int hue = (int)hsv_color.at<Vec3b>(0, 0)[0];
		int saturation = (int)hsv_color.at<Vec3b>(0, 0)[1];
		int value = (int)hsv_color.at<Vec3b>(0, 0)[2];

		int low_hue = hue - 10;
		int high_hue = hue + 10;

		int low_hue1 = 0, low_hue2 = 0;
		int high_hue1 = 0, high_hue2 = 0;

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

		/**************************		[ 상단 캠 ] 공의 좌표(xPos, yPos) 검출		***************************

	Mat img_frame, img_frame_top, img_hsv;
	try {
		*_count = *_count + 1;


		// 프레임 읽어오기. 캠 화면을 img_frame에 저장
		cap.read(img_frame);

		// 카메라에 빈 영상이 담겼을 경우
		if (img_frame.empty()) {
			return false;
		}

		//HSV로 변환
		cvtColor(img_frame, img_hsv, COLOR_BGR2HSV);

		//지정한 HSV 범위를 이용하여 영상을 이진화
		Mat img_mask1, img_mask2;
		inRange(img_hsv, Scalar(low_hue1, 50, 50), Scalar(high_hue1, 255, 255), img_mask1);

		if (range_count == 2) {
			inRange(img_hsv, Scalar(low_hue2, 50, 50), Scalar(high_hue2, 255, 255), img_mask2);
			img_mask1 |= img_mask2;
		}


		// 정확도를 높이기 위한 구문. 성능에 영향을 준다.
		// 가장 크게 인식된 것(공)만 사용하면 되기 때문에 굳이 그 외 작은 점들을 지우는 과정은 불필요하므로 생략한다?


		////morphological opening 작은 점들을 제거
		//erode(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));
		//dilate(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));

		////morphological closing 영역의 구멍 메우기
		//dilate(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));
		//erode(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));

		//라벨링
		Mat img_labels, stats, centroids;
		int numOfLables = connectedComponentsWithStats(img_mask1, img_labels, stats, centroids, 8, CV_32S);

		//영역박스 그리기
		int max = -1, idx = 0;
		for (int j = 1; j < numOfLables; j++) {
			int area = stats.at<int>(j, CC_STAT_AREA);
			if (max < area)
			{
				if (stats.at <int>(j, CC_STAT_WIDTH) < stats.at <int>(j, CC_STAT_HEIGHT) + 10 && stats.at <int>(j, CC_STAT_WIDTH) > stats.at <int>(j, CC_STAT_HEIGHT) - 10)
				{
					max = area;
					idx = j;
				}
			}
		}

		// 물체의 인식 정보를 유니티로 넘겨서 그 정보를 이용해 좌표(x, z축) 탐색
		int left = stats.at<int>(idx, CC_STAT_LEFT);
		int top = stats.at<int>(idx, CC_STAT_TOP);
		int width = stats.at <int>(idx, CC_STAT_WIDTH);
		int height = stats.at<int>(idx, CC_STAT_HEIGHT);




		// pass to unity
		*_left = left;
		*_top = top;
		*_width = width;
		//*_height = height;

		rectangle(img_frame, Point(left, top), Point(left + width, top + height), Scalar(0, 0, 255), 1);

		hconcat(img_frame, saved_frame_top, img_frame);

		//imshow("이진화 영상", img_mask1);
		imshow("top(x, y)", img_frame); // 빨간 박스로 영역 표시
		waitKey(1);

		//if (*_left != left) *_callFlag = true; // 함수가 호출(값 갱신)되는 순간마다 true -> Unity 내에서 갱신된 값을 읽을 때마다 false로 바꾸어 준다.
		*_callFlag = true;

	}
	catch (Exception e) {
		return false;
	}
	/************ 이상 상단 캠 ************



	// 1 ms 동안 ESC의 키 입력을 대기
	// 해당 키 입력이 확인되면 캠 종료
	//if (waitKey(1) == 27) cv_TrackingOff();

	return true;
	}

	//__declspec(dllexport) bool cv_Tracking_2(int* _left, int* _width, int* _height, bool* _callFlag_2, int* _count)
	__declspec(dllexport) bool cv_Tracking_2(int* _height, bool* _callFlag_2, int* _count)
	{
		//*_callFlag = true; // 함수가 호출(값 갱신)되는 순간마다 true -> Unity 내에서 갱신된 값을 읽을 때마다 false로 바꾸어 준다.
		//*_count = cap.get(CAP_PROP_FPS);
		//count++;
		//*_count = count;


		int range_count = 0;

		Scalar red(0, 0, 255);
		Scalar blue(255, 0, 0);
		Scalar yellow(0, 255, 255);
		Scalar magenta(255, 0, 255);
		Scalar lime(0, 238, 179); // lime color

		// Set tracking target(ball)'s color
		Scalar target = lime;

		Mat rgb_color = Mat(1, 1, CV_8UC3, target);
		//Mat rgb_color = Mat(1, 1, CV_8UC3, red);
		Mat hsv_color;

		cvtColor(rgb_color, hsv_color, COLOR_BGR2HSV); // 녹색의 hsv color 추출 어떻게?

		int hue = (int)hsv_color.at<Vec3b>(0, 0)[0];
		int saturation = (int)hsv_color.at<Vec3b>(0, 0)[1];
		int value = (int)hsv_color.at<Vec3b>(0, 0)[2];

		int low_hue = hue - 10;
		int high_hue = hue + 10;

		int low_hue1 = 0, low_hue2 = 0;
		int high_hue1 = 0, high_hue2 = 0;

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

		/**************************		[ 전면 캠 ] 공의 높이(zPos) 검출		***************************

		Mat img_frame, img_frame_top, img_hsv;

		try {
			// 프레임 읽어오기. 캠 화면을 img_frame에 저장
			cap2.read(img_frame_top);

			// 카메라에 빈 영상이 담겼을 경우
			if (img_frame_top.empty()) {
				return false;
			}

			//HSV로 변환
			cvtColor(img_frame_top, img_hsv, COLOR_BGR2HSV);

			//지정한 HSV 범위를 이용하여 영상을 이진화
			Mat img_mask1, img_mask2;
			inRange(img_hsv, Scalar(low_hue1, 50, 50), Scalar(high_hue1, 255, 255), img_mask1);

			if (range_count == 2) {
				inRange(img_hsv, Scalar(low_hue2, 50, 50), Scalar(high_hue2, 255, 255), img_mask2);
				img_mask1 |= img_mask2;
			}


			////morphological opening 작은 점들을 제거
			//erode(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));
			//dilate(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));

			////morphological closing 영역의 구멍 메우기 ?
			//dilate(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));
			//erode(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));

			//라벨링
			Mat img_labels, stats, centroids;

			int numOfLables = connectedComponentsWithStats(img_mask1, img_labels, stats, centroids, 8, CV_32S);

			//영역박스 그리기
			int max = -1, idx = 0;
			for (int j = 1; j < numOfLables; j++) {
				int area = stats.at<int>(j, CC_STAT_AREA);
				if (max < area)
				{
					if (stats.at <int>(j, CC_STAT_WIDTH) < stats.at <int>(j, CC_STAT_HEIGHT) + 10 && stats.at <int>(j, CC_STAT_WIDTH) > stats.at <int>(j, CC_STAT_HEIGHT) - 10)
					{
						max = area;
						idx = j;
					}
				}
			}

			// 물체의 인식 정보를 유니티로 넘겨서 그 정보를 이용해 좌표(x, z축) 탐색
			int left = stats.at<int>(idx, CC_STAT_LEFT);
			int top = stats.at<int>(idx, CC_STAT_TOP);
			int width = stats.at <int>(idx, CC_STAT_WIDTH);
			int height = stats.at<int>(idx, CC_STAT_HEIGHT);

			//if (*_left != left) *_callFlag_2 = true; // 함수가 호출(값 갱신)되는 순간마다 true -> Unity 내에서 갱신된 값을 읽을 때마다 false로 바꾸어 준다.

			//*_height = top; // 영상 내 y 좌표가 공의 높이에 해당.
			*_height = 0;

			rectangle(img_frame_top, Point(left, top), Point(left + width, top + height), Scalar(0, 0, 255), 1);

			saved_frame_top = img_frame_top;

			//imshow("이진화 영상", img_mask1);
			//imshow("front(z)", img_frame_top); // 빨간 박스로 영역 표시
			waitKey(1);

			*_callFlag_2 = true; // 함수가 호출됨을 알림


		}
		catch (Exception e) {
			return false;
		}

		/************ 이상 전면 캠 ************

		// 1 ms 동안 ESC의 키 입력을 대기
		// 해당 키 입력이 확인되면 캠 종료
		//if (waitKey(1) == 27) cv_TrackingOff();

		return true;
	}
	*/

	// 02-05 18:00 // Tracking Func Save. right before roll-back. because of Runtime error
	/*

	__declspec(dllexport) bool cv_Tracking(int* _left, int* _width, int* _top, bool* _callFlag, int* _count)
	{
		//*_callFlag = true; // 함수가 호출(값 갱신)되는 순간마다 true -> Unity 내에서 갱신된 값을 읽을 때마다 false로 바꾸어 준다.
		//*_count = cap.get(CAP_PROP_FPS);
		//count++;
		//*_count = count;

		using namespace std;

		int range_count = 0;

		Scalar red(0, 0, 255);
		Scalar blue(255, 0, 0);
		Scalar yellow(0, 255, 255);
		Scalar magenta(255, 0, 255);
		Scalar lime(0, 255, 191); // lime color

		// Set tracking target(ball)'s color
		Scalar target = lime;

		Mat rgb_color = Mat(1, 1, CV_8UC3, target);
		//Mat rgb_color = Mat(1, 1, CV_8UC3, red);
		Mat hsv_color;

		cvtColor(rgb_color, hsv_color, COLOR_BGR2HSV); // 녹색의 hsv color 추출 어떻게?

		int hue = (int)hsv_color.at<Vec3b>(0, 0)[0];
		int saturation = (int)hsv_color.at<Vec3b>(0, 0)[1];
		int value = (int)hsv_color.at<Vec3b>(0, 0)[2];

		int low_hue = hue - 10;
		int high_hue = hue + 10;

		int low_hue1 = 0, low_hue2 = 0;
		int high_hue1 = 0, high_hue2 = 0;

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

		/**************************		[ 상단 캠 ] 공의 좌표(xPos, yPos) 검출		***************************

		Mat img_frame, img_frame_top, img_hsv;
		try {
			for (;;) {
				*_count = *_count + 1;


				// 프레임 읽어오기. 캠 화면을 img_frame에 저장
				cap.read(img_frame);

				// 카메라에 빈 영상이 담겼을 경우
				if (img_frame.empty()) {
					return false;
				}

				//HSV로 변환
				cvtColor(img_frame, img_hsv, COLOR_BGR2HSV);

				//지정한 HSV 범위를 이용하여 영상을 이진화
				Mat img_mask1, img_mask2;
				inRange(img_hsv, Scalar(low_hue1, 50, 50), Scalar(high_hue1, 255, 255), img_mask1);

				if (range_count == 2) {
					inRange(img_hsv, Scalar(low_hue2, 50, 50), Scalar(high_hue2, 255, 255), img_mask2);
					img_mask1 |= img_mask2;
				}


				// 정확도를 높이기 위한 구문. 성능에 영향을 준다.
				// 가장 크게 인식된 것(공)만 사용하면 되기 때문에 굳이 그 외 작은 점들을 지우는 과정은 불필요하므로 생략한다?


				////morphological opening 작은 점들을 제거
				//erode(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));
				//dilate(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));

				////morphological closing 영역의 구멍 메우기
				//dilate(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));
				//erode(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));

				//라벨링
				Mat img_labels, stats, centroids;
				int numOfLables = connectedComponentsWithStats(img_mask1, img_labels, stats, centroids, 8, CV_32S);

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

				// 물체의 인식 정보를 유니티로 넘겨서 그 정보를 이용해 좌표(x, z축) 탐색
				int left = stats.at<int>(idx, CC_STAT_LEFT);
				int top = stats.at<int>(idx, CC_STAT_TOP);
				int width = stats.at <int>(idx, CC_STAT_WIDTH);
				int height = stats.at<int>(idx, CC_STAT_HEIGHT);




				// pass to unity
				*_left = left;
				*_top = top;
				*_width = width;
				//*_height = height;

				rectangle(img_frame, Point(left, top), Point(left + width, top + height), Scalar(0, 0, 255), 1);

				//imshow("이진화 영상", img_mask1);
				imshow("top(x, y)", img_frame); // 빨간 박스로 영역 표시
				waitKey(1);

				//if (*_left != left) *_callFlag = true; // 함수가 호출(값 갱신)되는 순간마다 true -> Unity 내에서 갱신된 값을 읽을 때마다 false로 바꾸어 준다.
				*_callFlag = true;
			}
		}
		catch (Exception e) {
			return false;
		}
		/************ 이상 상단 캠 ************



		// 1 ms 동안 ESC의 키 입력을 대기
		// 해당 키 입력이 확인되면 캠 종료
		//if (waitKey(1) == 27) cv_TrackingOff();

		return true;
	}

	//__declspec(dllexport) bool cv_Tracking_2(int* _left, int* _width, int* _height, bool* _callFlag_2, int* _count)
	__declspec(dllexport) bool cv_Tracking_2(int* _height, bool* _callFlag_2, int* _count)
	{
		//*_callFlag = true; // 함수가 호출(값 갱신)되는 순간마다 true -> Unity 내에서 갱신된 값을 읽을 때마다 false로 바꾸어 준다.
		//*_count = cap.get(CAP_PROP_FPS);
		//count++;
		//*_count = count;

		using namespace std;

		int range_count = 0;

		Scalar red(0, 0, 255);
		Scalar blue(255, 0, 0);
		Scalar yellow(0, 255, 255);
		Scalar magenta(255, 0, 255);
		Scalar lime(0, 255, 191); // lime color

		// Set tracking target(ball)'s color
		Scalar target = lime;

		Mat rgb_color = Mat(1, 1, CV_8UC3, target);
		//Mat rgb_color = Mat(1, 1, CV_8UC3, red);
		Mat hsv_color;

		cvtColor(rgb_color, hsv_color, COLOR_BGR2HSV); // 녹색의 hsv color 추출 어떻게?

		int hue = (int)hsv_color.at<Vec3b>(0, 0)[0];
		int saturation = (int)hsv_color.at<Vec3b>(0, 0)[1];
		int value = (int)hsv_color.at<Vec3b>(0, 0)[2];

		int low_hue = hue - 10;
		int high_hue = hue + 10;

		int low_hue1 = 0, low_hue2 = 0;
		int high_hue1 = 0, high_hue2 = 0;

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

		/**************************		[ 전면 캠 ] 공의 높이(zPos) 검출		***************************

		Mat img_frame, img_frame_top, img_hsv;

		try {
			for (;;) {
				// 프레임 읽어오기. 캠 화면을 img_frame에 저장
				cap2.read(img_frame_top);

				// 카메라에 빈 영상이 담겼을 경우
				if (img_frame_top.empty()) {
					return false;
				}

				//HSV로 변환
				cvtColor(img_frame_top, img_hsv, COLOR_BGR2HSV);

				//지정한 HSV 범위를 이용하여 영상을 이진화
				Mat img_mask1, img_mask2;
				inRange(img_hsv, Scalar(low_hue1, 50, 50), Scalar(high_hue1, 255, 255), img_mask1);

				if (range_count == 2) {
					inRange(img_hsv, Scalar(low_hue2, 50, 50), Scalar(high_hue2, 255, 255), img_mask2);
					img_mask1 |= img_mask2;
				}


				////morphological opening 작은 점들을 제거
				//erode(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));
				//dilate(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));

				////morphological closing 영역의 구멍 메우기 ?
				//dilate(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));
				//erode(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));

				//라벨링
				Mat img_labels, stats, centroids;

				int numOfLables = connectedComponentsWithStats(img_mask1, img_labels, stats, centroids, 8, CV_32S);

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

				// 물체의 인식 정보를 유니티로 넘겨서 그 정보를 이용해 좌표(x, z축) 탐색
				int left = stats.at<int>(idx, CC_STAT_LEFT);
				int top = stats.at<int>(idx, CC_STAT_TOP);
				int width = stats.at <int>(idx, CC_STAT_WIDTH);
				int height = stats.at<int>(idx, CC_STAT_HEIGHT);

				//if (*_left != left) *_callFlag_2 = true; // 함수가 호출(값 갱신)되는 순간마다 true -> Unity 내에서 갱신된 값을 읽을 때마다 false로 바꾸어 준다.

				*_height = top; // 영상 내 y 좌표가 공의 높이에 해당.
				//*_height = 0;

				rectangle(img_frame_top, Point(left, top), Point(left + width, top + height), Scalar(0, 0, 255), 1);


				//imshow("이진화 영상", img_mask1);
				imshow("front(z)", img_frame_top); // 빨간 박스로 영역 표시
				waitKey(1);

				*_callFlag_2 = true; // 함수가 호출됨을 알림

			}
		}
		catch (Exception e) {
			return false;
		}

		/************ 이상 전면 캠 ************

		// 1 ms 동안 ESC의 키 입력을 대기
		// 해당 키 입력이 확인되면 캠 종료
		//if (waitKey(1) == 27) cv_TrackingOff();

		return true;
	}

	*/

	// 02-03 11:23 // Tracking Func Save
	/*
	__declspec(dllexport) bool cv_Tracking(int* _left, int* _width, int* _top, int* _height, bool* _callFlag, int* _count)
	{
		//*_callFlag = true; // 함수가 호출(값 갱신)되는 순간마다 true -> Unity 내에서 갱신된 값을 읽을 때마다 false로 바꾸어 준다.
		//*_count = cap.get(CAP_PROP_FPS);
		//count++;
		//*_count = count;

		using namespace std;

		int range_count = 0;

		Scalar red(0, 0, 255);
		Scalar blue(255, 0, 0);
		Scalar yellow(0, 255, 255);
		Scalar magenta(255, 0, 255);
		Scalar lime(0, 255, 191); // lime color

		// Set tracking target(ball)'s color
		Scalar target = lime;

		Mat rgb_color = Mat(1, 1, CV_8UC3, target);
		//Mat rgb_color = Mat(1, 1, CV_8UC3, red);
		Mat hsv_color;

		cvtColor(rgb_color, hsv_color, COLOR_BGR2HSV); // 녹색의 hsv color 추출 어떻게?

		int hue = (int)hsv_color.at<Vec3b>(0, 0)[0];
		int saturation = (int)hsv_color.at<Vec3b>(0, 0)[1];
		int value = (int)hsv_color.at<Vec3b>(0, 0)[2];

		int low_hue = hue - 10;
		int high_hue = hue + 10;

		int low_hue1 = 0, low_hue2 = 0;
		int high_hue1 = 0, high_hue2 = 0;

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

		/**************************		[ 상단 캠 ] 공의 좌표(xPos, yPos) 검출		***************************

		Mat img_frame, img_frame_top, img_hsv;

		// 프레임 읽어오기. 캠 화면을 img_frame에 저장
		cap.read(img_frame);

		// 카메라에 빈 영상이 담겼을 경우
		if (img_frame.empty()) {
			return false;
		}

		//HSV로 변환
		cvtColor(img_frame, img_hsv, COLOR_BGR2HSV);

		//지정한 HSV 범위를 이용하여 영상을 이진화
		Mat img_mask1, img_mask2;
		inRange(img_hsv, Scalar(low_hue1, 50, 50), Scalar(high_hue1, 255, 255), img_mask1);

		if (range_count == 2) {
			inRange(img_hsv, Scalar(low_hue2, 50, 50), Scalar(high_hue2, 255, 255), img_mask2);
			img_mask1 |= img_mask2;
		}


		// 정확도를 높이기 위한 구문. 성능에 영향을 준다.
		// 가장 크게 인식된 것(공)만 사용하면 되기 때문에 굳이 그 외 작은 점들을 지우는 과정은 불필요하므로 생략한다?


		////morphological opening 작은 점들을 제거
		//erode(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));
		//dilate(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));

		////morphological closing 영역의 구멍 메우기
		//dilate(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));
		//erode(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));

		//라벨링
		Mat img_labels, stats, centroids;
		int numOfLables = connectedComponentsWithStats(img_mask1, img_labels, stats, centroids, 8, CV_32S);

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

		// 물체의 인식 정보를 유니티로 넘겨서 그 정보를 이용해 좌표(x, z축) 탐색
		int left = stats.at<int>(idx, CC_STAT_LEFT);
		int top = stats.at<int>(idx, CC_STAT_TOP);
		int width = stats.at <int>(idx, CC_STAT_WIDTH);
		int height = stats.at<int>(idx, CC_STAT_HEIGHT);

		if (*_left != left) *_callFlag = true; // 함수가 호출(값 갱신)되는 순간마다 true -> Unity 내에서 갱신된 값을 읽을 때마다 false로 바꾸어 준다.

		// pass to unity
		*_left = left;
		*_top = top;
		*_width = width;
		//*_height = height;

		rectangle(img_frame, Point(left, top), Point(left + width, top + height), Scalar(0, 0, 255), 1);

		//imshow("이진화 영상", img_mask1);
		imshow("top(x, y)", img_frame); // 빨간 박스로 영역 표시


		/************ 이상 상단 캠 ************

		/**************************		[ 전면 캠 ] 공의 높이(zPos) 검출		***************************

		//// 프레임 읽어오기. 캠 화면을 img_frame에 저장
		//cap2.read(img_frame_top);

		//// 카메라에 빈 영상이 담겼을 경우
		//if (img_frame_top.empty()) {
		//	return false;
		//}

		////HSV로 변환
		//cvtColor(img_frame_top, img_hsv, COLOR_BGR2HSV);

		////지정한 HSV 범위를 이용하여 영상을 이진화
		//inRange(img_hsv, Scalar(low_hue1, 50, 50), Scalar(high_hue1, 255, 255), img_mask1);

		//if (range_count == 2) {
		//	inRange(img_hsv, Scalar(low_hue2, 50, 50), Scalar(high_hue2, 255, 255), img_mask2);
		//	img_mask1 |= img_mask2;
		//}


		//////morphological opening 작은 점들을 제거
		//erode(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));
		//dilate(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));

		////morphological closing 영역의 구멍 메우기 ?
		//dilate(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));
		//erode(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));

		////라벨링
		//numOfLables = connectedComponentsWithStats(img_mask1, img_labels, stats, centroids, 8, CV_32S);

		////영역박스 그리기
		//max = -1; idx = 0;
		//for (int j = 1; j < numOfLables; j++) {
		//	int area = stats.at<int>(j, CC_STAT_AREA);
		//	if (max < area)
		//	{
		//		max = area;
		//		idx = j;
		//	}
		//}

		//// 물체의 인식 정보를 유니티로 넘겨서 그 정보를 이용해 좌표(x, z축) 탐색
		//left = stats.at<int>(idx, CC_STAT_LEFT);
		//top = stats.at<int>(idx, CC_STAT_TOP);
		//width = stats.at <int>(idx, CC_STAT_WIDTH);
		//height = stats.at<int>(idx, CC_STAT_HEIGHT);

		//*_height = top; // 영상 내 y 좌표가 공의 높이에 해당.


		//rectangle(img_frame_top, Point(left, top), Point(left + width, top + height), Scalar(0, 0, 255), 1);

		////imshow("이진화 영상", img_mask1);
		//imshow("front(z)", img_frame_top); // 빨간 박스로 영역 표시

		/************ 이상 전면 캠 ************

		// 1 ms 동안 ESC의 키 입력을 대기
		// 해당 키 입력이 확인되면 캠 종료
		if (waitKey(1) == 27) cv_TrackingOff();

		return true;
	}
	*/

	// 01-17 16:50 // Tracking Func Save
	/*
	__declspec(dllexport) bool cv_Tracking(int* _left, int* _width, int* _top, int* _height, bool* _callFlag, int* _count)
	{
		//*_callFlag = true; // 함수가 호출(값 갱신)되는 순간마다 true -> Unity 내에서 갱신된 값을 읽을 때마다 false로 바꾸어 준다.
		*_count = cap.get(CAP_PROP_FPS);

		using namespace std;

		int range_count = 0;

		Scalar red(0, 0, 255);
		Scalar blue(255, 0, 0);
		Scalar yellow(0, 255, 255);
		Scalar magenta(255, 0, 255);
		Scalar lime(0, 255, 191); // lime color

		// Set tracking target(ball)'s color
		Scalar target = lime;

		Mat rgb_color = Mat(1, 1, CV_8UC3, target);
		//Mat rgb_color = Mat(1, 1, CV_8UC3, red);
		Mat hsv_color;

		cvtColor(rgb_color, hsv_color, COLOR_BGR2HSV); // 녹색의 hsv color 추출 어떻게?

		int hue = (int)hsv_color.at<Vec3b>(0, 0)[0];
		int saturation = (int)hsv_color.at<Vec3b>(0, 0)[1];
		int value = (int)hsv_color.at<Vec3b>(0, 0)[2];

		int low_hue = hue - 10;
		int high_hue = hue + 10;

		int low_hue1 = 0, low_hue2 = 0;
		int high_hue1 = 0, high_hue2 = 0;

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

		/**************************		[ 상단 캠 ] 공의 좌표(xPos, yPos) 검출		***************************

	Mat img_frame, img_frame_top, img_hsv;

	// 프레임 읽어오기. 캠 화면을 img_frame에 저장
	cap.read(img_frame);

	// 카메라에 빈 영상이 담겼을 경우
	if (img_frame.empty()) {
		return false;
	}

	//HSV로 변환
	cvtColor(img_frame, img_hsv, COLOR_BGR2HSV);

	//지정한 HSV 범위를 이용하여 영상을 이진화
	Mat img_mask1, img_mask2;
	inRange(img_hsv, Scalar(low_hue1, 50, 50), Scalar(high_hue1, 255, 255), img_mask1);

	if (range_count == 2) {
		inRange(img_hsv, Scalar(low_hue2, 50, 50), Scalar(high_hue2, 255, 255), img_mask2);
		img_mask1 |= img_mask2;
	}


	// 정확도를 높이기 위한 구문. 성능에 영향을 준다.
	// 가장 크게 인식된 것(공)만 사용하면 되기 때문에 굳이 그 외 작은 점들을 지우는 과정은 불필요하므로 생략한다.

	//morphological opening 작은 점들을 제거
	erode(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));
	dilate(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));

	//morphological closing 영역의 구멍 메우기
	dilate(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));
	erode(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));

	//라벨링
	Mat img_labels, stats, centroids;
	int numOfLables = connectedComponentsWithStats(img_mask1, img_labels, stats, centroids, 8, CV_32S);

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




	// 물체의 인식 정보를 유니티로 넘겨서 그 정보를 이용해 좌표(x, z축) 탐색
	int left = stats.at<int>(idx, CC_STAT_LEFT);
	int top = stats.at<int>(idx, CC_STAT_TOP);
	int width = stats.at <int>(idx, CC_STAT_WIDTH);
	int height = stats.at<int>(idx, CC_STAT_HEIGHT);

	if (*_left != left) *_callFlag = true; // 함수가 호출(값 갱신)되는 순간마다 true -> Unity 내에서 갱신된 값을 읽을 때마다 false로 바꾸어 준다.

	// pass to unity
	*_left = left;
	*_top = top;
	*_width = width;
	//*_height = height;

	rectangle(img_frame, Point(left, top), Point(left + width, top + height), Scalar(0, 0, 255), 1);

	//imshow("이진화 영상", img_mask1);
	imshow("top(x, y)", img_frame); // 빨간 박스로 영역 표시

	/************ 이상 상단 캠 ************

	/**************************		[ 전면 캠 ] 공의 높이(zPos) 검출		***************************

	// 프레임 읽어오기. 캠 화면을 img_frame에 저장
	cap2.read(img_frame_top);

	// 카메라에 빈 영상이 담겼을 경우
	if (img_frame_top.empty()) {
		return false;
	}

	//HSV로 변환
	cvtColor(img_frame_top, img_hsv, COLOR_BGR2HSV);

	//지정한 HSV 범위를 이용하여 영상을 이진화
	inRange(img_hsv, Scalar(low_hue1, 50, 50), Scalar(high_hue1, 255, 255), img_mask1);

	if (range_count == 2) {
		inRange(img_hsv, Scalar(low_hue2, 50, 50), Scalar(high_hue2, 255, 255), img_mask2);
		img_mask1 |= img_mask2;
	}


	////morphological opening 작은 점들을 제거
	erode(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));
	dilate(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));

	//morphological closing 영역의 구멍 메우기 ?
	dilate(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));
	erode(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));

	//라벨링
	numOfLables = connectedComponentsWithStats(img_mask1, img_labels, stats, centroids, 8, CV_32S);

	//영역박스 그리기
	max = -1; idx = 0;
	for (int j = 1; j < numOfLables; j++) {
		int area = stats.at<int>(j, CC_STAT_AREA);
		if (max < area)
		{
			max = area;
			idx = j;
		}
	}

	// 물체의 인식 정보를 유니티로 넘겨서 그 정보를 이용해 좌표(x, z축) 탐색
	left = stats.at<int>(idx, CC_STAT_LEFT);
	top = stats.at<int>(idx, CC_STAT_TOP);
	width = stats.at <int>(idx, CC_STAT_WIDTH);
	height = stats.at<int>(idx, CC_STAT_HEIGHT);

	*_height = top; // 영상 내 y 좌표가 공의 높이에 해당.


	rectangle(img_frame_top, Point(left, top), Point(left + width, top + height), Scalar(0, 0, 255), 1);

	//imshow("이진화 영상", img_mask1);
	imshow("front(z)", img_frame_top); // 빨간 박스로 영역 표시

	/************ 이상 전면 캠 ************


	// 1 ms 동안 ESC의 키 입력을 대기
	// 해당 키 입력이 확인되면 캠 종료
	if (waitKey(1) == 27) cv_TrackingOff();

	return true;
	}
	*/

	// 01-06 13:50 // Tracking Func Save
	/*
	__declspec(dllexport) bool cv_Tracking(int* _left, int* _width, int* _top, int* _height)
	{
		using namespace std;

		int range_count = 0;

		Scalar red(0, 0, 255);
		Scalar blue(255, 0, 0);
		Scalar yellow(0, 255, 255);
		Scalar magenta(255, 0, 255);
		Scalar lime(0, 255, 191); // lime color

		// Set tracking target(ball)'s color
		Scalar target = lime;

		Mat rgb_color = Mat(1, 1, CV_8UC3, target);
		//Mat rgb_color = Mat(1, 1, CV_8UC3, red);
		Mat hsv_color;

		cvtColor(rgb_color, hsv_color, COLOR_BGR2HSV); // 녹색의 hsv color 추출 어떻게?

		int hue = (int)hsv_color.at<Vec3b>(0, 0)[0];
		int saturation = (int)hsv_color.at<Vec3b>(0, 0)[1];
		int value = (int)hsv_color.at<Vec3b>(0, 0)[2];

		int low_hue = hue - 10;
		int high_hue = hue + 10;

		int low_hue1 = 0, low_hue2 = 0;
		int high_hue1 = 0, high_hue2 = 0;

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

		Mat img_frame, img_hsv;

		//// fps 계산?
		//cap.get(CAP_PROP_FPS);

		// 프레임 읽어오기. 캠 화면을 img_frame에 저장
		cap.read(img_frame);

		// 카메라에 빈 영상이 담겼을 경우
		if (img_frame.empty()) {
			return false;
		}

		//HSV로 변환
		cvtColor(img_frame, img_hsv, COLOR_BGR2HSV);

		//지정한 HSV 범위를 이용하여 영상을 이진화
		Mat img_mask1, img_mask2;
		inRange(img_hsv, Scalar(low_hue1, 50, 50), Scalar(high_hue1, 255, 255), img_mask1);

		if (range_count == 2) {
			inRange(img_hsv, Scalar(low_hue2, 50, 50), Scalar(high_hue2, 255, 255), img_mask2);
			img_mask1 |= img_mask2;
		}


		////morphological opening 작은 점들을 제거
		erode(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));
		dilate(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));

		////morphological closing 영역의 구멍 메우기 ?
		//dilate(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));
		//erode(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));

		//라벨링
		Mat img_labels, stats, centroids;
		int numOfLables = connectedComponentsWithStats(img_mask1, img_labels, stats, centroids, 8, CV_32S);

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

		// 물체의 인식 정보를 유니티로 넘겨서 그 정보를 이용해 좌표(x, z축) 탐색
		int left = stats.at<int>(idx, CC_STAT_LEFT);
		int top = stats.at<int>(idx, CC_STAT_TOP);
		int width = stats.at<int>(idx, CC_STAT_WIDTH);
		int height = stats.at<int>(idx, CC_STAT_HEIGHT);

		// pass to unity
		*_left = left;
		*_top = top;
		*_width = width;
		*_height = height;

		rectangle(img_frame, Point(left, top), Point(left + width, top + height), Scalar(0, 0, 255), 1);

		imshow("이진화 영상", img_mask1);
		imshow("원본 영상", img_frame); // 빨간 박스로 영역 표시

		waitKey(1);

		return true;
	}
	*/

	// 01-01 14:26 // Tracking Func Save
	/*
	__declspec(dllexport) bool cv_Tracking(int* _left, int* _width, int* _top, int* _height, int targetColor = TARGET_LIME) // 0~4 r,b,y,m,l(d) selectColor
	{
		using namespace std;

		int range_count = 0;

		Scalar red(0, 0, 255);
		Scalar blue(255, 0, 0);
		Scalar yellow(0, 255, 255);
		Scalar magenta(255, 0, 255);
		Scalar lime(0, 255, 191); // lime color

		// Set tracking target(ball)'s color
		Scalar target;
		switch (targetColor) {
		case TARGET_RED:
			target = red;
			break;
		case TARGET_BLUE:
			target = blue;
			break;
		case TARGET_YELLOW:
			target = yellow;
			break;
		case TARGET_MAGENTA:
			target = magenta;
			break;
		case TARGET_LIME:
			target = lime;
			break;
		}

		Mat rgb_color = Mat(1, 1, CV_8UC3, target);
		//Mat rgb_color = Mat(1, 1, CV_8UC3, red);
		Mat hsv_color;

		cvtColor(rgb_color, hsv_color, COLOR_BGR2HSV); // 녹색의 hsv color 추출 어떻게?

		int hue = (int)hsv_color.at<Vec3b>(0, 0)[0];
		int saturation = (int)hsv_color.at<Vec3b>(0, 0)[1];
		int value = (int)hsv_color.at<Vec3b>(0, 0)[2];

		int low_hue = hue - 10;
		int high_hue = hue + 10;

		int low_hue1 = 0, low_hue2 = 0;
		int high_hue1 = 0, high_hue2 = 0;

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

		Mat img_frame, img_hsv;

		//// fps 계산?
		//cap.get(CAP_PROP_FPS);

		// 프레임 읽어오기. 캠 화면을 img_frame에 저장
		cap.read(img_frame);

		// 카메라에 빈 영상이 담겼을 경우
		if (img_frame.empty()) {
			return false;
		}

		//HSV로 변환
		cvtColor(img_frame, img_hsv, COLOR_BGR2HSV);

		//지정한 HSV 범위를 이용하여 영상을 이진화
		Mat img_mask1, img_mask2;
		inRange(img_hsv, Scalar(low_hue1, 50, 50), Scalar(high_hue1, 255, 255), img_mask1);

		if (range_count == 2) {
			inRange(img_hsv, Scalar(low_hue2, 50, 50), Scalar(high_hue2, 255, 255), img_mask2);
			img_mask1 |= img_mask2;
		}

		////morphological opening 작은 점들을 제거
		//erode(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));
		//dilate(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));

		////morphological closing 영역의 구멍 메우기 ?
		//dilate(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));
		//erode(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));

		//라벨링
		Mat img_labels, stats, centroids;
		int numOfLables = connectedComponentsWithStats(img_mask1, img_labels, stats, centroids, 8, CV_32S);

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

		// 박스의 상하좌우 >> 유니티로 넘겨서 크기를 이용해 깊이(z축) 탐색
		int left = stats.at<int>(idx, CC_STAT_LEFT);
		int top = stats.at<int>(idx, CC_STAT_TOP);
		int width = stats.at<int>(idx, CC_STAT_WIDTH);
		int height = stats.at<int>(idx, CC_STAT_HEIGHT);

		// pass to unity
		*_left = left;
		*_top = top;
		*_width = width;
		*_height = height;

		rectangle(img_frame, Point(left, top), Point(left + width, top + height), Scalar(0, 0, 255), 1);

		imshow("이진화 영상", img_mask1);
		imshow("원본 영상", img_frame); // 빨간 박스로 영역 표시

		return true;
	}
	*/

	// 12-30 13:31 // Tracking Func Save
	/*
	__declspec(dllexport) bool Tracking(int* _left, int* _width, int* _top, int* _height)
	{
		using namespace cv;
		using namespace std;

		int range_count = 0;

		Scalar red(0, 0, 255);
		Scalar blue(255, 0, 0);
		Scalar yellow(0, 255, 255);

		Scalar magenta(255, 0, 255);

		Scalar target(0, 255, 191); // 라임색

		Mat rgb_color = Mat(1, 1, CV_8UC3, target);
		//Mat rgb_color = Mat(1, 1, CV_8UC3, red);
		Mat hsv_color;

		cvtColor(rgb_color, hsv_color, COLOR_BGR2HSV); // 녹색의 hsv color 추출 어떻게?

		int hue = (int)hsv_color.at<Vec3b>(0, 0)[0];
		int saturation = (int)hsv_color.at<Vec3b>(0, 0)[1];
		int value = (int)hsv_color.at<Vec3b>(0, 0)[2];

		int low_hue = hue - 10;
		int high_hue = hue + 10;

		int low_hue1 = 0, low_hue2 = 0;
		int high_hue1 = 0, high_hue2 = 0;

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

		Mat img_frame, img_hsv;

		VideoCapture cap(0);

		//VideoCapture cap;

		//if (!cap.isOpened())
		//	cap.open(0);
		// open , release?



		// 여기 위로는 반복 호출될 필요가 없음 *********************************************************************
		// 아니면 무한루프 걸고 async 호출?

		//// fps 계산?
		//cap.get(CAP_PROP_FPS);

		// 캠 화면을 img_frame에 저장
		cap.read(img_frame);

		// 카메라에 빈 영상이 담기는 경우
		if (img_frame.empty()) {
			//cerr << "ERROR! blank frame grabbed\n";
			return false;
			//break;
		}

		//HSV로 변환
		cvtColor(img_frame, img_hsv, COLOR_BGR2HSV);

		//지정한 HSV 범위를 이용하여 영상을 이진화
		Mat img_mask1, img_mask2;
		inRange(img_hsv, Scalar(low_hue1, 50, 50), Scalar(high_hue1, 255, 255), img_mask1);

		if (range_count == 2) {
			inRange(img_hsv, Scalar(low_hue2, 50, 50), Scalar(high_hue2, 255, 255), img_mask2);
			img_mask1 |= img_mask2;
		}

		//morphological opening 작은 점들을 제거
		erode(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));
		dilate(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));

		//morphological closing 영역의 구멍 메우기 ?
		dilate(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));
		erode(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));

		//라벨링
		Mat img_labels, stats, centroids;
		int numOfLables = connectedComponentsWithStats(img_mask1, img_labels, stats, centroids, 8, CV_32S);

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


		// 박스의 상하좌우 >> 유니티로 넘겨서 크기를 이용해 깊이(z축) 탐색
		int left = stats.at<int>(idx, CC_STAT_LEFT);
		int top = stats.at<int>(idx, CC_STAT_TOP);
		int width = stats.at<int>(idx, CC_STAT_WIDTH);
		int height = stats.at<int>(idx, CC_STAT_HEIGHT);

		// pass to unity
		*_left = left;
		*_top = top;
		*_width = width;
		*_height = height;

		rectangle(img_frame, Point(left, top), Point(left + width, top + height), Scalar(0, 0, 255), 1);

		//imshow("이진화 영상", img_mask1);
		imshow("원본 영상", img_frame); // 빨간 박스로 영역 표시

		cap.release();

		return true;
	}
	*/

	// 12-27.15:54 // Tracking Func Save
	/*_declspec(dllexport) bool Tracking(void)
	{
		using namespace cv;
		using namespace std;

		int range_count = 0;

		Scalar red(0, 0, 255);
		Scalar blue(255, 0, 0);
		Scalar yellow(0, 255, 255);

		Scalar magenta(255, 0, 255);

		Scalar target(0, 255, 191); // 라임색

		Mat rgb_color = Mat(1, 1, CV_8UC3, target);
		//Mat rgb_color = Mat(1, 1, CV_8UC3, red);
		Mat hsv_color;

		cvtColor(rgb_color, hsv_color, COLOR_BGR2HSV); // 녹색의 hsv color 추출 어떻게?


		int hue = (int)hsv_color.at<Vec3b>(0, 0)[0];
		int saturation = (int)hsv_color.at<Vec3b>(0, 0)[1];
		int value = (int)hsv_color.at<Vec3b>(0, 0)[2];



		int low_hue = hue - 10;
		int high_hue = hue + 10;

		int low_hue1 = 0, low_hue2 = 0;
		int high_hue1 = 0, high_hue2 = 0;

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

		Mat img_frame, img_hsv;

		VideoCapture cap(0);

		// 카메라를 열 수 없는 경우
		//if (!cap.isOpened()) {
		//	//cerr << "ERROR! Unable to open camera\n";
		//	return false;
		//}

		// Tracking Start
		//for (;;)
		//{
		// wait for a new frame from camera and store it into 'frame'

		// 카메라로부터 캡처한 영상을 img_frame에 저장
		cap.read(img_frame);


		//// 카메라에 빈 영상이 담기는 경우
		//if (img_frame.empty()) {
		//	//cerr << "ERROR! blank frame grabbed\n";
		//	return;
		//	//break;
		//}

		//imshow("Frame", img_frame);

		//HSV로 변환
		cvtColor(img_frame, img_hsv, COLOR_BGR2HSV);



		//지정한 HSV 범위를 이용하여 영상을 이진화
		Mat img_mask1, img_mask2;
		inRange(img_hsv, Scalar(low_hue1, 50, 50), Scalar(high_hue1, 255, 255), img_mask1);
		//inRange(img_hsv, Scalar(50, low_hue1, 50), Scalar(255, high_hue1, 255), img_mask1);

		if (range_count == 2) {
			inRange(img_hsv, Scalar(low_hue2, 50, 50), Scalar(high_hue2, 255, 255), img_mask2);
			//inRange(img_hsv, Scalar(50, low_hue2, 50), Scalar(255, high_hue2, 255), img_mask2);
			img_mask1 |= img_mask2;
		}


		//morphological opening 작은 점들을 제거
		erode(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));
		dilate(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));


		//morphological closing 영역의 구멍 메우기
		dilate(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));
		erode(img_mask1, img_mask1, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));


		//라벨링
		Mat img_labels, stats, centroids;
		int numOfLables = connectedComponentsWithStats(img_mask1, img_labels, stats, centroids, 8, CV_32S);


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


		// 박스의 상하좌우 >> 유니티로 넘겨서 크기를 이용해 깊이(z축) 탐색
		int left = stats.at<int>(idx, CC_STAT_LEFT);
		int top = stats.at<int>(idx, CC_STAT_TOP);
		int width = stats.at<int>(idx, CC_STAT_WIDTH);
		int height = stats.at<int>(idx, CC_STAT_HEIGHT);

		rectangle(img_frame, Point(left, top), Point(left + width, top + height), Scalar(0, 0, 255), 1);


		//imshow("이진화 영상", img_mask1);
		imshow("원본 영상", img_frame); // 빨간 박스로 영역 표시

		return true;
	}*/

#pragma endregion
}