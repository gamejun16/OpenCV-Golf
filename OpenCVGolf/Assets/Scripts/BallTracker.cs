using UnityEngine;
using UnityEngine.UI;
using System.Runtime.InteropServices;
using System.Collections;

using System.Threading;

public class BallTracker : MonoBehaviour
{
    enum TARGET_COLOR
    {
        TARGET_RED, TARGET_BLUE, TARGET_YELLOW, TARGET_MAGENTA, TARGET_LIME,
    };

    [SerializeField]
    GhostBallController ghostBallController;

    [DllImport("OpenCVDLL")] // 상단 캠. x, z 축 정보 추출
    private static extern bool cv_Tracking(ref int _left, ref int _width, ref int _top, ref bool cvCallFlag, ref int clock, ref int count, bool isReadyDone = false);

    [DllImport("OpenCVDLL")] // 전면 캠. y 축 정보 추출
    private static extern bool cv_Tracking_2(ref int _height, ref bool cvCallFlag_2, ref int count, bool isReadyDone = false);

    [DllImport("OpenCVDLL")]
    private static extern bool cv_TrackingOn(); // cam open

    [DllImport("OpenCVDLL")]
    private static extern bool cv_TrackingOff(); // cam release

    // OpenCV에서 인식된 박스(공)의 좌표와 크기를 전달받는다
    // x: (topView) left
    // y: (topView) top
    // z: (frontView) height
    public int left, top, height, width;

    // 캠에서 매 프레임 cv_Tracking()이 호출될 때마다 true가 된다
    // 영상(약 30fps)은 아직 갱신되지 않았는데 유니티(약 100fps)만 공회전하며
    // 갱신되지 않은 값들 간 변화의 비교를 시도하는 것을 방지하기 위한 플래그
    [SerializeField]
    public bool cvCallFlag, cvCallFlag_2;

    // OpenCV 내, 프레임이 업데이트된 순간의 시간.
    // clock의 차를 이용해 프레임 시간 검출 가능
    public int clock;

    public int count;

    [SerializeField]
    public Text left_t, top_t, width_t, height_t, exposure_t, frameRate_t;
     
    [SerializeField]
    int camDelay = 1; // frame delay(단위: ms)

    // 스레드를 관리하기위한 변수
    Thread thread = null, thread_2 = null;
    bool threadIsOn= false; // 스레드 종료 여부를 전달하는 플래그

    float timer;
    int fps;

    // Start is called before the first frame update
    void Start()
    {
        timer = 0f;
        count = 0;
        fps = 0;

        if (thread == null && thread_2 == null)
        {
            // 카메라 준비
            if (!TrackingOn())
            {
                Debug.Log("Tracking On Error !!");
            }
            else
            {
                Debug.Log("Tracking On Success !!");
            }
            // 스레드 생성 및 Start
            TrackingStart();
        }
    }


    private void Update()
    {
        left_t.text = "leftPos >> " + left;
        top_t.text = "topPos >> " + top;
        width_t.text = "width >> " + width;
        height_t.text = "height >> " + height;
    }

    // 생명 주기. 프로그램이 종료되는 시점에 호출. 에디터 동작X
    private void OnApplicationQuit()
    {
        Debug.Log("[QUIT IS CALL]");
        TrackingOff();
    }

    // 트래킹 시작 준비
    public bool TrackingOn()
    {
        if (!cv_TrackingOn())
        {
            Debug.Log("(-1) cam on fail");

            return false;
        }
        Debug.Log("(1) cam on");
        return true;
    }
    // 트래킹(thread) 시작
    public void TrackingStart(int targetColor = 4)
    {
        thread = new Thread(ThreadTracking);
        thread_2 = new Thread(ThreadTracking_2);
        threadIsOn = true; //스레드 중지를 위한 플래그?

        thread.Start();
        thread_2.Start();
        Debug.Log("(2) thread start");
    }
    
    // 트래킹(카메라) 완전 종료
    public bool TrackingOff()
    {
        // 스레드를 가동케 하던 루프의 작동을 중단시킴
        threadIsOn = false;
        
        if (!cv_TrackingOff())
        {
            Debug.Log("cam off fail");
            return false;
        }

        Debug.Log("(4) cam off");

        // 스레드가 완전히 정지할 때까지 대기
        if (thread != null)
        {
            thread.Interrupt();
            thread = null;
        }
        if (thread_2 != null)
        {
            thread_2.Interrupt();
            thread_2 = null;
        }

        Debug.Log("(5) thread off");

        // 스레드 종료 완료?
        
        return true;
    }

