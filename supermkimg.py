#!/usr/bin/env python3
"""supermkimg - build YOUR OWN super.img: a small Linux (Debian arm64) with Python 3 + Qt5 (PyQt5),
a splash screen, your home screen and your apps, wrapped in Android's dynamic-partition format.
No systemd: /sbin/init is a tiny C launcher that starts home.py (see README.md).

Run on an x86-64 Linux PC (Ubuntu/Debian), as root:

    sudo apt install debootstrap e2fsprogs python3 qemu-user-binfmt binfmt-support debian-archive-keyring
    sudo ./supermkimg.py build                          # full build, output in ./out/
    sudo ./supermkimg.py build --rootfs-dir build/rootfs --no-pyc   # re-pack only (after editing system/)
    ./supermkimg.py inspect out/super_mine.raw.img      # verify an image

What goes into the image:
    1. a Debian arm64 base + python3 + python3-pyqt5 + fonts        (debootstrap, once)
    2. ./system/*   copied over it   (home screen, apps, mine_ui, /etc/mine/env)    <- YOUR files
    3. ./bin/init_launcher -> /sbin/init   fallback_gui -> /sbin/fallback-gui   fb_pump -> /sbin/fb-pump
       splash -> /sbin/splash
    4. an ext4 image of all that, wrapped as the single logical partition "system" of a super image
"""
import argparse, os, shutil, subprocess, sys, textwrap

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import make_super                                           # the LP-metadata + sparse writer

BASE_PKGS = ["python3", "python3-pyqt5", "fonts-dejavu-core"]
DIRS = ["android", "userdata", "run", "dev", "proc", "sys", "tmp", "lib/modules",
        "var/lib/lxc/android/rootfs", "etc/mine", "opt/mine"]
BINS = (("bin/init_launcher", "sbin/init"), ("bin/fallback_gui", "sbin/fallback-gui"),
        ("bin/fb_pump", "sbin/fb-pump"), ("bin/splash", "sbin/splash"))

# bits an older kit (the systemd experiment) may have left in a reused rootfs
OLD_SYSTEMD_BITS = ["etc/systemd/system/mine-prepare.service", "etc/systemd/system/mine-fbpump.service",
                    "etc/systemd/system/mine-session.service", "etc/systemd/system/mine-rescue.service",
                    "etc/systemd/system.conf.d/mine.conf", "etc/systemd/journald.conf.d/mine.conf",
                    "opt/mine/bin/session.py", "opt/mine/bin/mine-prepare", "sbin/mine-launcher",
                    "etc/systemd/system/multi-user.target.wants/mine-prepare.service",
                    "etc/systemd/system/multi-user.target.wants/mine-fbpump.service",
                    "etc/systemd/system/multi-user.target.wants/mine-session.service"]
OLD_MASKS = ["getty@.service", "serial-getty@.service", "console-getty.service", "systemd-remount-fs.service"]
OLD_FSTAB = "tmpfs /tmp tmpfs mode=1777,nosuid,nodev 0 0"

def say(msg): print(f"\n==> {msg}", flush=True)
def run(cmd, **kw): print("   $", " ".join(cmd), flush=True); return subprocess.run(cmd, check=True, **kw)

def size_arg(s):
    s = str(s).strip().upper(); mult = {"K": 1 << 10, "M": 1 << 20, "G": 1 << 30}
    return int(float(s[:-1]) * mult[s[-1]]) if s[-1] in mult else int(s)

def need(tool, hint):
    if not shutil.which(tool): sys.exit(f"missing tool '{tool}'.  {hint}")

def qemu_into_rootfs(rootfs):
    """Make qemu-aarch64 runnable INSIDE the arm64 chroot.
    Old systems ship a static qemu-aarch64-static. Newer Ubuntu only ships a dynamic qemu-aarch64
    (packages qemu-user + qemu-user-binfmt), which needs its x86 shared libraries inside the chroot,
    so we copy the binary and those libraries in (remove_qemu() deletes them again)."""
    copied = []
    def put(src, dst_rel):
        dst_rel = dst_rel.lstrip("/"); dst = os.path.join(rootfs, dst_rel)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        if os.path.lexists(dst): os.unlink(dst)
        shutil.copy2(src, dst); copied.append(dst_rel)
    static = "/usr/bin/qemu-aarch64-static"
    if os.path.exists(static):
        put(static, "usr/bin/qemu-aarch64-static")
    else:
        q = shutil.which("qemu-aarch64")
        if not q: sys.exit("no qemu-aarch64 found.  sudo apt install qemu-user-binfmt binfmt-support   (older Ubuntu/Debian: qemu-user-static)")
        print("   no static qemu here: copying the dynamic qemu-aarch64 + its libraries into the rootfs (removed again afterwards)")
        for dst in ("usr/bin/qemu-aarch64-static", "usr/bin/qemu-aarch64", "usr/libexec/qemu-binfmt/aarch64-binfmt-P"):
            put(q, dst)
        for line in subprocess.run(["ldd", q], capture_output=True, text=True).stdout.splitlines():
            path = next((x for x in line.split() if x.startswith("/")), None)
            if path and os.path.exists(path): put(path, path)
    with open(os.path.join(rootfs, ".qemu-copied"), "w") as f: f.write("\n".join(copied))

