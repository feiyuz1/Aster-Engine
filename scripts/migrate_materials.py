"""Offline, explicit conversion to TINY_MAT 3. Never used by the runtime.

Run with a project/source asset directory. Without --apply this only reports changes.
Backups are written before replacing any source. Generated build/saved/dist folders
are excluded; regenerate them with the new engine after migration.
"""
import argparse
import json
from pathlib import Path
import re
import shlex
import shutil
from datetime import datetime

TEMPLATE = Path(__file__).parent / "templates" / "pbr.mat"
SKIP = {".git", "intermediate", "build", "saved", "dist", "binaries", "captures", "artifacts"}
NAMES = {"color": "BaseColor", "metallic": "Metallic", "roughness": "Roughness",
         "emissive": "Emissive", "texture": "BaseMap", "normalTexture": "NormalMap",
         "metallicTexture": "MetallicMap", "roughnessTexture": "RoughnessMap",
         "aoTexture": "AOMap", "emissiveTexture": "EmissiveMap", "packedTexture": "PackedMap",
         "packedLayout": "PackedLayout"}
TYPES = {"BaseColor": "Vec4", "Metallic": "Float", "Roughness": "Float",
         "Emissive": "Vec3", "PackedLayout": "Float"}


def quoted(value):
    return json.dumps(value, ensure_ascii=False)


def pbr_graph(values, alpha=False):
    source = TEMPLATE.read_text(encoding="utf-8")
    for name, (kind, value) in values.items():
        if kind == "Texture2D":
            value = quoted(value)
        pattern = r'(^parameter "' + re.escape(name) + r'" \w+ \w+ )[^\n]*'
        source, count = re.subn(pattern, lambda m: m[1] + str(value), source, flags=re.M)
        if count != 1:
            raise ValueError("No PBR declaration for " + name)
    if alpha:
        source = source.replace("settings Lit Opaque", "settings Lit Alpha", 1)
    return source


def convert(source):
    if source.startswith("TINY_MAT 3 "):
        return source
    if source.startswith("TINY_MATGRAPH 2\n"):
        return source.replace("TINY_MATGRAPH 2", "TINY_MAT 3 graph", 1)
    rows = [shlex.split(line, comments=True, posix=True) for line in source.splitlines()]
    rows = [row for row in rows if row]
    if not rows:
        raise ValueError("Empty material")
    values, flags, parent = {}, {}, ""
    alpha = False
    if rows[0] == ["TINY_MAT", "2"]:
        for row in rows[1:]:
            key = row[0]
            if key in ("parent", "definition"):
                if parent:
                    raise ValueError("Multiple material parents")
                parent = row[1]
            elif key == "parameter":
                values[row[1]] = (row[2], " ".join(row[3:]))
            elif key == "texture":
                values[row[1]] = ("Texture2D", row[2])
            elif key != "validated_override":
                raise ValueError("Unsupported v2 field: " + key)
    else:
        for row in rows:
            key = row[0]
            if key == "parent":
                parent = row[1]
            elif key == "override":
                flags[row[1]] = row[2] != "0"
            elif key == "blend":
                alpha = row[1] == "alpha"
            elif key in NAMES:
                name = NAMES[key]
                kind = TYPES.get(name, "Texture2D")
                value = " ".join(row[1:])
                if key == "packedLayout":
                    value = "1" if value == "RMA" else "0"
                values[name] = (kind, value)
            else:
                raise ValueError("Unsupported legacy material field: " + key)
        for key, enabled in flags.items():
            if not enabled and key in NAMES:
                values.pop(NAMES[key], None)
        if "BaseColor" in values:
            alpha = alpha or float(values["BaseColor"][1].split()[3]) < .999
    if parent and parent != "builtin:PBR":
        if alpha:
            raise ValueError("Instance render-state override requires an explicit parent graph")
        result = "TINY_MAT 3 instance\nparent " + quoted(parent) + "\n"
        for name, (kind, value) in values.items():
            if kind == "Texture2D":
                result += "texture " + quoted(name) + " " + quoted(value) + "\n"
            else:
                result += "parameter " + quoted(name) + " " + kind + " " + value + "\n"
        return result
    return pbr_graph(values, alpha)


def migrate(root, apply=False):
    root = root.resolve(strict=True)
    files = [p for p in root.rglob("*") if p.is_file() and
             not any(part.lower() in SKIP for part in p.relative_to(root).parts)]
    for path in files:
        if path.suffix == ".matgraph" and path.with_suffix(".mat").exists():
            raise ValueError("Migration collision: " + str(path.with_suffix(".mat")))
    changes, renames = {}, {}
    for path in files:
        if path.suffix not in (".mat", ".matgraph"):
            continue
        destination = path.with_suffix(".mat")
        if destination != path and destination.exists():
            raise ValueError("Migration collision: " + str(destination))
        try:
            changes[path] = convert(path.read_text(encoding="utf-8-sig"))
        except ValueError as error:
            raise ValueError(str(path) + ": " + str(error)) from error
        if destination != path:
            renames[path] = destination
            meta = Path(str(path) + ".meta")
            if meta.exists():
                new_meta = Path(str(destination) + ".meta")
                if new_meta.exists():
                    raise ValueError("Migration collision: " + str(new_meta))
                renames[meta] = new_meta
    # Every reference keeps the same basename, only the obsolete extension changes.
    text_suffixes = {".mat", ".matgraph", ".meta", ".scene", ".prefab", ".tinyproject", ".cpp", ".h", ".py"}
    for path in files:
        if path.suffix not in text_suffixes:
            continue
        try:
            old = path.read_text(encoding="utf-8-sig")
        except UnicodeError:
            continue
        new = changes.get(path, old).replace(".matgraph", ".mat")
        if path.suffix == ".meta" and "asset.type=MaterialGraph" in new:
            new = new.replace("asset.type=MaterialGraph", "asset.type=Material")
        if new != old:
            changes[path] = new
        elif path in changes:
            del changes[path]
    for old, new in renames.items():
        print("rename", old.relative_to(root), "->", new.relative_to(root))
    for path in changes:
        print("convert", path.relative_to(root))
    if apply and (changes or renames):
        backup_root = root.parent if root.name.lower() in {"assets", "content"} else root
        backup = backup_root / "saved" / "MaterialMigration" / datetime.now().strftime("%Y%m%d-%H%M%S-%f")
        for path in set(changes) | set(renames):
            copy = backup / path.relative_to(root)
            copy.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, copy)
        for path, text in changes.items():
            with path.open("w", encoding="utf-8", newline="\n") as output:
                output.write(text)
        for old, new in renames.items():
            old.rename(new)
        print("Backup:", backup)
    print(f"{len(changes)} rewrites, {len(renames)} renames; " + ("applied" if apply else "dry run"))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()
    migrate(args.root, args.apply)
