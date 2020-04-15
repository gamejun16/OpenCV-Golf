using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.UI;


public class ProcessController : MonoBehaviour
{


    /*************************
     * 
     * 게임의 전체 프로세스를 총괄하는 스크립트
     * 
     * 준비 - 시작 - 종료
     * 
     * **********************/




    [SerializeField]
    GhostBallController ghostBallController; // 타격 방향, 세기의 계산에 사용되는 렌더링되지 않는 공

    [SerializeField]
    BallController ballController;

    [SerializeField]
    BallMaterialController ballMaterialController;

    [SerializeField]
    BallTracker ballTracker;

    [SerializeField]
    LogPrinter logPrinter;


    [SerializeField]
    GameObject BALL;// 움직일 공

    /// 공이 완전히 '정지'한 상황에 대해 각 축으로의 미세한 흔들림을 허용하는 범위
    /// X cm 의 오차 허용
    //  OpenCV 내에서 흔들림이 보정(허용)된 값이 전달됨. 따라서 오차의 범위를 또 설정할 필요 x
    //  [SerializeField]
    float errorRange = 0f;

    [SerializeField]
    Start2EndPointSaver[] start2EndPointSaver;

    //[SerializeField]
    //Transform[] StartBallPosition; // 각 홀의 시작 지점을 저장
    //[SerializeField]
    //Transform[] FinishPoint; // 도착 지점
    int curHall; // 현재 진행중인 홀

    float flyingDistance;


    [HideInInspector]
    public ScoreRecorder scoreRecorder;

    Vector3 startPos; // start position of ball
    Vector3 hitStartPos; // first frame position right after hit the ball


    // 프레임을 비교해 공의 타격 순간 운동량을 계산
    Vector3 beforePos;
    int beforeTime;
    Vector3 afterPos;

    // 공이 진행한 거리를 계산하기 위한 타격 직전 공의 좌표
    Vector3 beforeHitPosition;

    float timer;

    /// 0 -> PROCESS.READY 첫 번째 프레임 위치 정보 저장 -> 1
    /// 1 -> OpenCV 내 공 위치 정보 이동 대기 -> 이동 확인 -> 2
    /// 2 -> PROCESS.READY 두 번째 프레임 위치 정보 저장 -> 0
    // 0 -> PROCESS.READY 진입 후 위치에 변화를 보이는 첫 프레임 대기 -> 이동 확인 -> 1
    // 1 -> 위치 정보 저장 -> 2
    // 2 -> PROCESS.READY 진입 후 위치에 변화를 보이는 두 번째 프레임 대기 -> 이동 확인 -> 3
    // 3 -> 위치 정보 저장 -> 0
    byte secondFrameFlag;

    // 첫 번째 프레임의 left 값
    // 공의 이동이 검출되었다가 두 번째 프레임에 다시 제자리가 찍힌다면(진행 방향이 반대가 된다면)
    // 순간적인 오작동(오인식)으로 간주하고 이를 무시한다
    int errorCheckLeftValue = 0;

    [SerializeField]
    Image fadeImage; // 준비가 덜 되었을 때 화면을 어둡게 하기 위한 페이드 이미지

    public static int process; // 진행 단계
                               // 0: 준비 전
                               // 1: (공 n초 제자리에 머무를 시) 준비 완료 및 타격 대기 - 타격
                               //    (타격시) 1~2 프레임 검사 및 발사세기, 발사각 등 계산 후 AddForce()
                               // 2: 날아가는 공 보여주기 및 정지 확인
                               // 3: 골인 X. 그 자리에서 계속 진행(0 으로 복귀)
                               // 4: 골인 O. 결과 출력 및 종료(0 으로 복귀)
                               // 5: FALLED. 물 등에 빠져 경기 진행이 불가한 경우

    public bool isFinish;

    public enum PROCESS
    {
        WAIT, READY, HITTED, NOTFINISH, FINISH, FALLED, 
    }

    // Start is called before the first frame update
    void Start()
    {
        curHall = 0;
        fadeImage.enabled = true;
        process = 0;
        timer = 0f;
        beforeTime = 0;
        //BALL.transform.position = StartBallPosition[curHall].position;
        BALL.transform.position = start2EndPointSaver[curHall].StartPoint.position;
        Camera.main.transform.parent.position = BALL.transform.position;

        BALL.transform.LookAt(start2EndPointSaver[curHall].FinishPointAxis.position);
        scoreRecorder.initScore();

        isFinish = false;
        secondFrameFlag = 0;
        logPrinter.setLeftDistance(BALL.transform.position, start2EndPointSaver[curHall].FinishPointAxis.position);
    }

