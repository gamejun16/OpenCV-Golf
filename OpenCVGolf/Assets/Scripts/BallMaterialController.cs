using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class BallMaterialController : MonoBehaviour
{
    /*
     * 
     * 1) 현재 공의 위치에 따른 Physics Material을 컨트롤
     * 바닥 레이어의 표면과 충돌(OnTriggerEnter)하면 그 충돌체의 태그를 받아온다
     * Collisoin Matrix에 의해 오직 바닥과의 충돌만을 체크한다
     * 
     * state)
     *      0: Fairway(default)
     *      1: Bunker
     *      2: ..
     * 
     * */

    [SerializeField]
    BallController ballController;
    public string curMaterial;
    public float curSlope;

    enum MATERIALS
    {
        FAIRWAY, BUNKER, 
    }

    [SerializeField]
    PhysicMaterial[] materials; // idx[0] : default
    
    [SerializeField]
    Collider myCollider;

    [SerializeField]
    ProcessController processController;

    public Vector3 spawnPoint; // OB, HAZARD 등에 공이 빠져 스폰될 지점. ProessController에서 참조.

    Vector3 beforePos;

    public bool isFlying; // 공이 하늘을 날고 있을 때에는 Drag를 Add하지 않는다

    private void Start()
    {
        isFlying = false;
        curMaterial = "FAIRWAY";
        processController.isFinish = false;
    }

    private void Update()
    {
        calcSlope();
    }

    public void initRotateForSlope(Vector3 target)
    {
        gameObject.transform.LookAt(target);
    }

    void calcSlope()
    {
        RaycastHit hit;

        Physics.Raycast(gameObject.transform.position, Vector3.down, out hit, Mathf.Infinity);

        curSlope = Mathf.Round(Vector3.Angle(Vector3.up, hit.normal));

        //gameObject.transform.LookAt(gameObject.transform.position + (gameObject.transform.position - beforePos));

        //Vector3 aftUp = gameObject.transform.up;

        //curSlope = Mathf.Round(Vector3.Angle(Vector3.up, aftUp));

        //beforePos = gameObject.transform.position;
    }
    
    private void OnTriggerEnter(Collider other)
    {
        if (other.gameObject.layer == LayerMask.NameToLayer("field"))
        {
        }

        // 우선순위가 높은 것부터 조건 확인
        // 1순위 : 맵 이탈, 물에 빠짐 등
        // 2순위 : 속성 변경 - 벙커, 다른 재질의 바닥 등
        if (other.CompareTag("FinishPoint"))
        {
            Debug.Log("FINISH");
            curMaterial = other.tag;
            //myCollider.material = materials[(int)MATERIALS.BUNKER];
            processController.isFinish = true;
        }

        else if (other.CompareTag("Water")) // Water , OB , HAZARD
        {
            // 카메라 공 추적 정지
            // 공이 튀어서 다시 수면으로 올라오지 않게 
            // 물(혹은 그 외)에 공이 반만 잠기는 경우 없게?
            // 게임 종료
            myCollider.material = materials[(int)MATERIALS.BUNKER];
            Transform spawnPoints = other.transform.parent.Find("SpawnPoints");
            spawnPoint = spawnPoints.GetChild(Random.Range(0, spawnPoints.childCount)).position;

            ProcessController.process = (int)ProcessController.PROCESS.FALLED;
        }

        else if (other.CompareTag("Bunker"))
        {
            myCollider.material = materials[(int)MATERIALS.BUNKER];
            curMaterial = other.tag;
        }
    }

    private void OnTriggerStay(Collider other)
    {
        if (other.gameObject.layer == LayerMask.NameToLayer("field"))
        {
            isFlying = false;

        }
    }

    //private void OnCollisionStay(Collision collision)
    //{
    //    if (collision.gameObject.layer == LayerMask.NameToLayer("field"))
    //    {
    //        Debug.Log("Field Stay");
    //    }
    //}

    private void OnTriggerExit(Collider other)
    {
        if (other.CompareTag("FinishPoint"))
        {
            processController.isFinish = false;
            curMaterial = "FAIRWAY";
        }
        //if (other.gameObject.layer == LayerMask.NameToLayer("field") && !other.CompareTag("Fairway")){
        //    Debug.Log("Material 초기화");
        //    myCollider.material = materials[(int)MATERIALS.FAIRWAY];
        //    curMaterial = "FAIRWAY";
        //}

        if (other.gameObject.layer == LayerMask.NameToLayer("field"))
        {
            isFlying = true;
            ballController.initDrag();

            if (!other.CompareTag("Fairway"))
            {
                Debug.Log("Material 초기화");
                myCollider.material = materials[(int)MATERIALS.FAIRWAY];
                curMaterial = "FAIRWAY";
            }
        }
    }   
}
