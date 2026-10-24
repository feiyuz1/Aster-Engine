"""Offline material migration preserves authored data and references."""
import contextlib
import io
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from migrate_materials import convert, migrate
from material_graph import pbr_material


class MaterialMigrationTests(unittest.TestCase):
    def temporary_directory(self):
        root = Path(__file__).resolve().parents[1] / "saved" / "MaterialMigrationTests"
        root.mkdir(parents=True, exist_ok=True)
        return tempfile.TemporaryDirectory(prefix="case-", dir=root)

    def test_pbr_values_sampling_alpha_and_connections(self):
        source = convert("color .2 .3 .4 .5\nmetallic .8\ntexture texture.ppm\n")
        self.assertTrue(source.startswith("TINY_MAT 3 graph\nsettings Lit Alpha"))
        self.assertIn('parameter "BaseColor" Vec4 Linear .2 .3 .4 .5', source)
        self.assertIn('parameter "BaseMap" Texture2D SRgb "texture.ppm"', source)
        self.assertGreater(source.count("\nlink "), 20)
        self.assertEqual(convert(source), source)

    def test_instance_false_override_inherits(self):
        source = convert("parent parent.mat\noverride color 0\ncolor 1 0 0 1\nroughness .2\n")
        self.assertIn('parent "parent.mat"', source)
        self.assertNotIn("BaseColor", source)
        self.assertIn('parameter "Roughness" Float .2', source)

    def test_unsupported_shader_is_reported(self):
        with self.assertRaisesRegex(ValueError, "shader"):
            convert("shader old.frag\n")

    def test_new_generator_writes_graph_and_render_settings(self):
        source = pbr_material(color=(1, 0, 0, .4), textures={"NormalMap": "n.png"})
        self.assertIn("settings Lit Alpha", source)
        self.assertIn('parameter "NormalMap" Texture2D Normal "n.png"', source)

    def test_rename_keeps_guid_repairs_parent_and_backs_up(self):
        with self.temporary_directory() as temporary:
            root = Path(temporary)
            graph = root / "graph.matgraph"
            graph.write_text("TINY_MATGRAPH 2\nsettings Lit Opaque 0 .5\n", encoding="utf-8")
            (root / "graph.matgraph.meta").write_text("asset.id=keep-guid\nasset.type=MaterialGraph\n", encoding="utf-8")
            (root / "child.mat").write_text('TINY_MAT 2\ndefinition "graph.matgraph"\n', encoding="utf-8")
            with contextlib.redirect_stdout(io.StringIO()):
                migrate(root, True)
            self.assertFalse(graph.exists())
            self.assertIn("asset.id=keep-guid", (root / "graph.mat.meta").read_text(encoding="utf-8"))
            self.assertIn("asset.type=Material\n", (root / "graph.mat.meta").read_text(encoding="utf-8"))
            self.assertIn('parent "graph.mat"', (root / "child.mat").read_text(encoding="utf-8"))
            self.assertEqual(len(list((root / "saved/MaterialMigration").rglob("graph.matgraph"))), 1)
            with contextlib.redirect_stdout(io.StringIO()) as output:
                migrate(root, False)
            self.assertIn("0 rewrites, 0 renames", output.getvalue())

    def test_collision_prevents_any_write(self):
        with self.temporary_directory() as temporary:
            root = Path(temporary)
            old = root / "graph.matgraph"
            old.write_text("TINY_MATGRAPH 2\nsettings Lit Opaque 0 .5\n", encoding="utf-8")
            current = root / "graph.mat"
            current.write_text("existing", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "collision"):
                migrate(root, True)
            self.assertEqual(current.read_text(encoding="utf-8"), "existing")
            self.assertTrue(old.exists())


if __name__ == "__main__":
    unittest.main()
