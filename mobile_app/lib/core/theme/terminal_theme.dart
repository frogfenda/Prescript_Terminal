import 'package:flutter/material.dart';

class TerminalColors {
  static const background = Color(0xFF050707);
  static const panel = Color(0xFF081212);
  static const panelAlt = Color(0xFF0B1818);
  static const cyan = Color(0xFF00FFFF);
  static const dimCyan = Color(0xFF007777);
  static const green = Color(0xFF48FF7A);
  static const yellow = Color(0xFFFFD05A);
  static const red = Color(0xFFFF335C);
  static const text = Color(0xFFE8FFFF);
  static const muted = Color(0xFF7C9292);
  static const border = Color(0xFF0A8A8A);
}

class TerminalTheme {
  static ThemeData dark() {
    const mono = 'monospace';
    final base = ThemeData.dark(useMaterial3: true);
    return base.copyWith(
      scaffoldBackgroundColor: TerminalColors.background,
      colorScheme: const ColorScheme.dark(
        primary: TerminalColors.cyan,
        secondary: TerminalColors.green,
        error: TerminalColors.red,
        surface: TerminalColors.panel,
      ),
      textTheme: base.textTheme.apply(
        fontFamily: mono,
        bodyColor: TerminalColors.text,
        displayColor: TerminalColors.text,
      ),
      appBarTheme: const AppBarTheme(
        backgroundColor: TerminalColors.background,
        foregroundColor: TerminalColors.cyan,
        elevation: 0,
        centerTitle: false,
        titleTextStyle: TextStyle(
          fontFamily: mono,
          fontSize: 18,
          fontWeight: FontWeight.w700,
          letterSpacing: 0,
          color: TerminalColors.cyan,
        ),
      ),
      bottomNavigationBarTheme: const BottomNavigationBarThemeData(
        backgroundColor: TerminalColors.background,
        selectedItemColor: TerminalColors.cyan,
        unselectedItemColor: TerminalColors.muted,
        type: BottomNavigationBarType.fixed,
      ),
      inputDecorationTheme: const InputDecorationTheme(
        filled: true,
        fillColor: Color(0xFF020404),
        border: OutlineInputBorder(
          borderSide: BorderSide(color: TerminalColors.border),
        ),
        enabledBorder: OutlineInputBorder(
          borderSide: BorderSide(color: TerminalColors.border),
        ),
        focusedBorder: OutlineInputBorder(
          borderSide: BorderSide(color: TerminalColors.cyan, width: 1.5),
        ),
        labelStyle: TextStyle(color: TerminalColors.muted),
      ),
    );
  }
}
