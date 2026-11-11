# State Machine Lab

Macaw의 Third Person 샘플을 Week11의 애니메이션·Lua·씬 형식에 맞춰 옮긴 시연입니다.

## 시작

`Scenes/Demo_StateMachineLab.scene`를 열고 Play를 누릅니다. 플레이어와 네 전시 캐릭터가 같은 그래프를 독립된 인스턴스로 실행합니다. `G`로 43개 클립을 반복 재생하는 `Scenes/Demo_StateMachineClips.scene`와 왕복할 수 있습니다.

## 조작

| 입력 | 동작 |
|---|---|
| WASD / Shift / Z | 이동 / 달리기 / 걷기 |
| 방향키 | 카메라 회전과 높이 조정 |
| Space / C | 점프 / 웅크리기 전환 |
| T 또는 마우스 오른쪽 버튼 | 조준 전환 |
| 마우스 왼쪽 버튼 | 펀치 또는 조준 중 발사 동작 |
| Q / E / R | 구르기 동작 / 상호작용 / 재장전 |
| X / K / H | 검 공격 / 주문 / 피격 |
| F / V / Y | 춤 / 수영 포즈 / 사망 |
| Backspace | 플레이어 초기화 |
| B | 30초 자동 시연 전환 |
| Tab / 1·2 | 클립 브라우저 / 이전·다음 클립 |
| G | Motion Yard와 클립 갤러리 왕복 |

## 그래프 확인

`Assets/StateMachineLab/ThirdPerson.animgraph`를 엽니다. 상위 State Machine은 Movement, Action, Hit Reaction, Dance, Death, Clip Browser를 갖습니다. Movement 안에는 Grounded, Crouch, Aim, Swim, Jump Start, Falling, Land의 중첩 State Machine이 있습니다.

State Machine 노드를 더블클릭하면 상태 그래프, State를 더블클릭하면 포즈 그래프, 전이선을 더블클릭하면 조건 그래프가 열립니다. 노드·State·전이선의 우클릭 메뉴에서 속성을 편집하고, 상단 경로 버튼으로 상위 그래프로 이동합니다. 저장은 Ctrl+S입니다.

Play 중 캐릭터를 선택하고 Anim Graph 패널에서 현재 State, 대상 State, 전이 진행률, 조건 결과와 변수 값을 확인할 수 있습니다.

## 코드와 동작 범위

`Scripts/state_machine_lab.lua`가 입력·캐릭터 이동·카메라·HUD를 처리하고 Float와 Bool 변수를 전달합니다. 상태 선택, 전이 조건 평가, 애니메이션 초기화와 포즈 블렌딩은 C++ State Machine이 처리합니다. 전시 캐릭터도 같은 경로를 사용합니다.

기존 Week11의 AnimSequence 샘플링, BoneContainer, 스키닝 경로를 사용합니다. 애니메이션은 `.fbxbin`, 그래프는 `.animgraph`, Blend Space는 `.blendspace`로 저장합니다. 원본 FBX나 Macaw 실행 파일 없이 이 저장소의 에셋으로 실행할 수 있습니다.

이동은 시연용 컨트롤러가 위치와 코스의 지면 높이를 계산합니다. 구르기·수영·공격은 애니메이션 시연이며 별도의 전투·수영 물리 시스템을 포함하지 않습니다. 전이 블렌딩은 선형이며 진행 중인 블렌딩이 끝난 다음 새 전이를 검사합니다.

## 검증

저장소 루트에서 `Scripts/TestAnimationStateMachine.ps1`을 실행합니다. Release 빌드와 함께 JSON 왕복 저장, 조건 타입 검사, 포즈 혼합, 공유 노드 갱신, 실제 Lua 입력, 중첩 상태 전환, 자동 시연, HUD, 씬 왕복, 43개 독립 클립 재생을 검사합니다. 별도의 헤드리스 에디터 검사에서 계층 이동과 노드 저장 순서에 무관한 조건 연결선 표시를 검증합니다.

## 애니메이션 출처

Quaternius Universal Animation Library의 Standard 캐릭터와 43개 클립을 사용합니다. 배포 라이선스는 이 폴더의 `License.txt`에 있습니다.
