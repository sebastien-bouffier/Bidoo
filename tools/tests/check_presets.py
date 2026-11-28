# Checks the factory presets: valid JSON, the right plugin and model, every param id
# once and each value inside its configParam/configSwitch range (read from the source).
# Every folder in presets/ is checked; its module's source is found by its model slug (createModel).
# Run from the plugin folder: python tools/tests/check_presets.py
import glob, json, os, re, sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools", "presets"))
import preset_kit

CONSTANTS = {"NUM_INTERVALS": 10, "PrismReverb::NumAlgos": 8, "filtre::kNumTypes": 71, "particules::kNumIntervals": 7, "quantizer::numNotes": 13,
	"quantizer::numScales": 46}

def number(expr, defines):
	expr = expr.strip()
	for k, v in list(CONSTANTS.items()) + list(defines.items()):
		expr = re.sub(r"\b" + re.escape(k) + r"\b", str(v), expr)
	expr = re.sub(r"(\d\.?\d*)f\b", r"\1", expr)
	expr = re.sub(r"std::pow\(([^,]+),([^)]+)\)", r"((\1)**(\2))", expr)
	if not re.fullmatch(r"[\d\s.+\-*/()e]+", expr):
		raise ValueError("cannot read " + expr)
	return float(eval(expr))

def defines(text):
	# #define NAME 16, and (static) const(expr) float/int NAME = 0.5f;: names a range or an enum may use
	d = {m.group(1): int(m.group(2)) for m in re.finditer(r"#define\s+(\w+)\s+(\d+)\b", text)}
	for m in re.finditer(r"(?:static\s+)?const(?:expr)?\s+(?:float|int|double)\s+(\w+)\s*=\s*(-?[\d.]+)f?\s*;", text):
		d[m.group(1)] = float(m.group(2)) if "." in m.group(2) else int(m.group(2))
	return d

def sources():
	# model slug -> source file, from createModel<Module, Widget>("slug")
	out = {}
	for path in glob.glob(os.path.join(ROOT, "src", "*.cpp")):
		text = open(path, encoding="utf-8-sig").read()
		for m in re.finditer(r'createModel<\s*\w+\s*,\s*\w+\s*>\(\s*"([^"]+)"', text):
			out[m.group(1)] = os.path.basename(path)
	return out

def ranges(src, names):
	text = open(os.path.join(ROOT, "src", src), encoding="utf-8-sig").read()
	defs = defines(text)
	r = {}
	# configParam(NAME_PARAM, ...) or, in a loop, configParam(NAME_PARAM + i, ...): the range of the whole array
	for m in re.finditer(r"config(Param|Switch|Button)(?:<\w+>)?\((\w+_PARAM)(\s*\+\s*\w+)?\s*,([^;]*)", text):
		kind, name, inLoop, rest = m.groups()
		if kind == "Button":
			lo, hi = 0.0, 1.0
		else:
			parts = rest.split(",")
			lo, hi = number(parts[0], defs), number(parts[1], defs)
		for n in names:
			if n == name or (inLoop and n.startswith(name + "+")):
				r[n] = (lo, hi)
	return r, defs

errors = 0
count = 0
srcs = sources()
for d in sorted(glob.glob(os.path.join(ROOT, "presets", "*"))):
	slug = os.path.basename(d)
	if slug not in srcs:
		print("%s: no module with this slug" % slug); errors += 1; continue
	src = srcs[slug]
	text = open(os.path.join(ROOT, "src", src), encoding="utf-8-sig").read()
	names = preset_kit.param_names(src, defines(text))
	r, _ = ranges(src, names)
	# a param in the enum but never configured keeps Rack's default range, 0 to 1
	for n in names:
		r.setdefault(n, (0.0, 1.0))
	files = sorted(f for f in os.listdir(d) if f.endswith(".vcvm"))
	# 3 at least; up to 16 (PRiSME has 15: an additive voice needs a sound per family to show what it does)
	if not 3 <= len(files) <= 16:
		print("%s: %d presets" % (slug, len(files))); errors += 1
	for f in files:
		count += 1
		try:
			j = json.load(open(os.path.join(d, f), encoding="utf-8"))
		except ValueError as e:
			print("%s/%s: bad JSON: %s" % (slug, f, e)); errors += 1; continue
		if j.get("plugin") != "Bidoo" or j.get("model") != slug:
			print("%s/%s: plugin/model" % (slug, f)); errors += 1
		if "themeId" in j.get("data", {}) or "panelTheme" in j.get("data", {}):
			print("%s/%s: theme in data" % (slug, f)); errors += 1
		seen = sorted(p["id"] for p in j["params"])
		if seen != list(range(len(names))):
			print("%s/%s: param ids %s" % (slug, f, seen)); errors += 1
			continue
		for p in j["params"]:
			lo, hi = r[names[p["id"]]]
			if not lo <= p["value"] <= hi:
				print("%s/%s: %s = %g outside %g..%g" % (slug, f, names[p["id"]], p["value"], lo, hi)); errors += 1
print("%d presets, %d errors" % (count, errors))
sys.exit(1 if errors else 0)
