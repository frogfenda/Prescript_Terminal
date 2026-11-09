"""只读校验三类自包含指令库：元数据、公共属性、本库引用、概率和分类权重。"""
from pathlib import Path
import argparse
import json
import re
import uuid

ROOT = Path(__file__).resolve().parents[2]


def identity(value):
    parsed = uuid.UUID(value)
    assert parsed.int and str(parsed) == value
    return value


def contents(doc, lang, kind):
    assert doc["schema_version"] == 2 and doc["lang"] == lang and doc["kind"] == kind
    identity(doc["library_id"])
    assert type(doc["revision"]) is int and 0 <= doc["revision"] <= 0xFFFFFFFF
    assert not ({"items", "ordinary", "references"} & doc.keys())
    ids = set()
    for item in doc["contents"]:
        key = identity(item["id"])
        assert key not in ids
        ids.add(key)
        assert isinstance(item["text"], str) and item["text"]
        if "title" in item:
            assert isinstance(item["title"], str) and not any(ord(c) < 32 or ord(c) == 127 for c in item["title"])
        if "color" in item:
            assert re.fullmatch(r"#[0-9A-Fa-f]{6}", item["color"])
        assert item.get("audio", "default") in {"default", "heads", "tails", "Ahab", "procedure", "final"}
        for flag in ("apply_target", "alert"):
            assert flag not in item or type(item[flag]) is bool
    return ids


def check(root):
    shipped_ids, library_ids = set(), set()
    for lang in ("ZH", "EN"):
        folder = lang.lower()
        paths = {
            "ordinary": f"prescript/common/text/{folder}/prescripts.json",
            "special": f"prescript/special/text/{folder}/specials.json",
            "oracle": f"loom/text/{folder}/oracle.json",
        }
        docs, pools = {}, {}
        for kind, relative in paths.items():
            doc = json.loads((root / "fat_data/Resources" / relative).read_text(encoding="utf-8-sig"))
            ids = contents(doc, lang, kind)
            assert doc["library_id"] not in library_ids
            library_ids.add(doc["library_id"])
            # 仓库默认库的内容身份互不重复；运行时只做本库查找，不建立全局引用。
            assert not shipped_ids & ids
            shipped_ids.update(ids)
            docs[kind], pools[kind] = doc, ids
        special = docs["special"]
        rules_ids, aliases = set(), set()
        def rule_id(item):
            key = identity(item["id"])
            assert key not in rules_ids
            rules_ids.add(key)
            alias = item.get("legacy_id")
            if alias:
                assert alias not in aliases
                aliases.add(alias)
        budget = 0
        for trigger in special["pure_specials"]:
            rule_id(trigger)
            assert trigger["content_id"] in pools["special"]
            assert type(trigger["probability_bp"]) is int and 0 <= trigger["probability_bp"] <= 10000
            budget += trigger["probability_bp"]
        for chain in special["character_chains"]:
            rule_id(chain)
            assert "legacy_slot" not in chain and chain["steps"] and len(chain["steps"]) <= 255
            assert all(type(chain[k]) is int and 0 <= chain[k] <= 10000 for k in ("base_bp", "increment_bp", "max_bp"))
            assert chain["base_bp"] <= chain["max_bp"]
            budget += min(chain["max_bp"], chain["base_bp"] + (len(chain["steps"])-1)*chain["increment_bp"])
            for step in chain["steps"]:
                rule_id(step)
                assert step["content_id"] in pools["special"]
        assert budget <= 10000
        entries, weights = set(), {}
        for entry in docs["oracle"]["entries"]:
            assert entry["content_id"] in pools["oracle"] and "legacy_id" not in entry
            assert isinstance(entry["type"], str) and entry["type"]
            key = entry["type"], entry["content_id"]
            assert key not in entries
            entries.add(key)
            assert type(entry["weight"]) is int and 1 <= entry["weight"] <= 0x7FFFFFFF
            weights[entry["type"]] = weights.get(entry["type"], 0) + entry["weight"]
        assert all(total <= 0x7FFFFFFF for total in weights.values())
        print(f"{lang}: 普通{len(pools['ordinary'])}条，特殊{len(pools['special'])}条，纺织机{len(pools['oracle'])}条；独立库/本库引用/概率权重校验通过")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=ROOT)
    check(parser.parse_args().root)
