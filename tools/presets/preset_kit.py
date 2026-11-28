# Writes factory presets (presets/<model>/NN_Name.vcvm) from a module's param names.
# A preset script lists its params by enum name; the ids come from the source's enum ParamIds, so a preset can't
# point at the wrong knob. Every param must be given (the defaults are the script's job, read from configParam).
#   python tools/presets/acide.py      writes presets/ACIDE/
import json, os, re, sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))

def version():
	return json.load(open(os.path.join(ROOT, "plugin.json"), encoding="utf-8"))["version"]

def param_names(src, constants=None):
	# the enum ParamIds of src/<src>, in order, one name per id: an array (NAME = OTHER + n, or NUM_PARAMS = LAST + n)
	# gives "NAME+1", "NAME+2"... to the ids after its first. `constants`: values of names used in the enum (BANDS)
	text = open(os.path.join(ROOT, "src", src), encoding="utf-8-sig").read()
	consts = dict(constants or {})
	if "enum ParamIds" not in text:
		# the enum in a shared engine header (ZOUMAI.cpp, ENCORE.cpp: ZouSeq.hpp), with its int constants (NUM_TRACKS)
		for h in re.findall(r'#include\s+"(\w+Seq\.hpp)"', text):
			hdr = open(os.path.join(ROOT, "src", h), encoding="utf-8-sig").read()
			for m in re.finditer(r"constexpr\s+int\s+(\w+)\s*=\s*(\d+)\s*;", hdr):
				consts.setdefault(m.group(1), int(m.group(2)))
			text += hdr
	body = re.search(r"enum ParamIds\s*\{(.*?)\}", text, re.S).group(1)
	body = re.sub(r"//.*", "", body)
	value = {}
	starts = []   # (id, name) of each enum item
	nxt = 0
	for item in body.split(","):
		item = item.strip()
		if not item:
			continue
		m = re.fullmatch(r"(\w+)(?:\s*=\s*(\w+)(?:\s*\+\s*(\w+))?)?", item)
		if not m:
			raise ValueError("cannot read enum item " + item)
		name, base, off = m.groups()
		if base is not None:
			b = value[base] if base in value else int(base)
			o = 0 if off is None else (int(off) if off.isdigit() else consts[off])
			nxt = b + o
		value[name] = nxt
		starts.append((nxt, name))
		nxt += 1
	total = value["NUM_PARAMS"]
	names = [None] * total
	for v, name in starts:
		if name != "NUM_PARAMS":
			names[v] = name
	for i in range(total):
		if names[i] is None:
			k = i
			while names[k] is None or "+" in names[k]:
				k -= 1
			names[i] = "%s+%d" % (names[k], i - k)
	return names

def write(model, src, presets, data=None, constants=None):
	# presets: list of (name, {param name or "NAME+i": value}, data dict or None)
	names = param_names(src, constants)
	d = os.path.join(ROOT, "presets", model)
	os.makedirs(d, exist_ok=True)
	for f in os.listdir(d):
		if f.endswith(".vcvm"):
			os.remove(os.path.join(d, f))
	for i, (title, values, pdata) in enumerate(presets):
		missing = [n for n in names if n not in values]
		extra = [n for n in values if n not in names]
		if missing or extra:
			sys.exit("%s '%s': missing %s, unknown %s" % (model, title, missing, extra))
		j = {
			"plugin": "Bidoo",
			"model": model,
			"version": version(),
			"params": [{"value": float(values[n]), "id": k} for k, n in enumerate(names)],
			"data": pdata if pdata is not None else (data or {}),
		}
		path = os.path.join(d, "%02d_%s.vcvm" % (i + 1, title))
		with open(path, "w", encoding="utf-8", newline="\n") as f:
			json.dump(j, f, indent=2, ensure_ascii=False)
			f.write("\n")
		print("wrote", os.path.relpath(path, ROOT))