    // Update is called once per frame
    void Update()
    {
        //ballTracker.cvCallFlag = true;
        //ballTracker.cvCallFlag_2 = true;
        //Camera.main.transform.parent.LookAt(FinishPoint[curHall].position);
        Camera.main.transform.parent.LookAt(start2EndPointSaver[curHall].FinishPointAxis);

        

        if (process == (int)PROCESS.WAIT && ballTracker.left == 0)
        {
            Debug.Log("공이 검출되지 않음");
        }
        else
        {
            timer += Time.deltaTime;
        }


        // 0~1 process 까지는 영상 프레임에 맞춰서 동작
        // 2~ 부터는 유니티 Update() 루프에 맞춰서 동작
        if (process >= (int)PROCESS.HITTED || (ballTracker.cvCallFlag))// && ballTracker.cvCallFlag_2))//|| ballTracker.cvCallFlag_front)
        {

            // 두 개의 캠이 모두 갱신되었다면
            if (ballTracker.cvCallFlag )//&& ballTracker.cvCallFlag_2)
                //if (ballTracker.cvCallFlag_2)
            {
                // Ready 상태에서 , 4 장의 프레임을 검사.
                if(process == (int)PROCESS.READY && (secondFrameFlag == 0 || secondFrameFlag == 2 || secondFrameFlag == 4 || secondFrameFlag == 6 || secondFrameFlag == 8))
                {
                    secondFrameFlag += 1; // 위치정보가 갱신되었으므로 읽어들이면 됨을 전달
                }
                Debug.Log("갱신 성공");
                //ghostBallController.move(ballTracker.left + (int)(ballTracker.width * 0.5f), ballTracker.height + (int)(ballTracker.width * 0.5f), ballTracker.top + (int)(ballTracker.width * 0.5f));
                //ghostBallController.move(ballTracker.left, ballTracker.height, ballTracker.top); // (좌우, 상하, 앞뒤)
                ghostBallController.move(ballTracker.top, ballTracker.height, ballTracker.left); // (좌우, 상하, 앞뒤)

                ballTracker.cvCallFlag = false;
                ballTracker.cvCallFlag_2 = false;
                //ballTracker.count++; // 1frame
            }

            switch (process)
            {
                case (int)PROCESS.WAIT: // 준비 전
                                        // 공이 흔들림 없이 제자리에서 n초간 머무르면 준비 완료
                    if (timer > 3f)
                    {
                        timer = 0f;
                        fadeImage.enabled = false;

                        startPos = ghostBallController.transform.position;

                        process = (int)PROCESS.READY;
                        Debug.Log("2초 경과, 준비 완료");
                    }
                    else if (isStay(ghostBallController.gameObject, startPos))
                    {
                        // 공이 제 자리에 서 있는 경우. 3초 경과시 준비 완료
                        Debug.Log(">>> waiting ... (" + (int)timer + "/3 sec)");
                    }

                    // 공의 흔들림이 감지된 경우
                    else
                    {
                        Debug.Log("새로운 시작 지점 설정");
                        startPos = ghostBallController.transform.position; // 새로운 시작 지점 갱신
                        timer = 0f;
                    }
                    break;

                case (int)PROCESS.READY: // 준비 완료 - 타격 대기

    

                    // 타격이 이루어진 순간 공 위치의 급격한 변화를 검출
                    if (secondFrameFlag == 1) // 네 장의 프레임을 검사하기위한 조건 (1/5)
                    {
                        if (!isStay(ghostBallController.gameObject, startPos, true, true))
                        {
                            // 공이 인식되지 않은 경우 : error
                            if (ballTracker.left == 0)
                            {
                                Debug.Log("오인식 : 공이 사라짐");
                                ballTracker.exposure_t.text = "ERROR";

                                // 초기화 및 프로세스 되돌리기
                                timer = 0f;
                                secondFrameFlag = 0;
                                process = (int)PROCESS.WAIT;
                                break;
                            }

                            // 타격한 공의 진행 방향이 다음 프레임에서 급격히 반대가 되는 경우를 에러로 판단한다
                            errorCheckLeftValue = ballTracker.left;

                            hitStartPos = ghostBallController.transform.position; // 첫 번째 검출된 위치
                            beforePos = hitStartPos;
                            Debug.Log("First Hit is checker >> "+beforePos);


                            beforeTime = ballTracker.clock;
                            //Debug.Log("beforeTime >> " + ballTracker.clock);

                            // 한 프레임만 진행되고 공이 검출되고 있음에도 추가적인 움직임이 없는 경우를 확인하기위한 장치
                            timer = 0f;

                            secondFrameFlag += 1;
                        }
                    }

                    else if(secondFrameFlag == 3 || secondFrameFlag == 5 || secondFrameFlag == 7 || secondFrameFlag == 9) // 네 장의 프레임을 검사하기위한 조건 (2~4/5)
                    {
                        if (!isStay(ghostBallController.gameObject, beforePos, true, true))
                        {
                            if (errorCheckLeftValue < ballTracker.left)
                            {
                                Debug.Log("오인식 : 일순간 공이 아닌 물체를 인식");
                                ballTracker.exposure_t.text = "ERROR";

                                // 초기화 및 프로세스 되돌리기
                                timer = 0f;
                                secondFrameFlag = 0;
                                process = (int)PROCESS.WAIT;
                                break;
                            }
                            // 공이 사라져 너무 큰 값이 잡힌 경우 OpenCV에서 left에 0을 반환한다
                            // 즉. 타격한 공이 카메라 밖으로 사라진 경우를 의미한다
                            // 바로 타격을 진행하고 다음 프로세스로 진행한다
                            else if (ballTracker.left == 0 || secondFrameFlag == 9)
                                // 공은 이미 지나갔는데 다른 무언가를 공으로 인지하고 검출하고 있다면?
                            {
                                Vector3 hit;

                                // 움직임이 확인되었는데 그 다음 프레임에 공이 확인되지 못한 경우
                                if (secondFrameFlag == 3)
                                {
                                    // 너무 강하게 타격이 이루어져 두 프레임만에 공이 화면 밖으로 벗어났다고 판단
                                    // = 방향만 체크하고 크기를 확인하지 못한 경우
                                    // 한 번 검출 된 타격 방향만 확인, 해당 방향으로 미리 설정한 최대 출력으로 타격을 진행한다
                                    hit = Vector3.Normalize(ghostBallController.transform.position - startPos) * 20f; // 임의의 세기로 출력

                                   
                                }
                                
                                // secondFrameFlag == 5, 7, 9
                                else // if (secondFrameFlag == 5 || secondFrameFlag == 7 || secondFrameFlag == 9)
                                {
                                    afterPos = ghostBallController.transform.position;


                                    Debug.Log("secondFrameFlag >> " + secondFrameFlag);
                                    
                                    hit = (afterPos - hitStartPos) * (1000f / (ballTracker.clock - beforeTime));
                                   
                                }
                                // 타격 직전 공의 좌표 저장(날아건 거리 계산용)
                                beforeHitPosition = BALL.transform.position;

                                // [임시] 높이 값 임의로 셋팅)


                                // 검출된 세기로 공 타격
                                // 타격 직전, 경사에서 정지되어있는 공의 마찰 정도를 초기화
                                ballController.initDrag();
                                ballTracker.exposure_t.text = "hit >> " + hit.magnitude;
                                ballTracker.frameRate_t.text = "" + (ballTracker.clock - beforeTime);

                                if (hit.magnitude > 5)
                                    hit.y = 3f;

                                //BALL.GetComponent<Rigidbody>().AddRelativeForce(hit * 0.25f * 0.7f, ForceMode.Impulse);
                                BALL.GetComponent<Rigidbody>().AddRelativeForce(hit * 0.2f, ForceMode.Impulse);

                                scoreRecorder.addHitCount(); // 1타 기록

                                // 초기화 및 프로세스 진행
                                timer = 0f;
                                ballTracker.TrackingOff();
                                secondFrameFlag = 0;

                                // 정지해있지 않은데 정지된것으로 인식되어 타격 직후 공이 제자리에 멈추는 버그를 방지하기 위함
                                ballController.velocity = 100;
                                process = (int)PROCESS.HITTED;

                                break;
                            }

                            // 타격한 공의 진행 방향이 급격히 반대가 되는 경우를 에러로 판단한다
                            errorCheckLeftValue = ballTracker.left;

                            // 검출된 순간 공의 위치
                            // 다음 프레임에서 이 위치와 공의 위치를 비교해 진행 방향을 확인한다
                            beforePos = ghostBallController.transform.position; 
               
                            secondFrameFlag += 1;
                        }
                        else
                        {
                            // 한 프레임 움직임이 검출되었는데 그 다음 공의 움직임이 없는 경우
                            // frameRate를 지연시키면서 공의 세기 등을 검출할 때에 한없이 작은 값을 야기하는 원인
                            // 따라서, 이 상황에서 일정 시간(약 0.5초 등)이 지연되면 secondFrameRate를 다시 1로 되돌린다

                            if(timer > 0.5f)
                            {
                                Debug.Log("오인식 : 불필요한 흔들림 감지");
                                ballTracker.exposure_t.text = "ERROR";

                                // 초기화 및 프로세스 되돌리기
                                timer = 0f;
                                secondFrameFlag = 0;
                                process = (int)PROCESS.WAIT;
                                break;
                            }
                        }
                    }
                  

      
                    break;

                case (int)PROCESS.HITTED: // 타격 직후. 카메라를 공에 고정시킨 후 날아가는 모습 출력 

                    // 카메라의 축(부모)을 공에 고정
                    Camera.main.transform.parent.position = BALL.transform.position;

                    // 허공에 떠 있지 않으면서 경사가 N 도 미만이라면 공의 저항 정도를 높여 무한히 구르지 못하게 함
                    // 다시금 경사가 높아진다면 저항 정도를 초기화
                    if (!ballMaterialController.isFlying && ballController.velocity < 2 && ballMaterialController.curSlope < 10)
                    {
                        ballController.addDrag();
                    }
                    else // 허공에 떠 있거나, 경사가 N 도 이상인 경우에는 저항을 높이지 않는다
                    {
                        ballController.initDrag();
                    }

                    // 타격한 공의 정지가 확인되면 다음 프로세스
                    if (ballController.velocity <= 0.01f) // 정지한 상태로 1초 경과하면 종료
                    {
                        //timer += Time.deltaTime;

                        // 종료
                        if (timer > 1f)
                        {
                            // 날아온 거리 계산
                            logPrinter.setFlyingDistance(beforeHitPosition, BALL.transform.position);
                            //logPrinter.setLeftDistance(BALL.transform.position, start2EndPointSaver[curHall].FinishPointAxis.position);

                            if (isFinish)
                            {
                                isFinish = false;
                                process = (int)PROCESS.FINISH;
                            }
                            else
                            {
                                Debug.Log("Ball is stop");
                                process = (int)PROCESS.NOTFINISH;
                            }
                        }
                    }
                    else
                    {
                        timer = 0f;
                    }

                    // 현재는 임시로 키 입력을 통해 다음 프로세스로 진행
                    if (Input.GetKeyDown(KeyCode.Space))
                    {
                        process = (int)PROCESS.FINISH;
                    }
                    break;

                case (int)PROCESS.NOTFINISH: // 정지. 결과 출력. 후 재시작
                    Debug.Log(">>> 계속 진행 >>>");
                    Camera.main.transform.parent.position = BALL.transform.position;

                    gameInit();
                    break;
                    
                case (int)PROCESS.FINISH:
                    Debug.Log(">>> 결과 출력 >>>");
                    Camera.main.transform.parent.position = BALL.transform.position;

                    scoreRecorder.finishHall();
                    gameInit(true);
                    break;

                case (int)PROCESS.FALLED:
                    // OB or Hazard에 공이 빠져 정상 진행이 불가한 경우
                    // 벌타 부여 및 공을 근처의 가까운 스폰 포인트로 이동

                    // 카메라 공 추적 정지 >> 프로세스가 넘어오면서 자동으로 풀림
                    // 공이 튀어서 다시 수면으로 올라오지 않게 

                    timer += Time.deltaTime;


                    // 게임 종료
                    if (timer > 2f)
                    {
                        timer = 0f;
                        process = (int)PROCESS.WAIT;


                        gameInit(BALL.GetComponentInChildren<BallMaterialController>().spawnPoint);
                    }
                    break;
            }
        }
        else
        {

            //print("영상의 다음 프레임이 업데이트되지 않았음");
        }
    }



