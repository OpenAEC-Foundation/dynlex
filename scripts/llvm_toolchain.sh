#!/usr/bin/env bash

DYNLEX_LLVM_SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DYNLEX_LLVM_PROJECT_DIR="$(cd "$DYNLEX_LLVM_SCRIPT_DIR/.." && pwd)"
DYNLEX_LLVM_METADATA_FILE="$DYNLEX_LLVM_PROJECT_DIR/metadata/LLVM_TOOLCHAIN"
if [ ! -f "$DYNLEX_LLVM_METADATA_FILE" ]; then
	echo "Error: missing LLVM toolchain metadata: $DYNLEX_LLVM_METADATA_FILE" >&2
	return 1 2>/dev/null || exit 1
fi

while read -r metadata_key metadata_value; do
	case "$metadata_key" in
	repository) DYNLEX_LLVM_REPOSITORY="$metadata_value" ;;
	revision) DYNLEX_LLVM_REVISION="$metadata_value" ;;
	major) DYNLEX_LLVM_MAJOR_VERSION="$metadata_value" ;;
	bootstrap) DYNLEX_LLVM_BOOTSTRAP_CLANG_VERSION="$metadata_value" ;;
	schema) DYNLEX_LLVM_TOOLCHAIN_SCHEMA="$metadata_value" ;;
	*)
		echo "Error: unknown LLVM toolchain metadata field: $metadata_key" >&2
		return 1 2>/dev/null || exit 1
		;;
	esac
done <"$DYNLEX_LLVM_METADATA_FILE"

if [ -z "${DYNLEX_LLVM_REPOSITORY:-}" ] || [ -z "${DYNLEX_LLVM_REVISION:-}" ] ||
	[ -z "${DYNLEX_LLVM_MAJOR_VERSION:-}" ] || [ -z "${DYNLEX_LLVM_BOOTSTRAP_CLANG_VERSION:-}" ] ||
	[ -z "${DYNLEX_LLVM_TOOLCHAIN_SCHEMA:-}" ]; then
	echo "Error: incomplete LLVM toolchain metadata: $DYNLEX_LLVM_METADATA_FILE" >&2
	return 1 2>/dev/null || exit 1
fi

DYNLEX_LLVM_PRIMARY_CHECKOUT_DIR="$DYNLEX_LLVM_PROJECT_DIR"
if git -C "$DYNLEX_LLVM_PROJECT_DIR" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
	if ! DYNLEX_LLVM_GIT_COMMON_DIR="$(git -C "$DYNLEX_LLVM_PROJECT_DIR" rev-parse --path-format=absolute --git-common-dir)"; then
		echo "Error: could not resolve the shared Git directory for the LLVM toolchain cache." >&2
		return 1 2>/dev/null || exit 1
	fi
	DYNLEX_LLVM_PRIMARY_CHECKOUT_DIR="$(cd "$(dirname "$DYNLEX_LLVM_GIT_COMMON_DIR")" && pwd -P)"
fi
DYNLEX_LLVM_CACHE_DIR="${DYNLEX_LLVM_CACHE_DIR:-$DYNLEX_LLVM_PRIMARY_CHECKOUT_DIR/.cache/llvm-toolchain}"
DYNLEX_LLVM_SOURCE_DIR="$DYNLEX_LLVM_CACHE_DIR/source"
DYNLEX_LLVM_NATIVE_BUILD_DIR="$DYNLEX_LLVM_CACHE_DIR/native/build"
DYNLEX_LLVM_NATIVE_INSTALL_DIR="$DYNLEX_LLVM_CACHE_DIR/native/install"
DYNLEX_LLVM_WEB_BUILD_DIR="$DYNLEX_LLVM_CACHE_DIR/web/build"
DYNLEX_LLVM_WEB_INSTALL_DIR="$DYNLEX_LLVM_CACHE_DIR/web/install"
DYNLEX_LLVM_SOURCE_MARKER="$DYNLEX_LLVM_SOURCE_DIR/.dynlex-llvm-source"
DYNLEX_LLVM_HOST_SYSTEM="$(uname -s)"
case "$DYNLEX_LLVM_HOST_SYSTEM" in
*MINGW* | *MSYS* | *CYGWIN*)
	DYNLEX_LLVM_HOST_EXECUTABLE_SUFFIX=".exe"
	DYNLEX_LLVM_NATIVE_RUNTIME_PROFILE="static-msvc-crt"
	;;
