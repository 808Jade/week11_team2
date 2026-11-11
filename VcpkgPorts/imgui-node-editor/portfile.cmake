vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

# 같은 소스와 빌드 설정을 재현하도록 커밋과 다운로드 해시를 고정한다.
vcpkg_from_github(
    OUT_SOURCE_PATH SourcePath
    REPO thedmd/imgui-node-editor
    REF 1e0e494f1a86885eb332ca518543108ccd88d13e
    SHA512 b726511daca3cb189c1a8651ac9968705292dabf7766b83f8df1456c50d326846a27a0687de501d94493c7789a027b2157a330d88cedc15544f66a2ce3491ad7
)

file(COPY "${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt" DESTINATION "${SourcePath}")

vcpkg_cmake_configure(SOURCE_PATH "${SourcePath}")
vcpkg_cmake_install()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

vcpkg_install_copyright(FILE_LIST "${SourcePath}/LICENSE")
