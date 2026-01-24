#!/usr/bin/env python3
"""
일괄로 .mat 파일 내 assetPath / albedoTexturePath 를 논리 경로(Assets/, Resource/, Cooked/)로 치환합니다.
절대 경로가 커밋된 상태를 수정할 때 사용하세요.

사용: python scripts/normalize_mat_paths.py [--project-root PATH]
     --project-root 생략 시, 스크립트 위치 기준으로 프로젝트 루트(Assets 상위) 추정.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


def normalize_to_logical(path: str, project_root: Path) -> str:
    if not path.strip():
        return path
    p = Path(path)
    if not p.is_absolute():
        s = p.as_posix()
        if s.startswith("Assets/") or s.startswith("Resource/") or s.startswith("Cooked/"):
            return s
        return path
    try:
        rel = p.relative_to(project_root)
        r = rel.as_posix()
        if r.startswith("Assets/") or r.startswith("Resource/") or r.startswith("Cooked/"):
            return r
    except ValueError:
        pass
    return path


def main() -> int:
    parser = argparse.ArgumentParser(description="Normalize .mat assetPath/albedoTexturePath to logical paths.")
    parser.add_argument(
        "--project-root",
        type=Path,
        default=None,
        help="Project root (parent of Assets). Default: script dir or repo root.",
    )
    parser.add_argument("--dry-run", action="store_true", help="Only print changes, do not write.")
    args = parser.parse_args()

    repo = Path(__file__).resolve().parent.parent
    root = args.project_root
    if root is None:
        root = repo
    root = root.resolve()

    assets = root / "Assets"
    if not assets.is_dir():
        print("Assets/ not found under project root. Use --project-root.", file=sys.stderr)
        return 1

    mat_files = list(assets.rglob("*.mat"))
    changed = 0
    for fp in mat_files:
        try:
            text = fp.read_text(encoding="utf-8")
        except Exception as e:
            print(f"Read fail: {fp} - {e}", file=sys.stderr)
            continue
        try:
            data = json.loads(text)
        except json.JSONDecodeError as e:
            print(f"JSON parse fail: {fp} - {e}", file=sys.stderr)
            continue

        orig_ap = data.get("assetPath", "")
        orig_at = data.get("albedoTexturePath", "")
        new_ap = normalize_to_logical(orig_ap, root)
        new_at = normalize_to_logical(orig_at, root)

        if new_ap != orig_ap or new_at != orig_at:
            changed += 1
            print(f"  {fp.relative_to(root)}")
            if new_ap != orig_ap:
                print(f"    assetPath: {orig_ap!r} -> {new_ap!r}")
            if new_at != orig_at:
                print(f"    albedoTexturePath: {orig_at!r} -> {new_at!r}")

        data["assetPath"] = new_ap
        data["albedoTexturePath"] = new_at

        if not args.dry_run and (new_ap != orig_ap or new_at != orig_at):
            fp.write_text(json.dumps(data, indent=4, ensure_ascii=False), encoding="utf-8")

    print(f"Updated {changed} .mat files.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