*)
	DYNLEX_LLVM_HOST_EXECUTABLE_SUFFIX=""
	DYNLEX_LLVM_NATIVE_RUNTIME_PROFILE="platform-default-crt"
	;;
esac

dynlex_require_command() {
	local command_name="$1"
	if ! command -v "$command_name" >/dev/null 2>&1; then
		echo "Error: required command not found: $command_name" >&2
		return 1
	fi
}

dynlex_resolve_bootstrap_tool() {
	local tool_name="$1"
	local versioned_name="${tool_name}-${DYNLEX_LLVM_BOOTSTRAP_CLANG_VERSION}"
	if command -v "$versioned_name" >/dev/null 2>&1; then
		command -v "$versioned_name"
		return
	fi
	if command -v "$tool_name" >/dev/null 2>&1; then
		command -v "$tool_name"
		return
	fi
	echo "Error: required bootstrap tool not found: $versioned_name or $tool_name" >&2
	return 1
}

dynlex_resolve_bootstrap_compilers() {
	DYNLEX_LLVM_BOOTSTRAP_CC="$(dynlex_resolve_bootstrap_tool clang)"
	DYNLEX_LLVM_BOOTSTRAP_CXX="$(dynlex_resolve_bootstrap_tool clang++)"

	local compiler_major
	local compiler_cxx_major
	compiler_major="$("$DYNLEX_LLVM_BOOTSTRAP_CC" --version | sed -n '1 s/.*clang version \([0-9][0-9]*\).*/\1/p')"
	compiler_cxx_major="$("$DYNLEX_LLVM_BOOTSTRAP_CXX" --version | sed -n '1 s/.*clang version \([0-9][0-9]*\).*/\1/p')"
	if [ -z "$compiler_major" ] || [ "$compiler_major" -lt "$DYNLEX_LLVM_BOOTSTRAP_CLANG_VERSION" ]; then
		echo "Error: DynLex requires Clang ${DYNLEX_LLVM_BOOTSTRAP_CLANG_VERSION} or newer to build LLVM." >&2
		return 1
	fi
	if [ "$compiler_cxx_major" != "$compiler_major" ]; then
		echo "Error: bootstrap clang and clang++ must use the same major version." >&2
		return 1
	fi
}

dynlex_parallel_jobs() {
	if command -v nproc >/dev/null 2>&1; then
		nproc
	elif command -v getconf >/dev/null 2>&1; then
		getconf _NPROCESSORS_ONLN
	elif command -v sysctl >/dev/null 2>&1; then
		sysctl -n hw.ncpu
	else
		echo 1
	fi
}

dynlex_host_llvm_target() {
	case "$(uname -m)" in
	x86_64 | amd64 | AMD64 | i386 | i686) echo X86 ;;
	aarch64 | arm64) echo AArch64 ;;
	armv7* | armv8l) echo ARM ;;
	riscv64) echo RISCV ;;
	*)
		echo "Error: unsupported host architecture for the DynLex LLVM toolchain: $(uname -m)" >&2
		return 1
		;;
	esac
}

dynlex_validate_llvm_source_layout() {
	local source_component
	for source_component in llvm cmake libc third-party; do
		if [ ! -d "$DYNLEX_LLVM_SOURCE_DIR/$source_component" ]; then
			echo "Error: LLVM source snapshot is missing $source_component." >&2
			return 1
		fi
	done
}

