from conan import ConanFile
from conan.tools.cmake import CMakeDeps, CMakeToolchain

class PALRecipe(ConanFile):
    settings = "os", "build_type"

    def requirements(self):
        self.requires("clipper/6.4.2")
        # self.requires("protobuf/3.21.12")
        self.requires("rapidjson/cci.20230929")
        self.requires("stb/cci.20230920")
        self.requires("boost/1.83.0")
        self.requires("spdlog/1.12.0")
        self.requires("fmt/10.2.1")
        self.requires("range-v3/0.12.0")
        self.requires("mapbox-wagyu/0.5.0")

    def generate(self):
        tc = CMakeToolchain(self)
        tc.user_presets_path = False
        tc.generate()
        cd = CMakeDeps(self)
        cd.generate()