    bool isStay(GameObject target, Vector3 stdPos, bool notCheckWidth = false, bool isHitted = false) // 타겟 , 기준 좌표
    {
        if (!notCheckWidth) // 기본값(false)으로는 공의 지름을 감안한다
        {
            //if (ballTracker.width > ghostBallController.maxWidth || ballTracker.width < ghostBallController.minWidth)
            //{
            //    // 공 크기 오차(오인식 검출)
            //    // 잔상을 오인식으로 검출하는 오류(길게 남은 잔상 전체를 인식하면 크기가
            //    // 너무 커져 이를 공으로 인식하지 않음)
            //    // 인식 크기를 고려하지 않을 수 있는 별도의 플래그 존재(notCheckWidth)
            //    //Debug.Log("공 인식 오류");
            //    return false;
            //}
        }


       /// float tmpErrorRange = errorRange;
        //if (isHitted)
        //{
        //    tmpErrorRange *= 5; // 타격이 이루어진 경우 훨씬 큰 좌표 변화에 대한 검출이 필요
        //}
   
        if (target.transform.position.x > stdPos.x + errorRange || target.transform.position.x < stdPos.x - errorRange)
        {
            // x 범위 오차
           // Debug.Log("x축 흔들림 감지");
            return false;
        }

        if (target.transform.position.y > stdPos.y + errorRange || target.transform.position.y < stdPos.y - errorRange)
        {
            // y 범위 오차
          //  Debug.Log("y축 흔들림 감지");
            return false;
        }

        if (target.transform.position.z > stdPos.z + errorRange || target.transform.position.z < stdPos.z - errorRange)
        {
            // z 범위 오차
           // Debug.Log("z축 흔들림 감지");
            return false;
        }
        return true;
    }

