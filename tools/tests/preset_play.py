# Builds and runs tools/tests/preset_play.cpp: plays the factory presets of some modules on a clock and prints, per
# output, the gates and notes of each clock tick (see the .cpp). Uses the packaged plugin (make dist) and Rack Pro's
# libRack.dll, like module_harness.py.
#   python tools/tests/preset_play.py ZOUMAI
#   python tools/tests/preset_play.py dTrOY --bpm 120 --ppq 4 --ticks 32 --in "ext clock=clock" --in run=cv:10
import glob, os, shutil, subprocess, sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
SDK = os.environ.get("RACK_DIR", "G:/Rack-SDK")
RACK = "C:/Program Files/VCV/Rack2Pro"
MINGW = "C:/msys64/mingw64/bin"
OUT = os.path.join(ROOT, "build", "preset_play")

def main(args):
	if not args:
		print(__doc__ if __doc__ else "usage: preset_play.py SLUG ... [options]")
		return 2
	os.makedirs(OUT, exist_ok=True)
	env = dict(os.environ)
	env["PATH"] = os.pathsep.join([RACK, MINGW, env.get("PATH", "")])
	env["TMP"] = env["TEMP"] = OUT
	exe = os.path.join(OUT, "preset_play.exe")
	src = os.path.join(ROOT, "tools", "tests", "preset_play.cpp")
	if not os.path.exists(exe) or os.path.getmtime(exe) < os.path.getmtime(src):
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
	p = subprocess.run([exe, user, os.path.join(ROOT, "presets")] + args, env=env, capture_output=True, text=True, timeout=600)
	print(p.stdout)
	if p.returncode not in (0, 1):
		print("CRASH (exit code %d)" % p.returncode)
		return 3
	return p.returncode

if __name__ == "__main__":
	sys.exit(main(sys.argv[1:]))
