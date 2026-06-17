import 'package:flutter/material.dart';

class AppColors {
  AppColors._();

  // ── Core palette ─────────────────────────────────────────
  static const Color primary = Color(0xFF6C5CE7); // Electric indigo
  static const Color accent = Color(0xFF00D2D3); // Teal / Cyan
  static const Color success = Color(0xFF00C48C); // Green
  static const Color warning = Color(0xFFFFAA2C); // Amber
  static const Color danger = Color(0xFFFF6B6B); // Coral red
  static const Color info = Color(0xFF54A0FF); // Sky blue

  // ── Light theme surfaces ─────────────────────────────────
  static const Color background = Color(0xFFEAF0F8);
  static const Color surface = Color(0xFFFFFFFF);
  static const Color surfaceAlt = Color(0xFFE2E9F4);
  static const Color border = Color(0xFFC5D0E0);

  // ── Text ─────────────────────────────────────────────────
  static const Color textPrimary = Color(0xFF172033);
  static const Color textSecondary = Color(0xFF40506A);
  static const Color textMuted = Color(0xFF69758B);

  // ── Bin-specific colours ─────────────────────────────────
  static const Color binPlastic = Color(0xFF54A0FF); // Metal bin
  static const Color binOrganic = Color(0xFF00C48C); // Paper bin
  static const Color binOther = Color(0xFFFFAA2C); // Other bin

  // ── Chart colours ────────────────────────────────────────
  static const List<Color> chartColors = [
    Color(0xFF6C5CE7),
    Color(0xFF00D2D3),
    Color(0xFFFF6B6B),
    Color(0xFFFFAA2C),
    Color(0xFF54A0FF),
    Color(0xFF00C48C),
  ];
}
