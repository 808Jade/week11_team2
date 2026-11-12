# Locomotion Stage

`Scenes/Demo_LocomotionStage.scene`를 열고 **Play**를 누릅니다. 씬 버전은 5입니다.

Quaternius Universal Animation Library의 `UAL1_Standard.fbx`와 `UAL1_Standard_RM.fbx`를 새로 임포트했습니다. 일반·Root Motion 각각 43개 클립이 같은 스켈레톤을 공유합니다. 실행에는 Downloads의 원본 FBX가 필요하지 않습니다.

## 구간

| 키 | 구간 | 확인할 동작 |
|---|---|---|
| 1 | Flow | 정지·걷기·조깅·질주, 가속과 감속, OwnerSpeed 기반 Blend Space |
| 2 | Traverse | 실제 메시를 검사하는 경사로·계단·점프 간격, Jump Start → Falling → Land |
| 3 | Combat | 펀치·검·주문·재장전·상호작용, 피격, 앉기 진입·대기·종료 |
| 4 | Root Motion | 이동·구르기·검 공격의 루트 트랙을 액터 이동으로 추출 |
| 5 / Tab | Clip Lab | 43개 전체 클립 선택, 일반·Root Motion 비교, 정지·역재생·배속·시간 이동 |

구간 키는 플레이어를 해당 위치로 이동시킵니다. 낮은 통로와 수영 수조는 무대 북쪽에 있습니다. 주변 네 캐릭터는 각자 독립된 인스턴스로 이동·액션·조준·Root Motion을 시연합니다.

## 조작

| 입력 | 동작 |
|---|---|
| WASD / Shift / Z | 이동 / 질주 / 걷기 |
| 마우스 / 방향키 | 카메라 회전과 높이 조절 |
| Space / C | 점프 / 웅크리기 |
| T / RMB / V | 조준 전환 / 누르는 동안 조준 / 수영 자세 전환 |
| LMB | 펀치, 조준 중 발사 |
| Q / E / R | 구르기 / 상호작용 / 재장전 |
| X / K / H | 검 공격 / 주문 / Any State 피격 |
| U | 앉기·일어서기 |
| F / Y / Backspace | 춤 / 사망 / 현재 구간 초기화 |
| B / I | 25초 자동 시연 / 전체 도움말 |

경사로 위 간격은 **Shift + Space**로 뛰어넘습니다. 이동은 Lua의 키네마틱 컨트롤러가 지면과 벽 메시를 검사하며 처리합니다. 수영은 수조에서 높이를 유지하는 시연용 동작입니다.

Clip Lab에서는 **Q/E**로 클립을 선택하고, **P**로 일시정지, **N**으로 역재생, **O**로 0.25·0.5·1·1.5·2배속을 순환합니다. **J/L**은 클립 길이의 10%씩 시간을 옮기며, **M**은 같은 클립의 Root Motion 버전으로 전환합니다. **Tab**으로 이전 구간에 돌아옵니다.

## 애니메이션 구성

- `Graphs/AG_InPlace.animgraph`: OwnerSpeed, Float·Bool 변수, 개별 Get 노드, 중첩 State Machine, Any State, Finished 조건을 사용합니다.
- `Graphs/AG_RootMotion.animgraph`: 같은 상태 구조에서 RM 클립과 실제 이동량에 맞춘 속도 축을 사용합니다. 이 구간의 수평 이동은 C++ Root Motion 추출 결과입니다.
- 지상·웅크리기·수영의 1D Blend Space와 조준 상·중·하 클립의 Blend Space, Two-Way Blend를 사용합니다.
- `.notifies.json`에 발걸음·착지·상호작용·탄창 교체 등의 Notify와 공격·회피 NotifyState 구간을 저장했습니다. HUD의 이벤트 수와 활성 창은 실제 런타임 콜백으로 갱신됩니다.
- 앉기는 별도 중첩 상태 머신에서 Enter → Idle → Exit를 재생합니다. 동작 종료 판정은 C++ 그래프가 담당합니다.

에디터에서는 그래프 노드·상태·전이선을 더블클릭해 내부 포즈와 조건을 확인할 수 있습니다. PIE 중 플레이어의 Anim Graph 디버그 창을 열면 현재 상태와 블렌딩 경로를 볼 수 있습니다. 시퀀스 에디터에서 Notify 트랙과 Root Motion을, Skeletal Mesh 에디터에서 Bone Weight Heatmap을 확인할 수 있습니다.

## 재생성·검증

저장소 루트에서 실행합니다.

```powershell
./Scripts/BuildLocomotionStage.ps1 -Capture
```

Debug x64 빌드 후 86개 클립의 공유 스켈레톤, 씬 v5 왕복 저장, 실제 Lua 입력, 이동·점프·상태 전환·앉기·Root Motion, 전체 클립 접근, 일시정지·역재생·시간 이동과 HUD를 검사합니다. 렌더 캡처는 `Build/LocomotionStage`에 저장됩니다. 화면을 여는 대신 엔진의 오프스크린 렌더러를 사용합니다.

다시 임포트하려면 다음을 실행합니다.

```powershell
./Scripts/BuildLocomotionStage.ps1 -Import -Capture -SourceDirectory 'Universal Animation Library[Standard]의 Unity 폴더 경로'
```

일반 버전의 스켈레톤을 RM 버전에도 지정합니다. 생성 규칙은 `Scripts/GenerateLocomotionStage.mjs`, 플레이어 제어는 `Scripts/locomotion_stage.lua`, 독립 시연은 `Scripts/locomotion_stage_demo.lua`에 있습니다. 라이선스는 `License.txt`의 CC0 조건을 따릅니다.
