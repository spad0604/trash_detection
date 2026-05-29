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

  // ── Dark theme surfaces ──────────────────────────────────
  static const Color background = Color(0xFF0D0F14);
  static const Color surface = Color(0xFF161A23);
  static const Color surfaceAlt = Color(0xFF1E2230);
  static const Color border = Color(0xFF2A2E3B);

  // ── Text ─────────────────────────────────────────────────
  static const Color textPrimary = Color(0xFFF0F1F5);
  static const Color textSecondary = Color(0xFF8B90A0);
  static const Color textMuted = Color(0xFF5A5F72);

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
