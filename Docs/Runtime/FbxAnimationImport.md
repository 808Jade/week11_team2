# Week11 FBX 애니메이션과 공유 스켈레톤 임포트

`File → Import FBX…`에서 **Skeletal Meshes**와 **Animations**의 `Skeleton`을 각각 선택한다. 같은 FBX에서 메시와 애니메이션을 가져올 때에도 서로 다른 기존 `USkeleton`을 지정할 수 있다. 에디터가 FBX를 해석해 `.fbxbin`을 만들고, 런타임은 저장된 애셋을 로드한다. [임포트 창 코드](C:/Users/KJW/source/repos/week11_team2/Source/Editor/LevelEditor/LevelEditorTab.cpp:77) · [UE Interchange 임포트 옵션](https://dev.epicgames.com/documentation/unreal-engine/interchange-import-reference-in-unreal-engine)

## 1. 옵션과 결과물

| 설정 | Week11 동작 |
|---|---|
| **Skeletal Meshes → Skeleton** | `None`이면 가져오는 스켈레탈 메시의 리그마다 `SKEL_*.fbxbin`을 만든다. 기존 애셋을 고르면 각 메시 리그를 그 Skeleton에 대해 검사한다. |
| **Animations → Skeleton** | 기존 애셋을 고르면 각 클립을 그 Skeleton에 대해 검사한다. `None`이면 이번 임포트에서 같은 리그의 스켈레탈 메시가 사용할 Skeleton을 따른다. 같은 리그에서 가져오는 메시가 없으면 그 클립은 기존 Animation Skeleton을 지정해야 가져올 수 있다. |
| **Import Animations** | 기본값은 켜짐이다. 끄면 애니메이션 시퀀스는 만들지 않는다. 기존 메시 그룹과 본 계층을 찾는 데에는 FBX의 애니메이션 연결 정보도 계속 사용한다. |
| **Import Only Animations** | **Animations** 쪽에서 기존 Skeleton을 지정해야 한다. 메시 쪽 선택은 적용하지 않는다. 지오메트리, 머티리얼, 텍스처, 신규 Skeleton 파일을 만들지 않으며 기존 Skeleton 본도 추가하지 않는다. 소스에 스킨 메시가 있어도 클러스터 정보는 본 계층을 찾는 데 사용할 수 있다. |
| **Animation Length** | 기본 `Timeline`은 FBX 스택의 시간 범위다. `Animated Time`은 실제 키 범위, `Set Range`는 입력한 시작과 끝 프레임을 사용한다. |
| **Custom Sample Rate** | `0`이면 FBX 씬의 프레임 레이트를 쓴다. 양수는 해당 Hz로 재샘플링한다. 예를 들어 `30`은 30 Hz 고정이다. |
| **Frame Alignment** | 샘플 간격에 맞지 않는 길이는 기본 `None`에서 해당 클립을 건너뛴다. `Floor`, `Closest`, `Ceiling`을 고르면 시작과 끝을 각각 샘플 프레임에 맞춘다. |

두 목록 모두 `Assets` 아래에서 Skeleton 타입 헤더를 가진 `.fbxbin`을 표시하며, 같은 이름의 애셋은 상대 경로로 구분한다. 사용자가 각 목록에서 직접 선택한다. 메시 쪽과 애니메이션 쪽 호환성은 각각 검사하므로, 한쪽 결과를 건너뛰어도 명시적인 선택이 있는 다른 쪽은 계속 처리한다. 기존 Static/Skeletal Mesh의 Bake·Combine·본 계층 내 정적 메시 변환 옵션은 메시 임포트에 계속 적용된다. [Week11 임포트 창](C:/Users/KJW/source/repos/week11_team2/Source/Editor/LevelEditor/LevelEditorTab.cpp:77)

## 2. 소스 리그와 클립을 나누는 단위

1. FBX의 Skeleton 노드와 스킨 클러스터 링크를 출발점으로 본 계층을 찾는다. 부모 Null 경계와 Blender 최상위 `Armature` 예외를 포함한 기존 루트 탐색 규칙을 사용한다. 기존 Skeleton을 지정하면 대상 루트 이름에 대응하는 Null 또는 어트리뷰트 없는 노드도 해당 임포트의 탐색 후보가 된다. 애니메이션 쪽 선택에서만 찾은 루트는 앞서 분류한 메시 그룹을 바꾸지 않는다. 한 스킨 메시의 첫 번째 Skin 디포머가 여러 리그 루트를 가리키면 소스 구조 오류로 임포트를 중단한다.
2. 확정된 루트 안에서 본과 필요한 부모를 모은다. 변환 키가 달린 보조 Null도 이 계층에 들어올 수 있다. 스킨 없이 본 아래에 놓인 메시를 조인트와 스켈레탈 메시의 일부로 넣을지는 기존 **Convert Static Meshes in Bone Hierarchy** 설정을 따른다.
3. **스택 하나 × 리그 루트 하나**를 클립 후보로 본다. 한 스택이 여러 리그의 본을 움직이면 리그별 시퀀스로 나눌 수 있다. 스택에 본 변환 키가 없거나 대상 Skeleton과 연결되는 키가 하나도 없으면 그 조합의 시퀀스를 만들지 않는다. 루트 밖의 카메라, 소품 등 일반 노드에 키가 있다는 이유만으로 리그나 시퀀스가 생기지 않는다.
4. 해당 스택의 레이어 중 `Mute`된 레이어를 키 검색에서 제외한다. 활성 Solo 레이어가 있으면 키 검색도 해당 레이어로 제한한다. 스택을 FBX 씬에서 활성화하고 SDK 평가기로 최종 변환을 샘플링하므로, Base·Override·Additive 레이어는 스택 평가 결과로 구워진다. 레이어별 시퀀스를 생성하거나 FBX Additive 레이어를 런타임 Additive 애셋으로 보존하는 기능은 포함하지 않는다.

[Week11 리그 탐색](C:/Users/KJW/source/repos/week11_team2/Source/Editor/Asset/FbxImporter/FbxImporter.cpp:467) · [Week11 스택 평가](C:/Users/KJW/source/repos/week11_team2/Source/Editor/Asset/FbxImporter/FbxImporter.cpp:875) · [UE 루트별 애니메이션 트랙 생성](C:/Users/KJW/source/repos/UnrealEngineForked-release/Engine/Plugins/Interchange/Runtime/Source/Parsers/Fbx/Private/FbxScene.cpp:1064) · [UE 스택 활성화](C:/Users/KJW/source/repos/UnrealEngineForked-release/Engine/Plugins/Interchange/Runtime/Source/Parsers/Fbx/Private/FbxAnimation.cpp:1103)

## 3. 기존 Skeleton을 지정했을 때

### Skeletal Mesh

- 본 이름은 먼저 전체 이름을 대소문자 무시로 찾는다. 일치하는 전체 이름이 없으면 마지막 `:` 뒤의 이름을 고유할 때만 사용한다. 임포트하는 메시에는 매칭된 대상 본의 기존 표기를 저장한다.
- 소스와 대상의 루트가 대응하고, 양쪽에 존재하는 본은 부모 계층도 맞아야 한다. 본의 배열 순서가 다르거나 대상 Skeleton에 메시에서 사용하지 않는 본이 더 있는 것은 허용한다.
- 일치하는 부모 아래의 새 가지는 메시 쪽에서 선택한 Skeleton 끝에 부모부터 추가한다. 기존 본의 이름, 인덱스, 부모, 참조 자세를 보존한다. UE의 `Update Skeleton Reference Pose`는 기존 본의 자세를 갱신하는 별도 옵션이며, Week11은 기존 자세를 보존하는 경로를 사용한다. 본 삽입으로 기존 본의 부모가 바뀌거나 이름이 중복되어 대응이 모호하면 그 리그의 메시를 건너뛴다. [UE 참조 자세 옵션](https://dev.epicgames.com/documentation/unreal-engine/interchange-import-reference-in-unreal-engine)
- 메시의 버텍스 웨이트, 로컬 바인드 자세와 역바인드 행렬은 해당 메시의 본 테이블을 따른다. 공유 Skeleton의 본 인덱스로 웨이트를 다시 쓰거나, 공유 Skeleton의 참조 자세로 메시 바인드 자세를 덮어쓰지 않는다. 런타임 로더는 메시와 Skeleton을 연결할 때 이름과 부모 관계를 다시 검증한다.

[Week11 Skeleton 선택](C:/Users/KJW/source/repos/week11_team2/Source/Editor/Asset/FbxImporter/FbxImporter.cpp:720) · [Week11 본 병합](C:/Users/KJW/source/repos/week11_team2/Source/Runtime/Animation/ReferenceSkeleton.cpp:55) · [UE 호환성 검사](C:/Users/KJW/source/repos/UnrealEngineForked-release/Engine/Plugins/Interchange/Runtime/Source/Import/Private/Mesh/InterchangeSkeletonHelper.cpp:90) · [UE 병합과 재생성 확인](C:/Users/KJW/source/repos/UnrealEngineForked-release/Engine/Plugins/Interchange/Runtime/Source/Import/Private/Mesh/InterchangeSkeletalMeshFactory.cpp:1798)

### Animation Sequence

- 메시와 클립이 같은 Skeleton을 사용하면 클립 매칭에는 메시 임포트에서 병합된 본도 포함한다. 다른 Skeleton을 선택하면 그 애셋의 기존 본만 사용한다. 애니메이션 쪽에서 선택했다는 이유로 Skeleton에 새 본을 추가하지 않는다.
- 소스 리그에서 대상 루트에 대응하는 본을 찾는다. 루트가 소스의 래퍼 아래에 있어도 해당 본을 클립의 루트로 삼을 수 있다. 이후 하위 본을 이름과 부모 대응으로 매칭한다. 이름 매칭은 메시와 같은 규칙을 사용한다.
- 소스에만 있는 본은 트랙을 생략한다. 이름이 맞아도 부모 대응이 틀리거나 여러 소스 본이 같은 대상 본을 가리키면 해당 리그의 클립을 건너뛴다. 대상 루트와 일치하는 후보가 없거나 둘 이상인 경우도 마찬가지다. UE의 애니메이션 전용 1차 검사에는 본 하나의 이름이 겹치는지 보는 경로가 있다. Week11은 루트와 부모를 확인해 연결 대상을 결정한다.
- 키를 가져오지 않은 본은 런타임에서 메시의 로컬 바인드 자세를 사용한다. 그 본의 부모가 움직이면 컴포넌트 공간 포즈는 부모를 따라간다. Skeleton에는 있지만 재생하는 메시에는 없는 본의 트랙도 그 메시를 평가할 때 생략한다.

[Week11 클립 본 매핑](C:/Users/KJW/source/repos/week11_team2/Source/Editor/Asset/FbxImporter/FbxImporter.cpp:789) · [Week11 포즈 평가](C:/Users/KJW/source/repos/week11_team2/Source/Runtime/Animation/AnimSequence.cpp:94) · [UE 애니메이션의 대상 검사](C:/Users/KJW/source/repos/UnrealEngineForked-release/Engine/Plugins/Interchange/Runtime/Source/Pipelines/Private/InterchangeGenericAnimationPipeline.cpp:78) · [UE 팩토리의 미매칭 본 생략](C:/Users/KJW/source/repos/UnrealEngineForked-release/Engine/Plugins/Interchange/Runtime/Source/Import/Private/Animation/InterchangeAnimSequenceFactory.cpp:1308)

## 4. 시간, 좌표계, 데이터

- **시간 범위.** `Timeline`은 `FbxAnimStack::GetLocalTimeSpan()`을 사용한다. 범위를 쓸 수 없거나 스택 길이가 0인데 실제 키 구간이 더 길면 키 범위로 대체한다. `Animated Time`은 매칭된 본의 활성 레이어 TRS 키 범위와 루트 평가에 영향을 주는 부모 키 범위를 포함한다. `Set Range`에서는 입력 프레임을 유효 샘플 레이트로 나눈 초를 사용한다.
- **샘플 간격.** 시작과 끝을 포함해 일정한 간격으로 샘플링하고 시퀀스 시간은 0초부터 센다. 예를 들어 30 Hz에서 0~30프레임이면 31키, 재생 길이는 1초다. 애니메이션 데이터가 있어 범위가 한 시점으로 줄어들면 키 하나의 자세를 저장할 수 있다. 유효하지 않은 소스 프레임 레이트는 30 Hz로 대체한다. 사용자 샘플 레이트가 음수이거나 Set Range의 시작이 끝보다 뒤에 있으면 임포트 오류다.
- **공간.** 기존 메시 임포터와 같은 Z-up 축 변환과 좌표계 반사를 적용하고, 위치는 Week11의 미터로 변환한다. 자식 본의 로컬 변환은 샘플 시점의 FBX 글로벌 변환을 부모 글로벌 기준으로 환산한다. 클립과 함께 가져오는 메시가 같은 Skeleton을 쓰면 이번 메시의 Bake 설정을 사용한다. Animation Skeleton이 다르거나 메시를 함께 가져오지 않으면 선택한 Animation Skeleton에 기록된 Bake 설정을 사용한다. Bake가 꺼져 있으면 루트 부모의 바인드 변환을 제거한다. 실제 본에 매칭되는 키가 있는 스택에서는 루트 부모의 움직임도 루트 샘플에 반영할 수 있다.
- **키 내용.** 트랙은 부모 상대 위치, 쿼터니언 회전, 스케일을 저장한다. 스큐 성분은 별도로 저장되지 않는다. 비정상 또는 역행렬을 만들 수 없는 변환이 감지되면 해당 클립을 건너뛴다. 시퀀스에는 최대 1,000,000개 시점과 16,777,216개 채널 키를 허용한다. 용량 계산에는 위치·회전·스케일을 각각 센다.
- **적용 범위.** 이름과 계층이 맞아도 리그별 기본 자세나 비율 차이를 보정하는 리타게팅은 수행하지 않는다. 루트 본의 변환은 포즈에 저장되며 액터 이동으로 추출하지 않는다. 모프, 커스텀 속성 커브, 머티리얼 커브, 애니메이션 노티파이, 압축, 재생 루프 설정은 이번 임포트 페이로드에 포함되지 않는다. 루프는 재생 호출에서 선택한다.

[Week11 시간 계산](C:/Users/KJW/source/repos/week11_team2/Source/Editor/Asset/FbxImporter/FbxImporter.cpp:829) · [UE 샘플 레이트와 범위](C:/Users/KJW/source/repos/UnrealEngineForked-release/Engine/Plugins/Interchange/Runtime/Source/Pipelines/Private/InterchangeGenericAnimationPipeline.cpp:716) · [UE 본 변환 평가](C:/Users/KJW/source/repos/UnrealEngineForked-release/Engine/Plugins/Interchange/Runtime/Source/Parsers/Fbx/Private/FbxAnimation.cpp:515)

## 5. 저장, 재임포트, 런타임 로딩

| 항목 | 동작 |
|---|---|
| **파일명** | 애니메이션은 `AS_<소스>_<스택>[_<리그>].fbxbin` 규칙을 따른다. 같은 소스와 이름 부분이 중복되면 접어 쓰며, 여러 루트가 발견된 파일에서는 루트 이름을 덧붙인다. 충돌 시 기존 FBX 임포터의 안정적인 접미사를 사용한다. |
| **파일 데이터** | 새 애니메이션 타입은 버전 5다. 기존 메시·스켈레톤·머티리얼·텍스처 타입은 버전 4를 계속 읽고 쓴다. 애니메이션에는 소스와 스택/리그 식별자, Skeleton의 Assets 상대 경로, 샘플 시작 시간·레이트·키 수, 본 이름별 TRS가 들어 있다. |
| **런타임 로딩** | `UAssetManager::GetAssetByPath<UAnimSequence>()`에서 Skeleton을 먼저 해결한다. 저장된 트랙 이름을 현재 대상 본 인덱스로 바꾸고 `UAnimSequence`를 초기화한다. 의존 Skeleton 파일이나 트랙의 대상 본을 찾을 수 없으면 애셋 로딩을 실패로 처리한다. 재생에는 메시와 시퀀스가 같은 런타임 `USkeleton` 객체를 사용해야 한다. |
| **재임포트** | 같은 출력 폴더에서 소스 경로와 리그·스택 식별자가 같으면 기존 파일을 교체한다. FBX에서 삭제했거나 이번 설정으로 건너뛴 스택의 기존 출력은 자동 삭제하지 않는다. 선택한 Skeleton은 새 본을 병합한 경우에만 저장한다. |
| **임포트 트랜잭션** | 임시 폴더에 쓰고 다시 읽어 검증한 뒤, 런타임 객체와 의존성을 준비하고 출력 파일을 교체한다. 파일 교체 실패 시 이전 파일을 복구한다. 이미 로드된 선택 Skeleton에 본을 추가할 때는 객체 포인터를 유지하며, 배치 실패 시 그 본 테이블도 복원한다. |
| **결과 확인** | 정상 결과가 있으면 그 결과를 저장하고, 건너뛴 클립·리그·범위는 Output Log에 경고한다. 선택 파일을 열 수 없거나, 유효한 출력이 하나도 없거나, 바이너리 검증·의존성·파일 교체가 실패하면 임포트 창에서 오류를 표시한다. |

기존 에디터의 애셋 브라우저에서 `.fbxbin` 결과물을 볼 수 있다. 로드한 애니메이션은 `PlayAnimation(Sequence, bLooping, PlayRate, Error)`로 재생할 수 있다. [파일 형식](C:/Users/KJW/source/repos/week11_team2/Source/Runtime/Asset/FbxImporter/FbxAssetBake.cpp:101) · [런타임 시퀀스 생성](C:/Users/KJW/source/repos/week11_team2/Source/Runtime/Asset/FbxImporter/FbxAssetLoader.cpp:122) · [임포트 트랜잭션](C:/Users/KJW/source/repos/week11_team2/Source/Runtime/Asset/FbxImporter/FbxAssetLoader.cpp:321) · [Epic 애셋 접두어](https://dev.epicgames.com/documentation/en-us/unreal-engine/recommended-asset-naming-conventions-in-unreal-engine)

## 6. 확인한 동작

Debug x64에서 에디터와 게임을 빌드했다. FBX SDK로 생성한 임시 소스에서 스택과 리그별 분리, namespace가 다른 본의 대응, 호환되지 않는 리그 생략, 기존 Skeleton 포인터를 보존하는 가지 병합을 확인했다. Additive 레이어를 섞은 루트 이동을 런타임에서 미터 단위로 재생했고, 애니메이션만 가져오기, 사용자 샘플 레이트와 프레임 범위, 프레임 정렬도 확인했다.

같은 소스의 재임포트가 출력 이름을 유지하는지와, 잘못된 트랙을 주입해 배치가 실패했을 때 공유 Skeleton의 메모리 및 디스크 상태가 보존되는지를 확인했다. 별도 프로세스에서 저장된 시퀀스를 다시 로드했으며, 기존 버전 4 메시도 로드했다. 이 검증은 임포터와 런타임 호출을 통해 실행했다.

선택을 분리한 뒤에는 같은 FBX에서 메시가 A를, 클립이 B를 참조하는지 메모리와 `.fbxbin`에서 확인했다. 새 본은 A에만 추가됐다. 애니메이션의 `None`과 양쪽에 같은 Skeleton을 고르는 경우, 각 쪽의 호환성 실패, 애니메이션 전용 선택의 필수 여부도 확인했다. 애니메이션의 Null 루트 후보가 기존 정적 메시 분류에 영향을 주지 않는 케이스를 포함했다.
