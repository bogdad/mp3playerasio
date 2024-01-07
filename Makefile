buildall:
	echo building
	cd build && ninja
compile-commands-windows:
	conan install . --build=missing -s build_type=Debug --output-folder=build-win-ninja --profile:all ./.github/workflows/conan-windows-latest-profile \
	-c tools.cmake.cmaketoolchain:generator=Ninja
	cmake --preset conan-debug -DCMAKE_EXPORT_COMPILE_COMMANDS=1 -B build-win-ninja
	copy build-win-ninja\compile_commands.json compile_commands.json
conan-windows:
	conan install . --build=missing -s build_type=Debug --output-folder=build-win --profile:all ./.github/workflows/conan-windows-latest-profile
cmake-windows:
	cmake --preset conan-default -B build-win
build-windows:
	cmake --build build-win