def remove_qemu(rootfs):
    lst = os.path.join(rootfs, ".qemu-copied")
    if not os.path.exists(lst): return
    rels = [r for r in open(lst).read().split("\n") if r]
    for r in rels:
        p = os.path.join(rootfs, r)
        if os.path.lexists(p): os.unlink(p)
    dirs = set()
    for r in rels:
        d = os.path.dirname(r)
        while d: dirs.add(d); d = os.path.dirname(d)
    for d in sorted(dirs, key=len, reverse=True):
        try: os.rmdir(os.path.join(rootfs, d))                 # only removes directories that are now empty
        except OSError: pass
    os.unlink(lst)

def check_chroot(rootfs):
    if subprocess.run(["chroot", rootfs, "/bin/true"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode != 0:
        sys.exit("cannot run arm64 programs inside the chroot (qemu/binfmt problem). Check that this prints a line:\n"
                 "    ls /proc/sys/fs/binfmt_misc/ | grep qemu-aarch64\n"
                 "if it prints nothing:  sudo systemctl restart systemd-binfmt ; sudo update-binfmts --enable qemu-aarch64")

def debootstrap(a, rootfs):
    need("debootstrap", "sudo apt install debootstrap")
    if not (os.path.exists("/usr/bin/qemu-aarch64-static") or shutil.which("qemu-aarch64")):
        sys.exit("missing qemu for arm64.  sudo apt install qemu-user-binfmt binfmt-support   (older systems: qemu-user-static)")
    if "debian" in a.mirror and not os.path.exists("/usr/share/keyrings/debian-archive-keyring.gpg"):
        sys.exit("missing Debian signing keys (needed to bootstrap Debian on Ubuntu).  sudo apt install debian-archive-keyring")
    if not os.path.exists("/proc/sys/fs/binfmt_misc/qemu-aarch64"):
        print("   note: binfmt for qemu-aarch64 is not registered; try: sudo systemctl restart systemd-binfmt  (or: sudo update-binfmts --enable qemu-aarch64)")
    pkgs = ",".join(BASE_PKGS + [p for p in a.extra_pkgs.split(",") if p])
    say(f"debootstrap {a.dist} arm64 (first stage, downloads packages - a few hundred MB)")
    cache = os.path.join(HERE, "build", "cache")
    run(["debootstrap", "--arch=arm64", "--variant=minbase", "--foreign", f"--cache-dir={cache}", f"--include={pkgs}", a.dist, rootfs, a.mirror])
    qemu_into_rootfs(rootfs); check_chroot(rootfs)
    say("debootstrap second stage (runs arm64 code under qemu, takes a few minutes)")
    run(["chroot", rootfs, "/debootstrap/debootstrap", "--second-stage"])

def remove_old_systemd_bits(rootfs):
    """A rootfs reused from the systemd experiment: drop our units/mask links so nothing half-configured remains."""
    gone = 0
    for rel in OLD_SYSTEMD_BITS:
        p = os.path.join(rootfs, rel)
        if os.path.lexists(p): os.unlink(p); gone += 1
    for m in OLD_MASKS:
        p = os.path.join(rootfs, "etc/systemd/system", m)
        if os.path.islink(p) and os.readlink(p) == "/dev/null": os.unlink(p); gone += 1
    fstab = os.path.join(rootfs, "etc/fstab")
    if os.path.isfile(fstab) and OLD_FSTAB in open(fstab).read(): os.unlink(fstab); gone += 1
    for d in ("opt/mine/bin",):
        try: os.rmdir(os.path.join(rootfs, d))
        except OSError: pass
    if gone: print(f"   removed {gone} leftover systemd-experiment files from the reused rootfs")

def customise(a, rootfs):
    say("installing your files (system/ overlay, launcher, splash, fb-pump, crash screen)")
    remove_old_systemd_bits(rootfs)
    shutil.copytree(os.path.join(HERE, "system"), rootfs, dirs_exist_ok=True, symlinks=True,
                    ignore=shutil.ignore_patterns("__pycache__", "*.pyc"))
    for d in DIRS: os.makedirs(os.path.join(rootfs, d), exist_ok=True)
    os.chmod(os.path.join(rootfs, "tmp"), 0o1777)
    for src, dst in BINS:
        d = os.path.join(rootfs, dst)
        if os.path.lexists(d): os.unlink(d)
        shutil.copy2(os.path.join(HERE, src), d); os.chmod(d, 0o755)
    if not a.no_pyc:
        say("pre-compiling Python bytecode (the rootfs is mounted read-only on the device)")
        qemu_into_rootfs(rootfs)
        subprocess.run(["chroot", rootfs, "python3", "-m", "compileall", "-q", "/usr/lib/python3", "/opt/mine"])
    remove_qemu(rootfs)
    say("trimming")
    for p in ("var/lib/apt/lists", "var/cache/apt", "usr/share/doc", "usr/share/man", "usr/share/info", "usr/bin/qemu-aarch64-static", "debootstrap"):
        shutil.rmtree(os.path.join(rootfs, p), ignore_errors=True)
        if os.path.isfile(os.path.join(rootfs, p)): os.unlink(os.path.join(rootfs, p))
    if not os.path.exists(os.path.join(rootfs, "usr/bin/python3")):
        print("   WARNING: /usr/bin/python3 is missing in this rootfs - the home screen cannot start.")

def make_ext4(a, rootfs, out):
    used = int(subprocess.check_output(["du", "-sm", rootfs]).split()[0])
    size = a.size_mib or int(used * 1.12) + a.free_mib
    say(f"building ext4: {used} MiB of files -> {size} MiB image")
    base = ["mke2fs", "-q", "-t", "ext4", "-L", "system", "-b", "4096", "-m", "0"]
    tail = ["-E", "root_owner=0:0", "-d", rootfs, out, f"{size}M"]
    if os.path.exists(out): os.unlink(out)
    # newer e2fsprogs enable 'orphan_file', which a 4.19 kernel cannot mount -> switch it off when supported
    if subprocess.run(base + ["-O", "^orphan_file,^metadata_csum_seed"] + tail, stderr=subprocess.DEVNULL).returncode != 0:
        if os.path.exists(out): os.unlink(out)
        run(base + tail)
    run(["e2fsck", "-fn", out], stdout=subprocess.DEVNULL)
    return size

def build(a):
    if os.geteuid() != 0 and not a.allow_nonroot: sys.exit("run as root (sudo): chroot and file ownership need it")
    need("mke2fs", "sudo apt install e2fsprogs")
    out = os.path.abspath(a.out); os.makedirs(out, exist_ok=True)
    rootfs = os.path.abspath(a.rootfs_dir or os.path.join(HERE, "build", "rootfs"))
    if not a.rootfs_dir:
        if os.path.isdir(os.path.join(rootfs, "usr")) and not a.fresh:
            say(f"re-using existing {rootfs} (use --fresh to rebuild it from scratch)")
        else:
            shutil.rmtree(rootfs, ignore_errors=True); os.makedirs(os.path.dirname(rootfs), exist_ok=True)
            debootstrap(a, rootfs)
    customise(a, rootfs)
    ext4 = os.path.join(out, "system.ext4"); size = make_ext4(a, rootfs, ext4)
    dev = size_arg(a.device_size)
    if (size + 1) << 20 > dev: sys.exit(f"system ({size} MiB) does not fit the declared super size; pass --device-size {(size + 64) // 1024 + 1}G")
    say("wrapping in LP metadata (super format)")
    raw = make_super.build(ext4, dev)
    prefix = os.path.join(out, "super_mine")
    open(prefix + ".raw.img", "wb").write(raw); open(prefix + ".sparse.img", "wb").write(make_super.sparse(raw, len(raw)))
    if not a.keep_ext4: os.unlink(ext4)
    inspect(prefix + ".raw.img")
    print(textwrap.dedent(f"""
      done:
        {prefix}.raw.img      flash with mtkclient / SP Flash Tool / dd   ({len(raw) >> 20} MiB)
        {prefix}.sparse.img   flash with fastboot (fastbootd)
      pair it with the super-only boot image (ubports_superonly_boot.img).  Flash example:
        python3 mtk.py w super {prefix}.raw.img"""))

def inspect(path):
    r = subprocess.run([sys.executable, os.path.join(HERE, "validate_super.py"), path])
    if r.returncode: sys.exit("validation FAILED")

def main():
    ap = argparse.ArgumentParser(description="build your own super.img (Python + Qt)")
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("build")
    b.add_argument("--dist", default="bookworm"); b.add_argument("--mirror", default="http://deb.debian.org/debian")
    b.add_argument("--extra-pkgs", default="", help="comma separated extra Debian packages, e.g. python3-numpy,python3-requests")
    b.add_argument("--rootfs-dir", help="use this existing rootfs directory (no debootstrap)")
    b.add_argument("--fresh", action="store_true", help="rebuild build/rootfs from scratch")
    b.add_argument("--out", default=os.path.join(HERE, "out"))
    b.add_argument("--device-size", default="4G", help="size written into the LP metadata for the super block device (e.g. 4G, 10G)")
    b.add_argument("--size-mib", type=int, help="force the ext4 size"); b.add_argument("--free-mib", type=int, default=128, help="spare space in the system partition")
    b.add_argument("--no-pyc", action="store_true"); b.add_argument("--keep-ext4", action="store_true"); b.add_argument("--allow-nonroot", action="store_true")
    i = sub.add_parser("inspect"); i.add_argument("image")
    a = ap.parse_args()
    build(a) if a.cmd == "build" else inspect(a.image)

if __name__ == "__main__":
    main()
