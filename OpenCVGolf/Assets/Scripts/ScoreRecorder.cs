using System;
using System.Collections;
using System.Collections.Generic;
using UnityEngine;

[Serializable]
public class ScoreRecorder
{

    /*
     * 
     * 스코어 기록 스크립트
     * 
     * 1라운드 18홀 72타
     * 
     * */

    // 획득 점수
    int roundHitCount; // 각 홀에서의 누적 타수(포인트)
    int hitCount; // 해당 홀에서의 타수

    public void finishRound()// 해당 라운드 누적 타수 초기화
    {
        roundHitCount = 0;
    }

    public void finishHall() // 해당 홀에서의 타수 기록 및 초기화
    {
        roundHitCount += hitCount;
        hitCount = 0;
    }

    public void addHitCount(int c = 1) // 공을 칠 때마다 1씩 추가. 벌타는 그 이상.
    {
        hitCount += c;
    }
    
    public int getHitCount()
    {
        return hitCount;
    }

    public int getRoundHitCount()
    {
        return roundHitCount;
    }

    public void initScore() // 각 데이터를 0으로 초기화
    {
        hitCount = 0;
        roundHitCount = 0;
    }



}
