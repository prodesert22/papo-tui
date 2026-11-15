"""Collect corresponding distro sources for runtime libraries included in a bundle."""

import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import tempfile
import urllib.request


def capture(*args):
    return subprocess.check_output(
        list(map(str, args)), text=True, env=dict(os.environ, LC_ALL="C")
    ).strip()


def run(*args, cwd=None):
    subprocess.run(list(map(str, args)), cwd=cwd, check=True, env=dict(os.environ, LC_ALL="C"))


def collect(stage, prefix, destination):
    manifests = list(stage.rglob("runtime-libraries.json"))
    if len(manifests) != 1:
        raise RuntimeError("Runtime library provenance manifest is required")
    libraries = json.loads(manifests[0].read_text())
    destination.mkdir(parents=True, exist_ok=True)
    shutil.copy2(manifests[0], destination / "runtime-libraries.json")
    packages = set()
    private = prefix.resolve()
    for entry in libraries:
        path = Path(entry["source"]).resolve()
        if path.is_relative_to(private):
            continue
        # Media binaries must come from the audited LGPL source builds.
        if any(
            part in path.name.lower()
            for part in (
                "libmpv",
                "avcodec",
                "avformat",
                "avfilter",
                "avutil",
                "swscale",
                "swresample",
            )
        ):
            raise RuntimeError(f"Media library came from an unaudited prefix: {path.name}")
        if os.name == "nt":
            msys_path = capture("cygpath", "-u", path)
            packages.add(capture("pacman", "-Qqo", msys_path))
        elif shutil.which("dpkg-query"):
            package = owner(path)
            fields = capture("dpkg-query", "-W", "-f=${source:Package}\t${source:Version}", package)
            source, version = fields.split("\t")
            packages.add(source + "=" + version)
        elif shutil.which("rpm"):
            packages.add(
                capture("rpm", "-qf", "--qf", "%{NAME}-%{VERSION}-%{RELEASE}.%{ARCH}", path)
            )
        else:
            raise RuntimeError("Cannot collect distro sources on this packaging host")
    if os.name != "nt" and packages:
        if shutil.which("dpkg-query"):
            run("apt-get", "source", "--download-only", *sorted(packages), cwd=destination)
        else:
            run("dnf", "download", "--srpm", "--destdir", destination, *sorted(packages))
    else:
        for package in sorted(packages):
            collect_msys(package, destination)
    (destination / "packages.json").write_text(json.dumps(sorted(packages), indent=2) + "\n")


def remove_tree(path):
    """Remove a tree, including read-only files such as git objects on Windows."""

    def writable(function, name, _):
        os.chmod(name, stat.S_IWRITE)
        function(name)

    shutil.rmtree(path, onexc=writable)


