# The README's picture of every module (docs/modules.png), grouped by family, from Rack's own renders.
#   1. make dist, then render the modules: put the .vcvplugin in <folder>/plugins-win-x64 and run
#      "C:/Program Files/VCV/Rack2Pro/Rack.exe" -t 2 -u <folder>     (screenshots at 2x in <folder>/screenshots/Bidoo)
#   2. python tools/readme_picture.py <folder>/screenshots/Bidoo
# The rows and families are below; a module missing from them is an error, so a new module can't be left out.
import os, sys
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
BACKGROUND = (38, 38, 40)
HEADING = (255, 233, 0)            # the screens' yellow
RULE = (90, 90, 92)
PANEL_H = 380                      # a panel's height in the picture (Rack px at 100 %)
GAP, SECTION_GAP, MARGIN = 26, 60, 24
HEADING_SIZE, HEADING_ROOM = 26, 50

# Each row: its families, left to right; a family: (heading or None to go on under the previous one, model slugs)
ROWS = [
	[("Clocks and logic", ["tOCAnTe", "lATe", "ChUTE", "RATEAU", "DilEMO", "lambda", "LoURdE"]),
	 ("Oscillators", ["antN", "TiARE", "liMonADe", "ForK", "ACIDE", "BanCau"]),
	 ("Dynamics", ["baR", "mINIBar"])],
	[("Sequencers and pitch", ["dTrOY", "bordL", "ZOUMAI", "ZOUMAI-Expander", "ENCORE", "ENCORE-Expander"])],
	[(None, ["DIKTAT", "REGISTRE"]),
	 ("Controllers", ["MOiRE", "PILOT", "HUITre", "SAISON", "dUKe", "fLAME"])],
	[("Samplers", ["SOUNAIRE", "COPIA", "PARTICULES", "OUAIve", "eDsaroS", "POUPRE", "MAGMA", "OAI", "cANARd", "EMILE"]),
	 ("Blank panel", ["VOID"])],
	[("Filters", ["lIMbO", "pErCO", "FILTRE", "BAFIS", "ziNC"]),
	 ("Reverbs and effects", ["REI", "RICOCHET", "HCTIP", "SPORE", "FREIN", "rabBIT", "BISTROT"]),
	 ("Mixing and utilities", ["ACnE", "MS", "SIGMA", "DOSE"])],
]

def font():
	for d in (os.environ.get("RACK_DIR", ""), "C:/Program Files/VCV/Rack2Pro", "G:/RackV2"):
		p = os.path.join(d, "res", "fonts", "DejaVuSans.ttf")
		if d and os.path.isfile(p):
			return ImageFont.truetype(p, HEADING_SIZE)
	sys.exit("DejaVuSans.ttf not found")

def main(shots):
	plugin = __import__("json").load(open(os.path.join(ROOT, "plugin.json"), encoding="utf-8"))
	slugs = [m["slug"] for m in plugin["modules"]]
	listed = [s for row in ROWS for _, mods in row for s in mods]
	missing = [s for s in slugs if s not in listed]
	unknown = [s for s in listed if s not in slugs]
	if missing or unknown:
		sys.exit("modules missing from ROWS: %s; unknown in ROWS: %s" % (missing, unknown))
	panels = {}
	for s in listed:
		im = Image.open(os.path.join(shots, s + ".png")).convert("RGB")
		panels[s] = im.resize((round(im.width * PANEL_H / im.height), PANEL_H), Image.LANCZOS)
	# lay out each row: families side by side, a family's panels GAP apart, families SECTION_GAP apart
	layout, y, width = [], MARGIN, 0
	for row in ROWS:
		x = MARGIN
		for title, mods in row:
			x0 = x
			for s in mods:
				layout.append((s, x, y + HEADING_ROOM))
				x += panels[s].width + GAP
			if title:
				layout.append((("heading", title, x - GAP), x0, y))
			x += SECTION_GAP - GAP
		width = max(width, x - SECTION_GAP + MARGIN)
		y += HEADING_ROOM + PANEL_H + 36
	height = y - 36 + MARGIN
	sheet = Image.new("RGB", (width, height), BACKGROUND)
	d = ImageDraw.Draw(sheet)
	f = font()
	for item, x, yy in layout:
		if isinstance(item, tuple):
			_, title, x1 = item
			d.text((x, yy + 4), title, font=f, fill=HEADING)
			d.line((x, yy + HEADING_ROOM - 10, x1, yy + HEADING_ROOM - 10), fill=RULE, width=2)
		else:
			sheet.paste(panels[item], (x, yy))
	out = os.path.join(ROOT, "docs", "modules.png")
	sheet.save(out, optimize=True)
	print("wrote", out, sheet.size)

if __name__ == "__main__":
	main(sys.argv[1])
