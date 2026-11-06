# FBX SDK 의존성

FBX SDK는 `VcpkgPorts/fbxsdk`의 overlay port로 관리합니다. 저장소 루트에서 `Build.bat` 또는 `SlnGenerator.bat`을 실행하면 Autodesk FBX SDK 2020.3.11 Windows VS2022 설치 파일을 다운로드하고 SHA-512를 검증한 뒤 헤더, x64 Debug/Release import library, DLL을 `vcpkg_installed/x64-windows`에 복원합니다.

SDK를 별도로 설치하거나 `FBX_SDK_ROOT` 환경 변수를 설정할 필요가 없습니다. 이전에 저장한 환경 변수는 사용하지 않습니다.

`HitoriEditor`는 `FBXSDK_SHARED`를 정의하며 vcpkg의 MSBuild 통합으로 헤더와 라이브러리를 연결합니다. Debug와 Benchmark_Debug는 SDK debug 라이브러리, 나머지 구성은 release 라이브러리를 사용합니다. 빌드 후 실제 실행 파일의 의존 DLL이 출력 폴더에 자동 배치됩니다.

`HitoriEngine`과 `HitoriGame`의 코드는 FBX SDK를 참조하지 않습니다. 게임 패키징 시에도 게임 실행 파일에서 사용하는 DLL만 수집합니다.

팀에는 manifest, overlay port, `Directory.Build.props`, `Directory.Build.targets`, `Build.bat`, `SlnGenerator.bat`, `premake5.lua`를 공유합니다. `.tools/`, `vcpkg_installed/`, 생성된 프로젝트와 빌드 결과는 Git에 추가하지 않습니다.
