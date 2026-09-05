"""Portable build and runtime verification shared by local and hosted gates."""

from __future__ import annotations

import importlib.util
import os
import platform
import shlex
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from types import ModuleType
from typing import cast

from llvm_tools import (
    apple_clang_tool,
    apple_cpp_include_directories,
    find_homebrew_llvm_tool,
    find_llvm_tool,
)
from source_policy import ROOT, iter_sources

ANDROID_PORT_REVISION = "3079116e84ea4f581ae4bc42e8219df52ece16d7"
ANDROID_NDK_VERSION = "28.2.13676358"
ANDROID_ABIS = ("x86_64", "arm64-v8a")


@dataclass(frozen=True)
class NativeProfile:
    name: str
    c_compiler: str
    cxx_compiler: str
    compiler_id: str
    executable_suffix: str = ""
    formatter: str = "clang-format"
    linter: str = "clang-tidy"
    sdk_root: str | None = None
    resource_dir: str | None = None
    cpp_include_dirs: tuple[str, ...] = ()


def run(
    command: list[str], *, capture: bool = False
) -> subprocess.CompletedProcess[str]:
    print(f"$ {shlex.join(command)}", flush=True)
    return subprocess.run(
        command,
        cwd=ROOT,
        check=True,
        capture_output=capture,
        text=True,
    )


def discover_directory(command: list[str], purpose: str) -> str:
    try:
        completed = subprocess.run(command, check=False, capture_output=True, text=True)
    except OSError as error:
        raise RuntimeError(f"cannot resolve {purpose}: {error}") from error
    output = completed.stdout.strip()
    candidate = Path(output)
    if completed.returncode != 0 or not output or not candidate.is_dir():
        detail = completed.stderr.strip() or completed.stdout.strip() or "no output"
        raise RuntimeError(f"cannot resolve {purpose}: {detail}")
    return str(candidate.resolve())


def native_profile() -> NativeProfile:
    system = platform.system()
    machine = platform.machine().lower()
    if system == "Linux":
        if machine not in {"x86_64", "amd64"}:
            raise RuntimeError(f"Linux CI currently owns x86-64, not {machine}")
        return NativeProfile("linux-x86_64", "clang", "clang++", "Clang")
    if system == "Darwin":
        if machine != "arm64":
            raise RuntimeError(f"macOS CI currently owns Apple Silicon, not {machine}")
        discovered = {
            name: find_homebrew_llvm_tool(name)
            for name in ("clang-format", "clang-tidy")
        }
        missing = [name for name, path in discovered.items() if path is None]
        if missing:
            raise RuntimeError(
                "Homebrew LLVM toolchain is incomplete: " + ", ".join(missing)
            )
        tools = {name: cast(str, path) for name, path in discovered.items()}
        tool_roots = {Path(path).parent for path in tools.values()}
        if len(tool_roots) != 1:
            raise RuntimeError(
                "Homebrew LLVM verification tools do not share one toolchain: "
                + ", ".join(sorted(str(root) for root in tool_roots))
            )
        compiler = apple_clang_tool("clang++")
        sdk_root = discover_directory(
            ["xcrun", "--sdk", "macosx", "--show-sdk-path"], "macOS SDK"
        )
        return NativeProfile(
            "macos-arm64-appleclang",
            apple_clang_tool("clang"),
            compiler,
            "AppleClang",
            formatter=tools["clang-format"],
            linter=tools["clang-tidy"],
            sdk_root=sdk_root,
            resource_dir=discover_directory(
                [
                    str(Path(tools["clang-tidy"]).with_name("clang")),
                    "-print-resource-dir",
                ],
                "Clang linter resource directory",
            ),
            cpp_include_dirs=apple_cpp_include_directories(compiler, sdk_root),
        )
    if system == "Windows":
        if machine not in {"x86_64", "amd64"}:
            raise RuntimeError(f"Windows CI currently owns x86-64, not {machine}")
        return NativeProfile("windows-x86_64", "clang-cl", "clang-cl", "Clang", ".exe")
    raise RuntimeError(
        f"no maintained native verification profile for {system} {machine}"
    )


def first_party_native_sources() -> list[str]:
    return [
        str(path.relative_to(ROOT))
        for path in iter_sources()
        if path.suffix in {".c", ".cc", ".cpp", ".h", ".hpp"}
    ]


