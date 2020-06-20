# OpenCV-Golf
OpenCV와 Unity로 구현한 골프 시뮬레이션 게임

2019.12 ~ 2020.02(3학년 2학기 동계방학)

[주식회사푸딩](https://www.vpudding.com/about) 에서 현장실습기간동안 진행한 프로젝트

# 로직
0. 최적화를 위해 붉은 사각형 범위 내에서만 공을 인식(Ball Tracking)

1. 색을 기준으로 화면 내 공 인식 및 흔들림 보정

2. 정지 확인 후 타격 준비(인식 범위 확대)

3. 움직인 확인 및 인식된 x좌표의 변화량을 가공한 후 유니티로 전달

4. 전달된 데이터를 토대로 시뮬레이션 진행

# YoutubeLink

[![VIDEO](https://img.youtube.com/vi/Il-_TnemJFk/0.jpg)](https://www.youtube.com/watch?v=Il-_TnemJFk?t=0s)
