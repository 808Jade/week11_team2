vcpkg_check_linkage(ONLY_DYNAMIC_LIBRARY ONLY_DYNAMIC_CRT)

vcpkg_download_distfile(Archive
    URLS "https://damassets.autodesk.net/content/dam/autodesk/www/files/fbx2020311_fbxsdk_vs2022_win.exe"
    FILENAME "fbx2020311_fbxsdk_vs2022_win.exe"
    SHA512 60ed2d3656073bd7258fde6f33d9ef606f4d8c606f36e590abf6c221ae4ae7cbb40e25f219854dd27a60fb37f8d69668d61406f807dbd46d7fc441d1a4d20e04
)

set(SourcePath "${CURRENT_BUILDTREES_DIR}/src/${VERSION}")
file(REMOVE_RECURSE "${SourcePath}")
vcpkg_extract_archive(ARCHIVE "${Archive}" DESTINATION "${SourcePath}")

file(INSTALL "${SourcePath}/include/" DESTINATION "${CURRENT_PACKAGES_DIR}/include")

file(INSTALL "${SourcePath}/lib/x64/release/libfbxsdk.lib" DESTINATION "${CURRENT_PACKAGES_DIR}/lib")
file(INSTALL "${SourcePath}/lib/x64/release/libfbxsdk.dll" DESTINATION "${CURRENT_PACKAGES_DIR}/bin")
if(NOT VCPKG_BUILD_TYPE)
    file(INSTALL "${SourcePath}/lib/x64/debug/libfbxsdk.lib" DESTINATION "${CURRENT_PACKAGES_DIR}/debug/lib")
    file(INSTALL "${SourcePath}/lib/x64/debug/libfbxsdk.dll" DESTINATION "${CURRENT_PACKAGES_DIR}/debug/bin")
endif()

file(INSTALL "${SourcePath}/readme.txt" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
vcpkg_install_copyright(FILE_LIST "${SourcePath}/License.rtf")
