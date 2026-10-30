"""Minimal, shared native/Flatpak voice dependency build options."""


def cmake_options(name):
    if name == "abseil-cpp":
        # Static PIC avoids a public C++ ABI and MinGW component DLL mismatches.
        return [
            "-DBUILD_SHARED_LIBS=OFF",
            "-DCMAKE_POSITION_INDEPENDENT_CODE=ON",
            "-DCMAKE_CXX_STANDARD=17",
            "-DABSL_PROPAGATE_CXX_STD=ON",
            "-DABSL_BUILD_TESTING=OFF",
        ]
    return []


def meson_options(name, *, windows=False):
    options = {
        "opus": ["-Dtests=disabled", "-Ddocs=disabled", "-Dintrinsics=disabled", "-Dasm=disabled"],
        "libsrtp": [
            "-Dcrypto-library=openssl",
            "-Dcrypto-library-kdf=disabled",
            "-Dtests=disabled",
        ],
        "webrtc-audio-processing": [],
        "gstreamer": ["-Dtools=enabled", "-Dtests=disabled", "-Dintrospection=disabled"],
        "gst-plugins-base": [
            "-Daudioconvert=enabled",
            "-Daudiomixer=enabled",
            "-Daudioresample=enabled",
            "-Daudiotestsrc=enabled",
            "-Dvolume=enabled",
            "-Dopus=enabled",
            "-Dtests=disabled",
            "-Dintrospection=disabled",
        ]
        + ([] if windows else ["-Dalsa=enabled"]),
        "libnice": [
            "-Dgstreamer=enabled",
            "-Dcrypto-library=openssl",
            "-Dtests=disabled",
            "-Dexamples=disabled",
            "-Dintrospection=disabled",
        ],
        "gst-plugins-good": [
            "-Dautodetect=enabled",
            "-Dlevel=enabled",
            "-Drtp=enabled",
            "-Drtpmanager=enabled",
            "-Dtests=disabled",
        ]
        + ([] if windows else ["-Dpulse=enabled"]),
        "gst-plugins-bad": [
            "-Dwebrtc=enabled",
            "-Dwebrtcdsp=enabled",
            "-Ddtls=enabled",
            "-Dsrtp=enabled",
            "-Dsctp=enabled",
            "-Dsctp-internal-usrsctp=enabled",
            "-Dtests=disabled",
            "-Dintrospection=disabled",
            "-Dgpl=disabled",
        ]
        + (["-Dwasapi2=enabled"] if windows else []),
    }
    return options.get(name, [])
