# AIFX — Conan recipe (consumer only).
# SPDX-License-Identifier: BSD-3-Clause
#
# Declares the third-party dependencies AIFX needs at build time. AIFX itself
# is not published as a Conan package — this file only consumes deps so they
# are resolvable from the top-level CMakeLists.txt via find_package(... CONFIG).
#
# OpenFX is intentionally NOT a Conan dependency here: AIFX pulls it as source
# via CMake FetchContent (see CMakeLists.txt). This avoids needing to publish
# OpenFX to a Conan registry and makes "upgrade against new OpenFX" a one-line
# GIT_TAG bump.

from conan import ConanFile
from conan.tools.cmake import CMakeDeps, CMakeToolchain, cmake_layout

required_conan_version = ">=2.1"


class AIFX(ConanFile):
    name = "aifx"
    version = "1.0.0"
    license = "BSD-3-Clause"
    url = "https://github.com/Dev-Reepost/aifx"
    description = "AI-powered OpenFX plugins bridging ComfyUI to any OFX host."

    settings = "os", "arch", "compiler", "build_type"

    default_options = {
        # Statically link every third-party C/C++ dependency into each .ofx so
        # the bundle is self-contained and portable: no DT_RUNPATH / LC_RPATH
        # back into the Conan cache, nothing to ship beside the binary. This is
        # exactly what build-plugin.sh's verify_binary_portability() enforces.
        #
        # NOTE: a consumer's default_options is LOWER priority than profile
        # [options], so a developer profile carrying `*:shared=True` would
        # override this and reintroduce non-portable RUNPATHs. build-plugin.sh
        # therefore *also* forces these on the conan CLI (-o, highest priority)
        # so the supported build path can't be broken by an inherited profile.
        "*:shared": False,
        "spdlog/*:header_only": True,
        "fmt/*:header_only": True,
        # expat stays shared for OpenFX's HostSupport (host-side plist XML); it
        # is never linked into a plugin .ofx, so it does not affect portability.
        "expat/*:shared": True,
    }

    def requirements(self):
        # Required by OpenFX (fetched via CMake FetchContent). Its top-level
        # CMakeLists.txt does find_package(cimg REQUIRED) unconditionally,
        # even with BUILD_EXAMPLE_PLUGINS=OFF, so cimg has to be present.
        self.requires("expat/2.7.1")        # OpenFX HostSupport XML parsing
        self.requires("opengl/system")      # OpenFX Support OpenGL hooks
        self.requires("cimg/3.3.2")         # required unconditionally by OpenFX top-level CMakeLists

        # Required by AIFX's own ComfyUI bridge code.
        self.requires("spdlog/1.13.0")
        self.requires("nlohmann_json/3.11.3")
        self.requires("cpp-httplib/0.15.3")
        self.requires("ixwebsocket/11.4.6")
        self.requires("tinyexr/1.0.7")
        self.requires("miniz/3.0.2")
        self.requires("openssl/3.2.1")

    def layout(self):
        cmake_layout(self)

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()

        tc = CMakeToolchain(self)
        if self.settings.os == "Windows":
            tc.preprocessor_definitions["WINDOWS"] = 1
            tc.preprocessor_definitions["NOMINMAX"] = 1
        tc.generate()
