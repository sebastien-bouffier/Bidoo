# Reads an older hand-drawn panel (res/NAME.svg, from Inkscape) for its migration to a panel script:
#   labels()   each text (Inkscape keeps it in aria-label): its words, centre x, baseline y and height, in px
#   boxes()    the filled black and red rectangles (jack boxes, screens): colour and edges, in px
#   picture()  the picture (the shape at opacity under 1) as SVG, with its parents' transforms, for a NAME_parts.svg
# Positions come from Inkscape's --query-all, so transforms are applied.
#   python tools/panels/old_panel.py LIMBO      prints what it finds
import os, re, subprocess, sys
import xml.etree.ElementTree as ET

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
INKSCAPE = r"C:\Program Files\Inkscape\bin\inkscape.exe"
NS = {"svg": "http://www.w3.org/2000/svg"}

def _path(name):
	return os.path.join(REPO, "res", name + ".svg")

def _width_px(root):
	# the panel's width in Rack px: 380 px are 128.5 mm
	w = root.get("width")
	if w.endswith("mm"):
		return float(w[:-2]) * 380.0 / 128.5
	return float(re.sub(r"[a-z]+$", "", w))

def _bboxes(name, root):
	out = subprocess.run([INKSCAPE, "--query-all", _path(name)], capture_output=True, text=True).stdout
	rows = [l.split(",") for l in out.splitlines() if l.count(",") == 4]
	# Inkscape's px for the document; scale them to Rack px
	vb = [float(v) for v in root.get("viewBox").split()]
	doc_px = float(re.sub(r"[a-z]+$", "", root.get("width")))
	if root.get("width").endswith("mm"):
		doc_px = doc_px * 96 / 25.4
	s = _width_px(root) / doc_px
	return {r[0]: tuple(float(v) * s for v in r[1:]) for r in rows}

def _style(el, key):
	m = re.search(r"(?<![-\w])" + key + r"\s*:\s*([^;]+)", el.get("style", ""))   # opacity, not fill-opacity
	return (m.group(1).strip() if m else el.get(key, "")).lower()

def load(name):
	tree = ET.parse(_path(name))
	root = tree.getroot()
	return tree, root, _bboxes(name, root)

def labels(name):
	_, root, bb = load(name)
	out = []
	for el in root.iter():
		t = el.get("aria-label")
		if t and el.get("id") in bb:
			x, y, w, h = bb[el.get("id")]
			out.append((t, x + w / 2, y + h, h))
	return out

def boxes(name):
	_, root, bb = load(name)
	out = []
	for el in root.iter():
		if el.tag.split("}")[1] not in ("path", "rect") or el.get("id") not in bb:
			continue
		fill = _style(el, "fill")
		if fill in ("#000000", "#cd1f00") and _style(el, "opacity") in ("", "1"):
			x, y, w, h = bb[el.get("id")]
			if w > 12 and h > 12 and not el.get("aria-label"):
				out.append((fill, x, y, x + w, y + h, el.get("id")))
	return out

def picture(name):
	tree, root, _ = load(name)
	parents = {c: p for p in root.iter() for c in p}
	# the picture: the largest path at opacity under 1, outside <defs> (Inkscape leaves unused shapes there)
	inDefs = set()
	for d in root.iter():
		if d.tag.endswith("defs"):
			inDefs.update(d.iter())
	candidates = [el for el in root.iter() if el.tag.endswith("path") and el not in inDefs and _style(el, "opacity")
	              and float(_style(el, "opacity")) < 1]
	for el in sorted(candidates, key=lambda e: -len(e.get("d", ""))):
		if True:
			chain = []
			p = parents.get(el)
			while p is not None and p is not root:
				if p.get("transform"):
					chain.append(p.get("transform"))
				p = parents.get(p)
			d = el.get("d")
			own = f' transform="{el.get("transform")}"' if el.get("transform") else ""
			body = f'<path id="picture"{own} style="opacity:0.08;fill:#000000" d="{d}"/>'
			for t in chain:
				body = f'<g transform="{t}">{body}</g>'
			return body, root.get("viewBox")
	return None, root.get("viewBox")

if __name__ == "__main__":
	n = sys.argv[1]
	for l in labels(n):
		print("label %-12r x %6.1f  baseline %6.1f  h %4.1f" % l)
	for b in boxes(n):
		print("box   %s  %6.1f %6.1f %6.1f %6.1f  %s" % b)
	pic, vb = picture(n)
	print("picture:", "yes" if pic else "none", vb)