dynlex_prepare_llvm_source() {
	dynlex_require_command git
	dynlex_require_command cmake
	mkdir -p "$DYNLEX_LLVM_CACHE_DIR"

	if [ ! -e "$DYNLEX_LLVM_SOURCE_DIR" ]; then
		(
			local clone_directory
			clone_directory="$(mktemp -d "$DYNLEX_LLVM_CACHE_DIR/source.clone.XXXXXX")"
			trap 'if [ -n "$clone_directory" ]; then cmake -E remove_directory "$clone_directory"; fi' EXIT
			git init --quiet "$clone_directory"
			git -C "$clone_directory" remote add origin "$DYNLEX_LLVM_REPOSITORY"
			git -C "$clone_directory" sparse-checkout init --cone
			git -C "$clone_directory" sparse-checkout set llvm cmake libc third-party
			git -C "$clone_directory" fetch --depth 1 origin "$DYNLEX_LLVM_REVISION"
			git -C "$clone_directory" -c advice.detachedHead=false checkout --detach FETCH_HEAD
			mv "$clone_directory" "$DYNLEX_LLVM_SOURCE_DIR"
			clone_directory=""
		)
	fi

	if [ ! -d "$DYNLEX_LLVM_SOURCE_DIR/.git" ]; then
		if [ ! -f "$DYNLEX_LLVM_SOURCE_MARKER" ]; then
			echo "Error: LLVM source is missing Git metadata and packaged-source provenance." >&2
			return 1
		fi
		if [ "$(cat "$DYNLEX_LLVM_SOURCE_MARKER")" != "$DYNLEX_LLVM_REPOSITORY:$DYNLEX_LLVM_REVISION" ]; then
			echo "Error: packaged LLVM source does not match the pinned fork." >&2
			return 1
		fi
		dynlex_validate_llvm_source_layout
		return
	fi

	local source_revision
	source_revision="$(git -C "$DYNLEX_LLVM_SOURCE_DIR" rev-parse HEAD)"
	if [ "$source_revision" != "$DYNLEX_LLVM_REVISION" ]; then
		echo "Error: LLVM cache contains revision $source_revision, expected $DYNLEX_LLVM_REVISION." >&2
		return 1
	fi

	local source_repository
	source_repository="$(git -C "$DYNLEX_LLVM_SOURCE_DIR" remote get-url origin)"
	if [ "$source_repository" != "$DYNLEX_LLVM_REPOSITORY" ]; then
		echo "Error: LLVM cache uses repository $source_repository, expected $DYNLEX_LLVM_REPOSITORY." >&2
		return 1
	fi

	if [ -n "$(git -C "$DYNLEX_LLVM_SOURCE_DIR" status --porcelain)" ]; then
		echo "Error: cached LLVM source has local changes: $DYNLEX_LLVM_SOURCE_DIR" >&2
		return 1
	fi
	dynlex_validate_llvm_source_layout
}

dynlex_llvm_marker_contents() {
	local profile="$1"
	case "$profile" in
	native)
		if [ "$DYNLEX_LLVM_NATIVE_RUNTIME_PROFILE" = static-msvc-crt ]; then
			printf '%s\n' "$DYNLEX_LLVM_REVISION:$DYNLEX_LLVM_TOOLCHAIN_SCHEMA:native:static-msvc-crt"
		else
			printf '%s\n' "$DYNLEX_LLVM_REVISION:$DYNLEX_LLVM_TOOLCHAIN_SCHEMA:native"
		fi
		;;
	web) printf '%s\n' "$DYNLEX_LLVM_REVISION:$DYNLEX_LLVM_TOOLCHAIN_SCHEMA:web" ;;
	*)
		echo "Error: unknown LLVM toolchain marker profile: $profile" >&2
		return 1
		;;
	esac
}

dynlex_llvm_install_is_current() {
	local profile="$1"
	local install_directory="$2"
	local marker="$install_directory/.dynlex-llvm-toolchain"
	[ -f "$marker" ] || return 1
	[ -f "$install_directory/lib/cmake/llvm/LLVMConfig.cmake" ] || return 1
	[ "$(cat "$marker")" = "$(dynlex_llvm_marker_contents "$profile")" ] || return 1
	if [ "$profile" = native ]; then
		local tablegen="$install_directory/bin/llvm-tblgen$DYNLEX_LLVM_HOST_EXECUTABLE_SUFFIX"
		local dwarfdump="$install_directory/bin/llvm-dwarfdump$DYNLEX_LLVM_HOST_EXECUTABLE_SUFFIX"
		[ -x "$tablegen" ] || return 1
		[ -x "$dwarfdump" ] || return 1
		"$tablegen" --version >/dev/null 2>&1 || return 1
		"$dwarfdump" --version >/dev/null 2>&1 || return 1
	fi
}

