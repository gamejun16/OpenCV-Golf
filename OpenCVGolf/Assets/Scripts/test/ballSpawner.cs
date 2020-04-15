using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class ballSpawner : MonoBehaviour
{
    [SerializeField]
    GameObject startPoint;
    [SerializeField]
    GameObject ball;

    Vector3 beforePos; // 직전 좌표

    float t;

    private void Start()
    {

        t = 0f;
    }

    // Update is called once per frame
    void Update()
    {
        //t += Time.deltaTime;
        ////if (Input.GetMouseButton(0))
        //if(true)
        //{
        //    t = 0f;

        //    RaycastHit hit;
        //    if (Physics.Raycast(ball.transform.position, Vector3.down, out hit, 100f))
        //    {
        //        ball.transform.LookAt(ball.transform.position + (ball.transform.position - beforePos));
        //        Vector3 aftUp = ball.transform.up;

        //        Debug.Log("Angle >> "+ Vector3.Angle(Vector3.up, aftUp));

        //        Quaternion normalRot = Quaternion.FromToRotation(Vector3.up, hit.normal);

        //        Debug.Log("eulerAngles >> " + normalRot.eulerAngles);
                
        //        Debug.DrawRay(ball.transform.position, ball.transform.up * 10);
        //        Debug.DrawRay(ball.transform.position, (ball.transform.position - beforePos) * 100);
        //        //Debug.Log((ball.transform.position - beforePos).ToString());

        //        beforePos = ball.transform.position;
        //    }
        //}



        if (Input.GetKeyDown(KeyCode.Space)) { // set
            ball.transform.position = startPoint.transform.position;
            ball.GetComponent<Rigidbody>().velocity = Vector3.zero;
        }

        else if (Input.GetKeyDown(KeyCode.A))
        {
            //ball.GetComponent<Rigidbody>().AddForce(new Vector3(1,1, 0.5f) * 10);
            //ball.GetComponent<Rigidbody>().AddForce(new Vector3(0f, 6f, 0f) * 4);
            ball.GetComponent<Rigidbody>().AddForce(new Vector3(0.8f, 0.6f, 0.1f), ForceMode.Impulse);
        }
        
    }
}