    // 게임을 종료 및 초기화 할 때 호출하는 함수
    public void gameInit(bool setStartPosition = false) // 시작 지점으로 돌아가는 경우
    {
        ballController.initDrag(true);
        BALL.GetComponent<Rigidbody>().velocity = Vector3.zero; // 공의 움직임 제거

        // 공을 시작 위치로 되돌리는 구문
        if (setStartPosition)
        {
            curHall = (curHall + 1) % 3;
            BALL.transform.position = start2EndPointSaver[curHall].StartPoint.position; Camera.main.transform.parent.position = BALL.transform.position;

            //scoreRecorder.finishHall(); // (NOT)FINISH 여부를 판단할 때 처리
        }

        // 공이 목표 지점을 바라보게 하는 구문
        // 앞으로 타격했을 때 공이 목표 지점으로 올바르게 날아갈 수 있어야 한다
        // 공이 구르는 경우 방향이 뒤집어지는 버그 존재
        BALL.transform.LookAt(start2EndPointSaver[curHall].FinishPointAxis);
        ballMaterialController.initRotateForSlope(start2EndPointSaver[curHall].FinishPointAxis.position);
        
        // 값 셋팅
        fadeImage.enabled = true;
        secondFrameFlag = 0;
        timer = 0f;
        beforeTime = 0;
        logPrinter.setLeftDistance(BALL.transform.position, start2EndPointSaver[curHall].FinishPointAxis.position);

        // 카메라 정렬
        ballTracker.TrackingOn();
        ballTracker.TrackingStart(); 

        process = (int)PROCESS.WAIT;
    }

