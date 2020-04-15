using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.UI;

public class LogPrinter : MonoBehaviour
{
    /*
     * 
     * 화면에 출력되는 모든 정보들을 총괄하는 스크립트
     * 
     * 정보 목록 및 순서
     * 
     * process(__진행 상태__) < enum >
     *      WAIT
     *      READY
     *      HITTED
     *      ...
     *      
     * velocity(__속도__)
     *      VelocityCalculator.cs 의 velocity 참조
     *      
     *  distance(__거리__)
     *      직전 타격 때 공이 진행한 거리
     *      
     *  left_distance(__남은 거리__)
     *      홀까지 남은 거리
     *      
     *  state(__공의 상태__)
     *      stop
     *      roll
     *      bounce
     *      ...
     *      
     * field(__바닥 정보__)
     *      fairway
     *      bunker
     *      water?
     *      ...
     *      
     * 타(__해당 홀 타격 횟수__)
     *      
     * 경사도(__바닥의 노멀벡터(법선벡터)에서 추출__)
     * 
     * 바람 정보?
     * 
     * 날씨 정보?
     * 
     * */

    [SerializeField] 
    ProcessController processController;
    [SerializeField]
    BallMaterialController ballMaterialController;
    [SerializeField]
    BallController ballController;

    string output;

    [SerializeField]
    Text log;
    
    string process, state, distance, left_distance, hit_count, slope, wind, weather;

    float timer;

    private void Start()
    {
        timer = 0f;
    }

    private void Update()
    {
        timer += Time.deltaTime;
        if(timer > 0.5f)
        {
            timer = 0f;
            setLog();
        }
    }

    void setLog()
    {

        output = "";

        switch (ProcessController.process)
        {
            case 0: process = ProcessController.PROCESS.WAIT.ToString(); break;
            case 1: process = ProcessController.PROCESS.READY.ToString(); break;
            case 2: process = ProcessController.PROCESS.HITTED.ToString(); break;
            case 5: process = ProcessController.PROCESS.FALLED.ToString(); break;
            default: process = "UNKNOWN"; break;
        }
        output += "process(" + process + ") \t";

        output += "velocity(" + ballController.velocity.ToString("N2") + "m/s) \t";

        output += "dist(" +distance+ "m) \t";

        output += "hall_dist(" +left_distance+ "m) \t";

        output += "state(" + state + ") \t";

        output += "field(" + ballMaterialController.curMaterial + ") \t";

        output += "타(" + processController.scoreRecorder.getHitCount() + ") \t";

        output += "전체 타(" + processController.scoreRecorder.getRoundHitCount() + ") \t";

        output += "slope(" + ballMaterialController.curSlope + ") \t";

        log.text = output;
    }

    // 공이 날아간 거리를 계산
    public void setFlyingDistance(Vector3 before, Vector3 after)
    {
         distance = Vector3.Magnitude(after - before).ToString("N2");
    }

    // 목표 홀까지 남은 거리를 계산
    public void setLeftDistance(Vector3 ball, Vector3 hall)
    {
        left_distance = Vector3.Magnitude(hall - ball).ToString("N2");
    }
}
