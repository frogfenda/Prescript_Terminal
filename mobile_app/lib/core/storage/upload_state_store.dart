import 'dart:convert';

import 'package:shared_preferences/shared_preferences.dart';

class UploadStateStore {
  String _key(String lang) => 'terminal_uploaded_items_${lang.toLowerCase()}';

  Future<Set<String>> load(String lang) async {
    final prefs = await SharedPreferences.getInstance();
    final raw = prefs.getString(_key(lang)) ?? '[]';
    final decoded = jsonDecode(raw);
    if (decoded is List) {
      return decoded.map((e) => e.toString()).toSet();
    }
    return {};
  }

  Future<void> save(String lang, Set<String> values) async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString(_key(lang), jsonEncode(values.toList()));
  }
}
