"""只读校验指令内容与规则资源的UUID、引用、概率预算和双语数据，不再转换旧格式。"""
from pathlib import Path
import argparse
import json
import uuid

ROOT = Path(__file__).resolve().parents[2]


def check(root):
    all_ids = set()
    for lang in ("ZH", "EN"):
        folder = lang.lower()
        catalog = json.loads((root / f"data/{folder}/prescripts.json").read_text(encoding="utf-8-sig"))
        special = json.loads((root / f"fat_data/Resources/prescript/special/text/{folder}/specials.json").read_text(encoding="utf-8-sig"))
        loom = json.loads((root / f"fat_data/Resources/loom/text/{folder}/oracle.json").read_text(encoding="utf-8-sig"))
        assert all(doc["schema_version"] == 1 and doc["lang"] == lang for doc in (catalog, special, loom))
        ids = [item["id"] for item in catalog["items"]]
        assert len(ids) == len(set(ids)) and not (all_ids & set(ids))
        for value in ids:
            assert str(uuid.UUID(value)) == value
        all_ids.update(ids)
        assert set(catalog["ordinary"]) <= set(ids)
        refs = [item["content_id"] for item in special["pure_specials"]]
        max_sum = sum(item["probability_bp"] for item in special["pure_specials"])
        for chain in special["character_chains"]:
            assert "legacy_slot" not in chain
            assert 0 <= chain["base_bp"] <= chain["max_bp"] <= 10000 and chain["increment_bp"] >= 0
            max_sum += chain["max_bp"]
            assert chain["steps"]
            refs.extend(step["content_id"] for step in chain["steps"])
            assert len({step["id"] for step in chain["steps"]}) == len(chain["steps"])
        assert max_sum <= 10000
        assert set(refs) == set(catalog["references"]["special"]) <= set(ids)
        assert {e["content_id"] for e in loom["entries"]} == set(catalog["references"]["oracle"]) <= set(ids)
        assert all(1 <= e["weight"] <= 10000 and "legacy_id" not in e for e in loom["entries"])
        print(f"{lang}: {len(ids)}条内容，{len(catalog['ordinary'])}条普通候选，引用与概率校验通过")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=ROOT)
    args = parser.parse_args()
    check(args.root)
