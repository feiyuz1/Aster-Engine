"""Generate a new editable PBR material in the engine's current format."""
import json
from pathlib import Path
import re


def pbr_material(color=(.3, .7, 1, 1), metallic=0, roughness=.7,
                 emissive=(0, 0, 0), textures=None, alpha=False):
    source = (Path(__file__).parent / "templates" / "pbr.mat").read_text(encoding="utf-8")
    values = {"BaseColor": " ".join(map(str, color)), "Metallic": str(metallic),
              "Roughness": str(roughness), "Emissive": " ".join(map(str, emissive))}
    for name, path in (textures or {}).items():
        values[name] = json.dumps(str(path), ensure_ascii=False)
    for name, value in values.items():
        pattern = r'(^parameter "' + re.escape(name) + r'" \w+ \w+ )[^\n]*'
        source, count = re.subn(pattern, lambda match: match[1] + value, source, flags=re.M)
        if count != 1:
            raise ValueError("Unknown PBR parameter: " + name)
    if alpha or color[3] < .999:
        source = source.replace("settings Lit Opaque", "settings Lit Alpha", 1)
    return source
