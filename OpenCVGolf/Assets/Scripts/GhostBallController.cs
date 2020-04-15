using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class GhostBallController : MonoBehaviour
{
    /*
     * 캠에서 인식된 공의 좌표로 렌더링되지 않는 공을 실시간으로 이동시키는 스크립트
     * 이 공의 움직임을 토대로 운동 방향과 세기를 검출
     * 이후 실제 게임 내 공에 AddForce()로 힘 적용
     * 
     * */


    [SerializeField]
    public float stepSec; // 공 갱신 간격(단위: 초 , 범위: 0.01 ~ 1.0)

    float curX, curY, curZ, destX, destY, destZ; // 목표 좌표와 기존 좌표

    public Vector3 curV = Vector3.zero, destV = Vector3.zero; // 속도를 계산하기 위한 변수

    [HideInInspector]
    public float velocity;
    
    [SerializeField]
    public int maxWidth, minWidth; 
    
    int count = 0;

    public void move(int x, int y, int z)
    //public void move(int x, int y, int width)
    {
        getXPos(x);
        getYPos(y);
        getZPos(z); // 이동할 좌표 셋팅. 너비:높이 중 작은 값 전달

        transform.position = new Vector3(destX, destY, destZ);
        //StartCoroutine("c_move"); // 이동 코루틴 실행

        calcVelocity();
    }
    

    float calcVelocity()
    {
        // 속도 = 거리 / 시간 = m / sec
        float dist = Vector3.Distance(curV, destV); // 이동한 두 좌표 사이의 거리

        // dist 거리를 stepSec 초 동안 이동하는 속도
        velocity = dist / stepSec;

        return 0f;
    }

    void getXPos(int x) // x축(좌우)
    {
        //// 공 인식 박스가 작은 박스에서 큰 박스로 변경되면서 이에 따른 leftPos 기준값이 변동됨
        //// 작은 박스 중심 topPos: 50
        //// 큰 박스 중심 topPos: 240
        //// 큰 박스 최대 topPos: 480
        //x += 190;

        if (destX != curX) // 속도 계산을 위해서?
        {
            curX = destX; // 기존 _x값을 별도로 저장. 이동할 x 좌표
            curV.x = curX;
        }

        // 새 _x값 할당
        //좌우 0~480 to 0.65~ -0.65
        destX = 240 - x; // 0~480 to 240~ -240
        destX = 0.65f * (destX / 240f); // 240~ -240 to 0.65~ -0.65
 
        //destX *= -1f; // 

        destV.x = destX;
    }

    void getYPos(int y) // y축(상하, 높낮이)
    {
        if (destY != curY) // 속도 계산을 위해서?
        {
            curY = destY; // 기존 _x값을 별도로 저장. 이동할 x 좌표
            curV.y = curY;
        }

        // 새 _y값 할당
        //상하	300~0 to -0.75~0.75
        //destY = 0.025f;
        destY = y / 300f; // 300~0 to 1~0
        destY *= 1.5f; // 1~0 to 1.5~0
        destY -= 0.75f; // 1.5~0 to 0.75~ -0.75
        destY *= -1f; // 0.75~ -0.75 to -0.75~ 0.75

        destV.y = destY;
    }

    void getZPos(int z) // z축 이동(앞뒤)
    {
        //// 공 인식 박스가 작은 박스에서 큰 박스로 변경되면서 이에 따른 leftPos 기준값이 변동됨
        //// 작은 박스 최대 leftPos: 100
        //// 큰 박스 최대 leftPos: 500
        //z += 400; 

        if (destZ != curZ)
        {
            curZ = destZ; // 기존 _z값을 별도로 저장. 이동할 z 좌표
            curV.z = curZ;
        }

        // 새 _z값 할당
        //앞뒤	500~0 to 0~1.5
        destZ = 500 - z; // 500~0 to 0~500
        destZ = 1.5f * (destZ / 500f); // 0~500 to 0~1.5
        
        destV.z = destZ;
    }
}