    void ThreadTracking()
    {
        while (threadIsOn)
        {
            bool tmp = cv_Tracking(ref left, ref width, ref top, ref cvCallFlag, ref clock, ref count, (ProcessController.process == (int)ProcessController.PROCESS.WAIT ? false : true));
            if (!tmp)
            {
                Debug.Log("Top Cam Error");  
                TrackingOff();
                
                return;
            }
            else
            { } 
        }
        Debug.Log("(3) thread Top loop is done");
        return;
    }

    void ThreadTracking_2()
    {
        while (threadIsOn)
        {
            bool tmp = cv_Tracking_2(ref height, ref cvCallFlag_2, ref count, (ProcessController.process == (int)ProcessController.PROCESS.WAIT ? false : true));
            if (!tmp)
            {

                Debug.Log("Front Cam Error");
                TrackingOff();
                return;

            }
            else
            { }
        }
        Debug.Log("(3) thread Front loop is done");
        
        return;
    }
}

#region history

// 20200102 Backup beforeTryThread
/*
using UnityEngine;
using UnityEngine.UI;
using System.Runtime.InteropServices;
using System.Collections;

using System.Threading;

public class OpenCVTestScript : MonoBehaviour
{
    enum TARGET_COLOR
    {
        TARGET_RED, TARGET_BLUE, TARGET_YELLOW, TARGET_MAGENTA, TARGET_LIME,
    };

    [SerializeField]
    BallController ballController;

    [DllImport("OpenCVDLL")]
    private static extern bool cv_Tracking(ref int _left, ref int _width, ref int _top, ref int _height, int targetColor = 4);

    [DllImport("OpenCVDLL")]
    private static extern bool cv_TrackingOn();

    [DllImport("OpenCVDLL")]
    private static extern bool cv_TrackingOff();

    //// OpenCV에서 인식된 박스(공)의 좌표와 크기를 전달받는다
    int left, top, width, height;

    [SerializeField]
    Text left_t, top_t, width_t, height_t, velocity_t;

    float t = 0f;

    // 스레드를 관리하기위한 변수
    Thread thread = null;

    // Start is called before the first frame update
    void Start()
    {
        thread = new Thread(__FuncName__); 

        // 트래킹 시작 준비
        TrackingOn();
        // 트래킹 시작
        TrackingStart();
    }

    // Update is called once per frame
    void Update()
    {
        t += Time.deltaTime;

        left_t.text = "leftPos >> " + left;
        top_t.text = "topPos >> " + top;
        width_t.text = "width >> " + width;
        height_t.text = "height >> " + height;
        velocity_t.text = "velocity >> " + ballController.velocity.ToString("N2") + " m/s";
    }

    // 생명 주기. 프로그램이 종료되는 시점에 호출
    private void OnApplicationQuit()
    {
        TrackingOff();
    }

    void changeTargetColor(TARGET_COLOR targetColor)
    {
        Debug.Log("Now Tracking Color is " + targetColor);
        TrackingPause();
        TrackingStart((int)targetColor);
    }

    // 트래킹 시작 준비
    bool TrackingOn()
    {
        if (!cv_TrackingOn())
        {
            Debug.Log("TrackingOn() is Failed");
            return false;
        }
        Debug.Log("TrackingOn() is Successed");
        return true;
    }
    // 트래킹(coroutine) 시작
    void TrackingStart(int targetColor = 4)
    {
        StartCoroutine("c_Tracking", targetColor);
    }
    // 트래킹(coroutine) 중지
    void TrackingPause()
    {
        StopCoroutine("c_Tracking");
    }
    // 트래킹(카메라) 완전 종료
    bool TrackingOff()
    {
        TrackingPause();
        if (!cv_TrackingOff())
        {
            Debug.Log("TrackingOff() is Failed");
            return false;
        }
        Debug.Log("TrackingOff() is Successed");
        return true;
    }

    IEnumerator c_Tracking(int targetColor)
    {
        float t = 0f;
        for (;;)
        {
            t += Time.deltaTime;

            // 매 루프(한 바퀴의 Update)마다 cam을 껐다가 키면 심각한 프레임 저하를 야기
            // 그렇다고 cam을 그대로 돌리면 루프가 400회째 돌아갈 때마다 버그????가 발생
            // 따라서 15초에 한 번씩 cam을 껐다가 켜서 이를 피하고자 함
            // 추후 수정 필요
            // 뭐야 왜 되지 - 191230
            // .dll 내에서 전역변수로 만든 캠을 On 해주는 함수를 별도로 사용
            
            if (t > ballController.stepSec) 
            {
                t = 0f;
                if (!cv_Tracking(ref left, ref width, ref top, ref height, targetColor))
                {
                    {
                        Debug.Log("Cam Error");
                    }
                }
                ballController.move(left + width / 2, width, height);
            }
            yield return null;
        }
    }
}

    */
#endregion

