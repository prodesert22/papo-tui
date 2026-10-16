find_package(PkgConfig REQUIRED)
pkg_check_modules(CURL REQUIRED IMPORTED_TARGET libcurl>=7.86)
pkg_check_modules(JANSSON REQUIRED IMPORTED_TARGET jansson>=2.14)
pkg_check_modules(NCURSES REQUIRED IMPORTED_TARGET ncursesw>=6.1)
pkg_check_modules(FORM REQUIRED IMPORTED_TARGET formw>=6.1)
pkg_check_modules(UNIBREAK REQUIRED IMPORTED_TARGET libunibreak>=6.1)
pkg_check_modules(SIXEL REQUIRED IMPORTED_TARGET libsixel>=1.8)
pkg_check_modules(GLIB REQUIRED IMPORTED_TARGET glib-2.0>=2.66)
pkg_check_modules(GMODULE REQUIRED IMPORTED_TARGET gmodule-no-export-2.0>=2.66)
pkg_check_modules(CHAFA REQUIRED IMPORTED_TARGET chafa>=1.16)
pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv>=2.0)
pkg_check_modules(AVFORMAT REQUIRED IMPORTED_TARGET libavformat>=58)
pkg_check_modules(AVCODEC REQUIRED IMPORTED_TARGET libavcodec>=58)
pkg_check_modules(AVUTIL REQUIRED IMPORTED_TARGET libavutil>=56)
pkg_check_modules(SWSCALE REQUIRED IMPORTED_TARGET libswscale>=5)
pkg_check_modules(PANGO REQUIRED IMPORTED_TARGET pangocairo>=1.50)
pkg_check_modules(FONTCONFIG REQUIRED IMPORTED_TARGET fontconfig>=2.13)
find_package(Threads REQUIRED)
option(PAPO_DESKTOP_SESSIONS "Enable optional desktop credential managers" ON)
option(PAPO_DESKTOP_NOTIFICATIONS "Enable optional asynchronous desktop notifications" ON)
if(WIN32)
    pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0>=2.88)
else()
    pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0>=2.66)
endif()
if(PAPO_DESKTOP_SESSIONS AND NOT WIN32)
    pkg_check_modules(SECRET QUIET IMPORTED_TARGET libsecret-1>=0.20)
endif()

include(CheckCSourceRuns)
set(CMAKE_REQUIRED_LIBRARIES PkgConfig::CURL)
check_c_source_runs(
    "#include <curl/curl.h>
#include <string.h>
int main(void) {
  const curl_version_info_data *v = curl_version_info(CURLVERSION_NOW);
  int ws = 0, wss = 0;
  if (!v || !v->protocols || !(v->features & CURL_VERSION_ASYNCHDNS) || !(v->features & CURL_VERSION_SSL)) return 1;
  for (const char *const *p = v->protocols; *p; ++p) {
    ws |= strcmp(*p, \"ws\") == 0; wss |= strcmp(*p, \"wss\") == 0;
  }
  return !(ws && wss);
}"
    PAPO_CURL_WS_SUPPORTED)
unset(CMAKE_REQUIRED_LIBRARIES)
if(NOT PAPO_CURL_WS_SUPPORTED)
    message(FATAL_ERROR "libcurl must provide WS/WSS, TLS and asynchronous DNS")
endif()
