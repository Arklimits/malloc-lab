***********
# [WEEK05] 탐험준비 - Malloc Lab
📢 “정글끝까지 가기 전에, 준비운동을 하며 필수 스킬을 익혀봅시다!”

3주간 각 1주차 씩 Red-Black tree → malloc → 웹 proxy 서버를 C언어로 구현하면서, C언어 포인터의 개념, gdb 디버거 사용법 등을 익혀봅니다. 또한, Segmentation fault 등 새로운 에러들을 마주해봅니다! 🙂
알고리즘(CLRS), 컴퓨터 시스템(CS:APP) 교재를 참고하여 진행합니다.
RB tree - CLRS 13장, malloc - CS:APP 9장, 웹서버 - CS:APP 11장

***********
💡 Ubuntu 22.04 LTS (x86_64)환경을 사용합니다.

개발 환경 설치
```ubuntu
sudo apt update                         # package list update
sudo apt upgrade                        # upgrade packages
sudo apt install gcc make valgrind gdb  # gcc, make 등 개발 환경 설치
```

GitHub 토큰 관리를 위한 gh 설치 
```
curl -fsSL https://cli.github.com/packages/githubcli-archive-keyring.gpg | sudo dd of=/usr/share/keyrings/githubcli-archive-keyring.gpg
sudo chmod go+r /usr/share/keyrings/githubcli-archive-keyring.gpg
echo "deb [arch=$(dpkg --print-architecture) signed-by=/usr/share/keyrings/githubcli-archive-keyring.gpg] https://cli.github.com/packages stable main" | sudo tee /etc/apt/sources.list.d/github-cli.list > /dev/null
sudo apt update
sudo apt install gh
```
*(컨테이너에 GitHub Cli를 추가적으로 설치해놓기는 함)*

**********************************

```
Results for mm malloc:
trace  valid  util     ops      secs  Kops
 0       yes   99%    5694  0.000149 38112
 1       yes  100%    5848  0.000150 39013
 2       yes   99%    6648  0.000177 37517
 3       yes  100%    5380  0.000141 38129
 4       yes   99%   14400  0.000178 80990
 5       yes   96%    4800  0.000278 17291
 6       yes   95%    4800  0.000270 17758
 7       yes   55%   12000  0.000226 53097
 8       yes   51%   24000  0.000447 53667
 9       yes  100%   14401  0.000131110352
10       yes   84%   14401  0.000107134338
Total          89%  112372  0.002254 49850

Perf index = 53 (util) + 40 (thru) = 93/100
```

**********************************
```
./mdriver -V #trace별로 추적하여 점수 측정
./mdriver -f /traces/short1-bal.rep #traces폴더 안의 short1-bal.rep만 테스트
```
**********************************

* Buddy System은 구현하지 못함