dynlex_validate_managed_directory() {
	local directory="$1"
	case "$directory" in
	"$DYNLEX_LLVM_CACHE_DIR"/*) ;;
	*)
		echo "Error: refusing to manage a directory outside the LLVM cache: $directory" >&2
		return 1
		;;
	esac
}

dynlex_reset_llvm_profile() {
	local build_directory="$1"
	local install_directory="$2"
	dynlex_validate_managed_directory "$build_directory"
	dynlex_validate_managed_directory "$install_directory"
	cmake -E remove_directory "$build_directory"
	cmake -E remove_directory "$install_directory"
	mkdir -p "$build_directory"
}

dynlex_common_llvm_cmake_arguments() {
	local build_directory="$1"
	local install_directory="$2"
	printf '%s\n' \
		-S "$DYNLEX_LLVM_SOURCE_DIR/llvm" \
		-B "$build_directory" \
		-G Ninja \
		-DCMAKE_BUILD_TYPE=Release \
		"-DCMAKE_INSTALL_PREFIX=$install_directory" \
		-DCMAKE_INSTALL_LIBDIR=lib \
		-DLLVM_ENABLE_PROJECTS= \
		-DLLVM_ENABLE_RUNTIMES= \
		-DLLVM_BUILD_LLVM_DYLIB=OFF \
		-DLLVM_LINK_LLVM_DYLIB=OFF \
		-DLLVM_ENABLE_ASSERTIONS=ON \
		-DLLVM_ENABLE_BINDINGS=OFF \
		-DLLVM_ENABLE_CURL=OFF \
		-DLLVM_ENABLE_FFI=OFF \
		-DLLVM_ENABLE_HTTPLIB=OFF \
		-DLLVM_ENABLE_LIBEDIT=OFF \
		-DLLVM_ENABLE_LIBPFM=OFF \
		-DLLVM_ENABLE_LIBXML2=OFF \
		-DLLVM_ENABLE_ZLIB=OFF \
		-DLLVM_ENABLE_ZSTD=OFF \
		-DLLVM_INCLUDE_BENCHMARKS=OFF \
		-DLLVM_INCLUDE_DOCS=OFF \
		-DLLVM_INCLUDE_EXAMPLES=OFF \
		-DLLVM_INCLUDE_RUNTIMES=OFF \
		-DLLVM_INCLUDE_TESTS=OFF \
		-DLLVM_BUILD_UTILS=OFF
	printf '%s\n' \
		"-DLLVM_FORCE_VC_REPOSITORY=$DYNLEX_LLVM_REPOSITORY" \
		"-DLLVM_FORCE_VC_REVISION=$DYNLEX_LLVM_REVISION"
}

dynlex_native_llvm_cmake_arguments() {
	case "$DYNLEX_LLVM_NATIVE_RUNTIME_PROFILE" in
	static-msvc-crt) printf '%s\n' -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded ;;
	platform-default-crt) ;;
	*)
		echo "Error: unsupported native LLVM runtime profile: $DYNLEX_LLVM_NATIVE_RUNTIME_PROFILE" >&2
		return 1
		;;
	esac
}

dynlex_build_native_llvm() {
	if dynlex_llvm_install_is_current native "$DYNLEX_LLVM_NATIVE_INSTALL_DIR"; then
		return
	fi

	dynlex_prepare_llvm_source
	dynlex_resolve_bootstrap_compilers
	dynlex_require_command ninja
	dynlex_reset_llvm_profile "$DYNLEX_LLVM_NATIVE_BUILD_DIR" "$DYNLEX_LLVM_NATIVE_INSTALL_DIR"

	local host_target
	host_target="$(dynlex_host_llvm_target)"
	local common_arguments=()
	while IFS= read -r argument; do
		common_arguments+=("$argument")
	done < <(dynlex_common_llvm_cmake_arguments "$DYNLEX_LLVM_NATIVE_BUILD_DIR" "$DYNLEX_LLVM_NATIVE_INSTALL_DIR")
	local native_arguments=()
	while IFS= read -r argument; do
		native_arguments+=("$argument")
	done < <(dynlex_native_llvm_cmake_arguments)
	cmake "${common_arguments[@]}" "${native_arguments[@]}" \
		"-DCMAKE_C_COMPILER=$DYNLEX_LLVM_BOOTSTRAP_CC" \
		"-DCMAKE_CXX_COMPILER=$DYNLEX_LLVM_BOOTSTRAP_CXX" \
		-DLLVM_BUILD_TOOLS=ON \
		"-DLLVM_TARGETS_TO_BUILD=$host_target;WebAssembly;SPIRV" \
		"-DLLVM_DISTRIBUTION_COMPONENTS=llvm-libraries;llvm-headers;cmake-exports;llvm-tblgen;llvm-dwarfdump"
	cmake --build "$DYNLEX_LLVM_NATIVE_BUILD_DIR" --target install-distribution --parallel "$(dynlex_parallel_jobs)"

	dynlex_llvm_marker_contents native >"$DYNLEX_LLVM_NATIVE_INSTALL_DIR/.dynlex-llvm-toolchain"
	dynlex_llvm_install_is_current native "$DYNLEX_LLVM_NATIVE_INSTALL_DIR"
	cmake -E remove_directory "$DYNLEX_LLVM_NATIVE_BUILD_DIR"
}

dynlex_build_web_llvm() {
	if dynlex_llvm_install_is_current web "$DYNLEX_LLVM_WEB_INSTALL_DIR"; then
		return
	fi

	dynlex_build_native_llvm
	dynlex_require_command emcmake
	dynlex_require_command emcc
	dynlex_reset_llvm_profile "$DYNLEX_LLVM_WEB_BUILD_DIR" "$DYNLEX_LLVM_WEB_INSTALL_DIR"

	local common_arguments=()
	while IFS= read -r argument; do
		common_arguments+=("$argument")
	done < <(dynlex_common_llvm_cmake_arguments "$DYNLEX_LLVM_WEB_BUILD_DIR" "$DYNLEX_LLVM_WEB_INSTALL_DIR")
	emcmake cmake "${common_arguments[@]}" \
		-DLLVM_BUILD_TOOLS=OFF \
		"-DLLVM_TARGETS_TO_BUILD=WebAssembly;SPIRV" \
		"-DLLVM_NATIVE_TOOL_DIR=$DYNLEX_LLVM_NATIVE_INSTALL_DIR/bin" \
		"-DLLVM_TABLEGEN=$DYNLEX_LLVM_NATIVE_INSTALL_DIR/bin/llvm-tblgen$DYNLEX_LLVM_HOST_EXECUTABLE_SUFFIX" \
		"-DLLVM_DISTRIBUTION_COMPONENTS=llvm-libraries;llvm-headers;cmake-exports"
	cmake --build "$DYNLEX_LLVM_WEB_BUILD_DIR" --target install-distribution --parallel "$(dynlex_parallel_jobs)"

	dynlex_llvm_marker_contents web >"$DYNLEX_LLVM_WEB_INSTALL_DIR/.dynlex-llvm-toolchain"
	dynlex_llvm_install_is_current web "$DYNLEX_LLVM_WEB_INSTALL_DIR"
	cmake -E remove_directory "$DYNLEX_LLVM_WEB_BUILD_DIR"
}

dynlex_ensure_llvm_toolchain() {
	local profile="$1"
	case "$profile" in
	native) dynlex_build_native_llvm ;;
	web) dynlex_build_web_llvm ;;
	*)
		echo "Error: unknown LLVM toolchain profile: $profile" >&2
		return 1
		;;
	esac
}
