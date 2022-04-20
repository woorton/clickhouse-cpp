from conans import ConanFile, CMake, tools

class ClickHouse(ConanFile):
    name = "clickhouse"
    url = "https://github.com/ClickHouse/clickhouse-cpp"
    description = "ClickHouse C++ API"
    version = "2.1.0"
    settings = "os", "compiler", "build_type", "arch"
    generators = "cmake_find_package"
    options = {
        "shared": [True, False],
        "fPIC": [True, False],
        "build_bench": [True, False],
        "build_tests": [True, False],
        "with_openssl": [True, False],
        "with_system_lz4": [True, False],
        "with_system_cityhash": [True, False],
        "with_system_abseil": [True, False]
    }
    default_options = {
        "shared": False,
        "fPIC": True,
        "build_bench": False,
        "build_tests": False,
        "with_openssl": False,
        "with_system_lz4": False,
        "with_system_cityhash": False,
        "with_system_abseil": False
    }

    def requirements(self):
        if self.options.with_system_lz4:
            self.requires("lz4/1.9.3")
        if self.options.with_system_abseil:
            self.requires("abseil/20211102.0")
        if self.options.with_system_cityhash:
            self.requires("cityhash/cci.20130801")
        if self.options.with_openssl:
            self.requires("openssl/1.1.1l")

    def build_requirements(self):
        if self.options.build_bench:
            self.requires("benchmark/1.6.0")

    def _configure_cmake(self):
        cmake = CMake(self, set_cmake_flags=True)
        if self.options.build_bench:
            cmake.definitions['BUILD_BENCHMARK'] = 'ON'
        if self.options.build_tests:
            cmake.definitions['BUILD_TESTS'] = 'ON'
        if self.options.with_openssl:
            cmake.definitions['WITH_OPENSSL'] = 'ON'
        if self.options.with_system_abseil:
            cmake.definitions['WITH_SYSTEM_ABSEIL'] = 'ON'
        if self.options.with_system_lz4:
            cmake.definitions['WITH_SYSTEM_LZ4'] = 'ON'
        if self.options.with_system_cityhash:
            cmake.definitions['WITH_SYSTEM_CITYHASH'] = 'ON'
        if self.settings.compiler == 'clang' and self.settings.compiler.libcxx == 'libc++':
            cmake.definitions['CMAKE_CXX_FLAGS'] = '-stdlib=libc++'
        cmake.configure()
        return cmake

    def build(self):
        cmake = self._configure_cmake()
        cmake.build()
        if self.options.build_tests:
            cmake.test()

    def package(self):
        cmake = self._configure_cmake()
        cmake.install()

    def package_info(self):
        self.cpp_info.names["cmake_find_package"] = "clickhouse"
        if self.options.shared:
            self.cpp_info.libs.append("clickhouse-cpp-lib")
        else:
            self.cpp_info.libs.append("clickhouse-cpp-lib-static")