def collect_msys(package, destination):
    cache = destination / "binary-package-cache"
    cache.mkdir(exist_ok=True)
    path = capture("cygpath", "-u", cache)
    installed = capture("pacman", "-Q", package).split()
    run("pacman", "-Sw", "--noconfirm", "--nodeps", "--cachedir", path, package)
    binaries = [p for p in cache.glob(package + "-*.pkg.tar.*") if not p.name.endswith(".sig")]
    if len(binaries) != 1:
        raise RuntimeError("Expected one exact MSYS2 binary package")
    info = capture("bsdtar", "-xOf", binaries[0], ".PKGINFO")
    metadata = dict(line.split(" = ", 1) for line in info.splitlines() if " = " in line)
    if metadata["pkgver"] != installed[1]:
        raise RuntimeError(f"Repository no longer has the installed source version: {package}")
    base = metadata["pkgbase"]
    version = metadata["pkgver"]
    archive = destination / f"{base}-{version}.src.tar.zst"
    url = f"https://repo.msys2.org/mingw/sources/{archive.name}"
    with urllib.request.urlopen(url, timeout=120) as source, archive.open("wb") as output:
        shutil.copyfileobj(source, output)
    work = destination / (base + "-sources")
    work.mkdir(exist_ok=True)
    run("bsdtar", "-xf", archive, "-C", work)
    recipes = list(work.rglob("PKGBUILD"))
    if len(recipes) != 1:
        raise RuntimeError("Expected one source recipe")
    recipe = capture("cygpath", "-u", recipes[0].parent)
    # makepkg verifies upstream signatures against the keys the recipe trusts;
    # read them from .SRCINFO instead of executing the PKGBUILD.
    srcinfo = recipes[0].parent / ".SRCINFO"
    keys = (
        [
            line.split("=", 1)[1].strip()
            for line in srcinfo.read_text(encoding="utf-8").splitlines()
            if line.strip().startswith("validpgpkeys =")
        ]
        if srcinfo.is_file()
        else []
    )
    if keys:
        run(
            "bash",
            "-lc",
            'gpg --batch --keyserver hkps://keyserver.ubuntu.com --recv-keys "$@"',
            "papo-keys",
            *keys,
        )
    run(
        "bash",
        "-lc",
        'cd "$1" && makepkg-mingw --allsource --nodeps --noconfirm',
        "papo-sources",
        recipe,
    )
    # The all-source archive includes exact upstream sources and packaging patches.
    complete = list(recipes[0].parent.glob("*.src.tar.*"))
    if not complete:
        raise RuntimeError("MSYS2 did not produce a complete source archive")
    for source in complete:
        shutil.copy2(source, destination / source.name)
    remove_tree(work)
    remove_tree(cache)


def owner(path):
    """Resolve merged-/usr paths without guessing the corresponding package."""
    if os.name == "nt":
        return capture("pacman", "-Qqo", capture("cygpath", "-u", path))
    if shutil.which("dpkg-query"):
        candidates = [path]
        if str(path).startswith("/usr/lib/"):
            candidates.append(Path(str(path).removeprefix("/usr")))
        for candidate in candidates:
            result = subprocess.run(
                ["dpkg-query", "-S", str(candidate)], capture_output=True, text=True
            )
            if result.returncode == 0:
                return result.stdout.splitlines()[0].split(": ", 1)[0]
        raise RuntimeError(f"No owning package for {path.name}")
    return capture("rpm", "-qf", "--qf", "%{NAME}", path)


def rpm_licenses(package):
    """Installed license files of package, or of the packages built from its source RPM."""

    def declared(name):
        # Some packages, such as libX11-common, mark their license as documentation.
        return [
            path
            for path in map(Path, capture("rpm", "-ql", name).splitlines())
            if "/share/licenses/" in str(path)
            or "/share/doc/" in str(path)
            and path.name.upper().startswith(("COPYING", "LICENSE", "LICENCE"))
        ]

    # Subpackages such as cairo-gobject leave the license to the installed
    # package built from the same source RPM (cairo).
    origin = capture("rpm", "-q", "--qf", "%{SOURCERPM}", package)
    siblings = sorted(
        {
            name
            for name, source in (
                line.split(" ", 1)
                for line in capture("rpm", "-qa", "--qf", "%{NAME} %{SOURCERPM}\\n").splitlines()
            )
            if source == origin and name != package
        }
    )
    files = {name: declared(name) for name in [package, *siblings]}
    for paths in files.values():
        if installed := [path for path in paths if path.is_file()]:
            return installed
    if any(files.values()):
        raise RuntimeError(f"License files of {package} are not installed")
    return []


def rpm_notice(package, target):
    """Record the license of a package whose RPMs install no license file, such as libdrm."""
    fields = "%{NAME}-%{VERSION}-%{RELEASE}\\t%{LICENSE}\\t%{SOURCERPM}\\t%{URL}"
    name, license, source, url = capture("rpm", "-q", "--qf", fields, package).split("\t")
    notice = target / "NOTICE"
    notice.write_text(
        f"{name} is distributed under {license}; its packages install no license file.\n"
        f"Its license and copyright notices are in the source files of {source},\n"
        "included in the dependency source archive of this package.\n"
        f"Upstream: {url}\n"
    )
    return notice


