# Checks the factory presets: valid JSON, the right plugin and model, every param id
# once and each value inside its configParam/configSwitch range (read from the source).
# Run from the plugin folder: python tools/tests/check_presets.py
import json, os, re, sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
SOURCES = {"REI": "REI.cpp", "FILTRE": "FILTRE.cpp", "PARTICULES": "PARTICULES.cpp", "DIKTAT": "DIKTAT.cpp"}
CONSTANTS = {"NUM_INTERVALS": 10, "PrismReverb::NumAlgos": 8, "filtre::kNumTypes": 71, "particules::kNumIntervals": 7, "quantizer::numNotes": 13,
	"quantizer::numScales": 46}

def number(expr):
	expr = expr.strip()
	for k, v in CONSTANTS.items():
		expr = expr.replace(k, str(v))
	expr = re.sub(r"(\d\.?)f\b", r"\1", expr)
	if not re.fullmatch(r"[\d\s.+\-*/()]+", expr):
		raise ValueError("cannot read " + expr)
	return float(eval(expr))

def ranges(src):
	text = open(src, encoding="utf-8-sig").read()
	ids = re.search(r"enum ParamIds\s*\{(.*?)NUM_PARAMS", text, re.S).group(1)
	ids = re.sub(r"//.*", "", ids)   # comments on the ids
	names = [n.strip() for n in ids.split(",") if n.strip()]
	r = {}
	for m in re.finditer(r"config(Param|Switch|Button)(?:<\w+>)?\((\w+_PARAM)\s*,([^;]*)", text):
		kind, name, rest = m.groups()
		if kind == "Button":
			r[name] = (0.0, 1.0)
		else:
			parts = rest.split(",")
			r[name] = (number(parts[0]), number(parts[1]))
	return names, r

errors = 0
count = 0
for slug, src in SOURCES.items():
	names, r = ranges(os.path.join(ROOT, "src", src))
	d = os.path.join(ROOT, "presets", slug)
	files = sorted(f for f in os.listdir(d) if f.endswith(".vcvm"))
	if not 3 <= len(files) <= 5:
		print("%s: %d presets" % (slug, len(files))); errors += 1
	for f in files:
		count += 1
		try:
			j = json.load(open(os.path.join(d, f), encoding="utf-8"))
		except ValueError as e:
			print("%s/%s: bad JSON: %s" % (slug, f, e)); errors += 1; continue
		if j.get("plugin") != "Bidoo" or j.get("model") != slug:
			print("%s/%s: plugin/model" % (slug, f)); errors += 1
		if "themeId" in j.get("data", {}):
			print("%s/%s: themeId in data" % (slug, f)); errors += 1
		seen = sorted(p["id"] for p in j["params"])
		if seen != list(range(len(names))):
			print("%s/%s: param ids %s" % (slug, f, seen)); errors += 1
		for p in j["params"]:
			lo, hi = r[names[p["id"]]]
			if not lo <= p["value"] <= hi:
				print("%s/%s: %s = %g outside %g..%g" % (slug, f, names[p["id"]], p["value"], lo, hi)); errors += 1
print("%d presets, %d errors" % (count, errors))
sys.exit(1 if errors else 0)
