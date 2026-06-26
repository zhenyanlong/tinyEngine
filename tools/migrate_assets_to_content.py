import json
import shutil
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RES = ROOT / "res"
OLD_MATERIALS = RES / "materials"
CONTENT = RES / "content"
BIN = RES / "bin"


def sanitize(name: str) -> str:
    bad = '\\/:*?"<>|'
    return "".join("_" if c in bad else c for c in name)


def unique_path(path: Path) -> Path:
    if not path.exists():
        return path
    for i in range(1, 10000):
        candidate = path.with_name(f"{path.stem}_{i:03d}{path.suffix}")
        if not candidate.exists():
            return candidate
    return path


def rel_to_res(path: Path) -> str:
    return path.relative_to(RES).as_posix()


def classify_ast(data: dict) -> str:
    ast_type = data.get("type", "Mesh")
    if ast_type == "Anim":
        return "anim"
    if ast_type == "Material":
        return "material"
    if ast_type == "Box":
        return "box"
    return "mesh"


def copy_payload(rel_path: str, bin_type: str, report_entry: dict) -> str:
    if not rel_path:
        return rel_path
    src = RES / rel_path
    if not src.exists():
        report_entry.setdefault("missing", []).append(rel_path)
        return rel_path
    dst = unique_path(BIN / bin_type / src.name)
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)
    if bin_type == "mesh" and src.suffix.lower() == ".gltf":
        for sidecar in src.parent.glob("*.bin"):
            shutil.copy2(sidecar, dst.parent / sidecar.name)
            report_entry.setdefault("copiedPayloads", []).append({
                "from": rel_to_res(sidecar),
                "to": rel_to_res(dst.parent / sidecar.name),
            })
        texture_dir = src.parent / "textures"
        if texture_dir.is_dir():
            target_texture_dir = dst.parent / "textures"
            target_texture_dir.mkdir(parents=True, exist_ok=True)
            for tex in texture_dir.rglob("*"):
                if tex.is_file():
                    target = target_texture_dir / tex.relative_to(texture_dir)
                    target.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(tex, target)
                    report_entry.setdefault("copiedPayloads", []).append({
                        "from": rel_to_res(tex),
                        "to": rel_to_res(target),
                    })
    new_rel = rel_to_res(dst)
    report_entry.setdefault("copiedPayloads", []).append({"from": rel_path, "to": new_rel})
    return new_rel


def collect_ast_mapping() -> dict:
    mapping = {}
    if not OLD_MATERIALS.exists():
        return mapping
    for ast in OLD_MATERIALS.rglob("*.ast"):
        try:
            data = json.loads(ast.read_text(encoding="utf-8"))
        except Exception:
            data = {}
        kind = classify_ast(data)
        suffix = {
            "mesh": ".mesh.ast",
            "material": ".material.ast",
            "box": ".box.ast",
            "anim": ".anim.ast",
        }[kind]
        old_rel = rel_to_res(ast)
        old_folder = ast.parent.relative_to(OLD_MATERIALS)
        stem = sanitize(ast.stem)
        dst = CONTENT / old_folder / f"{stem}{suffix}"
        mapping[old_rel] = rel_to_res(dst)
    return mapping


def migrate():
    CONTENT.mkdir(parents=True, exist_ok=True)
    for name in ("mesh", "anim", "texture", "material", "box"):
        (BIN / name).mkdir(parents=True, exist_ok=True)

    mapping = collect_ast_mapping()
    report = {"assets": []}

    for old_rel, new_rel in sorted(mapping.items()):
        src_ast = RES / old_rel
        dst_ast = RES / new_rel
        entry = {"from": old_rel, "to": new_rel}
        try:
            data = json.loads(src_ast.read_text(encoding="utf-8"))
        except Exception as exc:
            entry["error"] = f"json parse failed: {exc}"
            report["assets"].append(entry)
            continue

        kind = classify_ast(data)

        if "model" in data:
            model = data["model"]
            if isinstance(model, str):
                data["model"] = copy_payload(model, "mesh", entry)
            elif isinstance(model, dict) and isinstance(model.get("path"), str):
                model["path"] = copy_payload(model["path"], "mesh", entry)

        if isinstance(data.get("textures"), dict):
            for key, value in list(data["textures"].items()):
                if isinstance(value, str):
                    data["textures"][key] = copy_payload(value, "texture", entry)

        old_subs = data.pop("subMaterials", None)
        if isinstance(old_subs, list):
            data["materials"] = [mapping.get(path, path) for path in old_subs if isinstance(path, str)]
        elif isinstance(data.get("materials"), list):
            data["materials"] = [mapping.get(path, path) for path in data["materials"] if isinstance(path, str)]

        if isinstance(data.get("animations"), list):
            data["animations"] = [mapping.get(path, path) for path in data["animations"] if isinstance(path, str)]

        if kind == "anim" and isinstance(data.get("binary"), str):
            data["binary"] = copy_payload(data["binary"], "anim", entry)
        if kind == "anim" and isinstance(data.get("rootModel"), str):
            data["rootModel"] = mapping.get(data["rootModel"], data["rootModel"])

        dst_ast.parent.mkdir(parents=True, exist_ok=True)
        dst_ast.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        report["assets"].append(entry)

    report_path = CONTENT / "migration-report.json"
    report_path.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"Migrated {len(report['assets'])} assets")
    print(f"Report: {report_path}")


if __name__ == "__main__":
    migrate()
