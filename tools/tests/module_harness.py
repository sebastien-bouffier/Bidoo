# Builds and runs tools/tests/module_harness.cpp on the packaged plugin (dist/Bidoo-*-win-x64.vcvplugin, from
# make dist) with Rack Pro's libRack.dll, in a user folder of its own under build/.
#   python tools/tests/module_harness.py              every module
#   python tools/tests/module_harness.py ACIDE baR    some modules (their slugs)
#   HARNESS_SEED=7 python tools/tests/module_harness.py    other random knobs and signals (default 12345)
# Needs C:/msys64/mingw64 (g++), the SDK in G:/Rack-SDK (or RACK_DIR) and Rack Pro 2 in C:/Program Files/VCV/Rack2Pro.
import glob, os, shutil, subprocess, sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
SDK = os.environ.get("RACK_DIR", "G:/Rack-SDK")
RACK = "C:/Program Files/VCV/Rack2Pro"
MINGW = "C:/msys64/mingw64/bin"
OUT = os.path.join(ROOT, "build", "harness")

def main(slugs):
	os.makedirs(OUT, exist_ok=True)
	env = dict(os.environ)
	env["PATH"] = os.pathsep.join([RACK, MINGW, env.get("PATH", "")])
	env["TMP"] = env["TEMP"] = OUT   # g++ writes its temporary files here, not in C:/Windows
	exe = os.path.join(OUT, "module_harness.exe")
	src = os.path.join(ROOT, "tools", "tests", "module_harness.cpp")
	cmd = [os.path.join(MINGW, "g++.exe"), "-std=c++17", "-O1", "-D_USE_MATH_DEFINES", "-I", SDK + "/include", "-I", SDK + "/dep/include",
	       src, "-L", SDK, "-lRack", "-o", exe]
	r = subprocess.run(cmd, env=env, capture_output=True, text=True)
	if r.returncode:
		print(r.stderr)
		return 2
	packages = sorted(glob.glob(os.path.join(ROOT, "dist", "Bidoo-*-win-x64.vcvplugin")), key=os.path.getmtime)
	if not packages:
		print("no dist/Bidoo-*-win-x64.vcvplugin: run make dist first")
		return 2
	user = os.path.join(OUT, "user")
	shutil.rmtree(user, ignore_errors=True)
	os.makedirs(os.path.join(user, "plugins-win-x64"))
	shutil.copy(packages[-1], os.path.join(user, "plugins-win-x64"))
	p = subprocess.run([exe, user] + slugs, env=env, capture_output=True, text=True, timeout=3600)
	lines = p.stdout.splitlines()
	fails = [l for l in lines if l.startswith("FAIL") or l.startswith("     ")]
	print("\n".join(fails))
	if p.returncode not in (0, 1):
		# a crash: the last step it started
		print("CRASH (exit code %d) during: %s" % (p.returncode, lines[-1] if lines else "start"))
		return 3
	print(lines[-1] if lines else "no output")
	return p.returncode

if __name__ == "__main__":
	sys.exit(main(sys.argv[1:]))