def translation_units() -> list[str]:
    return [
        path
        for path in first_party_native_sources()
        if Path(path).suffix in {".c", ".cc", ".cpp"}
    ]


def common_checks(formatter: str | None = None) -> None:
    run([sys.executable, str(ROOT / "tools" / "policy.py")])
    run([sys.executable, str(ROOT / "tests" / "test_dependency_pins.py")])
    run([sys.executable, str(ROOT / "tests" / "test_source_policy.py")])
    run([sys.executable, str(ROOT / "tests" / "test_link_audit.py")])
    run([sys.executable, str(ROOT / "tests" / "test_llvm_tools.py")])
    run([sys.executable, str(ROOT / "tests" / "test_verification.py")])
    run(
        [
            formatter or find_llvm_tool("clang-format"),
            "--dry-run",
            "--Werror",
            *first_party_native_sources(),
        ]
    )


def configure(build_dir: Path, arguments: list[str]) -> None:
    run(
        [
            "cmake",
            "-S",
            str(ROOT),
            "-B",
            str(build_dir),
            "-G",
            "Ninja",
            "-DCMAKE_BUILD_TYPE=Debug",
            *arguments,
        ]
    )


def assert_compiler(build_dir: Path, language: str, expected: str) -> None:
    matches = list((build_dir / "CMakeFiles").glob(f"*/CMake{language}Compiler.cmake"))
    if len(matches) != 1:
        raise RuntimeError(
            f"expected one CMake {language} compiler record in {build_dir}, found {len(matches)}"
        )
    marker = f'set(CMAKE_{language}_COMPILER_ID "{expected}")'
    if marker not in matches[0].read_text(encoding="utf-8"):
        raise RuntimeError(f"{build_dir} did not configure {language} with {expected}")


def build_and_audit(build_dir: Path, executable_suffix: str = "") -> Path:
    run(["cmake", "--build", str(build_dir)])
    executable = build_dir / f"amigaport_tests{executable_suffix}"
    run([sys.executable, str(ROOT / "tools" / "link_audit.py"), str(executable)])
    return executable


def lint(
    build_dir: Path, linter: str | None = None, extra_arguments: tuple[str, ...] = ()
) -> None:
    run(
        [
            linter or find_llvm_tool("clang-tidy"),
            "-p",
            str(build_dir),
            *extra_arguments,
            *translation_units(),
        ]
    )


def verify_native() -> None:
    profile = native_profile()
    build_dir = ROOT / "build" / f"verify-{profile.name}"
    common_checks(profile.formatter)
    configure_arguments = [
        f"-DCMAKE_C_COMPILER={profile.c_compiler}",
        f"-DCMAKE_CXX_COMPILER={profile.cxx_compiler}",
    ]
    if profile.sdk_root is not None:
        configure_arguments.append(f"-DCMAKE_OSX_SYSROOT={profile.sdk_root}")
    configure(build_dir, configure_arguments)
    assert_compiler(build_dir, "C", profile.compiler_id)
    assert_compiler(build_dir, "CXX", profile.compiler_id)
    build_and_audit(build_dir, profile.executable_suffix)
    run(["ctest", "--test-dir", str(build_dir), "--output-on-failure"])
    lint_arguments: tuple[str, ...] = ()
    if profile.resource_dir is not None:
        lint_arguments += (f"--extra-arg-before=-resource-dir={profile.resource_dir}",)
    if profile.sdk_root is not None:
        lint_arguments += (f"--extra-arg-before=-isysroot{profile.sdk_root}",)
    if profile.cpp_include_dirs:
        lint_arguments += ("--extra-arg-before=-nostdinc++",)
        lint_arguments += tuple(
            f"--extra-arg-before=-isystem{directory}"
            for directory in profile.cpp_include_dirs
        )
    lint(build_dir, profile.linter, lint_arguments)