    // [ Overrride ]
    public void gameInit(Vector3 spawnPoint) // Hazard에 빠져 공을 스폰 포인트로 이동시키는 경우
    {
        ballController.initDrag(true);
        BALL.GetComponent<Rigidbody>().velocity = Vector3.zero; // 공의 움직임 제거

        scoreRecorder.addHitCount(2); // 2벌타 부여

        // 위치 변경
        BALL.transform.position = spawnPoint;
        Camera.main.transform.parent.position = BALL.transform.position;

        // 공이 목표 지점을 바라보게 하는 구문
        // 앞으로 타격했을 때 공이 목표 지점으로 올바르게 날아갈 수 있어야 한다
        // 공이 구르는 경우 방향이 뒤집어지는 버그 존재
        BALL.transform.LookAt(start2EndPointSaver[curHall].FinishPointAxis);
        ballMaterialController.initRotateForSlope(start2EndPointSaver[curHall].FinishPointAxis.position);

        // 값 셋팅
        fadeImage.enabled = true;
        secondFrameFlag = 0;
        timer = 0f;
        beforeTime = 0;
        logPrinter.setLeftDistance(BALL.transform.position, start2EndPointSaver[curHall].FinishPointAxis.position);

        // 카메라 정렬
        ballTracker.TrackingOn();
        ballTracker.TrackingStart();
        
        // 진행
        process = (int)PROCESS.WAIT;
    }


}