def msys_source_licenses(package, target):
    """Copy the upstream license files of an MSYS2 package that installs none, such as libidn2."""
    names = ("COPYING", "LICENSE", "LICENCE")
    with tempfile.TemporaryDirectory() as directory:
        work = Path(directory)
        run(
            "pacman",
            "-Sw",
            "--noconfirm",
            "--nodeps",
            "--cachedir",
            capture("cygpath", "-u", work),
            package,
        )
        binaries = [p for p in work.glob(package + "-*.pkg.tar.*") if not p.name.endswith(".sig")]
        if len(binaries) != 1:
            raise RuntimeError("Expected one exact MSYS2 binary package")
        info = capture("bsdtar", "-xOf", binaries[0], ".PKGINFO")
        metadata = dict(line.split(" = ", 1) for line in info.splitlines() if " = " in line)
        if metadata["pkgver"] != capture("pacman", "-Q", package).split()[1]:
            raise RuntimeError(f"Repository no longer has the installed source version: {package}")
        archive = work / f"{metadata['pkgbase']}-{metadata['pkgver']}.src.tar.zst"
        url = f"https://repo.msys2.org/mingw/sources/{archive.name}"
        with urllib.request.urlopen(url, timeout=120) as source, archive.open("wb") as output:
            shutil.copyfileobj(source, output)
        recipe = work / "recipe"
        recipe.mkdir()
        run("bsdtar", "-xf", archive, "-C", recipe)
        upstream = work / "upstream"
        upstream.mkdir()
        for inner in sorted(recipe.glob("*/*.tar.*")):
            if not inner.name.endswith(".sig"):
                run("bsdtar", "-xf", inner, "-C", upstream)
        licenses = sorted(
            path
            for path in [*upstream.glob("*/*"), *recipe.glob("*/*")]
            if path.is_file() and path.name.upper().startswith(names)
        )
        for i, path in enumerate(licenses):
            shutil.copy2(path, target / f"{i}-{path.name}")
        return len(licenses)


def collect_licenses(stage, prefix):
    private_licenses = prefix / "share/licenses/papo-dependencies"
    if not private_licenses.is_dir():
        raise RuntimeError("Missing private dependency licenses")
    shutil.copytree(
        private_licenses, stage / "share/licenses/papo-tui/dependencies", dirs_exist_ok=True
    )
    manifests = list(stage.rglob("runtime-libraries.json"))
    if len(manifests) != 1:
        raise RuntimeError("Runtime provenance is required")
    packages = {
        owner(Path(e["source"]).resolve())
        for e in json.loads(manifests[0].read_text())
        if not Path(e["source"]).resolve().is_relative_to(prefix.resolve())
    }
    destination = stage / "share/licenses/papo-tui/system"
    for package in sorted(packages):
        target = destination / package.replace(":", "-")
        target.mkdir(parents=True, exist_ok=True)
        if os.name == "nt":
            lines = capture("pacman", "-Ql", package).splitlines()
            paths = [
                Path(capture("cygpath", "-w", line.split(" ", 1)[1]))
                for line in lines
                if "/share/licenses/" in line and not line.endswith("/")
            ]
            if not any(p.is_file() for p in paths):
                if not msys_source_licenses(package, target):
                    raise RuntimeError(f"No license for {package} in its source package")
                continue
        elif shutil.which("dpkg-query"):
            paths = [Path("/usr/share/doc") / package.split(":")[0] / "copyright"]
        else:
            paths = rpm_licenses(package)
            if not paths:
                rpm_notice(package, target)
                continue
        paths = [p for p in paths if p.is_file()]
        if not paths:
            raise RuntimeError(f"No installed license for {package}")
        for i, path in enumerate(paths):
            shutil.copy2(path, target / f"{i}-{path.name}")
