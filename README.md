# Techlab4_Week07_Team1

## 빌드 준비

Windows x64, Git for Windows, Visual Studio 2022 이상 또는 Build Tools의 C++ 데스크톱 개발 도구와 Windows 10/11 SDK가 필요합니다.

저장소 루트의 `Build.bat` 또는 `SlnGenerator.bat`을 실행합니다. Premake와 manifest에 고정한 vcpkg를 준비하고, 외부 라이브러리를 복원한 뒤 설치된 Visual Studio에 맞는 `Hitori.slnx` 또는 `Hitori.sln`을 생성합니다. 자동 실행에서는 `Build.bat -NoPause` 또는 `SlnGenerator.bat --no-pause`를 사용합니다. 최초 실행에는 인터넷 연결이 필요합니다.

생성된 솔루션을 열고 x64로 빌드합니다. Debug와 Benchmark_Debug는 vcpkg의 Debug 라이브러리를, Release·Shipping·ObjViewer·Benchmark는 Release 라이브러리를 사용합니다. MSBuild가 include·link 경로와 실행 파일에 필요한 DLL 배치를 처리합니다.

## 외부 라이브러리

모든 외부 라이브러리는 `vcpkg.json`에 선언합니다. `builtin-baseline`으로 레지스트리 버전을 고정하고, 기존 스크립트 호환성을 위해 Lua 5.4.7과 sol2 3.3.0을 override로 고정합니다.

| 패키지 | 용도 |
| --- | --- |
| directxtk | DDS 텍스처 로딩 |
| fbxsdk | Autodesk FBX SDK 2020.3.11, 저장소의 overlay port 사용 |
| imgui | docking, Direct3D 11, Win32 백엔드 |
| lua, sol2 | Lua 실행과 C++ 바인딩 |
| miniaudio | 오디오 |
| nlohmann-json | JSON 직렬화 |
| stb | 이미지 로딩 |

의존성은 `vcpkg_installed/`, 빌드 도구는 `.tools/`에 캐시합니다. 두 폴더 모두 Git에서 제외합니다. 외부 소스를 `Source/ThirdParty`에 복사하거나 ImGui 서브모듈을 초기화할 필요가 없습니다. FBX SDK 별도 설치와 `FBX_SDK_ROOT` 설정도 필요하지 않습니다. 상세한 SDK 연결 방식은 [Setup/README_FBX.md](Setup/README_FBX.md)를 참고합니다.

`Package.bat Release` 또는 `Package.bat Shipping`은 게임을 빌드하고 콘텐츠 및 게임에서 사용하는 vcpkg DLL을 `Build/Package/Hitori`에 모읍니다.