def resolve_android_port(explicit: Path | None) -> Path:
    candidates = [
        explicit,
        ROOT.parent / "android-port",
        ROOT / "build" / "deps" / "android-port",
    ]
    tried: list[Path] = []
    for candidate in candidates:
        if candidate is None:
            continue
        resolved = candidate.resolve()
        tried.append(resolved)
        if (resolved / "tools" / "android_port.py").is_file():
            revision = run(
                ["git", "-C", str(resolved), "rev-parse", "HEAD"], capture=True
            ).stdout.strip()
            if revision != ANDROID_PORT_REVISION:
                raise RuntimeError(
                    "shared/android-port revision mismatch: "
                    f"expected {ANDROID_PORT_REVISION}, got {revision}"
                )
            return resolved
    attempted = ", ".join(str(path) for path in tried)
    raise RuntimeError(f"shared/android-port is required; tried: {attempted}")


def load_android_contract(android_port: Path) -> ModuleType:
    source = android_port / "tools" / "android_port.py"
    spec = importlib.util.spec_from_file_location("amigaport_android_contract", source)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load shared Android contract: {source}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def resolve_ndk() -> Path:
    roots = [
        os.environ.get("ANDROID_HOME"),
        os.environ.get("ANDROID_SDK_ROOT"),
    ]
    candidates = [Path(root) / "ndk" / ANDROID_NDK_VERSION for root in roots if root]
    for candidate in candidates:
        if (candidate / "build" / "cmake" / "android.toolchain.cmake").is_file():
            return candidate.resolve()
    attempted = ", ".join(str(path) for path in candidates)
    raise RuntimeError(
        f"Android NDK {ANDROID_NDK_VERSION} is required; tried: {attempted}"
    )


def android_build(ndk: Path, abi: str, api: int) -> tuple[Path, Path]:
    build_dir = ROOT / "build" / f"verify-android-{abi}"
    configure(
        build_dir,
        [
            f"-DCMAKE_TOOLCHAIN_FILE={ndk / 'build/cmake/android.toolchain.cmake'}",
            f"-DANDROID_ABI={abi}",
            f"-DANDROID_PLATFORM=android-{api}",
            "-DANDROID_STL=c++_static",
        ],
    )
    assert_compiler(build_dir, "C", "Clang")
    assert_compiler(build_dir, "CXX", "Clang")
    return build_dir, build_and_audit(build_dir)


def select_android_device(contract: ModuleType, adb: str, serial: str) -> list[str]:
    """Return the exact online device command prefix or refuse ambiguous state."""
    devices = contract.adb_devices(adb)
    if devices != (serial,):
        visible = ", ".join(devices) if devices else "none"
        raise RuntimeError(
            f"Android verifier requires exactly {serial}; online devices: {visible}"
        )
    return [adb, "-s", serial]


def run_android_test(contract: ModuleType, executable: Path, serial: str) -> None:
    adb = shutil.which("adb")
    if adb is None:
        raise RuntimeError("Android runtime verification requires adb")
    device = select_android_device(contract, adb, serial)
    run([*device, "get-state"])
    abi = run(
        [*device, "shell", "getprop", "ro.product.cpu.abi"], capture=True
    ).stdout.strip()
    api = run(
        [*device, "shell", "getprop", "ro.build.version.sdk"], capture=True
    ).stdout.strip()
    if abi != "x86_64" or api != "35":
        raise RuntimeError(
            f"Android verifier requires API 35 x86_64, got API {api} {abi}"
        )
    remote = "/data/local/tmp/amigaport_tests"
    run([*device, "push", str(executable), remote])
    run([*device, "shell", "chmod", "755", remote])
    run([*device, "shell", remote])


def verify_android(android_port_dir: Path | None, serial: str) -> None:
    common_checks()
    android_port = resolve_android_port(android_port_dir)
    contract = load_android_contract(android_port)
    api = contract.DEFAULT_ANDROID_API
    if api != 21:
        raise RuntimeError(f"unexpected shared Android API floor: {api}")
    ndk = resolve_ndk()
    executables: dict[str, Path] = {}
    build_directories: dict[str, Path] = {}
    for abi in ANDROID_ABIS:
        if abi not in contract.NDK_TRIPLES:
            raise RuntimeError(
                f"shared Android contract does not support required ABI: {abi}"
            )
        contract.ndk_cxx_shared_library(ndk, abi)
        build_directories[abi], executables[abi] = android_build(ndk, abi, api)
    lint(build_directories["x86_64"])
    run_android_test(contract, executables["x86_64"], serial)
