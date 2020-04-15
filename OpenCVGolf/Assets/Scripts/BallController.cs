using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.UI;



public class BallController : MonoBehaviour
{
    /*
     * 
     * 공의 여러 상태, 속성 들을 계산 보관 및 관리하는 스크립트
     * 속도, 상태, 현 바닥의 종류 및 기울기 등
     * 
     * */

    
    static float __drag__ = 1.0f;

    [SerializeField]
    Rigidbody myRigidbody;
    
    // 속도 계산에 사용하기 위한 직전 공의 좌표
    Vector3 beforePosition;

    [HideInInspector]
    public float velocity; // 속도

    [SerializeField]
    Text velocity_t;

    // 속도 = 거리 / 시간
    float dist;
    float time;

    private void Awake()
    {
        myRigidbody = GetComponent<Rigidbody>();
    }

    private void Start()
    {
        dist = 0f;
        time = 0f;
    }

    private void Update()
    {

        

        if (time > .5f) // 매 초 속도 검출 및 출력 (단위: m/s)
        {
            velocity = dist / time;

            //string velocity_string = velocity.ToString("N2");
            //Debug.Log("cur speed >> " + velocity_string+ " m/s" + "ㅡㅡdist >> " + dist + "ㅡㅡtime >> " + time);
            //if (velocity_t)
            //    velocity_t.text = velocity_string+ " m/s";
            dist = 0f;
            time = 0f;

            return;
        }

        // 직전 좌표와 지금 좌표 사이 거리 계산
        dist += (transform.position - beforePosition).magnitude;
        time += Time.deltaTime;

        // '직전 좌표' 갱신
        beforePosition = transform.position;
    }


    
    public void initDrag(bool moveLock = false) // 공 저항력 초기화
    {
        // 확실히 움직이지 못하게 할 필요가 있는 경우.
        if (moveLock)
        {
            myRigidbody.drag = 999; // infinity
            return;
        }
        myRigidbody.drag = __drag__;
    }

    public void addDrag() // 공 저항력을 더함
    {
        myRigidbody.drag += 0.05f;
    }



}
