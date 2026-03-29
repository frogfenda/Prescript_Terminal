import json
#请从https://limbuscompany.huijiwiki.com/api/rest_v1/namespace/data?filter=%7B%22Data_Identitychoose%22%3A%22Identitychoose%22%7D&count=true&pagesize=500&sort_by=%7B%22belong%22%3A1%2C%22rarity%22%3A1%7D&page=1获取数据并粘贴到rawwids.json文件里面

def process_local_json():
    print("开始读取本地 rawids.json 文件...")
    
    try:
        with open("rawids.json", "r", encoding="utf-8") as f:
            data = json.load(f)
            
        # 兼容处理：判断数据结构
        raw_items = data.get("_embedded", []) if isinstance(data, dict) else data
        
        if not raw_items:
            print("❌ 警告：在文件里没有找到数据，请检查 rawids.json 是否复制完整。")
            return

        extracted_data = []

        # 遍历并提取所需字段
        for item in raw_items:
            full_name = item.get("name", "")
            sinner_name = item.get("belong", "")
            rarity = item.get("rarity", "")
            
            # --- 新增：获取赛季或提取途径的信息 ---
            season = item.get("season", "")
            get_way = item.get("get", "")
            
            # 判断逻辑：如果文本里包含“瓦尔普吉斯之夜”，则 walp 为 1，否则为 0
            is_walp = 1 if ("瓦尔普吉斯之夜" in season or "瓦尔普吉斯之夜" in get_way) else 0

            # 排除没有名字的空数据
            if not full_name:
                continue

            # 裁切名字（去掉前缀的罪人名字）
            identity_name = full_name
            if sinner_name and full_name.startswith(sinner_name):
                identity_name = full_name[len(sinner_name):]

            # 将新字段 walp 加入到最终字典中
            extracted_data.append({
                "sinner": sinner_name,
                "id": identity_name,
                "star": rarity,
                "walp": is_walp
            })

        # 保存为最终的完美格式
        with open("ids.json", "w", encoding="utf-8") as f:
            json.dump(extracted_data, f, ensure_ascii=False, indent=4)
            
        print(f"\n🎉 太棒了！成功清洗了 {len(extracted_data)} 条人格数据！")
        print("结果已保存至当前目录的 ids.json 中。")

    except FileNotFoundError:
        print("❌ 找不到 rawids.json 文件，请确认你已经将数据保存在了当前目录下。")
    except json.JSONDecodeError:
        print("❌ rawids.json 里的内容格式不对，请重新全选复制一次。")

if __name__ == "__main__":
    process_local_json()